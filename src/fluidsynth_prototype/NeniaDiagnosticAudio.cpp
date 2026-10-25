#include "NeniaDiagnosticAudio.h"

#include <span>

namespace nenia::detail {
namespace {

bool renderFrames(NeniaSoundFontSynthesizer &synthesizer,
                  std::vector<float> &samples,
                  int firstFrame,
                  int endFrame,
                  QString *errorMessage)
{
    const std::size_t firstSample = static_cast<std::size_t>(firstFrame) * 2U;
    const std::size_t sampleCount = static_cast<std::size_t>(endFrame - firstFrame) * 2U;
    return synthesizer.renderStereoFloat(
        std::span<float>(samples).subspan(firstSample, sampleCount), errorMessage);
}

} // namespace

bool renderDiagnosticAudio(NeniaSoundFontSynthesizer &synthesizer,
                           std::vector<float> &interleavedSamples,
                           QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!synthesizer.hasSoundFont()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("initialize the synthesizer and load a SoundFont first");
        }
        return false;
    }

    interleavedSamples.assign(static_cast<std::size_t>(kDiagnosticTotalFrames) * 2U, 0.0F);
    if (!synthesizer.programChange(0, 0, errorMessage)
        || !renderFrames(synthesizer, interleavedSamples, 0,
                         kDiagnosticNoteOnFrame, errorMessage)
        || !synthesizer.noteOn(0, 60, 100, errorMessage)
        || !renderFrames(synthesizer, interleavedSamples, kDiagnosticNoteOnFrame,
                         kDiagnosticNoteOffFrame, errorMessage)
        || !synthesizer.noteOff(0, 60, errorMessage)
        || !renderFrames(synthesizer, interleavedSamples, kDiagnosticNoteOffFrame,
                         kDiagnosticTotalFrames, errorMessage)) {
        interleavedSamples.clear();
        return false;
    }
    return true;
}

} // namespace nenia::detail
