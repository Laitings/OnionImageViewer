#pragma once

#include <QList>
#include <QString>

// The app's colour themes. "System" follows the Windows light/dark setting (and changes with it
// while the app runs); the rest are fixed palettes. The choice is stored in QSettings under
// `appearance/theme` and applied to the whole application with qApp->setPalette().
namespace Theme {

enum class Id { System, Light, Dark, Blue, Green, Red, Purple, Jamaica, Rainbow };

// All themes in the order they are listed in Preferences.
QList<Id> all();
// Name shown to the user, e.g. "Dark blue".
QString displayName(Id id);
// Stable name used in the settings file; unknown names fall back to System.
QString key(Id id);
Id fromKey(const QString &key);

// The theme saved in the settings (System when nothing was saved yet).
Id saved();
void save(Id id);

// Applies the theme to the running application: Fusion style, palette and, where Qt supports it,
// the matching Windows title bar. Call it again to switch; call watchSystem() once so "System"
// follows later changes of the Windows setting.
void apply(Id id);
void watchSystem();

} // namespace Theme
