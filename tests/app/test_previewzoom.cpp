#include <doctest/doctest.h>

#include "ImagePreview.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTest>
#include <QWheelEvent>

#include <memory>

namespace {

// The preview inside a panel that swallows wheel events, as PreviewPanel has it.
const char* kPreview = R"(
import QtQuick
import SmaltoTest
Window {
    width: 320; height: 320
    property alias preview: preview
    MouseArea { anchors.fill: parent; onWheel: wheel => wheel.accepted = true }
    Item {
        anchors.fill: parent
        ImagePreview {
            id: preview
            anchors.fill: parent
            wheelZooms: true
            DragHandler { target: null; enabled: preview.fixedZoom > 0 }
            TapHandler {}
        }
    }
}
)";

struct PreviewWindow {
    QQmlEngine engine;
    std::unique_ptr<QQuickWindow> window;
    ImagePreview* preview = nullptr;

    PreviewWindow()
    {
        qmlRegisterType<ImagePreview>("SmaltoTest", 1, 0, "ImagePreview");
        QQmlComponent component(&engine);
        component.setData(kPreview, QUrl());
        window.reset(qobject_cast<QQuickWindow*>(component.create()));
        REQUIRE_MESSAGE(window, component.errorString().toStdString());
        preview = window->property("preview").value<ImagePreview*>();
        QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        preview->setImage(image); // Fit: 10x in 320
        window->show();
        REQUIRE(QTest::qWaitForWindowExposed(window.get()));
    }

    void wheel(QPointF at, QPoint pixel, QPoint angle, Qt::ScrollPhase phase = Qt::NoScrollPhase)
    {
        QWheelEvent event(at, window->mapToGlobal(at), pixel, angle, Qt::NoButton, {}, phase, false);
        QGuiApplication::sendEvent(window.get(), &event);
    }
    // Where image pixel `p` is on screen.
    QPointF screenOf(QPointF p) const
    {
        const qreal z = preview->zoom();
        const QPointF pan = preview->fixedZoom() > 0 ? preview->pan() : QPointF();
        return QPointF(preview->width() - 32 * z, preview->height() - 32 * z) / 2 + pan + p * z;
    }
};

} // namespace

TEST_CASE("preview: scrolling zooms about the pointer")
{
    PreviewWindow w;
    REQUIRE(w.preview->zoom() == doctest::Approx(10));
    const QPointF at(80, 240);
    const QPointF pixel = (at - w.screenOf({})) / w.preview->zoom(); // image pixel under the pointer

    SUBCASE("a wheel notch is one step, from Fit")
    {
        w.wheel(at, {}, {0, 120});
        CHECK(w.preview->fixedZoom() == doctest::Approx(12.5));
        w.wheel(at, {}, {0, 60}); // half a notch: not yet
        CHECK(w.preview->fixedZoom() == doctest::Approx(12.5));
        w.wheel(at, {}, {0, -180}); // ...and back past it
        CHECK(w.preview->fixedZoom() == doctest::Approx(10));
    }
    SUBCASE("a trackpad swipe zooms a little per step; its start and end do nothing")
    {
        w.wheel(at, {}, {}, Qt::ScrollBegin);
        CHECK(w.preview->fixedZoom() == 0); // still Fit
        for (int i = 0; i < 10; ++i)
            w.wheel(at, {0, 3}, {0, 24}, Qt::ScrollUpdate);
        w.wheel(at, {}, {}, Qt::ScrollEnd);
        CHECK(w.preview->fixedZoom() == doctest::Approx(10 * std::pow(1.01, 30)));
    }
    SUBCASE("it stays within the limits")
    {
        for (int i = 0; i < 40; ++i)
            w.wheel(at, {}, {0, 120});
        CHECK(w.preview->fixedZoom() == doctest::Approx(ImagePreview::kMaxZoom));
        for (int i = 0; i < 80; ++i)
            w.wheel(at, {}, {0, -120});
        CHECK(w.preview->fixedZoom() == doctest::Approx(ImagePreview::kMinZoom));
    }
    SUBCASE("the pixel under the pointer stays there")
    {
        w.wheel(at, {}, {0, 120});
        w.wheel(at, {0, 20}, {0, 160}, Qt::ScrollUpdate);
        const QPointF now = w.screenOf(pixel);
        CHECK(now.x() == doctest::Approx(at.x()));
        CHECK(now.y() == doctest::Approx(at.y()));
    }
}
