#include "NeniaTypography.h"

#include "NeniaRenderer.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QStringList>

extern int qInitResources_NeniaResources();

namespace {

void initializeNeniaResources()
{
    ::qInitResources_NeniaResources();
}

} // namespace

namespace nenia::detail::typography {
namespace {

constexpr auto kFontResourcePath =
    ":/nenia/fonts/source-code-pro-regular.otf";
constexpr auto kFallbackFontResourcePath =
    ":/nenia/fonts/fusion-pixel-10px-monospaced-zh_hans.otf";

struct LoadedFonts
{
    QString primaryFamily;
    QString fallbackFamily;
    bool attempted = false;
};

LoadedFonts &loadedFonts()
{
    static LoadedFonts fonts;
    if (fonts.attempted || QGuiApplication::instance() == nullptr) {
        return fonts;
    }

    initializeNeniaResources();
    const int primaryId = QFontDatabase::addApplicationFont(
        QString::fromLatin1(kFontResourcePath));
    if (primaryId >= 0) {
        fonts.primaryFamily = QFontDatabase::applicationFontFamilies(primaryId).value(0);
    }

    const int fallbackId = QFontDatabase::addApplicationFont(
        QString::fromLatin1(kFallbackFontResourcePath));
    if (fallbackId >= 0) {
        fonts.fallbackFamily = QFontDatabase::applicationFontFamilies(fallbackId).value(0);
    }
    fonts.attempted = true;
    return fonts;
}

} // namespace

QString bundledFontResourcePath()
{
    return QString::fromLatin1(kFontResourcePath);
}

QString bundledFontFamily()
{
    return loadedFonts().primaryFamily;
}

bool bundledFontLoaded()
{
    return !bundledFontFamily().isEmpty();
}

QString bundledFallbackFontResourcePath()
{
    return QString::fromLatin1(kFallbackFontResourcePath);
}

QString bundledFallbackFontFamily()
{
    return loadedFonts().fallbackFamily;
}

bool bundledFallbackFontLoaded()
{
    return !bundledFallbackFontFamily().isEmpty();
}

QFont fontForRole(TextRole role)
{
    Q_UNUSED(role)

    const LoadedFonts &fonts = loadedFonts();
    QFont font = fonts.primaryFamily.isEmpty()
        ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
        : QFont(fonts.primaryFamily);
    if (!fonts.fallbackFamily.isEmpty()) {
        QStringList families = font.families();
        if (!families.contains(fonts.fallbackFamily)) {
            families.append(fonts.fallbackFamily);
        }
        font.setFamilies(families);
    }
    font.setStyleHint(QFont::Monospace);
    font.setStyleStrategy(QFont::StyleStrategy(QFont::PreferAntialias
                                                | QFont::PreferQuality));
    font.setFixedPitch(true);
    font.setWeight(QFont::Normal);
    font.setPixelSize(kBundledBasePixelSize);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
    return font;
}

} // namespace nenia::detail::typography
