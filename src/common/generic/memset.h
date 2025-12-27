/**
 * swr - a software rasterizer
 *
 * faster memset.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2025.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

// no namespace here, since the file is meant to be included in the "utils" namespace through "utils.h"

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
    const auto aligned_size = (size & (~7));
    std::uintptr_t i;
    for(i = 0; i < aligned_size; i += 8)
    {
        std::memcpy(reinterpret_cast<std::byte*>(buf) + i, &c, 8);
    }
    for(; i < size; ++i)
    {
        (reinterpret_cast<std::byte*>(buf))[i] = (reinterpret_cast<std::byte*>(&c))[i & 7];
    }
    return buf;
}

/**
 * memset which writes 2*32 bits at once, built from (c << 32) | c. See memset64 for an explanation.
 */
inline void* memset32(
  void* buf,
  std::uint32_t c,
  std::size_t size)
{
    return memset64(buf, (static_cast<std::uint64_t>(c) << 32) | static_cast<std::uint64_t>(c), size);
}
