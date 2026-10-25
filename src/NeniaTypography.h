#pragma once

#include <QFont>
#include <QString>

namespace nenia::detail {

enum class TextRole;

namespace typography {

inline constexpr int kBundledBasePixelSize = 12;

QString bundledFontResourcePath();
QString bundledFontFamily();
bool bundledFontLoaded();
QString bundledFallbackFontResourcePath();
QString bundledFallbackFontFamily();
bool bundledFallbackFontLoaded();
QFont fontForRole(TextRole role);

} // namespace typography

} // namespace nenia::detail
