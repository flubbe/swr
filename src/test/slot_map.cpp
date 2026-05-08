/**
 * swr - a software rasterizer
 *
 * test slot_map.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <deque>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE slot_map tests
#include <boost/test/unit_test.hpp>

#include "../common/utils.h"

/*
 * test cases.
 */

BOOST_AUTO_TEST_CASE(slot_map_basic_push)
{
    utils::slot_map<int> sm;

    // Initially empty
    BOOST_CHECK_EQUAL(sm.size(), 0);
    BOOST_CHECK_EQUAL(sm.capacity(), 0);
    BOOST_CHECK(sm.empty());

    // Push first element
    std::size_t idx1 = sm.push(42);
    BOOST_CHECK_EQUAL(idx1, 0);
    BOOST_CHECK_EQUAL(sm.size(), 1);
    BOOST_CHECK_EQUAL(sm.capacity(), 1);
    BOOST_CHECK(!sm.empty());
    BOOST_CHECK_EQUAL(sm[idx1], 42);

    // Push second element
    std::size_t idx2 = sm.push(24);
    BOOST_CHECK_EQUAL(idx2, 1);
    BOOST_CHECK_EQUAL(sm.size(), 2);
    BOOST_CHECK_EQUAL(sm.capacity(), 2);
    BOOST_CHECK_EQUAL(sm[idx2], 24);

    // Push third element
    std::size_t idx3 = sm.push(12);
    BOOST_CHECK_EQUAL(idx3, 2);
    BOOST_CHECK_EQUAL(sm.size(), 3);
    BOOST_CHECK_EQUAL(sm.capacity(), 3);
    BOOST_CHECK_EQUAL(sm[idx3], 12);
}

BOOST_AUTO_TEST_CASE(slot_map_push_move)
{
    utils::slot_map<std::string> sm;

    std::string s1 = "hello";
    std::string s2 = "world";

    std::size_t idx1 = sm.push(std::move(s1));
    BOOST_CHECK_EQUAL(sm[idx1], "hello");
    BOOST_CHECK(s1.empty());    // moved from

    std::size_t idx2 = sm.push(std::move(s2));
    BOOST_CHECK_EQUAL(sm[idx2], "world");
    BOOST_CHECK(s2.empty());    // moved from
}

BOOST_AUTO_TEST_CASE(slot_map_free_and_reuse)
{
    utils::slot_map<int> sm;

    // Push three elements
    std::size_t idx1 = sm.push(1);
    std::size_t idx2 = sm.push(2);
    std::size_t idx3 = sm.push(3);

    BOOST_CHECK_EQUAL(sm.size(), 3);
    BOOST_CHECK_EQUAL(sm.capacity(), 3);

    // Free middle element
    sm.free(idx2);
    BOOST_CHECK_EQUAL(sm.size(), 2);
    BOOST_CHECK_EQUAL(sm.capacity(), 3);
    BOOST_CHECK(sm.is_free(idx2));
    BOOST_CHECK(!sm.is_free(idx1));
    BOOST_CHECK(!sm.is_free(idx3));

    // Push new element - should reuse freed slot
    std::size_t idx4 = sm.push(4);
    BOOST_CHECK_EQUAL(idx4, idx2);    // Should reuse idx2
    BOOST_CHECK_EQUAL(sm.size(), 3);
    BOOST_CHECK_EQUAL(sm.capacity(), 3);
    BOOST_CHECK_EQUAL(sm[idx4], 4);
    BOOST_CHECK(!sm.is_free(idx4));

    // Free first element
    sm.free(idx1);
    BOOST_CHECK_EQUAL(sm.size(), 2);
    BOOST_CHECK(sm.is_free(idx1));

    // Push another element - should reuse idx1 (LIFO)
    std::size_t idx5 = sm.push(5);
    BOOST_CHECK_EQUAL(idx5, idx1);
    BOOST_CHECK_EQUAL(sm.size(), 3);
    BOOST_CHECK_EQUAL(sm[idx5], 5);
    BOOST_CHECK(!sm.is_free(idx5));
}

BOOST_AUTO_TEST_CASE(slot_map_lifo_free_slots)
{
    utils::slot_map<int> sm;

    // Push elements
    std::size_t idx1 = sm.push(1);
    std::size_t idx2 = sm.push(2);
    sm.push(3);
    std::size_t idx4 = sm.push(4);

    // Free in order: idx2, idx4, idx1
    sm.free(idx2);
    sm.free(idx4);
    sm.free(idx1);

    // Push should reuse in LIFO order: idx1, idx4, idx2
    std::size_t idx5 = sm.push(5);
    BOOST_CHECK_EQUAL(idx5, idx1);

    std::size_t idx6 = sm.push(6);
    BOOST_CHECK_EQUAL(idx6, idx4);

    std::size_t idx7 = sm.push(7);
    BOOST_CHECK_EQUAL(idx7, idx2);

    // Now push new element at end
    std::size_t idx8 = sm.push(8);
    BOOST_CHECK_EQUAL(idx8, 4);    // Next available slot
}

BOOST_AUTO_TEST_CASE(slot_map_clear)
{
    utils::slot_map<int> sm;

    sm.push(1);
    sm.push(2);
    sm.push(3);

    BOOST_CHECK_EQUAL(sm.size(), 3);
    BOOST_CHECK_EQUAL(sm.capacity(), 3);

    sm.clear();

    BOOST_CHECK_EQUAL(sm.size(), 0);
    BOOST_CHECK_EQUAL(sm.capacity(), 0);
    BOOST_CHECK(sm.empty());
}

BOOST_AUTO_TEST_CASE(slot_map_shrink_to_fit)
{
    utils::slot_map<int> sm;

    sm.push(1);
    sm.push(2);
    sm.push(3);

    BOOST_CHECK_EQUAL(sm.capacity(), 3);

    sm.free(1);    // Free middle element
    BOOST_CHECK_EQUAL(sm.size(), 2);
    BOOST_CHECK_EQUAL(sm.capacity(), 3);

    sm.shrink_to_fit();
    // Note: shrink_to_fit doesn't actually reduce capacity in this implementation
    // It just calls data.shrink_to_fit(), but since we still have the same logical size,
    // the underlying vector may or may not shrink
    BOOST_CHECK_EQUAL(sm.size(), 2);
}

BOOST_AUTO_TEST_CASE(slot_map_access_operators)
{
    utils::slot_map<std::string> sm;

    std::size_t idx1 = sm.push("hello");
    std::size_t idx2 = sm.push("world");

    // Test const access
    const auto& const_sm = sm;
    BOOST_CHECK_EQUAL(const_sm[idx1], "hello");
    BOOST_CHECK_EQUAL(const_sm[idx2], "world");

    // Test mutable access
    sm[idx1] = "goodbye";
    BOOST_CHECK_EQUAL(sm[idx1], "goodbye");
    BOOST_CHECK_EQUAL(sm[idx2], "world");
}

BOOST_AUTO_TEST_CASE(slot_map_edge_cases)
{
    utils::slot_map<int> sm;

    // Test empty slot_map
    BOOST_CHECK_EQUAL(sm.size(), 0);
    BOOST_CHECK(sm.empty());

    // Test single element operations
    std::size_t idx = sm.push(42);
    BOOST_CHECK_EQUAL(sm.size(), 1);
    BOOST_CHECK(!sm.empty());

    sm.free(idx);
    BOOST_CHECK_EQUAL(sm.size(), 0);
    BOOST_CHECK(sm.empty());

    // Push again after freeing all
    std::size_t idx2 = sm.push(24);
    BOOST_CHECK_EQUAL(idx2, idx);    // Should reuse
    BOOST_CHECK_EQUAL(sm.size(), 1);
    BOOST_CHECK_EQUAL(sm[idx2], 24);
}

BOOST_AUTO_TEST_CASE(slot_map_custom_container)
{
    // Test with std::deque as container
    utils::slot_map<int, std::deque<int>> sm;

    std::size_t idx1 = sm.push(1);
    std::size_t idx2 = sm.push(2);

    BOOST_CHECK_EQUAL(sm.size(), 2);
    BOOST_CHECK_EQUAL(sm.capacity(), 2);
    BOOST_CHECK_EQUAL(sm[idx1], 1);
    BOOST_CHECK_EQUAL(sm[idx2], 2);

    sm.free(idx1);
    BOOST_CHECK_EQUAL(sm.size(), 1);
    BOOST_CHECK(sm.is_free(idx1));

    std::size_t idx3 = sm.push(3);
    BOOST_CHECK_EQUAL(idx3, idx1);    // Should reuse
    BOOST_CHECK_EQUAL(sm[idx3], 3);
}

BOOST_AUTO_TEST_CASE(slot_map_multiple_free_reuse)
{
    utils::slot_map<int> sm;

    // Push many elements
    std::vector<std::size_t> indices;
    for(int i = 0; i < 10; ++i)
    {
        indices.push_back(sm.push(i));
    }

    BOOST_CHECK_EQUAL(sm.size(), 10);
    BOOST_CHECK_EQUAL(sm.capacity(), 10);

    // Free every other element
    for(std::size_t i = 0; i < indices.size(); i += 2)
    {
        sm.free(indices[i]);
    }

    BOOST_CHECK_EQUAL(sm.size(), 5);

    // Push new elements - should reuse freed slots in LIFO order
    std::vector<std::size_t> new_indices;
    for(int i = 100; i < 105; ++i)
    {
        new_indices.push_back(sm.push(i));
    }

    BOOST_CHECK_EQUAL(sm.size(), 10);

    // Verify the reused indices are from the freed ones
    std::sort(indices.begin(), indices.end());
    std::sort(new_indices.begin(), new_indices.end());
    // The new indices should be the same as the freed ones
    BOOST_CHECK_EQUAL(new_indices.size(), 5);
    for(std::size_t i = 0; i < new_indices.size(); ++i)
    {
        BOOST_CHECK_EQUAL(new_indices[i], indices[i * 2]);
    }
}

BOOST_AUTO_TEST_CASE(slot_map_size_capacity_consistency)
{
    utils::slot_map<int> sm;

    // Test that size + free_slots.size() == capacity
    BOOST_CHECK_EQUAL(sm.size() + sm.free_slots.size(), sm.capacity());

    sm.push(1);
    BOOST_CHECK_EQUAL(sm.size() + sm.free_slots.size(), sm.capacity());

    sm.push(2);
    BOOST_CHECK_EQUAL(sm.size() + sm.free_slots.size(), sm.capacity());

    sm.free(0);
    BOOST_CHECK_EQUAL(sm.size() + sm.free_slots.size(), sm.capacity());

    sm.push(3);
    BOOST_CHECK_EQUAL(sm.size() + sm.free_slots.size(), sm.capacity());
}