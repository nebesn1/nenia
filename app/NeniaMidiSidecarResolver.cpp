#include "NeniaMidiSidecarResolver.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QLocale>

#include <algorithm>
#include <vector>

namespace nenia::app {

QString findPlaylistMidiSidecar(const QString &audioPath)
{
    const QFileInfo audioInfo(audioPath);
    const QString baseName = audioInfo.completeBaseName();
    if (baseName.isEmpty()) {
        return {};
    }

    std::vector<QFileInfo> candidates;
    const QDir directory(audioInfo.absolutePath());
    const QFileInfoList entries = directory.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
        QDir::NoSort);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName().startsWith(QLatin1Char('.')) || entry.isSymLink()
            || !entry.isReadable()) {
            continue;
        }
        const QString suffix = entry.suffix().toLower();
        if (suffix != QStringLiteral("mid") && suffix != QStringLiteral("midi")) {
            continue;
        }
        if (entry.completeBaseName().compare(baseName, Qt::CaseInsensitive) != 0) {
            continue;
        }
        candidates.push_back(entry);
    }

    QCollator collator(QLocale::c());
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(candidates.begin(), candidates.end(), [&collator](
                                                        const QFileInfo &left,
                                                        const QFileInfo &right) {
        const bool leftIsMid = left.suffix().compare(QStringLiteral("mid"),
                                                      Qt::CaseInsensitive)
            == 0;
        const bool rightIsMid = right.suffix().compare(QStringLiteral("mid"),
                                                        Qt::CaseInsensitive)
            == 0;
        if (leftIsMid != rightIsMid) {
            return leftIsMid;
        }
        const int natural = collator.compare(left.fileName(), right.fileName());
        if (natural != 0) {
            return natural < 0;
        }
        const int exact = QString::compare(left.fileName(), right.fileName(),
                                           Qt::CaseSensitive);
        if (exact != 0) {
            return exact < 0;
        }
        return left.absoluteFilePath() < right.absoluteFilePath();
    });

    return candidates.empty() ? QString() : candidates.front().absoluteFilePath();
}

} // namespace nenia::app
