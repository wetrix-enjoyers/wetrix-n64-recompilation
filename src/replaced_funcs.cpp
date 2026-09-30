// ROM functions the port supplies itself, because N64Recomp cannot recompile
// them.
//
// These 14 names are listed under `ignored` in wetrix.toml. N64Recomp still
// emits a call to each of them, and still puts them in the runtime's function
// table, but emits no body -- so the definitions here are what a `jal` reaches.
//
// They used to be *stubs*: N64Recomp-emitted empty bodies, which is behaviourally
// identical to what this file does today, with one difference that turned out to
// matter. A stub is invisible. When a stub is suspected of causing a bug the only
// available evidence is "the bug is still there", which cannot distinguish "the
// function was never called" from "the function was called and was wrong". A
// definition in this file can simply say that it ran -- see note_entry() below.
// That is the entire reason for the change, and it also means an omission is now
// a link error instead of a silent no-op.
//
// There are four kinds here. The first two are complete, the third is unfinished
// work, and the fourth is the fix for the hang past the title screen:
//
//   1. func_8005C250 -- gcc folded a shared code tail into the next function.
//      Nothing `jal`s it (0 call sites in asm/), so a no-op is complete.
//
//   2. The R4300 hardware layer -- func_80011100, func_8001126C, func_800115B8,
//      func_8001167C, func_80052EC0, func_80061200, func_80061330. The exception
//      vector, Cause/EPC/ErrorEPC access, the TLB, and a GPR/COP0 dump. None of
//      this hardware exists in recompiled host code, and nothing on the host can
//      raise an N64 exception, so a no-op is not a shortcut -- it is correct.
//      The three that have call sites are called from within this same layer.
//
//   3. The 5 multi-entry functions -- func_8004C754, func_80050B84,
//      func_80059840, func_8005CC50, func_8005D160. These are hand-written
//      assembly whose bodies are shared between several entry points, and they
//      branch *backwards* into each other (func_8004C754 jumps to 0x8004C734,
//      which is inside func_8004C694). N64Recomp compiles one function at a time
//      and cannot emit a goto into another function's interior; widening only
//      grows a function forward, so function_sizes cannot repair it either.
//
//      These are no-ops, and that is a bug, not a design. Measured call sites in
//      asm/: func_80050B84 14, func_8004C754 1, func_80059840 1, func_8005CC50 1,
//      func_8005D160 0.
//
//      This group was the suspect for the freeze past the title screen, on the
//      theory that func_8004C754 calls func_8004DF04 -- holder of the only
//      instruction in the ROM that writes a non-zero +0x9E -- and that the 16
//      records missing that field could therefore never be built. That is now
//      disproved rather than merely doubted: note_entry() below reports the
//      first entry into each of these functions, and a reproducing run logs
//      none of them. The freeze is a thread-handoff problem, and it is group 4
//      that fixes it.
//
//   4. func_8004C1A0 -- the condition every sound-drain wait in the ROM spins
//      on. Not a missing body at all: the ROM's scan is transcribed below and
//      the port adds a handoff to it. See the section at the end of this file.
//
// Set WETRIX_STUB_PROBE=1 to log every entry instead of only the first per
// function, which is what makes the call counts above facts rather than guesses.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>

#include "recomp.h"
#include "wetrix_shims.h"

namespace {

struct Trap {
    const char* name;
    uint32_t vram;
    std::atomic<uint32_t> hits{ 0 };
};

// Indexed by the TRAP() calls below. Addresses are the ROM's own.
Trap g_traps[] = {
    { "func_8005C250", 0x8005C250u, {} },
    { "func_80011100", 0x80011100u, {} },
    { "func_8001126C", 0x8001126Cu, {} },
    { "func_800115B8", 0x800115B8u, {} },
    { "func_8001167C", 0x8001167Cu, {} },
    { "func_80052EC0", 0x80052EC0u, {} },
    { "func_80061200", 0x80061200u, {} },
    { "func_80061330", 0x80061330u, {} },
    { "func_8004C754", 0x8004C754u, {} },
    { "func_80050B84", 0x80050B84u, {} },
    { "func_80059840", 0x80059840u, {} },
    { "func_8005CC50", 0x8005CC50u, {} },
    { "func_8005D160", 0x8005D160u, {} },
};

bool verbose() {
    static const bool enabled = std::getenv("WETRIX_STUB_PROBE") != nullptr;
    return enabled;
}

// The first entry per function is always reported: it answers "is this reached at
// all", which is the question that decides whether each of these is a real hole
// or a harmless one. Everything after that needs the environment variable, since
// none of these is called at a rate that anyone wants to read.
//
// ra and a0..a2 are printed because they are what identifies the caller and the
// argument, and for the multi-entry functions the argument is the thing the
// missing body would have acted on.
void note_entry(Trap& trap, recomp_context* ctx) {
    const uint32_t count = trap.hits.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count > 1 && !verbose()) {
        return;
    }

    fprintf(stderr,
            "[stub] %s (0x%08X) entered, call #%u  ra=0x%08X a0=0x%08X a1=0x%08X a2=0x%08X\n",
            trap.name, trap.vram, count, (uint32_t)ctx->r31, (uint32_t)ctx->r4,
            (uint32_t)ctx->r5, (uint32_t)ctx->r6);
    fflush(stderr);
}

} // namespace

#define TRAP(index) note_entry(g_traps[index], ctx)

// --- 1. nothing calls it ---------------------------------------------------

extern "C" void func_8005C250(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(0);
}

// --- 2. the R4300 hardware layer -------------------------------------------

extern "C" void func_80011100(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(1);
}

extern "C" void func_8001126C(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(2);
}

extern "C" void func_800115B8(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(3);
}

extern "C" void func_8001167C(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(4);
}

extern "C" void func_80052EC0(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(5);
}

extern "C" void func_80061200(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(6);
}

extern "C" void func_80061330(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(7);
}

// --- 3. the multi-entry functions, whose bodies are still missing ----------
//
// Each of these needs to be written out by hand from the disassembly in asm/,
// respecting the register state its partner function leaves behind. Until then
// they report that they were entered and do nothing, which at least turns an
// invisible no-op into evidence.

extern "C" void func_8004C754(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(8);
}

extern "C" void func_80050B84(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(9);
}

extern "C" void func_80059840(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(10);
}

extern "C" void func_8005CC50(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(11);
}

extern "C" void func_8005D160(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    TRAP(12);
}

// --- 4. func_8004C1A0, the wait primitive the sound system spins on ---------
//
// Every "stop the sound and wait for the pool to settle" loop in this ROM has
// the same shape, at nine call sites:
//
//     func_8004C120(mode, 0)                  // stamp the records to release
//     while (func_8004C1A0(mode) != 0) { }    // and wait for the release
//
// So this one function is where the hang has to be fixed -- not at the nine
// sites. The body below is the ROM's, transcribed from 0x8004C1A0 in
// asm/12580.s: walk the 20-voice pool (count at 0x80080BA4, base at 0x80080BAC,
// stride 0x130) and return how many records are bound to a sample -- word[0]
// non-zero -- and either playing (h[0x9E] non-zero, mode 1) or bound but idle
// (h[0x9E] zero, mode 2).
//
// What the spinning thread is waiting for is done by another thread.
// func_8004C120 leaves a countdown in word[0xC] of each record it stamps, and
// func_8004C818 -- the alSyn player callback, run from alAudioFrame on the
// thread Thread_8004F050 runs -- decrements it once per audio frame and frees
// the record when it passes zero. One frame is all it needs, on hardware. In the
// port that frame never comes: N64ModernRuntime's threads are cooperative, so a
// thread that spins holds the only emulated CPU there is and the audio thread
// stays parked. Two call sites were captured that way -- entering a game mode
// (func_8002525C, mode 2, 16 idle-bound records) and the language option in the
// options menu (func_800252C8, mode 1, 2 playing records) -- both at ~118 million
// iterations, with the display lists and the audio stopping in the same second.
//
// Hence the handoff on a nonzero count, which is exactly when the caller is
// about to loop. Every other caller of this function pays only the scan.

extern "C" void wetrix_wait_handoff(uint8_t* rdram);

namespace {

// RDRAM reads matching recomp.h: N64 big-endian words are stored byte-swapped,
// so a word is a plain native read and a halfword lives at `addr ^ 2`. memcpy
// rather than a cast because the fields are only 2-byte aligned at best (the
// pool starts at 0x801774B0 with a stride of 0x130). Both are bounds-checked
// against the 8 MB of RDRAM, so a count or base read before func_8004BA7C has
// initialised them cannot turn this into a wild read.
int32_t pool_u32(const uint8_t* rdram, uint32_t addr) {
    const uint32_t offset = addr - 0x80000000u;
    if (offset > 8u * 1024u * 1024u - sizeof(int32_t)) {
        return 0;
    }

    int32_t value = 0;
    std::memcpy(&value, rdram + offset, sizeof(value));
    return value;
}

uint16_t pool_u16(const uint8_t* rdram, uint32_t addr) {
    const uint32_t offset = (addr ^ 2u) - 0x80000000u;
    if (offset > 8u * 1024u * 1024u - sizeof(uint16_t)) {
        return 0;
    }

    uint16_t value = 0;
    std::memcpy(&value, rdram + offset, sizeof(value));
    return value;
}

// How many times in a row the count has come back nonzero, i.e. how long the
// loop being run has been unable to drain. Reported on a doubling schedule,
// behind WETRIX_WAIT_LOG, because the interesting event is "this one never
// settles" and that is visible in a handful of lines rather than millions.
std::atomic<uint32_t> g_wait_polls{ 0 };
const bool g_wait_log = std::getenv("WETRIX_WAIT_LOG") != nullptr;

} // namespace

extern "C" void func_8004C1A0(uint8_t* rdram, recomp_context* ctx) {
    const int32_t mode = static_cast<int32_t>(ctx->r4);

    const uint32_t count = static_cast<uint32_t>(pool_u32(rdram, 0x80080BA4u));
    const uint32_t table = static_cast<uint32_t>(pool_u32(rdram, 0x80080BACu));

    // A count or base outside these ranges means the pool does not exist yet.
    uint32_t matches = 0;
    if (count <= 64u && table >= 0x80000000u && table < 0x80800000u) {
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t record = table + i * 0x130u;

            if (pool_u32(rdram, record) == 0) {
                continue;  // never allocated, or already released
            }

            const bool playing = pool_u16(rdram, record + 0x9Eu) != 0;
            if ((playing && (mode & 1) != 0) || (!playing && (mode & 2) != 0)) {
                ++matches;
            }
        }
    }

    ctx->r2 = static_cast<int32_t>(matches);

    if (matches == 0) {
        if (g_wait_log) {
            const uint32_t polls = g_wait_polls.exchange(0, std::memory_order_relaxed);
            if (polls != 0) {
                fprintf(stderr, "[wait] pool settled after %u polls\n", polls);
                fflush(stderr);
            }
        }
        return;
    }

    if (g_wait_log) {
        const uint32_t done = g_wait_polls.fetch_add(1, std::memory_order_relaxed);
        if (done == 0 || (done & (done - 1u)) == 0) {
            fprintf(stderr, "[wait] mode %d: %u record(s) still to release (poll %u)\n",
                    static_cast<int>(mode), matches, done + 1u);
            fflush(stderr);
        }
    }

    wetrix_wait_handoff(rdram);
}
