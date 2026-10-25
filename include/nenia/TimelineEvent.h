#pragma once

#include <QtGlobal>

#include <variant>

namespace nenia {

struct NoteOnEvent
{
    int track = 0;
    int note = 0;
    float velocity = 0.0f;
};

struct NoteOffEvent
{
    int track = 0;
    int note = 0;
};

struct MeterEvent
{
    int meter = 0;
    float level = 0.0f;
};

struct ProgramChangeEvent
{
    int track = 0;
    int program = 0;
};

struct PanChangeEvent
{
    int track = 0;
    int pan = 0;
};

using TimelineEventPayload = std::variant<NoteOnEvent,
                                          NoteOffEvent,
                                          MeterEvent,
                                          ProgramChangeEvent,
                                          PanChangeEvent>;

struct TimelineEvent
{
    qint64 timeMs = 0;
    TimelineEventPayload payload = NoteOnEvent{};
};

} // namespace nenia
