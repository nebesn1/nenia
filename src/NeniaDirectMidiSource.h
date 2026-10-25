#pragma once

#include <QFileInfo>
#include <QUrl>

namespace nenia::detail {

inline bool hasDirectMidiFileSuffix(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix();
    return suffix.compare(QStringLiteral("mid"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("midi"), Qt::CaseInsensitive) == 0;
}

inline bool isDirectMidiSource(const QUrl &source)
{
    return source.isLocalFile() && hasDirectMidiFileSuffix(source.toLocalFile());
}

} // namespace nenia::detail
