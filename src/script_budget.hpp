#pragma once
#include "langmake/mobile_platform.hpp"
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#elif defined(LANGMAKE_MOBILE)
#include <time.h>
#endif

namespace lm::detail {
// Private clock seam: fixtures can supply counters without sleeping or touching
// another process. Games never select a clock or change the execution allowance.
struct CpuReading {
    std::int64_t ns = 0;
    bool available = false;
    unsigned error = 0;
};
inline CpuReading thread_cpu() noexcept {
#ifdef _WIN32
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        return {0, false, GetLastError()};
    const auto units = [](FILETIME time) {
        return (std::uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return {std::int64_t((units(kernel) + units(user)) * 100), true, 0};
#elif defined(LANGMAKE_MOBILE) && defined(CLOCK_THREAD_CPUTIME_ID)
    timespec time{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) == 0)
        return {std::int64_t(time.tv_sec) * 1000000000 + time.tv_nsec, true, 0};
    return {0, false, unsigned(errno)};
#else
    return {};
#endif
}

class ScriptBudget {
public:
    static constexpr std::int64_t allowance_ns = 50000000;
    static constexpr std::int64_t sample_interval_ns = 1000000;
    enum class Policy { require_cpu, prefer_cpu, elapsed };
    struct Reading { std::int64_t elapsed_ns; CpuReading cpu; };
    using Reader = Reading (*)(void*);
    struct Sample {
        bool available = false, cpu_available = false, fallback = false;
        bool exceeded = false, clock_failed = false;
        std::int64_t cpu_ns = 0, elapsed_ns = 0;
        std::uint64_t queries = 0;
        unsigned os_error = 0;
        const char* clock = "unavailable";
        const char* reason = "";
    };
    static constexpr Policy platform_policy() {
#ifdef _WIN32
        return Policy::require_cpu;
#elif defined(LANGMAKE_MOBILE)
        return Policy::prefer_cpu;
#else
        return Policy::elapsed;
#endif
    }
    explicit ScriptBudget(Policy policy = platform_policy(), Reader reader = nullptr, void* context = nullptr)
        : policy_(policy), reader_(reader), context_(context) {}
    static std::int64_t monotonic_ns() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void begin() {
        sample_ = {};
        sample_.available = true;
        thread_ = std::this_thread::get_id();
        const auto reading = read();
        began_ = last_query_ = reading.elapsed_ns;
        cpu_began_ = reading.cpu.ns;
        sample_.queries = policy_ == Policy::elapsed ? 0 : 1;
        sample_.clock = policy_ == Policy::elapsed ? "monotonic_elapsed" :
#ifdef _WIN32
            "windows_thread_cpu";
#else
            "thread_cpu";
#endif
        sample_.cpu_available = policy_ != Policy::elapsed && reading.cpu.available;
        if (policy_ != Policy::elapsed && !reading.cpu.available)
            clock_failure("cpu_clock_initialization_failed", reading.cpu.error);
    }
    // Monotonic time controls query cadence, never a Windows CPU verdict.
    bool poll(bool force = false) {
        if (!sample_.available) return false;
        if (thread_ != std::this_thread::get_id()) {
            sample_.clock_failed = true;
            sample_.cpu_available = false;
            sample_.reason = "executing_thread_changed";
            return true;
        }
        const auto elapsed = reader_ ? read() : Reading{monotonic_ns(), {}};
        sample_.elapsed_ns = elapsed.elapsed_ns - began_;
        if (sample_.elapsed_ns < 0) {
            sample_.clock_failed = true; sample_.reason = "monotonic_clock_regressed";
            return true;
        }
        if (sample_.clock_failed) return true;
        if (policy_ != Policy::elapsed && !sample_.fallback &&
            (force || elapsed.elapsed_ns - last_query_ >= sample_interval_ns)) {
            const auto cpu = reader_ ? elapsed.cpu : thread_cpu();
            ++sample_.queries;
            last_query_ = elapsed.elapsed_ns;
            if (!cpu.available || cpu.ns < cpu_began_)
                clock_failure(cpu.available ? "cpu_clock_regressed" : "cpu_clock_read_failed", cpu.error);
            else { sample_.cpu_available = true; sample_.cpu_ns = cpu.ns - cpu_began_; }
        }
        const auto consumed = policy_ == Policy::elapsed || sample_.fallback ? sample_.elapsed_ns : sample_.cpu_ns;
        sample_.exceeded = sample_.exceeded || (!sample_.clock_failed && consumed >= allowance_ns);
        return sample_.exceeded || sample_.clock_failed;
    }
    const Sample& sample() const noexcept { return sample_; }
private:
    Reading read() { return reader_ ? reader_(context_) : Reading{monotonic_ns(), policy_ == Policy::elapsed ? CpuReading{} : thread_cpu()}; }
    void clock_failure(const char* reason, unsigned error) {
        sample_.reason = reason; sample_.os_error = error; sample_.cpu_available = false;
        if (policy_ == Policy::prefer_cpu) {
            sample_.fallback = true; sample_.clock = "monotonic_elapsed";
        } else sample_.clock_failed = true;
    }
    Policy policy_;
    Reader reader_;
    void* context_;
    Sample sample_;
    std::thread::id thread_;
    std::int64_t began_ = 0, cpu_began_ = 0, last_query_ = 0;
};
}
