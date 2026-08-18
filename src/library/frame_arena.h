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
 * A per-frame arena that reuses object storage between frames.
 *
 * The arena owns a contiguous array of objects. The first `size()` elements are
 * considered live for the current frame. Calling `reset()` marks all objects as
 * dormant without destroying them, allowing subsequent frames to reuse the same
 * storage without reallocating or reconstructing objects.
 *
 * Newly allocated slots reuse existing objects whenever possible. Therefore,
 * previously stored state is preserved until overwritten by the caller.
 *
 * This container is intended for high-frequency frame allocation where object
 * lifetime is naturally bounded by a frame.
 *
 * @tparam T A default-initializable type.
 * @tparam Allocator An allocator. Defaults to `std::allocator`.
 */
template<
  std::default_initializable T,
  typename Allocator = std::allocator<T>>
    requires(std::is_copy_assignable_v<T> && std::is_move_assignable_v<T>)
class frame_arena
{
    /** Object storage. */
    std::vector<T, Allocator> storage;

    /**
     * Live objects.
     *
     * @note `used <= storage.size()` always holds.
     * */
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
            storage[used] = T{std::forward<Args>(args)...};
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
     * Returns a reference to an existing object.
     *
     * If storage is reused, the object's previous contents are preserved.
     * The caller is responsible for assigning or reinitializing every field
     * before use.
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
     *     need to be initialized by the caller.
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

    /** View over a subspan of the live slots for the current frame. */
    [[nodiscard]]
    std::span<T> subspan(
      std::size_t offset,
      std::size_t count = std::dynamic_extent)
    {
        return span().subspan(offset, count);
    }

    /** View over a subspan of the live slots for the current frame. */
    [[nodiscard]]
    std::span<const T> subspan(
      std::size_t offset,
      std::size_t count = std::dynamic_extent) const
    {
        return span().subspan(offset, count);
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

    /**
     * Marks all objects as dormant.
     *
     * No destructors are run. Existing object state and any memory owned by those
     * objects are retained for reuse by subsequent frames.
     */
    void reset() noexcept
    {
        used = 0;
    }

    /**
     * Ensures at least `n` objects can become live without reallocating.
     *
     * Calling `reserve()` once per frame with the previous peak size avoids pointer
     * and span invalidation during allocation.
     */
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
