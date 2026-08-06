#include "migrate.hpp"

#include "circuit_file_io.hpp"

#include <cctype>
#include <stdexcept>
#include <string>

namespace cl {

namespace {

// -- handler 1: gate renames -------------------------------------------------
// Gate types removed or replaced between legacy versions. A null `note` means a
// silent alias (no behavioral change); a non-null note is a behavior change the
// user should see. This table replaces the legacy loader's if-ladder of
// wxMessageBox calls — add a row to migrate a newly-removed gate.
struct Rename {
	const char *from;
	const char *to;
	const char *note;
};

const Rename kRenames[] = {
	{ "AM_RAM_16x16_Single_Port", "AM_RAM_16x16", nullptr },
	{ "AA_DFF", "AE_DFF_LOW",
	  "The high-active-reset D flip-flop was removed; replaced with the low-active-reset version." },
	{ "BA_JKFF", "BE_JKFF_LOW",
	  "The high-active-reset JK flip-flop was removed; replaced with the low-active-reset version." },
	{ "BA_JKFF_NT", "BE_JKFF_LOW_NT",
	  "The high-active-reset negative-edge JK flip-flop was removed; replaced with the low-active-reset version." },
};

void applyRenames(CircuitFile &cf, std::vector<MigrationNotice> &out) {
	for (Page &pg : cf.pages)
		for (GateInstance &g : pg.gates)
			for (const Rename &r : kRenames) {
				if (g.libName != r.from) continue;
				std::string oldName = g.libName;
				g.libName = r.to;
				MigrationNotice n;
				n.gateUuid = g.uuid;
				n.libName = g.libName;
				n.autoFixed = true;
				if (r.note) {
					n.severity = Severity::Warning;
					n.summary = oldName + " is deprecated — replaced with " + r.to;
					n.detail = r.note;
				} else {
					n.severity = Severity::Info;
					n.summary = oldName + " renamed to " + r.to;
				}
				out.push_back(std::move(n));
				break;
			}
}

// -- handler 2: decoder output-width fix -------------------------------------
// The DECODER gate historically sized its output bus as ceil(pow(inBits, 2)) —
// inBits squared — instead of 2^inBits. For inBits 2 and 4 the two agree, but a
// 3-bit decoder had 9 outputs (OUT_0..OUT_8) where it should have 8. The core now
// declares the correct count; this handler only speaks up when that actually
// costs a saved circuit something — a wire attached to a dropped output. When
// nothing was wired to the vanished pins it stays silent (real files carry many
// such decoders, and a notice per gate would just be noise).

bool isDecoder(const std::string &libName) {
	std::string lower = libName;
	for (char &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return lower.find("decoder") != std::string::npos;
}

// The index n of an "OUT_n" pin, or -1 if the pin is not an output.
int outIndex(const std::string &pin) {
	if (pin.rfind("OUT_", 0) != 0) return -1;
	try {
		return std::stoi(pin.substr(4));
	} catch (...) {
		return -1;
	}
}

void applyDecoderWidth(CircuitFile &cf, std::vector<MigrationNotice> &out) {
	for (Page &pg : cf.pages) {
		for (GateInstance &g : pg.gates) {
			if (!isDecoder(g.libName)) continue;

			int inBits = -1;
			for (const Param &p : g.params)
				if (p.name == "INPUT_BITS") {
					try {
						inBits = std::stoi(p.value);
					} catch (...) {
					}
				}
			if (inBits <= 0) continue;

			int buggy = inBits * inBits; // old: ceil(pow(inBits, 2))
			int fixed = 1 << inBits;     // new: 2^inBits
			if (fixed >= buggy) continue; // the fix only adds/keeps outputs — safe

			// Outputs OUT_[fixed .. buggy-1] disappear; collect any that are wired.
			std::vector<int> dropped;
			for (const WireInstance &w : pg.wires)
				for (const WireConn &c : w.connects) {
					if (c.gateUuid != g.uuid) continue;
					int idx = outIndex(c.pin);
					if (idx >= fixed && idx < buggy) dropped.push_back(idx);
				}
			if (dropped.empty()) continue; // corrected silently — nothing was lost

			std::string pins;
			for (size_t i = 0; i < dropped.size(); ++i) {
				if (i) pins += ", ";
				pins += "OUT_" + std::to_string(dropped[i]);
			}
			MigrationNotice n;
			n.gateUuid = g.uuid;
			n.libName = g.libName;
			n.severity = Severity::Warning;
			n.autoFixed = false;
			n.summary = "Decoder loses wired output(s): " + pins;
			n.detail = "This decoder had " + std::to_string(buggy) +
			           " outputs due to a width bug; the corrected count is " +
			           std::to_string(fixed) + ". Wire(s) attached to " + pins +
			           " will be disconnected. Reconnect them to a valid output (OUT_0.." +
			           std::to_string(fixed - 1) + ") after loading.";
			out.push_back(std::move(n));
		}
	}
}

using Handler = void (*)(CircuitFile &, std::vector<MigrationNotice> &);

const Handler kHandlers[] = { applyRenames, applyDecoderWidth };

} // namespace

std::vector<MigrationNotice> migrate(CircuitFile &cf) {
	std::vector<MigrationNotice> notices;
	for (Handler h : kHandlers) h(cf, notices);
	return notices;
}

LoadResult loadCircuit(const std::string &text) {
	LoadResult r;
	r.source = detectFormat(text);
	switch (r.source) {
	case SourceFormat::SexprV3:
		r.file = readCircuitFile(text); // already the target format; no migration
		break;
	case SourceFormat::XmlV1:
	case SourceFormat::XmlV2:
		r.file = readLegacyCdl(text);
		r.notices = migrate(r.file);
		break;
	default:
		throw std::runtime_error("loadCircuit: unrecognized .cdl format");
	}
	return r;
}

} // namespace cl
