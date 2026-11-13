#pragma once

#include <QIcon>

// Line icons for the toolbar, navigation buttons and menus. They are drawn as vectors at whatever
// size and screen scale they are shown at, in the palette's text color (dimmed when disabled).
namespace Icons {

enum class Kind {
    Back,
    Up,
    FolderPanel,
    Open,
    Search,
    FitToWindow,
    ActualSize,
    Metadata,
    Color,
    ViewIcons,
    ViewList,
    ViewDetails,
    ViewTiles,
    QuickAccess, // a star, for the Quick access section of the folder panel
    ThisPC,      // a monitor, for the drives section of the folder panel
    Pin,
    Unpin,
};

QIcon icon(Kind kind);

} // namespace Icons
