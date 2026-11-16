#pragma once

#include <QDir>
#include <QSettings>
#include <QString>

// The keys (QSettings) and choices of the Preferences window. The dialog writes them when OK is
// pressed and MainWindow::applyPreferences() reads them and puts them to work, so a new
// preference needs: a constant here, a control on a PreferencesDialog page, and a line in
// applyPreferences() (or in the code that reads it directly).
namespace Preferences {

// Viewer
inline const QString AlphaBackground = QStringLiteral("viewer/alphaBackground"); // int, ImageViewport::AlphaBackground
inline const QString OpenZoom = QStringLiteral("viewer/openZoom");               // "auto", "fit" or "actual"
inline const QString WheelAction = QStringLiteral("viewer/wheelAction");         // "zoom" or "step"

// Grid
inline const QString GridView = QStringLiteral("grid/view");               // "icons", "list", "details" or "tiles"
inline const QString ThumbnailSize = QStringLiteral("grid/thumbnailSize"); // px
inline const QString ShowHidden = QStringLiteral("grid/showHidden");       // bool

// The extra QDir filter that lists hidden files and folders when "Show hidden files" is on.
inline QDir::Filters hiddenFilter()
{
    return QSettings().value(ShowHidden, false).toBool() ? QDir::Hidden : QDir::Filters();
}

// Startup
inline const QString StartupFolder = QStringLiteral("startup/folder");       // "none", "last", "pinned" or "pictures"
inline const QString StartupPinned = QStringLiteral("startup/pinnedFolder"); // a folder, for "pinned"
inline const QString LastFolder = QStringLiteral("startup/lastFolder");      // kept up to date for "last"

// Files
inline const QString PasteConflict = QStringLiteral("files/pasteConflict"); // "ask", "keepBoth" or "skip"

// Formats
inline const QString GhostscriptPath = QStringLiteral("formats/ghostscriptPath"); // empty: look for it

// Performance
inline const QString ThumbnailCacheMb = QStringLiteral("performance/thumbnailCacheMb"); // 0: no limit

// Updates
inline const QString CheckUpdatesOnStartup = QStringLiteral("updates/checkOnStartup"); // bool, default true

} // namespace Preferences
