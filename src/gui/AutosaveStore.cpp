/*****************************************************************************
   Project: CEDAR Logic Simulator

   AutosaveStore: see AutosaveStore.h.
*****************************************************************************/

#include "AutosaveStore.h"

#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/textfile.h>
#include <wx/utils.h>
#include <wx/process.h>
#include <wx/datetime.h>

namespace {

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

// The record for this session, naming what the snapshot is of and when it was
// taken. `takenAt` is a parameter rather than always "now" because an adopted
// snapshot is older than the moment it changed hands, and the user is deciding
// whether to recover it on the strength of that time.
void writeRecordAt(const std::string& originalPath, const wxString& takenAt) {
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

}  // namespace

namespace autosaveStore {

std::string snapshotPath() {
	return snapshotFor(sessionId()).ToStdString();
}

std::string pendingPath() {
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
	removeIfPresent(snapshotFor(sessionId()));
	removeIfPresent(pendingFor(sessionId()));
	removeIfPresent(recordFor(sessionId()));
}

std::vector<AutosaveEntry> findRecoverable() {
	std::vector<AutosaveEntry> found;
	wxDir dir(storeDir());
	if (!dir.IsOpened()) return found;

	wxString name;
	for (bool more = dir.GetFirst(&name, "*.txt", wxDIR_FILES); more;
	     more = dir.GetNext(&name)) {
		const wxString id = name.BeforeLast('.');
		if (id == sessionId()) continue;              // our own, still in use

		AutosaveEntry entry;
		entry.recordPath = recordFor(id).ToStdString();
		if (!readRecord(recordFor(id), &entry)) continue;

		// Still running? Then this is another copy of the app doing its job,
		// not the remains of a crash. Only trust the pid on the machine that
		// wrote it -- pids from another host mean nothing here.
		if (entry.host == wxGetHostName().ToStdString() && entry.pid > 0 &&
		    wxProcess::Exists((int)entry.pid)) {
			continue;
		}

		// Nobody comes back for a month-old crash, and a pid the system has
		// since handed to some other program reads as "still running" forever,
		// so without an age limit that record would never be cleaned up.
		wxDateTime modified;
		if (wxFileName(recordFor(id)).GetTimes(NULL, &modified, NULL) &&
		    modified.IsEarlierThan(wxDateTime::Now() - wxTimeSpan::Days(30))) {
			removeIfPresent(snapshotFor(id));
			removeIfPresent(pendingFor(id));
			removeIfPresent(recordFor(id));
			continue;
		}

		// The session is gone, so a half-written snapshot of its is only litter.
		removeIfPresent(pendingFor(id));

		// A record with no snapshot has nothing to offer; tidy it away rather
		// than promising the user a file that is not there.
		const wxString snapshot = snapshotFor(id);
		if (!wxFileName::FileExists(snapshot)) {
			wxRemoveFile(recordFor(id));
			continue;
		}
		entry.snapshotPath = snapshot.ToStdString();
		found.push_back(entry);
	}
	return found;
}

bool adopt(const AutosaveEntry& entry) {
	if (entry.snapshotPath.empty()) return false;

	// Move rather than copy: one snapshot, changing hands, so no launch after
	// this one can offer the same work twice.
	if (!wxRenameFile(entry.snapshotPath, snapshotFor(sessionId()),
	                  /*overwrite=*/true)) {
		return false;
	}
	writeRecordAt(entry.originalPath, entry.takenAt);
	if (!entry.recordPath.empty()) removeIfPresent(entry.recordPath);
	return true;
}

void discard(const AutosaveEntry& entry) {
	if (!entry.snapshotPath.empty()) removeIfPresent(entry.snapshotPath);
	if (!entry.recordPath.empty())   removeIfPresent(entry.recordPath);
}

}  // namespace autosaveStore
