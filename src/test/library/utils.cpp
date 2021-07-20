/**
 * swr - a software rasterizer
 * 
 * test (some) utility functions.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE utility tests
#include <boost/test/unit_test.hpp>

/* make sure we include the non-SIMD version of memset */
#ifdef SWR_USE_SIMD
#    undef SWR_USE_SIMD
#endif /* SWR_USE_SIMD */

#include "../../common/utils.h"

/* include SIMD version of memset */
namespace simd
{
#define SWR_USE_SIMD
#include "../../common/utils.h"
#undef SWR_USE_SIMD
} /* namespace simd */

/*
 * uncomment this to test memsets for larger memory blocks
 */
//#define DO_LARGE_MEMSIZE_TESTS

/*
 * helpers.
 */

/** pack bytes into uint32_t, a=hi, d=lo */
static std::uint32_t pack32(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d)
{
    return (a << 24) | (b << 16) | (c << 8) | d;
}

/** pack uint32_t into uint64_t */
static std::uint64_t pack64(std::uint32_t hi, std::uint32_t lo)
{
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

/** pack 64-bit valus into _m128i, a=hi, b=lo */
static __m128i pack128(std::uint64_t hi, std::uint64_t lo)
{
    return _mm_set_epi64x(hi, lo);
}

/** check memory pattern. */
template<typename T>
void check_pattern(const std::vector<std::byte>& mem, std::uint32_t offset, T pattern)
{
    std::uint8_t* p = reinterpret_cast<std::uint8_t*>(&pattern);

    // check.
    auto chunks = (mem.size() - offset) / sizeof(T);

    std::uint32_t i;
    for(i = 0; i < chunks; i++)
    {
        const std::uint8_t* mem_ptr = reinterpret_cast<const std::uint8_t*>(&mem[i * sizeof(T) + offset]);
        for(std::size_t k = 0; k < sizeof(T); ++k)
        {
            BOOST_TEST(mem_ptr[k] == p[k]);
        }
    }

    for(std::size_t k = 0; i * sizeof(T) + k + offset < mem.size(); ++k)
    {
        const std::uint8_t* mem_ptr = reinterpret_cast<const std::uint8_t*>(mem.data());
        BOOST_TEST(mem_ptr[i * sizeof(T) + k + offset] == p[k]);
    }
}

/*
 * memory sizes.
 */

namespace memsize
{

/** < 64 B */
constexpr int small_aligned = 1 << 5;
constexpr int small_unaligned = 57;

/** ~ 4 KiB */
constexpr int medium_aligned = 1 << 12;
constexpr int medium_unaligned = (1 << 12) + 4321;

#ifdef DO_LARGE_MEMSIZE_TESTS
/** ~ 16 MiB */
constexpr int large_aligned = 1 << 24;
constexpr int large_unaligned = (1 << 24) + 77777;
#endif

} /* namespace memsize */

/*
 * tests.
 */

BOOST_AUTO_TEST_SUITE(utility_tests)

/* test pack and unpack functions. */
BOOST_AUTO_TEST_CASE(internal_pack)
{
    std::uint8_t fill8[4] = {0x00, 0xaa, 0x11, 0xcc};
    auto out32 = pack32(fill8[0], fill8[1], fill8[2], fill8[3]);
    BOOST_TEST((out32 & 0xff000000) == 0);
    BOOST_TEST((out32 & 0x00ff0000) == 0xaa0000);
    BOOST_TEST((out32 & 0x0000ff00) == 0x1100);
    BOOST_TEST((out32 & 0x000000ff) == 0xcc);

    std::uint8_t* out_ptr = reinterpret_cast<uint8_t*>(&out32);
    BOOST_TEST(out_ptr[3] == 0);
    BOOST_TEST(out_ptr[2] == 0xaa);
    BOOST_TEST(out_ptr[1] == 0x11);
    BOOST_TEST(out_ptr[0] == 0xcc);

    std::uint64_t out64 = pack64(out32, 0xffeeddbb);
    BOOST_TEST((out64 & 0xffffffff) == 0xffeeddbb);
    BOOST_TEST((out64 & 0xffffffff00000000) == 0xaa11cc00000000);

    __m128i out128 = pack128(pack64(0xaa11cc, 0xffeeddbb), pack64(0xaa11cc, 0xaabbccaa));
    uint32_t* ptr128 = reinterpret_cast<uint32_t*>(&out128);
    BOOST_TEST(ptr128[3] == 0xaa11cc);
    BOOST_TEST(ptr128[2] == 0xffeeddbb);
    BOOST_TEST(ptr128[1] == 0xaa11cc);
    BOOST_TEST(ptr128[0] == 0xaabbccaa);
}

/* check pattern matching */
BOOST_AUTO_TEST_CASE(internal_pattern_check)
{
    std::vector<std::byte> mem;

    mem.resize(1);
    *reinterpret_cast<std::uint8_t*>(&mem[0]) = 0x72;
    check_pattern<std::uint8_t>(mem, 0, 0x72);

    mem.resize(sizeof(std::uint32_t));
    *reinterpret_cast<std::uint32_t*>(&mem[0]) = 0x12345678;
    check_pattern<std::uint32_t>(mem, 0, 0x12345678);
    check_pattern<std::uint32_t>(mem, 1, 0x123456);
    check_pattern<std::uint32_t>(mem, 2, 0x1234);
    check_pattern<std::uint32_t>(mem, 3, 0x12);
}

/* memtest32 */
BOOST_AUTO_TEST_CASE(memset32)
{
    std::vector<std::byte> mem;

    for(int k = 0; k < 16; ++k)
    {
        mem.resize(memsize::small_aligned + k);
        utils::memset32(mem.data() + k, pack32('1', '2', '3', '4'), memsize::small_aligned);
        check_pattern<std::uint32_t>(mem, k, 0x31323334);

        mem.resize(memsize::small_unaligned + k);
        utils::memset32(mem.data() + k, pack32('5', '6', '7', '8'), memsize::small_unaligned);
        check_pattern<std::uint32_t>(mem, k, 0x35363738);

        mem.resize(memsize::medium_aligned + k);
        utils::memset32(mem.data() + k, pack32('1', '2', '3', '4'), memsize::medium_aligned);
        check_pattern<std::uint32_t>(mem, k, 0x31323334);

        mem.resize(memsize::medium_unaligned + k);
        utils::memset32(mem.data() + k, pack32('5', '6', '7', '8'), memsize::medium_unaligned);
        check_pattern<std::uint32_t>(mem, k, 0x35363738);

#ifdef DO_LARGE_MEMSIZE_TESTS
        mem.resize(memsize::large_aligned + k);
        utils::memset32(mem.data() + k, pack32('1', '2', '3', '4'), memsize::large_aligned);
        check_pattern<std::uint32_t>(mem, k, 0x31323334);

        mem.resize(memsize::large_unaligned + k);
        utils::memset32(mem.data() + k, pack32('5', '6', '7', '8'), memsize::large_unaligned);
        check_pattern<std::uint32_t>(mem, k, 0x35363738);
#endif
    }
}

/* simd memset32 */
BOOST_AUTO_TEST_CASE(simd_memset32)
{
    std::vector<std::byte> mem;

    for(int k = 0; k < 16; ++k)
    {
        mem.resize(memsize::small_aligned + k);
        simd::utils::memset32(mem.data() + k, pack32('1', '2', '3', '4'), memsize::small_aligned);
        check_pattern<std::uint32_t>(mem, k, 0x31323334);

        mem.resize(memsize::small_unaligned + k);
        simd::utils::memset32(mem.data() + k, pack32('5', '6', '7', '8'), memsize::small_unaligned);
        check_pattern<std::uint32_t>(mem, k, 0x35363738);

        mem.resize(memsize::medium_aligned + k);
        simd::utils::memset32(mem.data() + k, pack32('1', '2', '3', '4'), memsize::medium_aligned);
        check_pattern<std::uint32_t>(mem, k, 0x31323334);

        mem.resize(memsize::medium_unaligned + k);
        simd::utils::memset32(mem.data() + k, pack32('5', '6', '7', '8'), memsize::medium_unaligned);
        check_pattern<std::uint32_t>(mem, k, 0x35363738);

#ifdef DO_LARGE_MEMSIZE_TESTS
        mem.resize(memsize::large_aligned + k);
        simd::utils::memset32(mem.data() + k, pack32('1', '2', '3', '4'), memsize::large_aligned);
        check_pattern<std::uint32_t>(mem, k, 0x31323334);

        mem.resize(memsize::large_unaligned + k);
        simd::utils::memset32(mem.data() + k, pack32('5', '6', '7', '8'), memsize::large_unaligned);
        check_pattern<std::uint32_t>(mem, k, 0x35363738);
#endif
    }
}

BOOST_AUTO_TEST_CASE(simd_memset64)
{
    std::vector<std::byte> mem;

    const uint64_t c1 = pack64(pack32('1', '2', '3', '4'), pack32('5', '6', '7', '8'));
    const uint64_t c2 = pack64(pack32('8', '7', '6', '5'), pack32('4', '3', '2', '1'));

    for(int k = 0; k < 16; ++k)
    {
        mem.resize(memsize::small_aligned + k);
        simd::utils::memset64(mem.data() + k, c1, memsize::small_aligned);
        check_pattern<std::uint64_t>(mem, k, 0x3132333435363738);

        mem.resize(memsize::small_unaligned + k);
        simd::utils::memset64(mem.data() + k, c2, memsize::small_unaligned);
        check_pattern<std::uint64_t>(mem, k, 0x3837363534333231);

        mem.resize(memsize::medium_aligned + k);
        simd::utils::memset64(mem.data() + k, c1, memsize::medium_aligned);
        check_pattern<std::uint64_t>(mem, k, 0x3132333435363738);

        mem.resize(memsize::medium_unaligned + k);
        simd::utils::memset64(mem.data() + k, c2, memsize::medium_unaligned);
        check_pattern<std::uint64_t>(mem, k, 0x3837363534333231);

#ifdef DO_LARGE_MEMSIZE_TESTS
        mem.resize(memsize::large_aligned + k);
        simd::utils::memset64(mem.data() + k, c1, memsize::large_aligned);
        check_pattern<std::uint64_t>(mem, k, 0x3132333435363738);

        mem.resize(memsize::large_unaligned + k);
        simd::utils::memset64(mem.data() + k, c2, memsize::large_unaligned);
        check_pattern<std::uint64_t>(mem, k, 0x3837363534333231);
#endif
    }
}

BOOST_AUTO_TEST_CASE(simd_memset128)
{
    std::vector<std::byte> mem;

    const __m128i c1 = pack128(pack64(pack32('1', '2', '3', '4'), pack32('5', '6', '7', '8')), pack64(pack32('8', '1', '7', '2'), pack32('6', '3', '5', '4')));
    const __m128i c2 = pack128(pack64(pack32('8', '7', '6', '5'), pack32('4', '3', '2', '1')), pack64(pack32('4', '5', '3', '6'), pack32('2', '7', '1', '8')));

    for(int k = 0; k < 16; ++k)
    {
        mem.resize(memsize::small_aligned + k);
        simd::utils::memset128(mem.data() + k, c1, memsize::small_aligned);
        check_pattern<__m128i>(mem, k, c1);

        mem.resize(memsize::small_unaligned + k);
        simd::utils::memset128(mem.data() + k, c2, memsize::small_unaligned);
        check_pattern<__m128i>(mem, k, c2);

        mem.resize(memsize::medium_aligned + k);
        simd::utils::memset128(mem.data() + k, c1, memsize::medium_aligned);
        check_pattern<__m128i>(mem, k, c1);

        mem.resize(memsize::medium_unaligned + k);
        simd::utils::memset128(mem.data() + k, c2, memsize::medium_unaligned);
        check_pattern<__m128i>(mem, k, c2);

#ifdef DO_LARGE_MEMSIZE_TESTS
        mem.resize(memsize::large_aligned + k);
        simd::utils::memset128(mem.data() + k, c1, memsize::large_aligned);
        check_pattern<__m128i>(mem, k, c1);

        mem.resize(memsize::large_unaligned + k);
        simd::utils::memset128(mem.data() + k, c2, memsize::large_unaligned);
        check_pattern<__m128i>(mem, k, c2);
#endif
    }
}

BOOST_AUTO_TEST_SUITE_END();
