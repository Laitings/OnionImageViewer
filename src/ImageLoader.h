#pragma once

#include <QImage>
#include <QString>

class ImageLoader
{
public:
    struct Result
    {
        QImage image;
        QString error;

        bool isValid() const;
    };

    virtual ~ImageLoader() = default;
    virtual Result load(const QString &filePath) const = 0;
};

class QtImageLoader final : public ImageLoader
{
public:
    Result load(const QString &filePath) const override;
};
