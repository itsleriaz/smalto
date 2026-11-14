#pragma once

class QWindow;

// macOS: the window's content already runs under the title bar (Qt's
// ExpandedClientAreaHint, see Main.qml); this hides the title text drawn over
// our tabs. The title is still set (Mission Control, the Window menu).
void hideTitleText(QWindow* window);

#include <QStringList>

// macOS: draws the native menu items whose titles are in `titles` in red
// (destructive actions); every other item is plain. Walks the main menu, so
// call it after the menus were made or their texts changed.
void markDestructiveMenuItems(const QStringList& titles);
