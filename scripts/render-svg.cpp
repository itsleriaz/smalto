// Renders an SVG to a square PNG: render-svg in.svg out.png size
// Built on the fly by make-icons.sh (Qt Gui + Svg only).
#include <QtCore/QString>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    if (argc != 4) return 2;
    const int n = QString(argv[3]).toInt();
    QSvgRenderer svg{QString(argv[1])};
    if (!svg.isValid() || n <= 0) return 1;
    QImage img(n, n, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    svg.render(&p, QRectF(0, 0, n, n));
    p.end();
    return img.save(argv[2]) ? 0 : 1;
}
