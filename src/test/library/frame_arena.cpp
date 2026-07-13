/**
 * swr - a software rasterizer
 *
 * test frame_arena helpers.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* C++ headers */
#include <iterator>
#include <string>
#include <vector>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE frame_arena
#include <boost/test/unit_test.hpp>

/* user headers. */
#include "frame_arena.h"

namespace
{

struct tracked_value
{
    static inline std::size_t constructions{0};
    static inline std::size_t destructions{0};
    static inline std::size_t alive{0};

    int value{};

    explicit tracked_value(int v = 0)
    : value(v)
    {
        ++constructions;
        ++alive;
    }

    tracked_value(const tracked_value& other)
    : value(other.value)
    {
        ++constructions;
        ++alive;
    }

    tracked_value(tracked_value&& other) noexcept
    : value(other.value)
    {
        ++constructions;
        ++alive;
        other.value = 0;
    }

    ~tracked_value()
    {
        ++destructions;
        --alive;
    }

    tracked_value& operator=(const tracked_value& other)
    {
        value = other.value;
        return *this;
    }

    tracked_value& operator=(tracked_value&& other) noexcept
    {
        value = other.value;
        return *this;
    }

    static void reset_counters()
    {
        constructions = 0;
        destructions = 0;
        alive = 0;
    }
};
}    // namespace

BOOST_AUTO_TEST_SUITE(frame_arena)

BOOST_AUTO_TEST_CASE(lifecycle_and_reset)
{
    swr::impl::frame_arena<int> arena;

    BOOST_TEST(arena.empty());
    BOOST_TEST(arena.size() == 0u);

    arena.emplace_back(7);
    arena.push_back(8);

    BOOST_TEST(arena.size() == 2u);
    BOOST_TEST(arena[0] == 7);
    BOOST_TEST(arena[1] == 8);

    auto span = arena.span();
    BOOST_TEST(span.size() == 2u);
    BOOST_TEST(span[1] == 8);

    arena.reset();

    BOOST_TEST(arena.empty());
    BOOST_TEST(arena.size() == 0u);
    BOOST_TEST(arena.capacity() >= 2u);

    auto& first = arena.allocate();
    first = 11;
    auto& second = arena.allocate();
    second = 12;

    BOOST_TEST(arena.size() == 2u);
    BOOST_TEST(arena[0] == 11);
    BOOST_TEST(arena[1] == 12);
}

BOOST_AUTO_TEST_CASE(allocate_range_and_std_like_interface)
{
    swr::impl::frame_arena<std::string> arena;
    arena.reserve(8);

    auto span = arena.allocate_range(3);
    span[0] = "alpha";
    span[1] = "beta";
    span[2] = "gamma";

    BOOST_TEST(arena.size() == 3u);
    BOOST_TEST(arena[0] == "alpha");
    BOOST_TEST(arena[1] == "beta");
    BOOST_TEST(arena[2] == "gamma");

    std::vector<std::string> collected;
    for(const auto& value: arena)
    {
        collected.emplace_back(value);
    }

    BOOST_REQUIRE_EQUAL(collected.size(), 3u);
    BOOST_CHECK_EQUAL(collected[0], "alpha");
    BOOST_CHECK_EQUAL(collected[1], "beta");
    BOOST_CHECK_EQUAL(collected[2], "gamma");

    const auto& const_arena = arena;
    std::vector<std::string> const_collected;
    for(const auto& value: const_arena)
    {
        const_collected.emplace_back(value);
    }

    BOOST_REQUIRE_EQUAL(const_collected.size(), 3u);
    BOOST_CHECK_EQUAL(const_collected[2], "gamma");

    BOOST_TEST(std::distance(arena.begin(), arena.end()) == 3);
}

BOOST_AUTO_TEST_CASE(clear_and_release)
{
    tracked_value::reset_counters();

    swr::impl::frame_arena<tracked_value> arena;
    arena.reserve(4);
    arena.emplace_back(1);
    arena.emplace_back(2);
    arena.emplace_back(3);

    BOOST_TEST(arena.size() == 3u);
    BOOST_TEST(tracked_value::alive == 3u);
    BOOST_TEST(tracked_value::constructions == 3u);

    arena.clear();
    BOOST_TEST(arena.empty());
    BOOST_TEST(arena.size() == 0u);
    BOOST_TEST(tracked_value::alive == 0u);
    BOOST_TEST(tracked_value::destructions == 3u);

    arena.emplace_back(4);
    BOOST_TEST(tracked_value::constructions == 4u);
    BOOST_TEST(arena.size() == 1u);
    BOOST_TEST(arena[0].value == 4);
    BOOST_TEST(tracked_value::alive == 1u);

    arena.reset();
    BOOST_TEST(tracked_value::alive == 1u);
    BOOST_TEST(tracked_value::destructions == 3u);

    auto& reused_slot = arena.allocate();
    reused_slot.value = 5;
    BOOST_TEST(arena[0].value == 5);
    BOOST_TEST(tracked_value::alive == 1u);
    BOOST_TEST(tracked_value::destructions == 3u);

    const auto capacity_before_release = arena.capacity();
    arena.release();
    BOOST_TEST(arena.empty());
    BOOST_TEST(arena.size() == 0u);
    BOOST_TEST(tracked_value::alive == 0u);
    BOOST_TEST(tracked_value::destructions == 4u);
    BOOST_TEST(arena.capacity() <= capacity_before_release);
}

BOOST_AUTO_TEST_SUITE_END()
