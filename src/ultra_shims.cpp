// The `_recomp` symbols this ROM calls that neither N64ModernRuntime nor the
// recompiled code itself provides.
//
// N64Recomp classifies libultra names into three lists (src/symbol_lists.cpp),
// and the difference between them matters a lot:
//
//   * reimplemented_funcs -- the runtime supplies an implementation, so the ROM
//     body is not recompiled. librecomp implements 45 of the 70 symbols this
//     ROM needs.
//   * renamed_funcs -- these are *renamed and still recompiled*. libc and libm
//     are here (strlen, sprintf, __sinf, ldiv) because a ROM implementation of
//     `strlen` cannot coexist with the host's, so it is emitted as
//     `strlen_recomp` and the host's is left alone. Nothing is missing for
//     these -- the ROM's own code runs. Do not write a shim for one: it will
//     collide with the generated definition at link time.
//   * ignored_funcs -- not recompiled, and not supplied by anything. This file
//     exists for these, plus the handful of reimplemented names that this
//     particular ROM needs and the runtime happens not to cover.
//
// That leaves 19 of the 70. They are not one problem, they are three, and the
// confidence in each is not the same.
//
// Address convention: recompiled code holds full 64-bit KSEG0 addresses, and
// recomp.h converts them by subtracting 0xFFFFFFFF80000000 (see MEM_W). The
// helpers below do the same in both directions.

#include <ultramodern/ultra64.h>
#include <ultramodern/ultramodern.hpp>
#include <librecomp/addresses.hpp>
#include <librecomp/game.hpp>
#include <librecomp/rsp.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>

#include "recomp.h"

// Declares every function defined below. The generated recompiled code needs the
// same declarations; it gets them by way of `recomp_include` in wetrix.toml, and
// including the header here as well keeps the two from drifting apart.
#include "wetrix_shims.h"

namespace {

template <typename T>
T* to_ptr(uint8_t* rdram, gpr addr) {
    return reinterpret_cast<T*>(rdram + (static_cast<uint64_t>(addr) - 0xFFFFFFFF80000000ULL));
}

} // namespace

// ---------------------------------------------------------------------------
// COP0 and thread internals. The riskiest group in the file.
// ---------------------------------------------------------------------------

// Clean: recomp.h already exposes the runtime's own COP0 Status accessor.
extern "C" void __osGetSR_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = cop0_status_read(ctx);
}

// RISKY. These are the parts of libultra's scheduler that ultramodern replaces
// wholesale -- __osDispatchThread is the routine that performs the context
// switch on hardware, and ultramodern/threads.cpp plus scheduling.cpp do that
// work instead. Nudging the runtime's queue is the closest equivalent available,
// but this needs testing against a real thread handoff before it is trusted.
extern "C" void __osDispatchThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    ultramodern::check_running_queue(rdram);
}

// These two manipulate thread queues that ultramodern manages itself, so the
// ROM's queue surgery has nothing left to do.
extern "C" void __osPopThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;
}

extern "C" void __osDequeueThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}

// ---------------------------------------------------------------------------
// Hardware the runtime manages elsewhere.
// ---------------------------------------------------------------------------

// TODO: this drives controller reads. ultramodern starts SI transfers from
// send_si_message(), and wiring this to it is the correct fix, but the
// sequencing against the game's own SI message handling needs checking first.
extern "C" void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;
}

extern "C" void __osSpGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;    // SP idle
}

// No-op: ultramodern owns timer interrupts (init_timers).
extern "C" void __osTimerInterrupt_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}

// No-op: ultramodern/vi.cpp owns the VI registers via get_vi_regs().
extern "C" void __osViSwapContext_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}

// ---------------------------------------------------------------------------
// Device-busy probes.
//
// These four are not "hardware the runtime owns", they are libultra internals
// that read or write one register and nothing else:
//
//   __osAiDeviceBusy   reads AI_STATUS, tests AI_STATUS_BUSY
//   __osDpDeviceBusy   reads DPC_STATUS, tests DPC_STATUS_DMA_BUSY (0x100)
//   __osSpDeviceBusy   reads SP_STATUS, tests DMA busy / full (0x300)
//   __osSpSetStatus    writes SP_STATUS and returns
//
// They were missing from the symbol map, so N64Recomp recompiled them, and the
// register access resolved to `rdram + 0x24000000`-ish -- an access violation
// rather than a no-op. Naming them in config/us/symbol_addrs_libultra.txt and
// listing them under `ignored` in wetrix.toml drops the bodies.
//
// Answering "not busy" is not a placeholder: on the host there is no DMA engine,
// every transfer is already finished, so idle is the true state. The three
// probes are exactly the loops the game spins on waiting for a device to become
// ready, and returning idle is what lets those loops fall through.
extern "C" void __osAiDeviceBusy_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;    // AI idle
}

extern "C" void __osDpDeviceBusy_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;    // RDP idle
}

extern "C" void __osSpDeviceBusy_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;    // RSP idle
}

// The serial interface differs from the three above in where the call comes
// from. The ROM's own __osSpRawReadIo and __osSpRawWriteIo are deliberately left
// unnamed in the map -- nothing implements them, so they must be recompiled, and
// both begin by refusing the access while the SI is busy. That makes this the
// one probe a recompiled body still reaches, which is why it is a shim here and
// not a name in the map: naming the caller is not available as a fix when the
// caller has to stay.
//
// The answer is the same as for the others, and true for the same reason. The
// port runs every SI transfer to completion inside the call that starts it, so
// by the time anything asks, the interface is idle. Returning 0 is not a
// placeholder -- it is the state of a bus with no outstanding transfer, and the
// two functions that ask are the two that would otherwise hand back PFS_ERR_NOPACK
// forever without ever reading a byte.
extern "C" void __osSiDeviceBusy_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;    // SI idle
}

// ---------------------------------------------------------------------------
// __osSetCompare(u32 value) -- the R4300 COMPARE register.
//
// Unlike the probes above this is not a question with an honest answer: the host
// has no cycle counter and no timer interrupt, so there is nothing to write and
// nothing that could observe the write. It reaches the port at all because the
// map deliberately leaves __osSetTimerIntr and __osInsertTimer unnamed -- nothing
// implements them, so they are recompiled -- and their bodies are what call
// this. Dropping the write keeps the emulated timer degenerate in the same way
// the port already makes it: osGetCount is patched to return 0 and
// __osTimerInterrupt does nothing, because ultramodern owns timer interrupts.
//
// The value is worth keeping in one place rather than discarding, so that a
// future osGetTime or scheduler fix has something to read.
// ---------------------------------------------------------------------------
uint32_t wetrix_compare_register = 0;

// The COUNT register, as osGetCount returns it (see the hook in wetrix.toml).
//
// It counts at half the CPU clock, 93.75 MHz / 2 = 46.875 MHz, from boot. This
// reads the host's monotonic clock the same way: 0.046875 counts per nanosecond
// since the first call. The ROM derives osGetTime from it, and func_8003BDE4
// mixes osGetTime into what looks like a random seed, so a frozen value makes
// every run draw from the same sequence (the rain clumping in one spot fits).
//
// WETRIX_DETERMINISTIC=1 gives the old behaviour: always 0. That is what
// side-by-side comparisons of two renderers want, because both then see the same
// "random" game.
extern "C" uint32_t wetrix_count_now(void) {
    static const bool frozen = std::getenv("WETRIX_DETERMINISTIC") != nullptr;
    if (frozen) return 0;
    static const auto t0 = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    return static_cast<uint32_t>(static_cast<uint64_t>(ns * 0.046875));
}

extern "C" void __osSetCompare_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    wetrix_compare_register = (uint32_t)ctx->r4;
}

// A status write has nothing to act on. Worth flagging for later: SP_STATUS_HALT
// and SP_STATUS_BROKE are how the game signals and detects task completion, so
// once there is a real RSP this needs to reach ultramodern's RSP layer rather
// than being dropped -- the same way __osSpGetStatus above is only honest while
// there is no SP doing anything.
extern "C" void __osSpSetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}

// ---------------------------------------------------------------------------
// Controller Pak / PFS.
//
// Reporting "no pak present" is the honest first pass. The Pak is optional,
// nothing in the game requires one, and every one of these functions returns an
// error code rather than taking an action -- so this makes the Pak menu say
// there is no Pak, instead of corrupting memory. It also keeps the eleven
// functions that would otherwise need a filesystem implementation out of the
// way until the port actually runs.
// ---------------------------------------------------------------------------

namespace {
constexpr gpr PFS_ERR_NOPACK = 2;
}

extern "C" void __osCheckId_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osCheckPackId_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osRepairPackId_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osContAddressCrc_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;
}

extern "C" void __osContDataCrc_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;
}

extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osPfsGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osPfsRWInode_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osPfsSelectBank_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = PFS_ERR_NOPACK;
}

// osPfsIsPlug writes a bitmask of which controllers have a Pak to its first
// argument and returns 0. With no Paks the mask is empty.
extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint8_t* pattern = to_ptr<uint8_t>(rdram, ctx->r4);
    if (pattern != nullptr) {
        *pattern = 0;
    }
    ctx->r2 = 0;
}

// ---------------------------------------------------------------------------
// Cartridge reads -- the one *live* hardware access in the recompiled output.
// ---------------------------------------------------------------------------

// osPiRawReadIo(devAddr, data): poll PI_STATUS until the cart is idle, then copy
// the word at (osRomBase | devAddr | 0xA0000000) into *data, and return 0.
//
// Every other hardware access left in the recompiled code sits in a function
// nothing reachable calls, so it never executes; this one does, from two sites
// that are both reachable from the entrypoint. Naming it in the symbol map is
// what removes its body from the recompiled output (see
// config/us/symbol_addrs_libultra.txt), and the runtime supplies the two halves
// that actually matter: osPiRawReadIo is on N64Recomp's `ignored` list rather
// than its `reimplemented` one, so answering this call is the port's job.
//
// Why it cannot be answered from rdram: the ROM's body builds that address as a
// register and reads it, and MEM_B resolves a KSEG1 address to rdram + 0x30001003
// -- 600MB out of bounds, which is the fault that led here. Masking the segment
// bits off devAddr leaves the physical cart address, and the runtime's own PIO
// helper converts that to a ROM offset and does the byte swap. Doing the swap by
// hand here would be one more place to get endianness wrong; this reuses the path
// a real PI transfer takes, which is also what makes the two callers see the data
// the ROM actually contains rather than zeroes.
//
// `osRomBase` is 0xB0000000, set by the runtime during recomp::init alongside
// osTvType and osMemSize, so the physical address is rom_base | devAddr.
extern "C" void osPiRawReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t dev_addr = static_cast<uint32_t>(ctx->r4);
    const uint32_t physical = recomp::rom_base | (dev_addr & 0x0FFFFFFFu);

    recomp::do_rom_pio(rdram, ctx->r5, physical);

    ctx->r2 = 0;
}
