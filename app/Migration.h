#pragma once

#include <QString>

class QSettings;

// The app was called Gridline until 2026-11-05. Its settings (QSettings,
// named after the organization and application) and its data folder
// (AppDataLocation: brushes, palettes, recovery copies) were named after it,
// so the first run as Smalto takes them over once.
namespace migration {

// Copies every key of `from` into `to` when `to` has none yet (a first run
// under the new name). The old settings stay as they were. Returns whether it
// copied anything.
bool copySettings(QSettings& from, QSettings& to);

// Moves the folder `from` to `to` when `from` exists and `to` doesn't yet.
// Returns whether it moved it.
bool moveFolder(const QString& from, const QString& to);

// Both, from Gridline's settings and data folder to the current
// application's. Call after the application and organization names are set
// and before anything reads settings or the data folder.
void fromGridline();

} // namespace migration
