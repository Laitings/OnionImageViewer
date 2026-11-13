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
    FitToWindow,
    ActualSize,
    Metadata,
    Color,
    ViewIcons,
    ViewList,
    ViewDetails,
    ViewTiles,
};

QIcon icon(Kind kind);

} // namespace Icons
