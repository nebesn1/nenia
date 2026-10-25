#pragma once

#include "NeniaExtendedMediaInfo.h"
#include "NeniaTagMetadataReader.h"

#include <nenia/NeniaMediaInfo.h>

#include <QMediaMetaData>
#include <QString>
#include <QUrl>

namespace nenia::detail {

struct NeniaAutomaticMediaPresentation
{
    NeniaMediaInfo primary;
    NeniaExtendedMediaInfo extended;
    QString location;
};

NeniaAutomaticMediaPresentation automaticMediaPresentation(
    const QUrl &source,
    const QMediaMetaData &metadata,
    const NeniaRichMediaMetadata &tagMetadata = {});

} // namespace nenia::detail
