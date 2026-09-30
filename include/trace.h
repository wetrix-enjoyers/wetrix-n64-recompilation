// The interface N64Recomp's trace mode expects.
//
// With `trace_mode = true` in wetrix.toml, N64Recomp prints a TRACE_ENTRY() at
// the top of every recompiled function and a TRACE_RETURN() before every return,
// and adds `#include "trace.h"` to funcs.h. It does not provide the header or the
// macros -- those are the port's, which is why this file exists.
//
// The point is the hang past the title screen. A crash tells you where you died;
// a spin tells you nothing, because the answer is the *history* rather than the
// address, and sampling thread instruction pointers (see src/stall_probe.cpp)
// only ever shows the loop that is still running. This records the calls that
// led into it.
//
// Keeping it cheap matters: the spinning loop calls one function ~3M times per
// second, and under trace mode that is 3M macro invocations. TRACE_ENTRY() is
// therefore a single store into a ring buffer, and repeats of the same function
// at the same depth are collapsed into a counter instead of consuming a slot.
// Without that collapsing, forty seconds of spin would overwrite every trace of
// how the game got there -- the instrument would erase its own evidence.
//
// The ring lives in src/trace.cpp, which is compiled as C++, not C: the
// generated code is C, so the interface here is C.

#ifndef WETRIX_TRACE_H
#define WETRIX_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Records one call. `name` is `__func__` from the caller, so no work is done to
// produce it -- it is a string literal whose address is constant.
void wetrix_trace_entry(const char* name);

// Pops one level. Called before every `return` in the generated code.
void wetrix_trace_return(void);

// Prints one thread's most recent entries to stderr, oldest first. `thread_id`
// is the native thread id the stall probe sampled (0 means "this thread"), and
// it matters that it is the stalled thread: each thread has its own ring, because
// a shared one is overwritten by the other threads' traffic while one thread is
// stuck, which is precisely when the history is wanted.
//
// Called by the stall probe at the moment a stall is detected, which is the only
// time it is useful.
void wetrix_trace_dump(uint64_t thread_id, int max_entries);

#ifdef __cplusplus
}
#endif

// The names N64Recomp emits.
//
// Note the semicolons *inside* the macros. N64Recomp prints the bare text
// `TRACE_ENTRY()` and `TRACE_RETURN()` with no trailing semicolon, so the macro
// has to supply it -- without it every generated line after a call fails to
// compile (the next statement becomes "expected ';' before ..."). Including it
// here is not a style choice, it is the interface.
//
// `__func__` is C99, so this compiles in the generated C as well as in C++.
#define TRACE_ENTRY() wetrix_trace_entry(__func__);
#define TRACE_RETURN() wetrix_trace_return();

#endif // WETRIX_TRACE_H
