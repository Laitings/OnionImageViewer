#pragma once

#include <QString>

// Lets a second launch hand its file to the window that is already open (Preferences > Startup).
// Every running instance listens on a local socket named after the Windows user (MainWindow
// starts the server); a launch that finds one connects, sends the path and quits.
namespace SingleInstance {

// The socket name; one per Windows user so users on the same PC don't meet each other's windows.
QString serverName();

// Sends the path to a running instance and lets it come to the front. Returns false when none
// is listening (the caller then opens the file itself).
bool forwardToRunning(const QString &path);

} // namespace SingleInstance
