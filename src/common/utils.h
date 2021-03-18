/**
 * swr - a software rasterizer
 * 
 * utility functions.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

// include dependencies
#include <list>
#include <memory>    /* std::align */
#include <cstring>   /* std::memcpy */
#include <algorithm> /* std::find */
#include <cassert>   /* assert */

namespace utils
{

/**
 * memset which writes 64 bits at once.
 * 
 * from http://stackoverflow.com/questions/108866/is-there-memset-that-accepts-integers-larger-than-char:
 * 
 *   When you assign to a pointer, the compiler assumes that the pointer is aligned to the type's natural alignment; 
 *   for uint64_t, that is 8 bytes. memcpy() makes no such assumption. On some hardware unaligned accesses are impossible, 
 *   so assignment is not a suitable solution unless you know unaligned accesses work on the hardware with small or no penalty, 
 *   or know that they will never occur, or both. The compiler will replace small memcpy()s and memset()s with more suitable 
 *   code so it is not as horrible is it looks; but if you do know enough to guarantee assignment will always work and your 
 *   profiler tells you it is faster, you can replace the memcpy with an assignment. The second for() loop is present in 
 *   case the amount of memory to be filled is not a multiple of 64 bits. If you know it always will be, you can simply drop 
 *   that loop.
 */
inline void* memset64( void* buf, size_t size, uint64_t c )
{
	const auto aligned_size = (size & (~7));
	uintptr_t i;
	for (i = 0; i < aligned_size; i += 8)
    {
        std::memcpy( reinterpret_cast<char*>(buf) + i, &c, 8 );
    }
    for( ; i < size; ++i )
    {
        (reinterpret_cast<char*>(buf))[i] = (reinterpret_cast<char*>(&c))[i&7];
    }
    return buf;
}

/**
 * memset which writes 2*32 bits at once, built from (c << 32) | c. See memset64 for an explanation.
 */
inline void* memset32( void* buf, size_t size, uint32_t c )
{
    uint64_t __c = (static_cast<uint64_t>(c) << 32) | static_cast<uint64_t>(c);

	const auto aligned_size = (size & (~7));
	uintptr_t i;
	for( i = 0; i < aligned_size; i+=8 )
    {
        std::memcpy( reinterpret_cast<char*>(buf) + i, &__c, 8 );
    }
    for( ; i < size; ++i )
    {
        (reinterpret_cast<char*>(buf))[i] = (reinterpret_cast<char*>(&__c))[i&7];
    }

    return buf;
}

/*
 * Support for aligned data.
 */
namespace alignment
{

/** alignment size used by SSE code */
const int sse = 16; 

} /* namespace utils::alignment */

/**
 * Create aligned memory by resizing a std::vector.
 */
template <typename T>
inline T* align_vector( std::size_t alignment, std::size_t size, std::vector<T>& v )
{
    v.resize(size + alignment-1);
    auto buffer_ptr = v.data();
    std::size_t buffer_size = v.size();
    return reinterpret_cast<T*>(std::align(alignment, size, reinterpret_cast<void*&>(buffer_ptr), buffer_size));
}

/*
 * simple slot map.
 */

/**
 * A container of objects that keeps track of empty slots. The free slot re-usage  pattern is LIFO.
 *
 * Some remarks:
 *  *) The data is not automatically compacted/freed.
 *  *) freeing only marks slots as "free" (e.g., without invalidating or destructing them).
 */
template<typename T, typename container=std::vector<T>>
struct slot_map
{
    /** Data. */
    container data;
    
    /** List of free object slots. */
    std::list<size_t> free_slots;
    
    /** insert a new item.. */
    size_t push( const T& item )
    {
        // first fill empty slots.
        if(free_slots.size())
        {
            auto i = free_slots.back();
            free_slots.pop_back();
            
            data[i] = item;
            return i;
        }
        
        data.emplace_back( item );
        return data.size()-1;
    }
    
    /** mark a slot as free. */
    void free( size_t i )
    {
        assert(i<data.size());
        free_slots.push_back(i);
    }

    /** check if an index is in the list of free slots. */
    bool is_free( size_t i )
    {
        return std::find(free_slots.begin(), free_slots.end(), i) != free_slots.end();
    }
    
    /** clear data and list of free slots. */
    void clear()
    {
        data.clear();
        free_slots.clear();
    }
    
    /** query size. */
    size_t size() const
    {
        assert(data.size() - free_slots.size() >= 0);
        return data.size() - free_slots.size();
    }

    /** query the current capacity. */
    size_t capacity() const
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
    const T& operator[](size_t i) const
    {
        assert(i<data.size());
        return data[i];
    }

    /**
     * element access. the caller has to take care of the validity of the index.
     * that is, we do not check if the supplied index not in the free_slots list.
     */
    T& operator[](size_t i)
    {
        assert(i<data.size());
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
 * !!todo: non-GNUC code is untested.
 */

#if defined(__GNUC__)

#include <x86intrin.h> /* for __rdtsc */

#define lfence _mm_lfence
#define rdtsc __rdtsc

#elif defined(_MSC_VER)

#include <intrin.h>

#define lfence _mm_lfence
#define rdtsc __rdtsc

#endif

namespace utils
{

#ifdef DO_BENCHMARKING

/** read the time stamp counter */
inline uint64_t get_tsc()
{
    lfence();
    uint64_t ret = rdtsc();
    lfence();
    return ret;
}

/** start a measurement. */
inline void clock(uint64_t& counter)
{
    counter -= get_tsc();
}

/** end a measurement. */
inline void unclock(uint64_t& counter)
{
    counter += get_tsc();
}

#else

inline uint64_t get_tsc() { return 0; }
inline void clock(uint64_t&) {}
inline void unclock(uint64_t&) {}

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
    rect( int in_x_min, int in_x_max, int in_y_min, int in_y_max )
    : x_min{in_x_min}, x_max{in_x_max}, y_min{in_y_min}, y_max{in_y_max}
    {
        assert(in_x_min<=in_x_max);
        assert(in_y_min<=in_y_max);
    }
};

} /* namespace utils */