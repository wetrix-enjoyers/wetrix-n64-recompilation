// A ring buffer of the most recent recompiled-function calls, one per thread.
//
// Enabled from wetrix.toml (`trace_mode = true`), which makes N64Recomp emit
// TRACE_ENTRY()/TRACE_RETURN() into every function it compiles. This file is the
// other half: the macros are defined in include/trace.h, the storage is here.
//
// Why it exists: the hang past the title screen is a spin, and a spin has no
// stack trace to resolve -- the process is alive and a sampler only ever sees the
// loop that is still running (see stall_probe.cpp, which found func_8004C1A0
// that way). What that instrument cannot show is the *sequence* that led into the
// loop, which is what this records.
//
// Three properties make it work on a loop that runs millions of times a second:
//
//   * the append is a store and an increment, and the name is a string literal
//     whose address is already in a register;
//
//   * repeats are collapsed. The same function entering at the same depth as the
//     previous entry increments that entry's counter instead of consuming a slot.
//     The spinning loop calls one function over and over, so without this the
//     ring would be entirely full of the spin and would have thrown away the very
//     history it was recording;
//
//   * the ring is PER THREAD, and that one was learned the hard way. A shared
//     ring does not survive a spin: because the loop collapses to a single slot,
//     that slot stops advancing while every other thread keeps appending, and the
//     other threads wrap the ring and overwrite the stalled thread's history
//     within a frame or two. The first version of this did exactly that -- the
//     dump fired ten seconds after the stall and showed only the audio thread's
//     heartbeat, having silently erased the answer. Per-thread rings also mean no
//     lock on the hot path, since only the owner writes its own ring.
//
// The dump therefore takes the id of the thread that stopped making progress
// (the sampler already knows it) and prints that thread's ring and nothing else.
//
// Depth is per-thread for the same reason -- a shared counter would report two
// threads' nesting as one nonsense chain. Note that depth is *indicative*: it is
// incremented on entry and decremented on return, and a function that leaves via
// a tail call (which N64Recomp emits as a plain call or goto) never reaches its
// TRACE_RETURN, so depth can drift upwards over a long run. Read it as
// "deeper/shallower", not as an exact frame count.

#include <cstdint>
#include <cstdio>
#include <atomic>

#include "trace.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

// Where the dump functions below print. stderr unless the debug server
// (src/debug_server.cpp) points it at a buffer so it can send a dump over its
// socket instead of into the log.
extern "C" FILE* wetrix_diag_out;
FILE* wetrix_diag_out = nullptr;
#define DIAG_OUT (wetrix_diag_out != nullptr ? wetrix_diag_out : stderr)

namespace {

struct Entry {
    const char* name;
    // Nesting level this call was entered at, 1-based.
    uint32_t depth;
    // How many consecutive times this exact (name, depth) occurred.
    uint32_t count;
};

// Per thread. 2048 slots is far more history than the approach to a stall needs,
// and it is a slot that the spinning loop itself never consumes, because of the
// collapsing above.
constexpr size_t RING_SIZE = 2048;

struct Ring {
    Entry entries[RING_SIZE];
    size_t head = 0;   // entries ever appended; newest is (head - 1) % RING_SIZE
    uint32_t depth = 0;
};

// thread_local rather than a heap allocation: the only writer is the owning
// thread, so no synchronisation is needed on the hot path.
thread_local Ring t_ring;

uint64_t current_thread_id() {
#if defined(_WIN32)
    return GetCurrentThreadId();
#else
    return static_cast<uint64_t>(pthread_self());
#endif
}

// Registry so the dump can read a ring belonging to a thread that is not the one
// calling it. Rows are never removed; a dead thread's ring simply goes cold.
constexpr size_t MAX_REGISTERED = 256;

struct Registered {
    Ring* ring;
    uint64_t thread_id;
};

Registered g_registered[MAX_REGISTERED];
std::atomic<size_t> g_registered_count{ 0 };
std::atomic_flag g_registry_lock = ATOMIC_FLAG_INIT;

bool register_ring() {
    const size_t slot = g_registered_count.fetch_add(1, std::memory_order_relaxed);
    if (slot >= MAX_REGISTERED) {
        return true;   // too many threads to track; this one's ring stays private
    }

    while (g_registry_lock.test_and_set(std::memory_order_acquire)) {
    }
    g_registered[slot] = Registered{ &t_ring, current_thread_id() };
    g_registry_lock.clear(std::memory_order_release);
    return true;
}

// Runs once per thread, on that thread's first traced call. Declared after
// register_ring() so the initialiser can see it.
thread_local bool t_registered = register_ring();

Ring* find_ring(uint64_t thread_id) {
    const size_t count = g_registered_count.load(std::memory_order_acquire);
    const size_t limit = count < MAX_REGISTERED ? count : MAX_REGISTERED;

    for (size_t i = 0; i < limit; ++i) {
        if (g_registered[i].ring != nullptr && g_registered[i].thread_id == thread_id) {
            return g_registered[i].ring;
        }
    }
    return nullptr;
}

void append(Ring& ring, const char* name, uint32_t depth) {
    if (ring.head > 0) {
        Entry& previous = ring.entries[(ring.head - 1) % RING_SIZE];
        if (previous.name == name && previous.depth == depth) {
            ++previous.count;
            return;
        }
    }

    ring.entries[ring.head % RING_SIZE] = Entry{ name, depth, 1 };
    ++ring.head;
}

void dump_ring(const Ring& ring, int max_entries) {
    const size_t recorded = ring.head;

    if (recorded == 0) {
        fprintf(DIAG_OUT, "[trace] no calls recorded for this thread\n");
        return;
    }

    const size_t available = recorded < RING_SIZE ? recorded : RING_SIZE;
    const size_t show = static_cast<size_t>(max_entries) < available
                            ? static_cast<size_t>(max_entries)
                            : available;
    const size_t first = available - show;

    fprintf(DIAG_OUT,
            "[trace] %zu calls recorded on this thread, %zu in the ring, showing the "
            "last %zu (oldest first; xN = repeats collapsed; dN = nesting)\n",
            recorded, available, show);

    // Depth is printed *relative to the shallowest entry in the window*, not
    // absolutely. It drifts upwards over a run (see the note at the top: a tail
    // call never reaches its TRACE_RETURN), so by the time a stall is detected the
    // absolute value is something like 12205 and says nothing. The differences
    // between neighbouring entries are still sound, and those are what show the
    // call chain.
    uint32_t shallowest = UINT32_MAX;
    for (size_t i = first; i < available; ++i) {
        const Entry& entry = ring.entries[(recorded - available + i) % RING_SIZE];
        if (entry.depth < shallowest) {
            shallowest = entry.depth;
        }
    }

    for (size_t i = first; i < available; ++i) {
        const Entry& entry = ring.entries[(recorded - available + i) % RING_SIZE];

        const uint32_t nesting = entry.depth - shallowest;
        fprintf(DIAG_OUT, "[trace]   %*s%s", static_cast<int>(nesting * 2), "", entry.name);
        if (entry.count > 1) {
            fprintf(DIAG_OUT, "  x%u", entry.count);
        }
        fprintf(DIAG_OUT, "\n");
    }
    fflush(DIAG_OUT);
}

} // namespace

extern "C" void wetrix_trace_entry(const char* name) {
    Ring& ring = t_ring;
    const uint32_t depth = ++ring.depth;
    append(ring, name, depth);
}

extern "C" void wetrix_trace_return(void) {
    if (t_ring.depth > 0) {
        --t_ring.depth;
    }
}

extern "C" void wetrix_trace_dump(uint64_t thread_id, int max_entries) {
    if (max_entries <= 0) {
        max_entries = 200;
    }

    // A thread id of 0 means "whatever thread is asking", which is only useful
    // for a manual call. The stall probe always passes the thread it sampled.
    if (thread_id == 0) {
        dump_ring(t_ring, max_entries);
        return;
    }

    Ring* ring = find_ring(thread_id);
    if (ring == nullptr) {
        fprintf(DIAG_OUT,
                "[trace] no recorded calls for thread %llu (it may never have "
                "entered recompiled code)\n",
                static_cast<unsigned long long>(thread_id));
        return;
    }

    dump_ring(*ring, max_entries);
}

// Every registered ring, for the case the single-thread dump cannot serve.
//
// That dump is written for a spin: one thread is burning the CPU, the sampler
// names it, and its ring is the history of how it got into the loop. A *boot*
// stall is the opposite shape -- nothing is executing recompiled code at all, so
// there is no thread to ask about, and the only evidence left is what each thread
// was doing when it parked. The tail of each ring is the last recompiled function
// that thread entered, which is the call that led into its wait.
//
// Host thread ids are printed because they are the join key: src/main.cpp prints
// the same id for each game thread it names, and the stall probe prints it for
// each thread it samples.
extern "C" void wetrix_trace_dump_all(int max_entries) {
    if (max_entries <= 0) {
        max_entries = 200;
    }

    const size_t count = g_registered_count.load(std::memory_order_acquire);
    const size_t limit = count < MAX_REGISTERED ? count : MAX_REGISTERED;

    size_t printed = 0;
    for (size_t i = 0; i < limit; ++i) {
        Ring* ring = g_registered[i].ring;
        if (ring == nullptr || ring->head == 0) {
            continue;
        }

        fprintf(DIAG_OUT, "[trace] ---- host thread %llu ----\n",
                static_cast<unsigned long long>(g_registered[i].thread_id));
        dump_ring(*ring, max_entries);
        ++printed;
    }

    fprintf(DIAG_OUT, "[trace] %zu of %zu registered thread(s) had recorded calls\n",
            printed, limit);
    fflush(DIAG_OUT);
}
