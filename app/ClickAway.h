#pragma once

#include <QObject>

// Pressing anywhere outside the text field being typed in takes the focus
// away from it, so the field finishes its edit (NumberControl applies the
// number, the hex field its color) as if Enter had been pressed. Without
// this, a click on a panel or the canvas leaves the field focused, since
// those don't take focus. Installed on the application.
class ClickAway : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};
