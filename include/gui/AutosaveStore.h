/*****************************************************************************
   Project: CEDAR Logic Simulator

   AutosaveStore: where crash-recovery snapshots live, and what is known about
   them. See docs/RFC-crash-recovery.md.

   Snapshots go in the user's own data directory rather than beside the document
   or in the working directory. The working directory is wherever the app
   happened to be launched from -- "/" from the Dock, a read-only install folder
   on Windows -- so a snapshot written there may silently fail, and one written
   successfully is only ever found again if the next launch starts in the same
   place. Beside the document is no better for this app's users, whose files sit
   on lab shares and USB sticks that may not be mounted next time.

   Each run owns one snapshot, named for its process, so two copies of the app
   cannot overwrite each other's work. Alongside it sits a small record naming
   the document the snapshot is of, when it was taken, and which process took
   it, so recovery can say what it is offering and can tell a crashed session
   from one that is still running.
*****************************************************************************/

#ifndef AUTOSAVESTORE_H_
#define AUTOSAVESTORE_H_

#include <string>
#include <vector>

// A snapshot left behind by a session that did not exit cleanly.
struct AutosaveEntry {
	std::string snapshotPath;   // the .cdl to load
	std::string recordPath;     // its sidecar, removed together with the snapshot
	std::string originalPath;   // document it was taken from; empty if never saved
	std::string takenAt;        // human-readable local time
	long        pid = 0;
	std::string host;
};

namespace autosaveStore {

// This run's snapshot path, creating the directory if need be. Stable for the
// lifetime of the process.
std::string snapshotPath();

// Where a snapshot being written goes, and how it is put in place afterwards.
// A snapshot is written here and then renamed over snapshotPath(), so a crash
// part way through the write leaves the last good snapshot alone instead of
// replacing it with half a file -- which is the one moment this whole feature
// exists for. commitPending() reports whether the rename took.
std::string pendingPath();
bool commitPending();

// Record what the current snapshot is of. Called on each successful autosave;
// `originalPath` may be empty for a circuit that has never been saved.
void writeRecord(const std::string& originalPath);

// Drop this run's snapshot and record. Called on a clean exit -- what is left
// behind afterwards is, by definition, a crash.
void clearOwn();

// Snapshots from other sessions that are no longer running. Sessions whose
// process is still alive are skipped: those are other copies of the app at
// work, not wreckage. Entries whose snapshot has vanished are skipped too, and
// their orphaned records cleaned up.
std::vector<AutosaveEntry> findRecoverable();

// Forget one entry once it has been recovered or declined.
void discard(const AutosaveEntry& entry);

}  // namespace autosaveStore

#endif /*AUTOSAVESTORE_H_*/
