#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QPair>
#include <QSize>
#include <QString>

#include <memory>

// WebP support through libwebp (BSD licence, linked statically; see vcpkg.json). It makes .webp
// files open and show thumbnails without the Qt Image Formats add-on, and plays animated WebP.
// Without libwebp, isWebpFile() is false and .webp files go to Qt's own plugin, if installed.
namespace Webp {

bool isAvailable();
// True for .webp files when the app is built with libwebp.
bool isWebpFile(const QString &filePath);

// Decodes the image (the first frame of an animation) as 8-bit RGBA. An embedded ICC profile is
// applied, so the result is sRGB. With a target size (thumbnails) the decoder scales down while
// decoding. On failure returns a null image and sets error.
QImage read(const QString &filePath, QString *error = nullptr, const QSize &targetSize = QSize());

// Facts about the file for the metadata panel: size, compression, alpha, frames, loop count,
// colour profile. Reads the file but doesn't decode pixels.
QList<QPair<QString, QString>> describe(const QString &filePath, QString *error = nullptr);

// The Exif block, starting at the TIFF header; empty when there is none. Pass it to Exif::describe.
QByteArray exifData(const QString &filePath);

// Plays an animated WebP frame by frame, keeping only the current canvas in memory.
class Animation final
{
public:
    Animation();
    ~Animation();
    Animation(const Animation &) = delete;
    Animation &operator=(const Animation &) = delete;

    // Opens the file. Returns false for still images and files that can't be read; only the
    // header is read in that case.
    bool open(const QString &filePath);
    // How many times to play the animation again after the first pass: -1 forever.
    int repeats() const;
    bool hasMoreFrames() const;
    // Starts again at the first frame.
    void rewind();
    // The next composed frame and how long to show it, in milliseconds. Null at the end.
    QImage next(int *delayMs);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Webp
