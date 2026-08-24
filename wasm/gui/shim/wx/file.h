#pragma once
#include "wx/wxheadless.h"

// wxTempFile writes to a scratch file and renames on Commit, so a crash cannot
// truncate the original. A browser has no filesystem to be atomic about: the
// shell receives the bytes and decides what to do with them, so this exists
// only to satisfy the save path's translation unit.
class wxTempFile {
public:
	bool Open(const wxString &) { return false; }
	bool IsOpened() const { return false; }
	bool Write(const wxString &, int = 0) { return false; }
	bool Flush() { return false; }
	bool Commit() { return false; }
	void Discard() {}
};

class wxFile {
public:
	bool Create(const wxString &, bool = false) { return false; }
	bool Open(const wxString &) { return false; }
	bool IsOpened() const { return false; }
	bool Write(const wxString &) { return false; }
	bool Close() { return false; }
};
