/**
 * swr - a software rasterizer
 *
 * utility functions.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

// include dependencies
#include <list>
#include <memory>    /* std::align, std::allocator_traits */
#include <cstring>   /* std::memcpy */
#include <algorithm> /* std::find */
#include <cassert>   /* assert */
#include <limits>    /* std::numeric_limits<std::size_t>::max() */
#include <new>       /* operator new[], operator delete[] */

/*
 * thread pool support.
 */
#include "concurrency_utils/thread_pool.h"

namespace utils
{

#ifdef SWR_USE_SIMD

/**
 * use SIMD for memset. try to write in 16-byte chunks. assumes that buf starts on a 16-byte boundary.
 */
inline void* memset128_aligned(void* buf, __m128i c, std::size_t size)
{
    auto chunks = (size & (~15)) >> 4;
    __m128i* ptr = reinterpret_cast<__m128i*>(buf);

    while(chunks--)
    {
        _mm_stream_si128(ptr++, c);
    }
    _mm_sfence();

    // write remaining bytes.
    std::size_t tail = reinterpret_cast<uintptr_t>(buf) + size - reinterpret_cast<uintptr_t>(ptr);
    for(std::size_t i = 0; i < tail; ++i)
    {
        reinterpret_cast<std::byte*>(ptr)[i] = reinterpret_cast<std::byte*>(&c)[i];
    }

    return buf;
}

inline void* memset128(void* buf, __m128i c, std::size_t size)
{
    constexpr std::size_t memset_small_size = 16384;

    // small sizes.
    if(size < memset_small_size)
    {
        const auto aligned_size = (size & (~15));
        uintptr_t i;
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

    std::size_t unaligned_start = 0x10 - (reinterpret_cast<uintptr_t>(buf) & 0xF);
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

#endif /* SWR_USE_SIMD */

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
inline void* memset64(void* buf, std::uint64_t c, std::size_t size)
{
#ifdef SWR_USE_SIMD
    return memset128(buf, _mm_set_epi64x(c, c), size);
#else  /* SWR_USE_SIMD */
    const auto aligned_size = (size & (~7));
    uintptr_t i;
    for(i = 0; i < aligned_size; i += 8)
    {
        std::memcpy(reinterpret_cast<std::byte*>(buf) + i, &c, 8);
    }
    for(; i < size; ++i)
    {
        (reinterpret_cast<std::byte*>(buf))[i] = (reinterpret_cast<std::byte*>(&c))[i & 7];
    }
    return buf;
#endif /* SWR_USE_SIMD */
}

/**
 * memset which writes 2*32 bits at once, built from (c << 32) | c. See memset64 for an explanation.
 */
inline void* memset32(void* buf, std::uint32_t c, std::size_t size)
{
#ifdef SWR_USE_SIMD
    return memset128(buf, _mm_set1_epi32(c), size);
#else  /* SWR_USE_SIMD */
    return memset64(buf, (static_cast<std::uint64_t>(c) << 32) | static_cast<std::uint64_t>(c), size);
#endif /* SWR_USE_SIMD */
}

/*
 * Support for aligned data.
 */
namespace alignment
{

/** alignment size used by SSE code */
const int sse = 16;

} /* namespace alignment */

/**
 * Create aligned memory by resizing a std::vector.
 *
 * @param alignment The alignment to use.
 * @param count The count of T's to align the memory for (i.e., count*sizeof(T) is the byte size of the requested buffer).
 * @param v The vector that will hold the buffer.
 * @returns Aligned memory for holding 'count' elements of type T.
 */
template<typename T, typename Allocator>
inline T* align_vector(std::size_t alignment, std::size_t count, std::vector<T, Allocator>& v)
{
    v.resize(count + alignment - 1);
    auto buffer_ptr = v.data();
    std::size_t buffer_size = v.size() * sizeof(T);
    return reinterpret_cast<T*>(std::align(alignment, count * sizeof(T), reinterpret_cast<void*&>(buffer_ptr), buffer_size));
}

/**
 * Align a parameter according to the specified alignment.
 *
 * @param alignment The alignment to use. Has to be a power of two.
 * @param p The parameter to align.
 * @returns A number bigger or equal to p, satisfying the alignment requirement.
 */
template<typename T>
inline T align(std::size_t alignment, T p)
{
    return reinterpret_cast<T>((reinterpret_cast<uintptr_t>(p) + (alignment - 1)) & ~(alignment - 1));
}

/**
 * References:
 *  1. https://en.cppreference.com/w/cpp/container/vector/resize
 *  2. https://stackoverflow.com/questions/21028299/is-this-behavior-of-vectorresizesize-type-n-under-c11-and-boost-container/21028912#21028912
 *
 * Allocator adaptor that interposes construct() calls to
 * convert value initialization into default initialization.
 */
template<typename T, typename A = std::allocator<T>>
class default_init_allocator : public A
{
    using traits = std::allocator_traits<A>;

public:
    template<typename U>
    struct rebind
    {
        using other = default_init_allocator<U, typename traits::template rebind_alloc<U>>;
    };

    using A::A;

    template<typename U>
    void construct(U* ptr) noexcept(std::is_nothrow_default_constructible<U>::value)
    {
        ::new(static_cast<void*>(ptr)) U;
    }
    template<typename U, typename... Args>
    void construct(U* ptr, Args&&... args)
    {
        traits::construct(static_cast<A&>(*this), ptr, std::forward<Args>(args)...);
    }
};

template<typename T1, typename A1, typename T2, typename A2>
bool operator==([[maybe_unused]] const default_init_allocator<T1, A1>& lhs, [[maybe_unused]] const default_init_allocator<T2, A2>& rhs) noexcept
{
    return true;
}

template<typename T1, typename A1, typename T2, typename A2>
bool operator!=([[maybe_unused]] const default_init_allocator<T1, A1>& lhs, [[maybe_unused]] const default_init_allocator<T2, A2>& rhs) noexcept
{
    return false;
}

/** Default allocator. */
template<typename T>
using allocator = default_init_allocator<T>;

/*
 * simple slot map.
 */

/**
 * A container of objects that keeps track of empty slots. The free slot re-usage pattern is LIFO.
 * The internal container needs to support the operations emplace_back, size, clear, shrink_to_fit, operator[].
 *
 * Some remarks:
 *  *) The data is not automatically compacted/freed.
 *  *) freeing only marks slots as "free" (e.g., without invalidating or destructing them).
 */
template<typename T, typename container = std::vector<T>>
struct slot_map
{
    /** data. */
    container data;

    /** list of free object slots. */
    std::list<std::size_t> free_slots;

    /** insert a new item. */
    std::size_t push(const T& item)
    {
        // first fill empty slots.
        if(free_slots.size())
        {
            auto i = free_slots.back();
            free_slots.pop_back();

            data[i] = item;
            return i;
        }

        data.emplace_back(item);
        return data.size() - 1;
    }

    /** insert a new item.. */
    std::size_t push(T&& item)
    {
        // first fill empty slots.
        if(free_slots.size())
        {
            auto i = free_slots.back();
            free_slots.pop_back();

            data[i] = std::move(item);
            return i;
        }

        data.emplace_back(std::move(item));
        return data.size() - 1;
    }

    /** mark a slot as free. */
    void free(std::size_t i)
    {
        assert(i < data.size());
        free_slots.emplace_back(i);
    }

    /** check if an index is in the list of free slots. */
    bool is_free(std::size_t i)
    {
        return std::find(free_slots.begin(), free_slots.end(), i) != free_slots.end();
    }

    /** clear data and list of free slots. */
    void clear()
    {
        data.clear();
        free_slots.clear();
    }

    /** shrink to fit elements. */
    void shrink_to_fit()
    {
        data.shrink_to_fit();
    }

    /** query size. */
    std::size_t size() const
    {
        assert(data.size() - free_slots.size() >= 0);
        return data.size() - free_slots.size();
    }

    /** query the current capacity. */
    std::size_t capacity() const
    {
        return data.size();
    }

    /*
     * element access.
     */

    /**
     * element access. the caller has to take care of the validity of the index.
     * that is, we do not check if the supplied index not in the free_slots list.
     */
    const T& operator[](std::size_t i) const
    {
        assert(i < data.size());
        return data[i];
    }

    /**
     * element access. the caller has to take care of the validity of the index.
     * that is, we do not check if the supplied index not in the free_slots list.
     */
    T& operator[](std::size_t i)
    {
        assert(i < data.size());
        return data[i];
    }
};

} /* namespace utils */

/*
 * CPU cycles/TSC measurement.
 *
 * Some comments:
 *  1) The measurement overhead itself is not taken care of and seems to ba at about 27-37 cycles.
 *     It seems that the cycle count also is expected to fluctuate a bit.
 *  2) The cycle count may not provide accurate results on all platforms, so use it with care.
 *  3) OS context switches may affect the output.
 *  4) Thread execution may shift to a different CPU core with a different TSC.
 *
 * TODO non-GNUC code is untested.
 */

#if defined(__x86_64__) || defined(_M_X64)

#    if defined(__GNUC__)

#        include <x86intrin.h> /* for __rdtsc */

#        define lfence _mm_lfence
#        define rdtsc  __rdtsc

#    elif defined(_MSC_VER)

#        include <intrin.h>

#        define lfence _mm_lfence
#        define rdtsc  __rdtsc

#    endif

#endif

namespace utils
{

#ifdef DO_BENCHMARKING

/** read the time stamp counter */
inline std::uint64_t get_tsc()
{
    lfence();
    std::uint64_t ret = rdtsc();
    lfence();
    return ret;
}

/** start a measurement. */
inline void clock(std::uint64_t& counter)
{
    counter -= get_tsc();
}

/** end a measurement. */
inline void unclock(std::uint64_t& counter)
{
    counter += get_tsc();
}

#else

inline std::uint64_t get_tsc()
{
    return 0;
}
inline void clock(std::uint64_t&)
{
}
inline void unclock(std::uint64_t&)
{
}

#endif /* DO_BENCHMARKING */

/*
 * rectangle.
 */

/** a rectangle, given as a pair (x_min,y_min), (x_max,y_max) */
struct rect
{
    /** x dimensions. */
    int x_min{0}, x_max{0};

    /** y dimensions. */
    int y_min{0}, y_max{0};

    /** default constructor. */
    rect() = default;

    /** constructor. */
    rect(int in_x_min, int in_x_max, int in_y_min, int in_y_max)
    : x_min{in_x_min}
    , x_max{in_x_max}
    , y_min{in_y_min}
    , y_max{in_y_max}
    {
        assert(in_x_min <= in_x_max);
        assert(in_y_min <= in_y_max);
    }
};

/*
 * powers of two.
 */

/** check if a given argument is a power of two. */
constexpr bool is_power_of_two(std::size_t c)
{
    return (c & (c - 1)) == 0;
}

/**
 * get the next power of two of the argument. e.g. next_power_of_two(1)=2, next_power_of_two(2)=4.
 *
 * source: https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
 */
constexpr std::size_t round_to_next_power_of_two(std::size_t n)
{
    static_assert(sizeof(std::size_t) == 8, "Adjust this code for different sizes of std::size_t.");

    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;

    return n + 1;
}

} /* namespace utils */