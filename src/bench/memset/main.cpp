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

/* Google benchmark */
#include <benchmark/benchmark.h>

/* make sure we include the non-SIMD version of memset */
#ifdef SWR_USE_SIMD
#    undef SWR_USE_SIMD
#endif /* SWR_USE_SIMD */

#include "../../common/utils.h"

/* include SIMD version of memset */
namespace simd
{
#include "../../common/memset_sse.h"
} /* namespace simd */

/** small size of memory */
constexpr auto memset_test_small_size = 33;    // intentionally unaligned.

/** medium size of memory */
constexpr auto memset_test_medium_size = 4096 + 35;    // intentionally unaligned.

/** large size of memory. */
constexpr auto memset_test_size = 640 * 480 * 16 * 4;

static void bench_fill_n(benchmark::State& state)
{
    std::size_t mem_size = state.range(0);

    std::vector<std::byte> mem;
    mem.resize(mem_size);

    for(auto _: state)
    {
        std::fill_n(mem.begin(), mem_size, static_cast<std::byte>('0'));
    }

    benchmark::DoNotOptimize(mem);
}
BENCHMARK(bench_fill_n)->Arg(memset_test_small_size)->Arg(memset_test_medium_size)->Arg(memset_test_size);

static void bench_fill_n_32(benchmark::State& state)
{
    std::size_t mem_size = state.range(0);

    std::vector<std::byte> mem;
    mem.resize(mem_size);

    std::size_t aligned_fill_size = mem_size & ~static_cast<std::size_t>(0x1f);
    std::size_t tail_size = mem_size & static_cast<std::size_t>(0x1f);

    for(auto _: state)
    {
        std::fill_n(reinterpret_cast<std::uint32_t*>(mem.data()), aligned_fill_size >> 2, static_cast<std::uint32_t>(0x30303030));
        std::fill_n(mem.begin() + aligned_fill_size, tail_size, static_cast<std::byte>(0x30));
    }

    benchmark::DoNotOptimize(mem);
}
BENCHMARK(bench_fill_n_32)->Arg(memset_test_small_size)->Arg(memset_test_medium_size)->Arg(memset_test_size);

static void bench_memset(benchmark::State& state)
{
    std::size_t mem_size = state.range(0);

    std::vector<std::byte> mem;
    mem.resize(mem_size);

    for(auto _: state)
    {
        std::memset(mem.data(), '0', mem_size);
    }

    benchmark::DoNotOptimize(mem);
}
BENCHMARK(bench_memset)->Arg(memset_test_small_size)->Arg(memset_test_medium_size)->Arg(memset_test_size);

static void bench_memset32(benchmark::State& state)
{
    std::size_t mem_size = state.range(0);

    std::vector<std::byte> mem;
    mem.resize(mem_size);

    for(auto _: state)
    {
        const std::uint32_t c = static_cast<std::uint32_t>('0') | (static_cast<std::uint32_t>('0') << 8) | (static_cast<std::uint32_t>('0') << 16) | (static_cast<std::uint32_t>('0') << 24);
        utils::memset32(mem.data(), c, mem_size);
    }

    benchmark::DoNotOptimize(mem);
}
BENCHMARK(bench_memset32)->Arg(memset_test_small_size)->Arg(memset_test_medium_size)->Arg(memset_test_size);

static void bench_memset64(benchmark::State& state)
{
    std::size_t mem_size = state.range(0);

    std::vector<std::byte> mem;
    mem.resize(mem_size);

    for(auto _: state)
    {
        const std::uint64_t c1 = static_cast<std::uint64_t>('0') | (static_cast<std::uint64_t>('0') << 8) | (static_cast<std::uint64_t>('0') << 16) | (static_cast<std::uint64_t>('0') << 24);
        const std::uint64_t c2 = c1 | (c1 << 32);
        utils::memset64(mem.data(), c2, mem_size);
    }

    benchmark::DoNotOptimize(mem);
}
BENCHMARK(bench_memset64)->Arg(memset_test_small_size)->Arg(memset_test_medium_size)->Arg(memset_test_size);

static void bench_memset32_simd(benchmark::State& state)
{
    std::size_t mem_size = state.range(0);

    std::vector<std::byte> mem;
    mem.resize(mem_size);

    for(auto _: state)
    {
        const std::uint32_t c = static_cast<std::uint32_t>('0') | (static_cast<std::uint32_t>('0') << 8) | (static_cast<std::uint32_t>('0') << 16) | (static_cast<std::uint32_t>('0') << 24);
        simd::memset32(mem.data(), c, mem_size);
    }

    benchmark::DoNotOptimize(mem);
}
BENCHMARK(bench_memset32_simd)->Arg(memset_test_small_size)->Arg(memset_test_medium_size)->Arg(memset_test_size);

static void bench_memset64_simd(benchmark::State& state)
{
    std::size_t mem_size = state.range(0);

    std::vector<std::byte> mem;
    mem.resize(mem_size);

    for(auto _: state)
    {
        const std::uint64_t c1 = static_cast<std::uint64_t>('0') | (static_cast<std::uint64_t>('0') << 8) | (static_cast<std::uint64_t>('0') << 16) | (static_cast<std::uint64_t>('0') << 24);
        const std::uint64_t c2 = c1 | (c1 << 32);
        simd::memset64(mem.data(), c2, mem_size);
    }

    benchmark::DoNotOptimize(mem);
}
BENCHMARK(bench_memset64_simd)->Arg(memset_test_small_size)->Arg(memset_test_medium_size)->Arg(memset_test_size);

static void bench_memset128_simd(benchmark::State& state)
{
    std::size_t mem_size = state.range(0);

    std::vector<std::byte> mem;
    mem.resize(mem_size);

    for(auto _: state)
    {
        const std::uint64_t c1 = static_cast<std::uint64_t>('0') | (static_cast<std::uint64_t>('0') << 8) | (static_cast<std::uint64_t>('0') << 16) | (static_cast<std::uint64_t>('0') << 24);
        const std::uint64_t c2 = c1 | (c1 << 32);
        simd::memset128(mem.data(), _mm_set_epi64x(c2, c2), mem_size);
    }

    benchmark::DoNotOptimize(mem);
}
BENCHMARK(bench_memset128_simd)->Arg(memset_test_small_size)->Arg(memset_test_medium_size)->Arg(memset_test_size);

BENCHMARK_MAIN();
