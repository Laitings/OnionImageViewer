#include "ImageLoader.h"

#include <QImageReader>

bool ImageLoader::Result::isValid() const
{
    return !image.isNull() && error.isEmpty();
}

ImageLoader::Result QtImageLoader::load(const QString &filePath) const
{
    QImageReader reader(filePath);
    reader.setAutoTransform(true);

    QImage image;
    if (!reader.read(&image)) {
        return {QImage(), reader.errorString()};
    }

    return {image, QString()};
}
