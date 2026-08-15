/*****************************************************************************
   Project: CEDAR Logic Simulator

   FileLock: advisory "someone else has this open" marker for circuit files.

   Nothing here prevents a write -- an advisory lock cannot, and a lab share
   full of stale locks nobody can clear is worse than no locks at all. What it
   does is let the app notice a second editor before the two of them start
   taking turns overwriting each other, and say who the other one is.

   The lock is a sidecar beside the document, `~<name>.lck`, holding
   `user@host:pid` and the time it was taken. That shape is borrowed from
   KiCad, LibreOffice and Emacs, which all record the holder rather than merely
   the fact of a lock -- without an owner there is no way to tell a live editor
   from the debris of a crash. A lock this machine wrote whose process is gone
   is recognised as stale and taken silently; anything else is the user's call.
*****************************************************************************/

#ifndef FILELOCK_H_
#define FILELOCK_H_

#include <string>

class FileLock {
public:
	FileLock() {}
	~FileLock();   // releases

	// Who holds the lock on `path`, "" if nobody does or if it is this
	// machine's own stale one. Ask before opening a document.
	static std::string heldBy(const std::string& path);

	// Take the lock for `path`, releasing any previously held. Overwrites a
	// lock that is already there: callers ask heldBy() first and decide.
	void acquire(const std::string& path);

	// Drop the lock, if any. Safe to call when none is held.
	void release();

private:
	std::string lockPath;   // empty when nothing is held

	FileLock(const FileLock&);
	FileLock& operator=(const FileLock&);
};

#endif /*FILELOCK_H_*/
