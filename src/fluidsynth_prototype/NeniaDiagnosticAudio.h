#pragma once

#include "NeniaSoundFontSynthesizer.h"

#include <QString>

#include <vector>

namespace nenia::detail {

inline constexpr int kDiagnosticTotalFrames = 88200;
inline constexpr int kDiagnosticNoteOnFrame = 4410;
inline constexpr int kDiagnosticNoteOffFrame = 26460;

bool renderDiagnosticAudio(NeniaSoundFontSynthesizer &synthesizer,
                           std::vector<float> &interleavedSamples,
                           QString *errorMessage = nullptr);

} // namespace nenia::detail
