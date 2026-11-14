#include "MenuStyle.h"

#ifdef Q_OS_MACOS
#include "MacTitleBar.h"
#endif

void MenuStyle::markDestructive(const QStringList& titles)
{
#ifdef Q_OS_MACOS
    markDestructiveMenuItems(titles);
#else
    Q_UNUSED(titles)
#endif
}
