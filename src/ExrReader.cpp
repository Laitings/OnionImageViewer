#include "ExrReader.h"

#include <QFileInfo>

#ifdef ONION_HAVE_OPENEXR
#include <ImfArray.h>
#include <ImfChannelList.h>
#include <ImfHeader.h>
#include <ImfPreviewImage.h>
#include <ImfRgbaFile.h>
#include <ImathBox.h>
#include <half.h>

#include <QByteArray>

#include <exception>
#include <set>
#include <string>
#endif

namespace {

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

#ifdef ONION_HAVE_OPENEXR

// Picks the layer to show: the unnamed top-level RGB/Y channels if present, otherwise the first
// layer that has R, G, B or Y channels (for example "beauty" in "beauty.R").
std::string displayLayer(const Imf::Header &header)
{
    const Imf::ChannelList &channels = header.channels();
    for (const char *name : {"R", "G", "B", "Y"}) {
        if (channels.findChannel(name)) {
            return std::string();
        }
    }

    std::set<std::string> layers;
    channels.layers(layers);
    for (const std::string &layer : layers) {
        for (const char *name : {".R", ".G", ".B", ".Y"}) {
            if (channels.findChannel(layer + name)) {
                return layer;
            }
        }
    }
    return std::string();
}

QImage previewImage(const Imf::Header &header)
{
    const Imf::PreviewImage &preview = header.previewImage();
    const int width = static_cast<int>(preview.width());
    const int height = static_cast<int>(preview.height());
    if (width <= 0 || height <= 0) {
        return QImage();
    }

    // Preview pixels are already 8-bit, gamma-corrected values.
    QImage image(width, height, QImage::Format_RGB32);
    const Imf::PreviewRgba *pixels = preview.pixels();
    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const Imf::PreviewRgba &p = pixels[y * width + x];
            line[x] = qRgb(p.r, p.g, p.b);
        }
    }
    return image;
}

#endif

} // namespace

namespace Exr {

bool isAvailable()
{
#ifdef ONION_HAVE_OPENEXR
    return true;
#else
    return false;
#endif
}

bool isExrFile(const QString &filePath)
{
    return QFileInfo(filePath).suffix().compare(QStringLiteral("exr"), Qt::CaseInsensitive) == 0;
}

QImage read(const QString &filePath, QString *error, bool preferPreview)
{
#ifdef ONION_HAVE_OPENEXR
    // OpenEXR takes UTF-8 file names on every platform, including Windows.
    const QByteArray fileName = filePath.toUtf8();

    try {
        Imf::RgbaInputFile probe(fileName.constData());
        const Imf::Header &header = probe.header();

        if (preferPreview && header.hasPreviewImage()) {
            const QImage preview = previewImage(header);
            if (!preview.isNull()) {
                return preview;
            }
        }

        const std::string layer = displayLayer(header);
        Imf::RgbaInputFile file(fileName.constData(), layer);

        const Imath::Box2i dataWindow = file.dataWindow();
        const Imath::Box2i displayWindow = file.displayWindow();
        const int dataWidth = dataWindow.max.x - dataWindow.min.x + 1;
        const int dataHeight = dataWindow.max.y - dataWindow.min.y + 1;
        if (dataWidth <= 0 || dataHeight <= 0 || dataWidth > 32768 || dataHeight > 32768) {
            fail(error, QStringLiteral("Unsupported EXR image size."));
            return QImage();
        }

        Imf::Array2D<Imf::Rgba> pixels(dataHeight, dataWidth);
        file.setFrameBuffer(&pixels[0][0] - dataWindow.min.x - static_cast<size_t>(dataWindow.min.y) * dataWidth,
            1,
            static_cast<size_t>(dataWidth));
        file.readPixels(dataWindow.min.y, dataWindow.max.y);

        // Show the display window (the intended frame); pixels outside the data window are black.
        Imath::Box2i frame = displayWindow;
        const int frameWidth = frame.max.x - frame.min.x + 1;
        const int frameHeight = frame.max.y - frame.min.y + 1;
        if (frameWidth <= 0 || frameHeight <= 0 || frameWidth > 32768 || frameHeight > 32768) {
            frame = dataWindow;
        }

        const int width = frame.max.x - frame.min.x + 1;
        const int height = frame.max.y - frame.min.y + 1;
        // Hand over the stored half-float values untouched; the view transform maps them to the
        // screen (scene-linear to sRGB by default, or a log curve).
        QImage image(width, height, QImage::Format_RGBX16FPx4);
        if (image.isNull()) {
            fail(error, QStringLiteral("Not enough memory for the EXR image."));
            return QImage();
        }
        image.fill(Qt::black);
        const int firstY = qMax(frame.min.y, dataWindow.min.y);
        const int lastY = qMin(frame.max.y, dataWindow.max.y);
        const int firstX = qMax(frame.min.x, dataWindow.min.x);
        const int lastX = qMin(frame.max.x, dataWindow.max.x);
        for (int y = firstY; y <= lastY; ++y) {
            auto *line = reinterpret_cast<quint16 *>(image.scanLine(y - frame.min.y));
            const Imf::Rgba *row = pixels[y - dataWindow.min.y];
            for (int x = firstX; x <= lastX; ++x) {
                const Imf::Rgba &p = row[x - dataWindow.min.x];
                quint16 *out = line + (x - frame.min.x) * 4;
                out[0] = p.r.bits();
                out[1] = p.g.bits();
                out[2] = p.b.bits();
                out[3] = 0x3C00; // 1.0
            }
        }
        return image;
    } catch (const std::exception &exception) {
        fail(error, QStringLiteral("Could not read EXR: %1").arg(QString::fromLocal8Bit(exception.what())));
        return QImage();
    }
#else
    Q_UNUSED(filePath)
    Q_UNUSED(preferPreview)
    fail(error, QStringLiteral("This build of the viewer has no EXR support (OpenEXR was not found)."));
    return QImage();
#endif
}

} // namespace Exr
