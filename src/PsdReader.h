#pragma once

#include <QImage>
#include <QSize>
#include <QString>

// Reader for Photoshop documents (.psd, and large documents .psb).
//
// Shows the flattened composite that Photoshop stores next to the layers: every colour mode
// (bitmap, greyscale, indexed, RGB, CMYK, multichannel, duotone, Lab), 8/16/32 bits, raw, RLE or
// ZIP compressed. RGB documents with an embedded ICC profile are converted to sRGB; 32-bit
// documents (linear light) are encoded to sRGB.
//
// Documents saved with "Maximize compatibility" turned off have no real composite. For those the
// visible pixel layers are blended here instead: normal blending with opacity, fill, layer masks,
// clipping and hidden groups. Blend modes, layer effects and adjustment layers are not applied, so
// such files can look different from Photoshop.
namespace Psd {

bool isPsdFile(const QString &filePath);

// On failure returns a null image and sets error. With a target size (thumbnails), the small
// preview Photoshop embeds is used when it is big enough, otherwise only every Nth line and
// pixel is decoded so the result is at least that big.
QImage read(const QString &filePath, QString *error = nullptr, const QSize &targetSize = QSize());

struct Info
{
    bool large = false; // PSB
    int width = 0;
    int height = 0;
    int depth = 0;      // bits per channel
    int channels = 0;
    QString colorMode;
    int layerCount = 0;
    bool mergedTransparency = false;
    bool hasComposite = true; // false: saved without "Maximize compatibility"
    QString iccProfile;       // profile description, empty if none
    QSize embeddedPreview;    // size of the embedded JPEG preview, invalid if none
};

// Reads only the header, resources and the start of the layer section.
bool readInfo(const QString &filePath, Info *info, QString *error = nullptr);

} // namespace Psd
