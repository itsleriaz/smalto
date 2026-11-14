#include "MacTitleBar.h"

#include <QWindow>

#import <AppKit/AppKit.h>

void hideTitleText(QWindow* window)
{
    NSView* view = reinterpret_cast<NSView*>(window->winId());
    view.window.titleVisibility = NSWindowTitleHidden;
}

static NSSet<NSString*>* gDestructive = nil;
static BOOL gColoring = NO;
static id gObserver = nil;

static void colorItems(NSMenu* menu, NSSet<NSString*>* titles)
{
    for (NSMenuItem* item in menu.itemArray) {
        if (!item.isSeparatorItem && [titles containsObject:item.title]) {
            // Qt rewrites an item's title when it syncs the menu (on opening
            // it, too), which drops this; the observer below puts it back.
            NSColor* red = NSColor.systemRedColor;
            if (!item.enabled)
                red = [red colorWithAlphaComponent:0.4];
            item.attributedTitle = [[[NSAttributedString alloc]
                initWithString:item.title
                    attributes:@{ NSForegroundColorAttributeName: red,
                                  NSFontAttributeName: [NSFont menuFontOfSize:0] }] autorelease];
        } else if (item.attributedTitle != nil && !item.isSeparatorItem && ![titles containsObject:item.title]) {
            item.attributedTitle = nil;
        }
        if (item.submenu)
            colorItems(item.submenu, titles);
    }
}

static void applyColors()
{
    if (gColoring || !NSApp.mainMenu)
        return;
    gColoring = YES;
    colorItems(NSApp.mainMenu, gDestructive);
    gColoring = NO;
}

void markDestructiveMenuItems(const QStringList& titles)
{
    NSMutableSet<NSString*>* set = [NSMutableSet set];
    for (const QString& t : titles)
        [set addObject:t.toNSString()];
    // This file is not built with ARC: keep the set (and let go of the old one).
    NSSet<NSString*>* old = gDestructive;
    gDestructive = [set copy];
    [old release];
    if (!gObserver) {
        gObserver = [[NSNotificationCenter.defaultCenter
            addObserverForName:NSMenuDidChangeItemNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification*) { applyColors(); }] retain];
    }
    applyColors();
}
