#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace nenia::detail {

// Single-producer/single-consumer interleaved-frame queue. reset() is only
// valid after both endpoints have been quiesced (seek/stop does this).
class NeniaPcmRingBuffer final {
public:
    NeniaPcmRingBuffer(std::size_t bytesPerFrame, std::size_t capacityFrames)
        : m_bytesPerFrame(bytesPerFrame)
        , m_capacityFrames(capacityFrames)
        , m_storage(storageSize(bytesPerFrame, capacityFrames))
    {
    }

    NeniaPcmRingBuffer(const NeniaPcmRingBuffer &) = delete;
    NeniaPcmRingBuffer &operator=(const NeniaPcmRingBuffer &) = delete;

    [[nodiscard]] std::size_t bytesPerFrame() const noexcept { return m_bytesPerFrame; }
    [[nodiscard]] std::size_t capacityFrames() const noexcept { return m_capacityFrames; }

    [[nodiscard]] std::size_t queuedFrames() const noexcept
    {
        const std::uint64_t tail = m_tail.load(std::memory_order_acquire);
        const std::uint64_t head = m_head.load(std::memory_order_acquire);
        const std::uint64_t queued = head - tail;
        return static_cast<std::size_t>(std::min<std::uint64_t>(queued, m_capacityFrames));
    }

    [[nodiscard]] std::size_t freeFrames() const noexcept
    {
        return m_capacityFrames - queuedFrames();
    }

    // Called only by the synthesis producer.
    std::size_t writeFrames(const void *source, std::size_t frames) noexcept
    {
        if (source == nullptr || frames == 0 || m_capacityFrames == 0) {
            return 0;
        }
        const std::uint64_t head = m_head.load(std::memory_order_relaxed);
        const std::uint64_t tail = m_tail.load(std::memory_order_acquire);
        const std::uint64_t used = head - tail;
        if (used > m_capacityFrames || head > std::numeric_limits<std::uint64_t>::max() - frames) {
            return 0;
        }
        const std::size_t count = std::min<std::size_t>(
            frames, m_capacityFrames - static_cast<std::size_t>(used));
        const std::size_t offset = static_cast<std::size_t>(head % m_capacityFrames);
        copyIntoStorage(source, offset, count);
        m_head.store(head + count, std::memory_order_release);
        return count;
    }

    // Called only by the audio callback (or a quiescent deterministic test consumer).
    std::size_t readFrames(void *destination, std::size_t frames) noexcept
    {
        if (destination == nullptr || frames == 0 || m_capacityFrames == 0) {
            return 0;
        }
        const std::uint64_t tail = m_tail.load(std::memory_order_relaxed);
        const std::uint64_t head = m_head.load(std::memory_order_acquire);
        const std::uint64_t available = head - tail;
        if (available > m_capacityFrames) {
            return 0;
        }
        const std::size_t count = std::min<std::size_t>(
            frames, static_cast<std::size_t>(available));
        const std::size_t offset = static_cast<std::size_t>(tail % m_capacityFrames);
        copyFromStorage(destination, offset, count);
        m_tail.store(tail + count, std::memory_order_release);
        return count;
    }

    // Realtime-safe callback helper: the missing suffix is deterministically
    // zeroed and the method never waits for the producer.
    std::size_t readFramesOrSilence(void *destination, std::size_t frames) noexcept
    {
        const std::size_t count = readFrames(destination, frames);
        if (destination != nullptr && count < frames) {
            std::memset(static_cast<std::byte *>(destination) + count * m_bytesPerFrame,
                        0, (frames - count) * m_bytesPerFrame);
        }
        return count;
    }

    // Quiescent-only operation: the producer and consumer must both be stopped.
    void reset() noexcept
    {
        m_tail.store(0, std::memory_order_relaxed);
        m_head.store(0, std::memory_order_release);
    }

private:
    static std::size_t storageSize(std::size_t bytesPerFrame, std::size_t capacityFrames)
    {
        if (bytesPerFrame == 0 || capacityFrames == 0
            || capacityFrames > std::numeric_limits<std::size_t>::max() / bytesPerFrame) {
            throw std::invalid_argument("PCM ring dimensions must be positive and bounded");
        }
        return bytesPerFrame * capacityFrames;
    }

    void copyIntoStorage(const void *source, std::size_t offset, std::size_t frames) noexcept
    {
        const std::size_t firstFrames = std::min(frames, m_capacityFrames - offset);
        const auto *bytes = static_cast<const std::byte *>(source);
        std::memcpy(m_storage.data() + offset * m_bytesPerFrame,
                    bytes, firstFrames * m_bytesPerFrame);
        if (firstFrames < frames) {
            std::memcpy(m_storage.data(), bytes + firstFrames * m_bytesPerFrame,
                        (frames - firstFrames) * m_bytesPerFrame);
        }
    }

    void copyFromStorage(void *destination, std::size_t offset, std::size_t frames) noexcept
    {
        const std::size_t firstFrames = std::min(frames, m_capacityFrames - offset);
        auto *bytes = static_cast<std::byte *>(destination);
        std::memcpy(bytes, m_storage.data() + offset * m_bytesPerFrame,
                    firstFrames * m_bytesPerFrame);
        if (firstFrames < frames) {
            std::memcpy(bytes + firstFrames * m_bytesPerFrame, m_storage.data(),
                        (frames - firstFrames) * m_bytesPerFrame);
        }
    }

    const std::size_t m_bytesPerFrame;
    const std::size_t m_capacityFrames;
    std::vector<std::byte> m_storage;
    alignas(64) std::atomic<std::uint64_t> m_head{0};
    alignas(64) std::atomic<std::uint64_t> m_tail{0};
};

} // namespace nenia::detail
