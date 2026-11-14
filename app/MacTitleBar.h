#pragma once

class QWindow;

// macOS: the window's content already runs under the title bar (Qt's
// ExpandedClientAreaHint, see Main.qml); this hides the title text drawn over
// our tabs. The title is still set (Mission Control, the Window menu).
void hideTitleText(QWindow* window);
