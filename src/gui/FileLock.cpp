/*****************************************************************************
   Project: CEDAR Logic Simulator

   FileLock: see FileLock.h and docs/RFC-crash-recovery.md.
*****************************************************************************/

#include "FileLock.h"

#include <wx/filename.h>
#include <wx/textfile.h>
#include <wx/utils.h>
#include <wx/process.h>
#include <wx/datetime.h>

namespace {

// `~<name>.lck` beside the document, the convention KiCad uses. Beside it, not
// in the user's data directory, because the point is to be visible to the OTHER
// person editing the same file on a shared drive.
wxString lockPathFor(const std::string& path) {
	wxFileName file(path);
	if (!file.IsOk() || file.GetFullName().empty()) return "";
	file.SetFullName("~" + file.GetFullName() + ".lck");
	return file.GetFullPath();
}

wxString ownerTag() {
	return wxString::Format("%s@%s:%ld", wxGetUserId(), wxGetHostName(),
	                        (long)wxGetProcessId());
}

}  // namespace

FileLock::~FileLock() {
	release();
}

std::string FileLock::heldBy(const std::string& path) {
	const wxString lock = lockPathFor(path);
	if (lock.empty() || !wxFileName::FileExists(lock)) return "";

	wxTextFile file;
	if (!file.Open(lock)) return "";
	const wxString owner = file.GetLineCount() > 0 ? file.GetFirstLine() : wxString();
	const wxString taken = file.GetLineCount() > 1 ? file.GetLine(1) : wxString();
	file.Close();

	const wxString host = owner.AfterFirst('@').BeforeLast(':');
	const long pid = wxAtol(owner.AfterLast(':'));

	// Ours, and the process is gone: the session that took it crashed. Clear it
	// rather than making the user hunt down a file they cannot interpret --
	// which is the single loudest complaint on KiCad's tracker about this exact
	// mechanism.
	if (host == wxGetHostName() && pid > 0 && !wxProcess::Exists((int)pid)) {
		wxRemoveFile(lock);
		return "";
	}

	wxString who = owner.BeforeFirst(':');   // user@host, the pid means nothing
	if (!taken.empty()) who += " since " + taken;
	return who.ToStdString();
}

void FileLock::acquire(const std::string& path) {
	release();

	const wxString lock = lockPathFor(path);
	if (lock.empty()) return;

	wxTextFile file;
	if (wxFileName::FileExists(lock)) {
		if (!file.Open(lock)) return;
		file.Clear();
	} else if (!file.Create(lock)) {
		return;   // read-only media, or a directory we cannot write: no lock
	}
	file.AddLine(ownerTag());
	file.AddLine(wxDateTime::Now().Format("%Y-%m-%d %H:%M"));
	file.Write();
	file.Close();

	lockPath = lock.ToStdString();
}

void FileLock::release() {
	if (lockPath.empty()) return;
	wxRemoveFile(lockPath);
	lockPath.clear();
}
