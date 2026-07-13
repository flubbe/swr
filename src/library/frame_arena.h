/**
 * swr - a software rasterizer
 *
 * Per-frame bump arena.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <cassert>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace swr
{

namespace impl
{

/**
 * A per-frame bump arena that reuses object storage across frames.
 *
 * Objects at indices `[0, size())` are live; the rest are dormant but retain their
 * allocated memory.
 */
template<
  std::default_initializable T,
  typename Allocator = std::allocator<T>>
    requires(std::is_copy_assignable_v<T> && std::is_move_assignable_v<T>)
class frame_arena
{
    /** Object storage. */
    std::vector<T, Allocator> storage;

    /** Live objects. */
    std::size_t used{0};

public:
    using value_type = T;
    using storage_type = std::vector<T, Allocator>;

    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;

    using iterator = storage_type::iterator;
    using const_iterator = storage_type::const_iterator;

    /** Emplace a value. */
    template<typename... Args>
        requires std::constructible_from<T, Args...>
    T& emplace_back(Args&&... args)
    {
        if(used >= storage.size())
        {
            storage.emplace_back(std::forward<Args>(args)...);
        }
        else
        {
            storage[used] = std::move(T{std::forward<Args>(args)...});
        }
        return storage[used++];
    }

    /** Copy-construct value at the end. */
    void push_back(const T& value)
    {
        if(used >= storage.size())
        {
            storage.emplace_back(value);
        }
        else
        {
            storage[used] = value;
        }
        used++;
    }

    /** Move-construct value at the end. */
    void push_back(T&& value)
    {
        if(used >= storage.size())
        {
            storage.emplace_back(std::move(value));
        }
        else
        {
            storage[used] = std::move(value);
        }
        used++;
    }

    /**
     * Allocate a value and return its reference.
     *
     * @note The returned object might contain previous state and
     *       needs to be initialized by the caller.
     */
    T& allocate()
    {
        if(used >= storage.size())
        {
            storage.emplace_back();
        }
        return storage[used++];
    }

    /**
     * Bump-allocate `n` contiguous slots and return a span over them.
     *
     * @note The returned objects might contain previous state and
     *       need to be initialized by the caller.
     *
     * @warning The returned span is only stable as long as no subsequent
     *     allocation causes the internal storage to reallocate. Pre-reserve the
     *     arena at the end of each frame (via `reserve()`) to prevent mid-frame
     *     reallocation from invalidating live spans.
     */
    std::span<T> allocate_range(std::size_t n)
    {
        if(n == 0)
        {
            return {};
        }
        if(used + n > storage.size())
        {
            storage.resize(used + n);
        }
        T* p = &storage[used];
        used += n;
        return {p, n};
    }

    /** View over the live slots for the current frame. */
    [[nodiscard]]
    std::span<T> span()
    {
        return std::span{storage}.first(used);
    }

    /** View over the live slots for the current frame. */
    [[nodiscard]]
    std::span<const T> span() const
    {
        return std::span{storage}.first(used);
    }

    /** Return the storage data. */
    T* data() noexcept
    {
        return storage.data();
    }

    /** Return the storage data. */
    const T* data() const noexcept
    {
        return storage.data();
    }

    /*
     * Iterators over the active range.
     */

    iterator begin() noexcept
    {
        return storage.begin();
    }

    iterator end() noexcept
    {
        return storage.begin() + used;
    }

    const_iterator begin() const noexcept
    {
        return storage.begin();
    }

    const_iterator end() const noexcept
    {
        return storage.begin() + used;
    }

    const_iterator cbegin() const noexcept
    {
        return storage.cbegin();
    }

    const_iterator cend() const noexcept
    {
        return storage.cbegin() + used;
    }

    /** Element access. */
    T& operator[](std::size_t i)
    {
        assert(i < used);
        return storage[i];
    }

    /** Element access. */
    const T& operator[](std::size_t i) const
    {
        assert(i < used);
        return storage[i];
    }

    /** Return whether the arena is empty (i.e., has no live objects). */
    [[nodiscard]]
    bool empty() const noexcept
    {
        return used == 0;
    }

    /** Return the current size (live object count). */
    [[nodiscard]]
    std::size_t size() const noexcept
    {
        return used;
    }

    /** Return the storage capacity. */
    [[nodiscard]]
    std::size_t capacity() const noexcept
    {
        return storage.capacity();
    }

    /** Reset size. Retains storage capacity, objects, and inner buffers. */
    void reset() noexcept
    {
        used = 0;
    }

    /** Ensure storage can hold at least `n` elements without reallocation. */
    void reserve(std::size_t n)
    {
        storage.reserve(n);
    }

    /** Clear storage. */
    void clear()
    {
        storage.clear();
        used = 0;
    }

    /** Release all storage (call at shutdown). */
    void release()
    {
        clear();
        storage.shrink_to_fit();
    }
};

} /* namespace impl */

} /* namespace swr */
