/*****************************************************************************
   Project: CEDAR Logic Simulator
   SparkleUpdater: macOS auto-update support via Sparkle framework
*****************************************************************************/

#ifndef SPARKLEUPDATER_H
#define SPARKLEUPDATER_H

#ifdef __APPLE__

// Initialize Sparkle updater (call once at app startup)
void SparkleUpdater_Initialize();

// Warn once at startup when this copy is running from a read-only location: a
// disk image it was never dragged out of, or Gatekeeper's own copy. Call after
// the main window exists, so the alert has something to sit in front of.
void SparkleUpdater_WarnIfReadOnlyLocation();

// Check for updates (shows UI)
void SparkleUpdater_CheckForUpdates();

#endif // __APPLE__

#endif // SPARKLEUPDATER_H
