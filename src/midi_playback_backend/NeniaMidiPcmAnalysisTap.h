#pragma once

#include "../NeniaSpectrumPcmBlock.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace nenia::detail {

inline constexpr std::size_t kMidiAnalysisQueueCapacity = 128;

// Separate bounded SPSC tap. The synthesis worker is the sole producer and
// the application/UI thread is the sole consumer; the audio callback never
// touches this queue. Overflow drops the newest analysis block without waiting.
class NeniaMidiPcmAnalysisTap final
{
public:
    bool publish(std::span<const float> interleavedStereo,
                 std::uint64_t firstOutputFrame,
                 std::uint64_t generation,
                 std::uint32_t sampleRate) noexcept
    {
        if (interleavedStereo.empty() || interleavedStereo.size() % 2U != 0U
            || interleavedStereo.size() > kMidiAnalysisBlockFrames * 2U) {
            return false;
        }
        const std::uint64_t head = m_head.load(std::memory_order_relaxed);
        const std::uint64_t tail = m_tail.load(std::memory_order_acquire);
        if (head - tail >= kMidiAnalysisQueueCapacity) {
            m_droppedBlocks.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        NeniaMidiPcmAnalysisBlock &block =
            m_blocks.at(static_cast<std::size_t>(head % kMidiAnalysisQueueCapacity));
        block.generation = generation;
        block.firstOutputFrame = firstOutputFrame;
        block.sampleRate = sampleRate;
        block.frameCount = interleavedStereo.size() / 2U;
        std::copy(interleavedStereo.begin(), interleavedStereo.end(),
                  block.stereoFloat.begin());
        m_head.store(head + 1U, std::memory_order_release);
        return true;
    }

    bool takeEligible(std::uint64_t currentGeneration,
                      std::uint64_t consumedOutputFrame,
                      NeniaMidiPcmAnalysisBlock *result) noexcept
    {
        if (result == nullptr) {
            return false;
        }
        for (;;) {
            const std::uint64_t tail = m_tail.load(std::memory_order_relaxed);
            const std::uint64_t head = m_head.load(std::memory_order_acquire);
            if (tail == head) {
                return false;
            }
            const NeniaMidiPcmAnalysisBlock &block =
                m_blocks.at(static_cast<std::size_t>(tail % kMidiAnalysisQueueCapacity));
            if (block.generation != currentGeneration) {
                m_tail.store(tail + 1U, std::memory_order_release);
                continue;
            }
            if (block.frameCount > UINT64_MAX - block.firstOutputFrame
                || block.firstOutputFrame + block.frameCount > consumedOutputFrame) {
                return false;
            }
            *result = block;
            m_tail.store(tail + 1U, std::memory_order_release);
            return true;
        }
    }

    [[nodiscard]] std::size_t queuedBlocks() const noexcept
    {
        const std::uint64_t head = m_head.load(std::memory_order_acquire);
        const std::uint64_t tail = m_tail.load(std::memory_order_acquire);
        return static_cast<std::size_t>(std::min<std::uint64_t>(
            head - tail, kMidiAnalysisQueueCapacity));
    }

    [[nodiscard]] std::uint64_t droppedBlocks() const noexcept
    {
        return m_droppedBlocks.load(std::memory_order_acquire);
    }

private:
    std::array<NeniaMidiPcmAnalysisBlock, kMidiAnalysisQueueCapacity> m_blocks{};
    alignas(64) std::atomic<std::uint64_t> m_head{0};
    alignas(64) std::atomic<std::uint64_t> m_tail{0};
    std::atomic<std::uint64_t> m_droppedBlocks{0};
};

} // namespace nenia::detail
