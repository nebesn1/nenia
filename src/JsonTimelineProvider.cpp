#include <nenia/JsonTimelineProvider.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace nenia {
namespace {

constexpr double kMaxJsonSafeInteger = 9007199254740991.0; // 2^53 - 1

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool requireMember(const QJsonObject &object,
                   const QString &objectPath,
                   const QString &name,
                   QJsonValue *value,
                   QString *errorMessage)
{
    if (!object.contains(name)) {
        setError(errorMessage,
                 QStringLiteral("%1: missing required field '%2'")
                     .arg(objectPath, name));
        return false;
    }

    *value = object.value(name);
    return true;
}

bool parseInteger(const QJsonValue &value,
                  const QString &path,
                  qint64 minimum,
                  qint64 maximum,
                  qint64 *result,
                  QString *errorMessage)
{
    if (!value.isDouble()) {
        setError(errorMessage, QStringLiteral("%1 must be an integer").arg(path));
        return false;
    }

    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || std::abs(number) > kMaxJsonSafeInteger
        || number < static_cast<double>(minimum)
        || number > static_cast<double>(maximum)) {
        setError(errorMessage,
                 QStringLiteral("%1 must be an integer in range %2..%3")
                     .arg(path)
                     .arg(minimum)
                     .arg(maximum));
        return false;
    }

    *result = static_cast<qint64>(number);
    return true;
}

bool parseNormalizedFloat(const QJsonValue &value,
                          const QString &path,
                          float *result,
                          QString *errorMessage)
{
    if (!value.isDouble()) {
        setError(errorMessage, QStringLiteral("%1 must be a number").arg(path));
        return false;
    }

    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || number > 1.0) {
        setError(errorMessage,
                 QStringLiteral("%1 must be a finite number in range 0..1").arg(path));
        return false;
    }

    *result = static_cast<float>(number);
    return true;
}

bool parseTimeline(const QByteArray &json,
                   std::vector<TimelineEvent> *events,
                   QString *errorMessage)
{
    if (json.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("JSON document is empty"));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(errorMessage,
                 QStringLiteral("JSON syntax error at offset %1: %2")
                     .arg(parseError.offset)
                     .arg(parseError.errorString()));
        return false;
    }
    if (!document.isObject()) {
        setError(errorMessage, QStringLiteral("root: expected a JSON object"));
        return false;
    }

    const QJsonObject root = document.object();
    QJsonValue formatValue;
    if (!requireMember(root, QStringLiteral("root"), QStringLiteral("format"),
                       &formatValue, errorMessage)) {
        return false;
    }
    if (!formatValue.isString()) {
        setError(errorMessage, QStringLiteral("root.format must be a string"));
        return false;
    }
    if (formatValue.toString() != QStringLiteral("nenia-timeline")) {
        setError(errorMessage,
                 QStringLiteral("root.format must be 'nenia-timeline'"));
        return false;
    }

    QJsonValue versionValue;
    if (!requireMember(root, QStringLiteral("root"), QStringLiteral("version"),
                       &versionValue, errorMessage)) {
        return false;
    }
    qint64 version = 0;
    if (!parseInteger(versionValue, QStringLiteral("root.version"), 0,
                      std::numeric_limits<qint64>::max(), &version, errorMessage)) {
        return false;
    }
    if (version != 1) {
        setError(errorMessage,
                 QStringLiteral("root.version: unsupported version %1; supported version is 1")
                     .arg(version));
        return false;
    }

    QJsonValue eventsValue;
    if (!requireMember(root, QStringLiteral("root"), QStringLiteral("events"),
                       &eventsValue, errorMessage)) {
        return false;
    }
    if (!eventsValue.isArray()) {
        setError(errorMessage, QStringLiteral("root.events must be an array"));
        return false;
    }

    std::vector<TimelineEvent> parsedEvents;
    const QJsonArray eventArray = eventsValue.toArray();
    parsedEvents.reserve(static_cast<std::size_t>(eventArray.size()));

    for (int index = 0; index < eventArray.size(); ++index) {
        const QString eventPath = QStringLiteral("events[%1]").arg(index);
        const QJsonValue eventValue = eventArray.at(index);
        if (!eventValue.isObject()) {
            setError(errorMessage,
                     QStringLiteral("%1: expected a JSON object").arg(eventPath));
            return false;
        }

        const QJsonObject eventObject = eventValue.toObject();
        QJsonValue timeValue;
        if (!requireMember(eventObject, eventPath, QStringLiteral("time_ms"),
                           &timeValue, errorMessage)) {
            return false;
        }
        qint64 timeMs = 0;
        if (!parseInteger(timeValue, eventPath + QStringLiteral(".time_ms"), 0,
                          std::numeric_limits<qint64>::max(), &timeMs, errorMessage)) {
            return false;
        }

        QJsonValue typeValue;
        if (!requireMember(eventObject, eventPath, QStringLiteral("type"),
                           &typeValue, errorMessage)) {
            return false;
        }
        if (!typeValue.isString()) {
            setError(errorMessage,
                     QStringLiteral("%1.type must be a string").arg(eventPath));
            return false;
        }

        const QString type = typeValue.toString();
        if (type == QStringLiteral("note_on")) {
            QJsonValue trackValue;
            QJsonValue noteValue;
            QJsonValue velocityValue;
            if (!requireMember(eventObject, eventPath, QStringLiteral("track"),
                               &trackValue, errorMessage)
                || !requireMember(eventObject, eventPath, QStringLiteral("note"),
                                  &noteValue, errorMessage)
                || !requireMember(eventObject, eventPath, QStringLiteral("velocity"),
                                  &velocityValue, errorMessage)) {
                return false;
            }

            qint64 track = 0;
            qint64 note = 0;
            float velocity = 0.0f;
            if (!parseInteger(trackValue, eventPath + QStringLiteral(".track"), 0, 9,
                              &track, errorMessage)
                || !parseInteger(noteValue, eventPath + QStringLiteral(".note"), 0, 127,
                                 &note, errorMessage)
                || !parseNormalizedFloat(velocityValue,
                                         eventPath + QStringLiteral(".velocity"),
                                         &velocity, errorMessage)) {
                return false;
            }

            parsedEvents.push_back({timeMs, NoteOnEvent{static_cast<int>(track),
                                                        static_cast<int>(note), velocity}});
        } else if (type == QStringLiteral("note_off")) {
            QJsonValue trackValue;
            QJsonValue noteValue;
            if (!requireMember(eventObject, eventPath, QStringLiteral("track"),
                               &trackValue, errorMessage)
                || !requireMember(eventObject, eventPath, QStringLiteral("note"),
                                  &noteValue, errorMessage)) {
                return false;
            }

            qint64 track = 0;
            qint64 note = 0;
            if (!parseInteger(trackValue, eventPath + QStringLiteral(".track"), 0, 9,
                              &track, errorMessage)
                || !parseInteger(noteValue, eventPath + QStringLiteral(".note"), 0, 127,
                                 &note, errorMessage)) {
                return false;
            }

            parsedEvents.push_back({timeMs, NoteOffEvent{static_cast<int>(track),
                                                         static_cast<int>(note)}});
        } else if (type == QStringLiteral("meter")) {
            QJsonValue meterValue;
            QJsonValue levelValue;
            if (!requireMember(eventObject, eventPath, QStringLiteral("meter"),
                               &meterValue, errorMessage)
                || !requireMember(eventObject, eventPath, QStringLiteral("level"),
                                  &levelValue, errorMessage)) {
                return false;
            }

            qint64 meter = 0;
            float level = 0.0f;
            if (!parseInteger(meterValue, eventPath + QStringLiteral(".meter"), 0, 18,
                              &meter, errorMessage)
                || !parseNormalizedFloat(levelValue,
                                         eventPath + QStringLiteral(".level"),
                                         &level, errorMessage)) {
                return false;
            }

            parsedEvents.push_back({timeMs, MeterEvent{static_cast<int>(meter), level}});
        } else {
            setError(errorMessage,
                     QStringLiteral("%1.type: unknown event type '%2'")
                         .arg(eventPath, type));
            return false;
        }
    }

    std::stable_sort(parsedEvents.begin(), parsedEvents.end(),
                     [](const TimelineEvent &left, const TimelineEvent &right) {
                         return left.timeMs < right.timeMs;
                     });
    *events = std::move(parsedEvents);
    return true;
}

} // namespace

bool JsonTimelineProvider::load(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage,
                 QStringLiteral("cannot open timeline '%1': %2")
                     .arg(path, file.errorString()));
        return false;
    }

    const QByteArray json = file.readAll();
    if (file.error() != QFile::NoError) {
        setError(errorMessage,
                 QStringLiteral("cannot read timeline '%1': %2")
                     .arg(path, file.errorString()));
        return false;
    }

    QString parseError;
    if (!loadParsedJson(json, &parseError)) {
        setError(errorMessage,
                 QStringLiteral("timeline '%1': %2").arg(path, parseError));
        return false;
    }
    return true;
}

bool JsonTimelineProvider::loadJson(const QByteArray &json, QString *errorMessage)
{
    return loadParsedJson(json, errorMessage);
}

bool JsonTimelineProvider::loadParsedJson(const QByteArray &json, QString *errorMessage)
{
    std::vector<TimelineEvent> parsedEvents;
    QString parseError;
    if (!parseTimeline(json, &parsedEvents, &parseError)) {
        setError(errorMessage, parseError);
        return false;
    }

    m_events = std::move(parsedEvents);
    m_loaded = true;
    setError(errorMessage, {});
    return true;
}

bool JsonTimelineProvider::isLoaded() const noexcept
{
    return m_loaded;
}

std::size_t JsonTimelineProvider::eventCount() const noexcept
{
    return m_events.size();
}

std::optional<qint64> JsonTimelineProvider::firstEventTimeMs() const noexcept
{
    if (m_events.empty()) {
        return std::nullopt;
    }
    return m_events.front().timeMs;
}

std::optional<qint64> JsonTimelineProvider::lastEventTimeMs() const noexcept
{
    if (m_events.empty()) {
        return std::nullopt;
    }
    return m_events.back().timeMs;
}

std::span<const TimelineEvent> JsonTimelineProvider::eventsBetween(
    qint64 fromExclusiveMs,
    qint64 toInclusiveMs) const noexcept
{
    if (m_events.empty() || fromExclusiveMs >= toInclusiveMs) {
        return {};
    }

    const auto first = std::upper_bound(
        m_events.begin(), m_events.end(), fromExclusiveMs,
        [](qint64 time, const TimelineEvent &event) { return time < event.timeMs; });
    const auto last = std::upper_bound(
        m_events.begin(), m_events.end(), toInclusiveMs,
        [](qint64 time, const TimelineEvent &event) { return time < event.timeMs; });

    if (first >= last) {
        return {};
    }
    return std::span<const TimelineEvent>(&*first,
                                          static_cast<std::size_t>(last - first));
}

void JsonTimelineProvider::clear() noexcept
{
    m_events.clear();
    m_loaded = false;
}

} // namespace nenia
