// Prototypes for the `_recomp` functions that the recompiled game code calls but
// that N64Recomp neither recompiles nor declares.
//
// N64Recomp writes a prototype into funcs.h for every function it recompiles, and
// for every name on its built-in "reimplemented" list. Everything else it leaves
// undeclared while still emitting the call -- so supplying the declaration is the
// port's job. This header is wired in through wetrix.toml's `recomp_include`,
// which N64Recomp prints verbatim at the top of funcs.h, so all 29 generated
// translation units see these without any generated file being edited by hand.
//
// Two kinds live here, declared for the same reason but supplied from different
// places:
//
//   * the 19 defined in src/ultra_shims.cpp, which this ROM needs and
//     N64ModernRuntime does not cover;
//   * osPfsChecker and osPfsNumFiles, which the runtime *does* implement
//     (librecomp/src/pak.cpp) -- it just defines them without a declaration.
//
// Why this only surfaced on Windows: an undeclared call is a warning, not an
// error, under C99 via GCC <= 13, which is what the Docker Linux image has. GCC
// 14 promoted -Wimplicit-function-declaration to an error by default, and MSYS2
// ships GCC 16.2. The Linux build was never correct here, only quiet -- the
// declarations are right either way.

#ifndef WETRIX_SHIMS_H
#define WETRIX_SHIMS_H

#include <stdint.h>

#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- COP0 and thread internals (scheduler-owned in ultramodern) ---
void __osGetSR_recomp(uint8_t* rdram, recomp_context* ctx);
void __osDispatchThread_recomp(uint8_t* rdram, recomp_context* ctx);
void __osPopThread_recomp(uint8_t* rdram, recomp_context* ctx);
void __osDequeueThread_recomp(uint8_t* rdram, recomp_context* ctx);

// --- the R4300 COUNT register ---
// osGetCount (func_80051140) reads it; wetrix.toml hooks the return to call this.
// A real 46.875 MHz counter (half the CPU clock) from the host clock, so the ROM's
// time and its RNG seeds vary like they do on hardware. WETRIX_DETERMINISTIC=1
// freezes it at 0, for runs that must repeat exactly (A/B captures).
uint32_t wetrix_count_now(void);

// --- hardware the runtime drives instead ---
void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx);
void __osSiDeviceBusy_recomp(uint8_t* rdram, recomp_context* ctx);
void __osSpGetStatus_recomp(uint8_t* rdram, recomp_context* ctx);
void __osTimerInterrupt_recomp(uint8_t* rdram, recomp_context* ctx);
void __osViSwapContext_recomp(uint8_t* rdram, recomp_context* ctx);
void __osSetCompare_recomp(uint8_t* rdram, recomp_context* ctx);

// Declared here rather than implemented here: librecomp defines it in
// ultra_translation.cpp, and its body is `assert(false)` -- which is a no-op in
// this build, because the runtime is compiled Release with NDEBUG. So a yield in
// the ROM becomes a yield the host does not perform. That is not obviously
// right, and it is worth revisiting alongside __osTimerInterrupt: the tell will
// be a game loop that spins instead of handing the CPU over. It is kept as-is
// for now because the map names this function, and a named function with no
// declaration fails the build outright.
void osYieldThread_recomp(uint8_t* rdram, recomp_context* ctx);

// --- device-busy probes, listed under `ignored` in wetrix.toml ---
//
// Named in config/us/symbol_addrs_libultra.txt and ignored so their MMIO bodies
// are never recompiled; these answer "device idle", which is correct on a host
// where DMA completes instantly. See the `ignored` block in wetrix.toml.
void __osAiDeviceBusy_recomp(uint8_t* rdram, recomp_context* ctx);
void __osDpDeviceBusy_recomp(uint8_t* rdram, recomp_context* ctx);
void __osSpDeviceBusy_recomp(uint8_t* rdram, recomp_context* ctx);
void __osSpSetStatus_recomp(uint8_t* rdram, recomp_context* ctx);

// --- Controller Pak / PFS ---
void __osCheckId_recomp(uint8_t* rdram, recomp_context* ctx);
void __osCheckPackId_recomp(uint8_t* rdram, recomp_context* ctx);
void __osRepairPackId_recomp(uint8_t* rdram, recomp_context* ctx);
void __osContAddressCrc_recomp(uint8_t* rdram, recomp_context* ctx);
void __osContDataCrc_recomp(uint8_t* rdram, recomp_context* ctx);
void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx);
void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx);
void __osPfsGetStatus_recomp(uint8_t* rdram, recomp_context* ctx);
void __osPfsRWInode_recomp(uint8_t* rdram, recomp_context* ctx);
void __osPfsSelectBank_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx);

// --- cartridge reads ---
//
// The one *live* hardware access in the recompiled output -- see the entry for
// osPiRawReadIo in config/us/symbol_addrs_libultra.txt. Unlike the device-busy
// probes above it cannot just answer "idle": its result is the data a caller
// asked for, so it goes through the runtime's own ROM PIO path.
void osPiRawReadIo_recomp(uint8_t* rdram, recomp_context* ctx);

// --- implemented by librecomp/src/pak.cpp, which publishes no header ---
void osPfsChecker_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsNumFiles_recomp(uint8_t* rdram, recomp_context* ctx);

// --- ROM functions the port supplies itself ---------------------------------
//
// These 14 are listed under `ignored` in wetrix.toml, so N64Recomp neither
// recompiles nor declares them -- and unlike the `_recomp` names above they keep
// their original symbol name, because the config's ignored list marks a function
// ignored without renaming it. They are defined in src/replaced_funcs.cpp.
//
// The first three groups are no-ops by design. The last two are not: the 5
// multi-entry functions are still to be written, and func_8004C1A0 carries the
// handoff that stops the sound-drain waits from deadlocking the emulated machine.

// Nothing calls it: gcc folded a shared code tail into another function.
void func_8005C250(uint8_t* rdram, recomp_context* ctx);

// The R4300 hardware layer -- exception machinery, TLB, register dump. There is
// no host equivalent, and nothing on the host can raise an N64 exception.
void func_80011100(uint8_t* rdram, recomp_context* ctx);
void func_8001126C(uint8_t* rdram, recomp_context* ctx);
void func_800115B8(uint8_t* rdram, recomp_context* ctx);
void func_8001167C(uint8_t* rdram, recomp_context* ctx);
void func_80052EC0(uint8_t* rdram, recomp_context* ctx);
void func_80061200(uint8_t* rdram, recomp_context* ctx);
void func_80061330(uint8_t* rdram, recomp_context* ctx);

// The 5 hand-written assembly functions whose bodies are shared between several
// entry points. A `jal` into one of these reaches replaced_funcs.cpp, which is
// where the missing behaviour has to be written. Currently a logging no-op.
void func_8004C754(uint8_t* rdram, recomp_context* ctx);
void func_80050B84(uint8_t* rdram, recomp_context* ctx);
void func_80059840(uint8_t* rdram, recomp_context* ctx);
void func_8005CC50(uint8_t* rdram, recomp_context* ctx);
void func_8005D160(uint8_t* rdram, recomp_context* ctx);

// The count every "stop the sound and wait for the pool to settle" loop in the
// ROM spins on. The port's definition is the ROM's own scan (transcribed from
// 0x8004C1A0) with a short handoff added whenever the count is nonzero, so that
// the loop can no longer starve the audio thread that has to satisfy it. The
// full account is in the `ignored` block of wetrix.toml and in group 4 of
// src/replaced_funcs.cpp.
void func_8004C1A0(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif

#endif // WETRIX_SHIMS_H
