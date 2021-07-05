/**
 * swr - a software rasterizer
 * 
 * memset benchmark.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <vector>
#include <cstring>

#include "fmt/format.h"

/* Google benchmark */
#include <benchmark/benchmark.h>

/* make sure we include the non-SIMD version of memset */
#ifdef SWR_USE_SIMD
#    undef SWR_USE_SIMD
#endif

#include "../../common/utils.h"

/* include SIMD version of memset */
namespace simd
{
#define SWR_USE_SIMD
#include "../../common/utils.h"
#undef SWR_USE_SIMD
} /* namespace simd */

/** small size of memory */
constexpr auto memset_test_small_size = 33;    // intentionally unaligned.

/** large size of memory. */
constexpr auto memset_test_size = 640 * 480 * 16 * 4;

static void bench_fill_n(benchmark::State& state)
{
    std::vector<std::byte> mem;
    mem.resize(memset_test_size);

    for(auto _: state)
    {
        std::fill_n(mem.begin(), memset_test_size, static_cast<std::byte>('0'));
        benchmark::DoNotOptimize(mem);
    }
}
BENCHMARK(bench_fill_n);

static void bench_memset(benchmark::State& state)
{
    std::vector<std::byte> mem;
    mem.resize(memset_test_size);

    for(auto _: state)
    {
        std::memset(mem.data(), '0', memset_test_size);
        benchmark::DoNotOptimize(mem);
    }
}
BENCHMARK(bench_memset);

static void bench_memset32(benchmark::State& state)
{
    std::vector<std::byte> mem;
    mem.resize(memset_test_size);

    for(auto _: state)
    {
        const uint32_t c = static_cast<uint32_t>('0') | (static_cast<uint32_t>('0') << 8) | (static_cast<uint32_t>('0') << 16) | (static_cast<uint32_t>('0') << 24);
        utils::memset32(mem.data(), c, memset_test_size);
        benchmark::DoNotOptimize(mem);
    }
}
BENCHMARK(bench_memset32);

static void bench_memset64(benchmark::State& state)
{
    std::vector<std::byte> mem;
    mem.resize(memset_test_size);

    for(auto _: state)
    {
        const uint64_t c1 = static_cast<uint64_t>('0') | (static_cast<uint64_t>('0') << 8) | (static_cast<uint64_t>('0') << 16) | (static_cast<uint64_t>('0') << 24);
        const uint64_t c2 = c1 | (c1 << 32);
        utils::memset64(mem.data(), c2, memset_test_size);
        benchmark::DoNotOptimize(mem);
    }
}
BENCHMARK(bench_memset64);

static void bench_memset32_simd(benchmark::State& state)
{
    std::vector<std::byte> mem;
    mem.resize(memset_test_size);

    for(auto _: state)
    {
        const uint32_t c = static_cast<uint32_t>('0') | (static_cast<uint32_t>('0') << 8) | (static_cast<uint32_t>('0') << 16) | (static_cast<uint32_t>('0') << 24);
        simd::utils::memset32(mem.data(), c, memset_test_size);
        benchmark::DoNotOptimize(mem);
    }
}
BENCHMARK(bench_memset32_simd);

static void bench_memset64_simd(benchmark::State& state)
{
    std::vector<std::byte> mem;
    mem.resize(memset_test_size);

    for(auto _: state)
    {
        const uint64_t c1 = static_cast<uint64_t>('0') | (static_cast<uint64_t>('0') << 8) | (static_cast<uint64_t>('0') << 16) | (static_cast<uint64_t>('0') << 24);
        const uint64_t c2 = c1 | (c1 << 32);
        simd::utils::memset64(mem.data(), c2, memset_test_size);
        benchmark::DoNotOptimize(mem);
    }
}
BENCHMARK(bench_memset64_simd);

static void bench_memset128_simd(benchmark::State& state)
{
    std::vector<std::byte> mem;
    mem.resize(memset_test_size);

    for(auto _: state)
    {
        const uint64_t c1 = static_cast<uint64_t>('0') | (static_cast<uint64_t>('0') << 8) | (static_cast<uint64_t>('0') << 16) | (static_cast<uint64_t>('0') << 24);
        const uint64_t c2 = c1 | (c1 << 32);
        simd::utils::memset128(mem.data(), _mm_set_epi64x(c2, c2), memset_test_size);
        benchmark::DoNotOptimize(mem);
    }
}
BENCHMARK(bench_memset128_simd);

constexpr int mult = 1024;

static void bench_memset_small(benchmark::State& state)
{
    std::vector<std::byte> mem;
    mem.resize(memset_test_small_size * mult);

    for(auto _: state)
    {
        for(int k = 0; k < mult; ++k)
        {
            std::memset(mem.data(), '0', memset_test_small_size * k);
        }
        benchmark::DoNotOptimize(mem);
    }
}
BENCHMARK(bench_memset_small);

static void bench_memset128_simd_small(benchmark::State& state)
{
    std::vector<std::byte> mem;
    mem.resize(memset_test_small_size * mult);

    const uint64_t c1 = static_cast<uint64_t>('0') | (static_cast<uint64_t>('0') << 8) | (static_cast<uint64_t>('0') << 16) | (static_cast<uint64_t>('0') << 24);
    const uint64_t c2 = c1 | (c1 << 32);
    auto c = _mm_set_epi64x(c2, c2);

    for(auto _: state)
    {
        for(int k = 0; k < mult; ++k)
        {
            simd::utils::memset128(mem.data(), c, memset_test_small_size * k);
        }
        benchmark::DoNotOptimize(mem);
    }
}
BENCHMARK(bench_memset128_simd_small);

BENCHMARK_MAIN();
