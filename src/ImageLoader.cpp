#include "ImageLoader.h"

#include "DngReader.h"
#include "DpxReader.h"
#include "ExrReader.h"
#include "HeifReader.h"
#include "PsdReader.h"
#include "VectorReader.h"

#include <QImageReader>

bool ImageLoader::Result::isValid() const
{
    return !image.isNull() && error.isEmpty();
}

ImageLoader::Result QtImageLoader::load(const QString &filePath) const
{
    if (Dng::isDngFile(filePath)) {
        QString error;
        QImage image = Dng::read(filePath, &error);
        return {image, image.isNull() ? error : QString()};
    }

    if (Dpx::isDpxFile(filePath)) {
        QString error;
        QImage image = Dpx::read(filePath, &error);
        return {image, image.isNull() ? error : QString()};
    }

    if (Exr::isExrFile(filePath)) {
        QString error;
        QImage image = Exr::read(filePath, &error);
        return {image, image.isNull() ? error : QString()};
    }

    if (Heif::isHeifFile(filePath)) {
        QString error;
        QImage image = Heif::read(filePath, &error);
        return {image, image.isNull() ? error : QString()};
    }

    if (Psd::isPsdFile(filePath)) {
        QString error;
        QImage image = Psd::read(filePath, &error);
        return {image, image.isNull() ? error : QString()};
    }

    if (Vector::isVectorFile(filePath)) {
        QString error;
        QImage image = Vector::read(filePath, &error);
        return {image, image.isNull() ? error : QString()};
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true);

    QImage image;
    if (!reader.read(&image)) {
        return {QImage(), reader.errorString()};
    }

    return {image, QString()};
}
