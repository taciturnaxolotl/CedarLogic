/*****************************************************************************
   Project: CEDAR Logic Simulator
   SparkleUpdater: macOS auto-update support via Sparkle framework
*****************************************************************************/

#ifndef SPARKLEUPDATER_H
#define SPARKLEUPDATER_H

#ifdef __APPLE__

// Initialize Sparkle updater (call once at app startup)
void SparkleUpdater_Initialize();

// Check for updates (shows UI)
void SparkleUpdater_CheckForUpdates();

#endif // __APPLE__

#endif // SPARKLEUPDATER_H
