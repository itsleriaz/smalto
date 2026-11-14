#include "MacTitleBar.h"

#include <QWindow>

#import <AppKit/AppKit.h>

void hideTitleText(QWindow* window)
{
    NSView* view = reinterpret_cast<NSView*>(window->winId());
    view.window.titleVisibility = NSWindowTitleHidden;
}
