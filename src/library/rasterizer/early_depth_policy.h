/**
 * swr - a software rasterizer
 *
 * adaptive early depth testing policy.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <cstdint>

#include "fragment.h"

namespace rast
{

/** Minimum block-level samples before the auto policy may disable probing. */
constexpr std::uint64_t block_early_depth_reject_auto_min_samples = 128;

/** Required rejected-test numerator for keeping block-level early rejection active. */
constexpr std::uint64_t block_early_depth_reject_auto_threshold_num = 10;    // 10%

/** Required rejected-test denominator for keeping block-level early rejection active. */
constexpr std::uint64_t block_early_depth_reject_auto_threshold_den = 100;

/** Decision interval used to occasionally re-probe after the auto policy disables testing. */
constexpr std::uint64_t block_early_depth_reject_auto_probe_period = 64;

/** Maximum retained block samples before the history is decayed. */
constexpr std::uint64_t block_early_depth_reject_auto_window_samples = 4096;

/** Whether the block-level auto policy should run an early depth reject test. */
[[nodiscard]]
inline bool block_early_depth_reject_auto_enabled(
  std::uint64_t observed_decision_count,
  std::uint64_t observed_sample_count,
  std::uint64_t observed_rejects)
{
    if(observed_sample_count < block_early_depth_reject_auto_min_samples)
    {
        return true;
    }

    if((observed_rejects * block_early_depth_reject_auto_threshold_den)
       >= (observed_sample_count * block_early_depth_reject_auto_threshold_num))
    {
        return true;
    }

    // Keep occasional probes active so the gate can recover if scene behavior changes.
    return (observed_decision_count % block_early_depth_reject_auto_probe_period) == 0;
}

/** Accumulated block-level early depth reject observations. */
struct block_early_depth_reject_auto_state
{
    /** Number of block-level auto-policy decisions made. */
    std::uint64_t decision_count{0};

    /** Number of block-level policy samples collected from executed tests. */
    std::uint64_t sample_count{0};

    /** Number of collected block-level samples that rejected the whole block. */
    std::uint64_t rejects{0};

    /** Advance one policy decision and return whether it should be tested. */
    [[nodiscard]]
    bool should_test()
    {
        return block_early_depth_reject_auto_enabled(
          ++decision_count,
          sample_count,
          rejects);
    }

    /** Record the outcome of one executed block-level early depth reject test. */
    void record_test_result(bool rejected)
    {
        ++sample_count;
        if(rejected)
        {
            ++rejects;
        }
        decay_history_if_needed();
    }

    /** Decay old samples so scene changes can affect future decisions. */
    void decay_history_if_needed()
    {
        if(sample_count > block_early_depth_reject_auto_window_samples)
        {
            sample_count >>= 1;
            rejects >>= 1;
        }
    }
};

/*
 * Quad/fragment-level early depth test auto policy and statistics.
 *
 * Fragment samples are counted per tested fragment, so these limits are not
 * directly comparable to the block-level sample counts above.
 */

/** Minimum fragment samples before the auto policy may disable early fragment testing. */
constexpr std::uint64_t early_fragment_depth_test_auto_min_fragments = 4096;

/** Required rejected-fragment numerator for keeping early fragment testing active. */
constexpr std::uint64_t early_fragment_depth_test_auto_threshold_num = 2;    // 2%

/** Required rejected-fragment denominator for keeping early fragment testing active. */
constexpr std::uint64_t early_fragment_depth_test_auto_threshold_den = 100;

/** Decision interval for refreshing stats while early fragment testing is active and useful. */
constexpr std::uint64_t early_fragment_depth_test_auto_sample_period = 16;

/** Decision interval used to occasionally re-probe after early fragment testing is disabled. */
constexpr std::uint64_t early_fragment_depth_test_auto_probe_period = 256;

/** Maximum retained fragment samples before the history is decayed. */
constexpr std::uint64_t early_fragment_depth_test_auto_window_fragments = 65536;

/** Action chosen by the fragment-level auto policy for one rasterizer block. */
enum class early_fragment_depth_test_auto_action
{
    /** Do not run fragment-level early depth testing for this block. */
    disabled,

    /** Run fragment-level early depth testing without collecting policy stats. */
    enabled_fast,

    /** Run fragment-level early depth testing and collect policy stats. */
    enabled_collect
};

/** Whether observed fragment-level rejects are high enough to keep the fast path active. */
[[nodiscard]]
inline bool early_fragment_depth_test_auto_reject_rate_is_useful(
  std::uint64_t observed_fragments,
  std::uint64_t observed_rejected_fragments)
{
    return (observed_rejected_fragments * early_fragment_depth_test_auto_threshold_den)
           >= (observed_fragments * early_fragment_depth_test_auto_threshold_num);
}

/** Choose whether to disable, run fast, or collect stats for fragment-level early depth testing. */
[[nodiscard]]
inline early_fragment_depth_test_auto_action early_fragment_depth_test_auto_choose_action(
  std::uint64_t observed_decision_count,
  std::uint64_t observed_fragments,
  std::uint64_t observed_rejected_fragments)
{
    if(observed_fragments < early_fragment_depth_test_auto_min_fragments)
    {
        return early_fragment_depth_test_auto_action::enabled_collect;
    }

    if(early_fragment_depth_test_auto_reject_rate_is_useful(
         observed_fragments,
         observed_rejected_fragments))
    {
        return (observed_decision_count % early_fragment_depth_test_auto_sample_period) == 0
                 ? early_fragment_depth_test_auto_action::enabled_collect
                 : early_fragment_depth_test_auto_action::enabled_fast;
    }

    // Keep occasional probes active so the gate can recover if scene behavior changes.
    return (observed_decision_count % early_fragment_depth_test_auto_probe_period) == 0
             ? early_fragment_depth_test_auto_action::enabled_collect
             : early_fragment_depth_test_auto_action::disabled;
}

/** Accumulated fragment-level early depth test observations. */
struct early_fragment_depth_test_auto_state
{
    /** Number of fragment-level auto-policy decisions made. */
    std::uint64_t decision_count{0};

    /** Number of fragments tested by the early fragment depth path. */
    std::uint64_t tested_fragments{0};

    /** Number of fragments rejected by the early fragment depth path. */
    std::uint64_t rejected_fragments{0};

    /** Advance one policy decision and return the action to take. */
    [[nodiscard]]
    early_fragment_depth_test_auto_action choose_action()
    {
        return early_fragment_depth_test_auto_choose_action(
          ++decision_count,
          tested_fragments,
          rejected_fragments);
    }

    /** Record aggregate early fragment depth test results. */
    void record_test_result(
      std::uint64_t tested,
      std::uint64_t rejected)
    {
        if(tested == 0)
        {
            return;
        }

        tested_fragments += tested;
        rejected_fragments += rejected;
        decay_history_if_needed();
    }

    /** Record one collected early depth sample. */
    void record_test_result(const early_depth_sample& sample)
    {
        record_test_result(
          sample.tested_fragments,
          sample.rejected_fragments);
    }

    /** Decay old samples so scene changes can affect future decisions. */
    void decay_history_if_needed()
    {
        if(tested_fragments > early_fragment_depth_test_auto_window_fragments)
        {
            tested_fragments >>= 1;
            rejected_fragments >>= 1;
        }
    }
};

/** Rasterizer-owned adaptive early depth policy state. */
struct early_depth_policy_state
{
    /** Block-level whole-tile early depth reject policy state. */
    block_early_depth_reject_auto_state block_reject;

    /** Quad/fragment-level early depth test policy state. */
    early_fragment_depth_test_auto_state fragment_test;
};

} /* namespace rast */
