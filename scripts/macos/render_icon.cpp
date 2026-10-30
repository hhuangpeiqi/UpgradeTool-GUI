#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 3)
        return 1;
    if (QString::fromLocal8Bit(argv[1]) == "--dmg-background") {
        QImage image(640, 260, QImage::Format_ARGB32_Premultiplied);
        // Finder sizes background pictures in points using their PNG resolution.
        // Use 72 DPI so one pixel occupies one point, including on Retina displays.
        image.setDotsPerMeterX(2835);
        image.setDotsPerMeterY(2835);
        image.fill(QColor("#f7f7f9"));
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#95959e"), 7, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(282, 130), QPointF(358, 130));
        painter.drawLine(QPointF(340, 112), QPointF(358, 130));
        painter.drawLine(QPointF(340, 148), QPointF(358, 130));
        painter.end();
        return image.save(QString::fromLocal8Bit(argv[2])) ? 0 : 1;
    }
    QSvgRenderer renderer(QString::fromLocal8Bit(argv[1]));
    if (!renderer.isValid())
        return 1;
    QDir dir(QString::fromLocal8Bit(argv[2]));
    if (!dir.mkpath("."))
        return 1;
    const int sizes[] = {16, 32, 128, 256, 512};
    for (int size : sizes) {
        for (int scale = 1; scale <= 2; ++scale) {
            QImage image(size * scale, size * scale, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            renderer.render(&painter);
            painter.end();
            QString name = QString("icon_%1x%1%2.png").arg(size)
                .arg(scale == 2 ? "@2x" : "");
            if (!image.save(dir.filePath(name)))
                return 1;
        }
    }
    return 0;
}
