#pragma once

#include <QString>

#include <span>

namespace nenia::detail {

bool writeStereoPcm16Wav(const QString &path,
                         std::span<const float> interleavedSamples,
                         int sampleRate,
                         QString *errorMessage = nullptr);

} // namespace nenia::detail
