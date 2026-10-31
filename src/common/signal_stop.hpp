#ifndef MEP_COMMON_SIGNAL_STOP_HPP_
#define MEP_COMMON_SIGNAL_STOP_HPP_
#include <csignal>
// Ctrl-C handling shared by every lab program.
//
// A signal can arrive between any two machine instructions, so a handler may
// only touch a volatile sig_atomic_t -- the one type the standard promises can
// be written atomically from a handler. It must not call printf, malloc, or
// any libgpiod function: those are not async-signal-safe, and if the signal
// lands while the same function is already running the process can deadlock.
//
// So the handler does exactly one thing: it raises a flag. The loop notices,
// falls out, and the destructors that already own the hardware release it on
// the way out. That is the whole point of the RAII wrappers.
//
// Each lab program is a single translation unit, so the flag living in an
// unnamed namespace is correct here: one program, one flag.

namespace { // NOLINT
    volatile sig_atomic_t g_stop_requested = 0;
    void HandleStopSignal(int signal_number) {
        (void) signal_number;
        g_stop_requested = 1;
    }
} // namespace

// SIGINT is Ctrl-C and SIGTERM is a polite kill. SIGHUP matters in this lab
// because it is what arrives when an SSH session drops, which is how these
// programs usually die on a Raspberry Pi.
inline bool InstallStopHandler() {
    return std::signal(SIGINT, HandleStopSignal) != SIG_ERR && std::signal(SIGTERM, HandleStopSignal) != SIG_ERR && std::signal(SIGHUP, HandleStopSignal) != SIG_ERR;
}
inline bool StopRequested() { return g_stop_requested != 0; }
#endif // MEP_COMMON_SIGNAL_STOP_HPP_