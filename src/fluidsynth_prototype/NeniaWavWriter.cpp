#include "NeniaWavWriter.h"

#include <QDataStream>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace nenia::detail {
namespace {

constexpr std::uint64_t kWavHeaderBytes = 44;
constexpr std::uint64_t kBytesPerFrame = 4;

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

} // namespace

bool writeStereoPcm16Wav(const QString &path,
                         std::span<const float> interleavedSamples,
                         int sampleRate,
                         QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (sampleRate <= 0) {
        setError(errorMessage, QStringLiteral("WAV sample rate must be positive"));
        return false;
    }
    if (sampleRate > std::numeric_limits<int>::max() / 4) {
        setError(errorMessage, QStringLiteral("WAV sample rate is outside the supported range"));
        return false;
    }
    if (interleavedSamples.empty() || (interleavedSamples.size() % 2U) != 0U) {
        setError(errorMessage, QStringLiteral("WAV requires a non-empty even-length stereo sample buffer"));
        return false;
    }
    const std::uint64_t frameCount = interleavedSamples.size() / 2U;
    if (frameCount > (std::numeric_limits<std::uint32_t>::max() - 36U) / kBytesPerFrame) {
        setError(errorMessage, QStringLiteral("audio is too large for a standard RIFF/WAV file"));
        return false;
    }
    const std::uint64_t dataBytes = frameCount * kBytesPerFrame;
    for (const float sample : interleavedSamples) {
        if (!std::isfinite(sample)) {
            setError(errorMessage, QStringLiteral("WAV sample buffer contains a non-finite value"));
            return false;
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        setError(errorMessage, QStringLiteral("cannot create WAV without replacing an existing file: %1")
                                   .arg(file.errorString()));
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << static_cast<quint32>(36U + dataBytes);
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << quint32(16U);
    stream << quint16(1U);
    stream << quint16(2U);
    stream << static_cast<quint32>(sampleRate);
    stream << static_cast<quint32>(sampleRate * 4);
    stream << quint16(4U);
    stream << quint16(16U);
    stream.writeRawData("data", 4);
    stream << static_cast<quint32>(dataBytes);

    for (const float sample : interleavedSamples) {
        const double clamped = std::clamp(static_cast<double>(sample), -1.0, 1.0);
        const double scale = clamped < 0.0 ? 32768.0 : 32767.0;
        const auto pcm = static_cast<qint16>(std::clamp(qRound(clamped * scale), -32768, 32767));
        stream << pcm;
    }

    if (stream.status() != QDataStream::Ok || !file.flush()) {
        const QString detail = file.errorString();
        file.close();
        QFile::remove(path);
        setError(errorMessage, QStringLiteral("failed while writing WAV: %1").arg(detail));
        return false;
    }
    return true;
}

} // namespace nenia::detail
