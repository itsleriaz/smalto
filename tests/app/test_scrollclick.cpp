#include <doctest/doctest.h>

#include "ScrollClick.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QWheelEvent>

#include <memory>

namespace {

// A tall list whose rows record the last one clicked.
const char* kList = R"(
import QtQuick
Window {
    width: 200; height: 300
    property int hitRow: -1
    property alias list: list
    ListView {
        id: list
        anchors.fill: parent
        model: 200
        delegate: Rectangle {
            required property int index
            width: 200; height: 20
            MouseArea { anchors.fill: parent; onClicked: hitRow = parent.index }
        }
    }
}
)";

struct ListWindow {
    QQmlEngine engine;
    std::unique_ptr<QQuickWindow> window;
    QObject* list = nullptr;

    ListWindow()
    {
        QQmlComponent component(&engine);
        component.setData(kList, QUrl());
        window.reset(qobject_cast<QQuickWindow*>(component.create()));
        REQUIRE_MESSAGE(window, component.errorString().toStdString());
        list = window->property("list").value<QObject*>();
        window->show();
        REQUIRE(QTest::qWaitForWindowExposed(window.get()));
    }

    int hitRow() const { return window->property("hitRow").toInt(); }
    bool moving() const { return list->property("moving").toBool(); }

    void wheel()
    {
        const QPointF at(100, 150);
        QWheelEvent event(at, window->mapToGlobal(at), {}, QPoint(0, -360), Qt::NoButton, {}, Qt::NoScrollPhase,
                          false);
        QGuiApplication::sendEvent(window.get(), &event);
        QTest::qWait(30);
    }
};

} // namespace

TEST_CASE("scroll click: a click on a scrolling list reaches the row")
{
    ScrollClick filter;

    SUBCASE("without it, Qt eats the click (the bug)")
    {
        ListWindow w;
        w.wheel();
        REQUIRE(w.moving());
        QTest::mouseClick(w.window.get(), Qt::LeftButton, {}, QPoint(100, 150));
        CHECK(w.hitRow() == -1);
    }
    SUBCASE("with it, the scroll stops and the row is clicked")
    {
        qApp->installEventFilter(&filter);
        ListWindow w;
        w.wheel();
        REQUIRE(w.moving());
        QTest::mouseClick(w.window.get(), Qt::LeftButton, {}, QPoint(100, 150));
        CHECK_FALSE(w.moving());
        CHECK(w.hitRow() >= 0);
        qApp->removeEventFilter(&filter);
    }
    SUBCASE("a list at rest is left alone")
    {
        ListWindow w;
        CHECK(ScrollClick::stopScrollingAt(w.window->contentItem(), QPointF(100, 150)) == 0);
    }
}
