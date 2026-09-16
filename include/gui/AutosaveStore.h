/*****************************************************************************
   Project: CEDAR Logic Simulator

   AutosaveStore: where crash-recovery snapshots live, and what is known about
   them.

   Snapshots go in the user's own data directory rather than beside the document
   or in the working directory. The working directory is wherever the app
   happened to be launched from -- "/" from the Dock, a read-only install folder
   on Windows -- so a snapshot written there may silently fail, and one written
   successfully is only ever found again if the next launch starts in the same
   place. Beside the document is no better for this app's users, whose files sit
   on lab shares and USB sticks that may not be mounted next time.

   Each run owns one session id -- its pid and the time it started -- and up to
   four files named for it:

       <id>.cdl        the snapshot itself
       <id>.cdl.part   a snapshot being written, renamed over the above
       <id>.txt        a plain-text record: what it is of, and when
       <id>.lock       held open and locked for as long as the session lives

   The lock is how a crashed session is told from a running one. A pid cannot do
   that job on its own: the system hands the number to some other program soon
   enough, and a snapshot whose pid has been recycled reads as "still in use"
   at every launch after, which hides it for good. An OS file lock is released
   when the process ends however it ends, so taking it means the owner is gone.
*****************************************************************************/

#ifndef AUTOSAVESTORE_H_
#define AUTOSAVESTORE_H_

#include <functional>
#include <string>
#include <vector>

// A snapshot left behind by a session that did not exit cleanly.
struct AutosaveEntry {
	std::string snapshotPath;   // the .cdl to load
	std::string recordPath;     // its sidecar; empty if the session never wrote one
	std::string lockPath;       // its lock, removed together with the snapshot
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

// Drop this run's files. Called on a clean exit -- what is left behind
// afterwards is, by definition, a crash.
void clearOwn();

// Answers whether a file holds a circuit this app can open. findRecoverable
// needs it to judge a half-committed snapshot, and asking the caller keeps the
// store itself out of the business of parsing circuits.
typedef std::function<bool(const std::string& path)> ReadableTest;

// What other sessions left behind that they are no longer coming back for.
//
// Sessions still holding their lock are skipped: those are other copies of the
// app at work, not wreckage. Everything else is fair game, including a session
// that crashed before it ever wrote a record -- its snapshot is offered with
// what can be read off the file itself.
//
// A `.cdl.part` is a snapshot its session finished writing but did not live to
// rename. If it reads as a circuit it is newer than the committed snapshot, and
// sometimes the only one there is, so the rename is finished here on the dead
// session's behalf. If it does not read, it was caught mid-write and is thrown
// away. Anything left untouched for a month goes the same way.
std::vector<AutosaveEntry> findRecoverable(const ReadableTest& isReadable);

// Take another session's snapshot over as this one's own, keeping what it says
// it is and when it was taken. Recovering work does not put it on disk anywhere
// new -- it only puts it back on screen -- so deleting the snapshot at that
// moment leaves the only copy in memory until the first autosave of the new
// session, and a second crash inside that window takes the lot. Adopting it
// instead means there is always a snapshot to come back to. Reports whether the
// snapshot could be taken over; if not, it is left alone for the next launch to
// offer again.
bool adopt(const AutosaveEntry& entry);

// Forget one entry, once it has been declined or superseded.
void discard(const AutosaveEntry& entry);

}  // namespace autosaveStore

#endif /*AUTOSAVESTORE_H_*/
