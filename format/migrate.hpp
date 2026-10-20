#pragma once

#include "circuit_file.hpp"
#include "legacy_cdl.hpp"
#include <string>
#include <vector>

namespace cl {

enum class Severity { Info, Warning };

// One thing a migration changed, or wants the user to know about. Deliberately
// GUI-free: the caller decides how to surface it (dialog, log line, inline badge).
// This replaces the legacy loader's inline wxMessageBox calls, so the same
// migration logic can run headless and be tested.
struct MigrationNotice {
	Severity severity = Severity::Info;
	std::string gateUuid; // the affected gate, if the notice is about one
	std::string libName;  // its post-migration library name
	std::string summary;  // one line, safe to show in a list
	std::string detail;   // longer explanation / what to do
	bool autoFixed = false; // the migration already resolved it; no user action needed
};

// The result of reading a .cdl from disk: the upgraded model, what it was read
// as, and everything the migration wants to tell the user.
struct LoadResult {
	CircuitFile file;
	SourceFormat source = SourceFormat::Unknown;
	std::vector<MigrationNotice> notices;
};

// Detect the format, parse it, and (for legacy files) run the migration handlers.
// v3 files pass through unmigrated. Throws std::runtime_error on unreadable input.
LoadResult loadCircuit(const std::string &text);

// Run the migration handlers over an already-parsed file, rewriting it in place
// and returning the notices. Exposed for testing; loadCircuit calls it for you.
std::vector<MigrationNotice> migrate(CircuitFile &cf);

} // namespace cl
