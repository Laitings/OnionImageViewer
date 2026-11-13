#pragma once

#include <QImage>
#include <QList>
#include <QPair>
#include <QSize>
#include <QString>

// Illustrator (.ai), EPS (.eps/.epsf/.epsi) and PDF (.pdf, first page) files, rasterised for
// viewing. Areas without artwork stay transparent (white logos would vanish on a white page).
//
// - PDF and Illustrator files (which are PDFs inside when saved "PDF compatible", the default)
//   render through the Qt PDF module when the app was built with it.
// - EPS files (and old PostScript-based .ai files) render through Ghostscript when it is
//   installed (gswin64c on the PATH or under C:\Program Files\gs). Without it, the preview that
//   EPS files often carry (a TIFF in DOS EPS files, or an EPSI bitmap) is shown instead.
// - Ghostscript is also the fallback for PDF/AI files when Qt PDF isn't available.
namespace Vector {

bool isVectorFile(const QString &filePath);

// Full view: rendered at about 300 dpi (long side between 2000 and 6000 px). With a target size
// (thumbnails), rendered to fit that size.
QImage read(const QString &filePath, QString *error = nullptr, const QSize &targetSize = QSize());

// Header facts for the metadata panel: (key, value) pairs.
QList<QPair<QString, QString>> describe(const QString &filePath);

bool hasQtPdf();
QString ghostscriptPath(); // empty if not found

} // namespace Vector
