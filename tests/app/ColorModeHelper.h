#pragma once

#include "Editor.h"

// Editor::setColorMode takes every argument (it is called from QML); tests
// mostly want the defaults.
inline bool toMode(Editor& editor, const QString& mode, const QString& palette = "current",
                   const QString& dither = "none", int colors = 255, const QVariantList& given = {},
                   bool keepAlpha = false, const QVariantMap& ditherOptions = {})
{
    return editor.setColorMode(mode, palette, dither, colors, given, keepAlpha, ditherOptions);
}
