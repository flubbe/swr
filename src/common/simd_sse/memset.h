/**
 * swr - a software rasterizer
 *
 * faster memset (SSE version).
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2025.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

// no namespace here, since the file is meant to be included in the "utils" namespace through "utils.h"

/**
 * use SIMD for memset. try to write in 16-byte chunks. assumes that buf starts on a 16-byte boundary.
 */
inline void* memset128_aligned(
  void* buf,
  __m128i c,
  std::size_t size)
{
    auto chunks = (size & (~15)) >> 4;
    __m128i* ptr = reinterpret_cast<__m128i*>(buf);

    while(chunks--)
    {
        _mm_stream_si128(ptr++, c);
    }
    _mm_sfence();

    // write remaining bytes.
    std::size_t tail = reinterpret_cast<std::uintptr_t>(buf) + size - reinterpret_cast<std::uintptr_t>(ptr);
    for(std::size_t i = 0; i < tail; ++i)
    {
        reinterpret_cast<std::byte*>(ptr)[i] = reinterpret_cast<std::byte*>(&c)[i];
    }

    return buf;
}

inline void* memset128(
  void* buf,
  __m128i c,
  std::size_t size)
{
    constexpr std::size_t memset_small_size = 16384;

    // small sizes.
    if(size < memset_small_size)
    {
        const auto aligned_size = (size & (~15));
        std::uintptr_t i;
        for(i = 0; i < aligned_size; i += 16)
        {
            std::memcpy(reinterpret_cast<std::byte*>(buf) + i, &c, 16);
        }
        for(; i < size; ++i)
        {
            (reinterpret_cast<std::byte*>(buf))[i] = (reinterpret_cast<std::byte*>(&c))[i & 15];
        }

        return buf;
    }

    std::size_t unaligned_start = 0x10 - (reinterpret_cast<std::uintptr_t>(buf) & 0xF);
    size -= unaligned_start;

    std::uint8_t* ptr = reinterpret_cast<std::uint8_t*>(buf);
    while(unaligned_start--)
    {
        *ptr++ = *reinterpret_cast<std::uint8_t*>(&c);
        c = _mm_or_si128(_mm_srli_si128(c, 1), _mm_slli_si128(c, 15));
    }

    memset128_aligned(ptr, c, size);

    return buf;
}

/**
 * memset which writes 64 bits at once.
 *
 * from http://stackoverflow.com/questions/108866/is-there-memset-that-accepts-integers-larger-than-char:
 *
 *   When you assign to a pointer, the compiler assumes that the pointer is aligned to the type's natural alignment;
 *   for std::uint64_t, that is 8 bytes. memcpy() makes no such assumption. On some hardware unaligned accesses are impossible,
 *   so assignment is not a suitable solution unless you know unaligned accesses work on the hardware with small or no penalty,
 *   or know that they will never occur, or both. The compiler will replace small memcpy()s and memset()s with more suitable
 *   code so it is not as horrible is it looks; but if you do know enough to guarantee assignment will always work and your
 *   profiler tells you it is faster, you can replace the memcpy with an assignment. The second for() loop is present in
 *   case the amount of memory to be filled is not a multiple of 64 bits. If you know it always will be, you can simply drop
 *   that loop.
 */
inline void* memset64(
  void* buf,
  std::uint64_t c,
  std::size_t size)
{
    return memset128(buf, _mm_set_epi64x(c, c), size);
}

/**
 * memset which writes 2*32 bits at once, built from (c << 32) | c. See memset64 for an explanation.
 */
inline void* memset32(
  void* buf,
  std::uint32_t c,
  std::size_t size)
{
    return memset128(buf, _mm_set1_epi32(c), size);
}
