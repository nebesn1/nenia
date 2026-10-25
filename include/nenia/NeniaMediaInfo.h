#pragma once

#include <QString>

namespace nenia {

struct NeniaMediaInfo
{
    QString title;
    QString artist;
    QString album;

    friend bool operator==(const NeniaMediaInfo &, const NeniaMediaInfo &) = default;
    friend bool operator!=(const NeniaMediaInfo &, const NeniaMediaInfo &) = default;
};

} // namespace nenia
