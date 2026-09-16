/*****************************************************************************
   Project: CEDAR Logic Simulator

   AutosaveStore: see AutosaveStore.h.
*****************************************************************************/

#include "AutosaveStore.h"

#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/log.h>
#include <wx/textfile.h>
#include <wx/utils.h>
#include <wx/process.h>
#include <wx/datetime.h>

#include <set>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace {

// Nobody comes back for a month-old crash.
const int MAX_AGE_DAYS = 30;

// <user data dir>/autosave, created on demand.
wxString storeDir() {
	wxFileName dir(wxStandardPaths::Get().GetUserLocalDataDir(), "");
	dir.AppendDir("autosave");
	if (!dir.DirExists()) dir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	return dir.GetPath();
}

// This run's identity. The pid makes two running copies distinct; the launch
// time keeps a recycled pid from colliding with an older session's leftovers.
const wxString& sessionId() {
	static const wxString id = wxString::Format(
		"%ld-%s", (long)wxGetProcessId(),
		wxDateTime::Now().Format("%Y%m%d%H%M%S"));
	return id;
}

wxString snapshotFor(const wxString& id) {
	return wxFileName(storeDir(), id + ".cdl").GetFullPath();
}

wxString pendingFor(const wxString& id) {
	return wxFileName(storeDir(), id + ".cdl.part").GetFullPath();
}

wxString recordFor(const wxString& id) {
	return wxFileName(storeDir(), id + ".txt").GetFullPath();
}

wxString lockFor(const wxString& id) {
	return wxFileName(storeDir(), id + ".lock").GetFullPath();
}

// The session id a file in the store belongs to, or empty for anything else
// that happens to be in the directory. Longest suffix first, so the id of a
// ".cdl.part" is not read as one ending in ".part".
wxString idFromFilename(const wxString& name) {
	static const char* const suffixes[] = { ".cdl.part", ".cdl", ".txt", ".lock" };
	for (unsigned int i = 0; i < WXSIZEOF(suffixes); i++) {
		wxString id;
		if (name.EndsWith(suffixes[i], &id)) return id;
	}
	return "";
}

// An exclusive lock on a file, held by keeping it open. The OS drops it when
// the process ends, however it ends, which is the whole point: a lock nobody
// holds is proof the session that took it is gone, where a pid is only a hint.
class ExclusiveLock {
public:
	ExclusiveLock() {}
	~ExclusiveLock() { release(); }

	bool take(const wxString& path, bool create) {
		release();
#ifdef __WXMSW__
		// Opening with no sharing is the lock: anyone else asking for the same
		// file gets a sharing violation until this handle closes.
		handle = ::CreateFile(path.t_str(), GENERIC_READ | GENERIC_WRITE,
		                      0, NULL, create ? OPEN_ALWAYS : OPEN_EXISTING,
		                      FILE_ATTRIBUTE_NORMAL, NULL);
		return handle != INVALID_HANDLE_VALUE;
#else
		fd = ::open(path.fn_str(), create ? (O_RDWR | O_CREAT) : O_RDWR, 0600);
		if (fd < 0) return false;
		if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
			::close(fd);
			fd = -1;
			return false;
		}
		return true;
#endif
	}

	void release() {
#ifdef __WXMSW__
		if (handle == INVALID_HANDLE_VALUE) return;
		::CloseHandle(handle);
		handle = INVALID_HANDLE_VALUE;
#else
		if (fd < 0) return;
		::close(fd);   // drops the flock with it
		fd = -1;
#endif
	}

	bool held() const {
#ifdef __WXMSW__
		return handle != INVALID_HANDLE_VALUE;
#else
		return fd >= 0;
#endif
	}

private:
	ExclusiveLock(const ExclusiveLock&);
	ExclusiveLock& operator=(const ExclusiveLock&);
#ifdef __WXMSW__
	HANDLE handle = INVALID_HANDLE_VALUE;
#else
	int fd = -1;
#endif
};

// This session's own lock, taken the first time it writes anything to the store
// and held until it exits. Anything written before the lock exists would be
// indistinguishable from a crashed session's leavings, so every path that
// creates a file calls this first.
ExclusiveLock& ownLock() {
	static ExclusiveLock lock;
	if (!lock.held()) lock.take(lockFor(sessionId()), /*create=*/true);
	return lock;
}

enum Liveness {
	NO_LOCK,   // nothing to ask -- left by a version that predates the lock
	DEAD,      // the lock is there and free, so its session is gone
	ALIVE      // somebody is holding it
};

Liveness livenessOf(const wxString& id) {
	const wxString path = lockFor(id);
	if (!wxFileName::FileExists(path)) return NO_LOCK;
	ExclusiveLock probe;
	return probe.take(path, /*create=*/false) ? DEAD : ALIVE;
}

// A "key: value" record, kept plain text so anyone poking around in the
// directory after a bad day can read it without the app.
bool readRecord(const wxString& path, AutosaveEntry* out) {
	wxTextFile file;
	if (!file.Open(path)) return false;
	for (wxString line = file.GetFirstLine(); !file.Eof(); line = file.GetNextLine()) {
		const int split = line.Find(": ");
		if (split == wxNOT_FOUND) continue;
		const wxString key = line.Left(split);
		const wxString value = line.Mid(split + 2);
		if      (key == "original") out->originalPath = value.ToStdString();
		else if (key == "taken")    out->takenAt      = value.ToStdString();
		else if (key == "pid")      out->pid          = wxAtol(value);
		else if (key == "host")     out->host         = value.ToStdString();
	}
	file.Close();
	return true;
}

// wxRemoveFile complains to the log when the file was never there, which is the
// ordinary case on a clean exit that never autosaved anything.
void removeIfPresent(const wxString& path) {
	if (wxFileName::FileExists(path)) wxRemoveFile(path);
}

void removeAllFor(const wxString& id) {
	removeIfPresent(snapshotFor(id));
	removeIfPresent(pendingFor(id));
	removeIfPresent(recordFor(id));
	removeIfPresent(lockFor(id));
}

// When a file was last written, or false if it is not there to ask. Asking
// about a missing file is routine here -- a session leaves behind whichever of
// its four files it got as far as writing -- but a failed stat is a wx system
// error, and in a GUI app an unhandled log error is a dialog in the user's
// face. Hence the existence check, and wxLogNull for whatever else the
// filesystem might have to say.
bool modifiedTime(const wxString& path, wxDateTime* out) {
	if (!wxFileName::FileExists(path)) return false;
	wxLogNull quiet;
	return wxFileName(path).GetTimes(NULL, out, NULL);
}

// The record for this session, naming what the snapshot is of and when it was
// taken. `takenAt` is a parameter rather than always "now" because an adopted
// snapshot is older than the moment it changed hands, and the user is deciding
// whether to recover it on the strength of that time.
void writeRecordAt(const std::string& originalPath, const wxString& takenAt) {
	ownLock();
	const wxString path = recordFor(sessionId());
	wxTextFile file;
	if (wxFileName::FileExists(path)) {
		if (!file.Open(path)) return;
		file.Clear();
	} else if (!file.Create(path)) {
		return;
	}
	file.AddLine("original: " + wxString(originalPath));
	file.AddLine("taken: " + takenAt);
	file.AddLine(wxString::Format("pid: %ld", (long)wxGetProcessId()));
	file.AddLine("host: " + wxGetHostName());
	file.Write();
	file.Close();
}

wxString whenWritten(const wxString& path) {
	wxDateTime modified;
	if (!modifiedTime(path, &modified)) return "";
	return modified.Format("%Y-%m-%d %H:%M");
}

// How long ago a session last wrote anything at all, judged by whichever of its
// files was touched most recently.
bool untouchedFor(const wxString& id, int days) {
	const wxString paths[] = { snapshotFor(id), pendingFor(id),
	                           recordFor(id), lockFor(id) };
	const wxDateTime cutoff = wxDateTime::Now() - wxTimeSpan::Days(days);
	for (unsigned int i = 0; i < WXSIZEOF(paths); i++) {
		wxDateTime modified;
		if (modifiedTime(paths[i], &modified) && modified.IsLaterThan(cutoff)) {
			return false;
		}
	}
	return true;
}

// Finish the rename a dead session did not live to make. A snapshot is written
// in full as a .part and then renamed into place, so one left behind is either
// a complete circuit newer than the snapshot beside it -- sometimes the only
// copy there is -- or the first half of one, caught mid-write. Reading it is
// the only way to tell those apart.
void promotePending(const wxString& id, const autosaveStore::ReadableTest& isReadable,
                    AutosaveEntry* entry) {
	const wxString pending = pendingFor(id);
	if (!wxFileName::FileExists(pending)) return;

	if (!isReadable || !isReadable(pending.ToStdString())) {
		removeIfPresent(pending);
		return;
	}
	// Quietly: a rename that fails here is handled by leaving the part where it
	// is for the next launch, and is no reason to put a system error in front of
	// someone who has not even been asked about recovery yet.
	bool renamed;
	{
		wxLogNull quiet;
		renamed = wxRenameFile(pending, snapshotFor(id), /*overwrite=*/true);
	}
	if (!renamed) return;

	// The record describes the snapshot this one supersedes, so its time now
	// understates what is on disk. The file's own is the honest answer.
	entry->takenAt = whenWritten(snapshotFor(id)).ToStdString();
}

}  // namespace

namespace autosaveStore {

std::string snapshotPath() {
	return snapshotFor(sessionId()).ToStdString();
}

std::string pendingPath() {
	ownLock();
	return pendingFor(sessionId()).ToStdString();
}

bool commitPending() {
	return wxRenameFile(pendingFor(sessionId()), snapshotFor(sessionId()),
	                    /*overwrite=*/true);
}

void writeRecord(const std::string& originalPath) {
	writeRecordAt(originalPath, wxDateTime::Now().Format("%Y-%m-%d %H:%M"));
}

void clearOwn() {
	ownLock().release();   // Windows will not delete a file that is still open
	removeAllFor(sessionId());
}

std::vector<AutosaveEntry> findRecoverable(const ReadableTest& isReadable) {
	std::vector<AutosaveEntry> found;
	wxDir dir(storeDir());
	if (!dir.IsOpened()) return found;

	// Group the directory by session id rather than walking records alone. A
	// snapshot is committed before it is named in a record, so a session that
	// died between those two steps leaves a good .cdl that a records-only sweep
	// would neither offer nor ever clean up.
	std::set<wxString> ids;
	wxString name;
	for (bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_FILES); more;
	     more = dir.GetNext(&name)) {
		const wxString id = idFromFilename(name);
		if (!id.empty() && id != sessionId()) ids.insert(id);
	}

	for (std::set<wxString>::const_iterator it = ids.begin(); it != ids.end(); ++it) {
		const wxString id = *it;

		AutosaveEntry entry;
		const bool hasRecord = readRecord(recordFor(id), &entry);
		if (hasRecord) entry.recordPath = recordFor(id).ToStdString();

		// Someone is holding the lock: another copy of the app at work, not
		// wreckage. This is the one test worth trusting, so it comes first --
		// nothing below may throw away the files of a session that is running.
		const Liveness liveness = livenessOf(id);
		if (liveness == ALIVE) continue;

		if (untouchedFor(id, MAX_AGE_DAYS)) {
			removeAllFor(id);
			continue;
		}

		// Leftovers from a version without the lock: the pid is all there is.
		// It is believed only this far because the files are recent -- a pid
		// the system has since given to some other program reads as "running"
		// for good, which would hide this snapshot at every launch after.
		if (liveness == NO_LOCK && hasRecord &&
		    entry.host == wxGetHostName().ToStdString() && entry.pid > 0 &&
		    wxProcess::Exists((int)entry.pid)) {
			continue;
		}

		promotePending(id, isReadable, &entry);

		// Nothing to offer: whatever is left is litter, and promising the user
		// a file that is not there would be worse than saying nothing.
		const wxString snapshot = snapshotFor(id);
		if (!wxFileName::FileExists(snapshot)) {
			removeAllFor(id);
			continue;
		}

		entry.snapshotPath = snapshot.ToStdString();
		entry.lockPath = wxFileName::FileExists(lockFor(id))
			? lockFor(id).ToStdString() : std::string();
		// A session that crashed before writing a record still left a snapshot
		// worth offering; the file itself says when.
		if (entry.takenAt.empty()) entry.takenAt = whenWritten(snapshot).ToStdString();
		found.push_back(entry);
	}
	return found;
}

bool adopt(const AutosaveEntry& entry) {
	if (entry.snapshotPath.empty()) return false;

	// Move rather than copy: one snapshot, changing hands, so no launch after
	// this one can offer the same work twice.
	ownLock();
	if (!wxRenameFile(entry.snapshotPath, snapshotFor(sessionId()),
	                  /*overwrite=*/true)) {
		return false;
	}
	writeRecordAt(entry.originalPath, entry.takenAt);
	if (!entry.recordPath.empty()) removeIfPresent(entry.recordPath);
	if (!entry.lockPath.empty())   removeIfPresent(entry.lockPath);
	return true;
}

void discard(const AutosaveEntry& entry) {
	if (!entry.snapshotPath.empty()) removeIfPresent(entry.snapshotPath);
	if (!entry.recordPath.empty())   removeIfPresent(entry.recordPath);
	if (!entry.lockPath.empty())     removeIfPresent(entry.lockPath);
}

}  // namespace autosaveStore
