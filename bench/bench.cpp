// Benchmarks built on Google Benchmark.
//
// BM_Throughput  : two threads. One iteration = move kItemsPerIter items producer -> consumer.
//                  The consumer thread is spawned inside the function each iteration (GB's ->Threads()
//                  runs the same body on every thread, which doesn't fit asymmetric producer/consumer roles).
//                  Args = {producer_cpu, consumer_cpu}
// BM_SingleThread: one thread, push+pop. Measures instruction cost alone, no coherence traffic.
//
// Examples:
//   ./build/bench --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
//   ./build/bench --benchmark_filter='Throughput.*pcpu:0'
#include "ref/boost_queue.hpp"
#include "ref/mutex_queue.hpp"
#include "spsc/spsc_queue.hpp"

#include <benchmark/benchmark.h>
#include <pthread.h>
#include <sched.h>

#include <cstdint>
#include <thread>

namespace {

constexpr std::size_t kCapacity = 65536;
constexpr std::uint64_t kItemsPerIter = 1 << 20;

bool pin_to_cpu(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

// Restores the main thread's affinity once the benchmark finishes.
struct AffinityGuard {
    cpu_set_t saved;
    AffinityGuard() { pthread_getaffinity_np(pthread_self(), sizeof(saved), &saved); }
    ~AffinityGuard() { pthread_setaffinity_np(pthread_self(), sizeof(saved), &saved); }
};

template <typename Q>
void BM_Throughput(benchmark::State& state) {
    const int cpu_p = static_cast<int>(state.range(0));
    const int cpu_c = static_cast<int>(state.range(1));

    AffinityGuard guard;
    if (!pin_to_cpu(cpu_p)) {
        state.SkipWithError("failed to pin producer");
        return;
    }

    Q q(kCapacity);
    for (auto _ : state) {
        std::uint64_t sum = 0;
        std::thread consumer([&] {
            pin_to_cpu(cpu_c);
            std::uint64_t v, s = 0;
            for (std::uint64_t i = 0; i < kItemsPerIter; ++i) {
                while (!q.try_pop(v)) {
                }
                s += v;
            }
            sum = s;
        });
        for (std::uint64_t i = 0; i < kItemsPerIter; ++i) {
            while (!q.try_push(i)) {
            }
        }
        consumer.join();

        if (sum != kItemsPerIter * (kItemsPerIter - 1) / 2) {
            state.SkipWithError("checksum mismatch");
            return;
        }
    }
    state.SetItemsProcessed(state.iterations() * kItemsPerIter);
}

template <typename Q>
void BM_SingleThread(benchmark::State& state) {
    Q q(kCapacity);
    std::uint64_t v = 0;
    for (auto _ : state) {
        q.try_push(v);
        q.try_pop(v);
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}

// cpu0-3: P-cores (private L2), cpu4-7: LP E-cores (shared L2)
#define SPSC_THROUGHPUT(Q)                      \
    BENCHMARK_TEMPLATE(BM_Throughput, Q)        \
        ->Args({0, 1}) /* P <-> P */            \
        ->Args({4, 5}) /* E <-> E, shared L2 */ \
        ->ArgNames({"pcpu", "ccpu"})            \
        ->UseRealTime()                         \
        ->Unit(benchmark::kMillisecond)

using U64 = std::uint64_t;

SPSC_THROUGHPUT(spsc::Queue<U64>);
SPSC_THROUGHPUT(spsc::compare::MutexQueue<U64>);
SPSC_THROUGHPUT(spsc::compare::BoostQueue<U64>);

BENCHMARK_TEMPLATE(BM_SingleThread, spsc::Queue<U64>);
BENCHMARK_TEMPLATE(BM_SingleThread, spsc::compare::MutexQueue<U64>);
BENCHMARK_TEMPLATE(BM_SingleThread, spsc::compare::BoostQueue<U64>);

}  // namespace

BENCHMARK_MAIN();
