// Turning a crash trace into a GitHub issue. Pure string work, so it is tested
// directly rather than by reading a rendered dialog.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "CrashIssue.h"

using namespace cl::crash;

namespace {

// What the reporter actually writes: a version/OS header, a blank line, then
// the exception and the walked stack.
const char *kTrace =
	"CedarLogic 3.2.1 (32-bit) on Windows 11 (build 26200), 64-bit edition\n"
	"\n"
	"Unhandled exception 0xC0000005 at 0093B171\n"
	"  read from address 0x00000000\n"
	"  guiWire::removeZeroLengthSegments          src/gui/guiWire.cpp:1069\n";

}  // namespace

TEST_CASE("the header and the stack land under separate headings") {
	CHECK(crashVersionLine(kTrace) ==
	      "CedarLogic 3.2.1 (32-bit) on Windows 11 (build 26200), 64-bit edition");

	const std::string body = crashTraceBody(kTrace);
	CHECK(body.find("Unhandled exception") == 0);
	CHECK(body.find("CedarLogic 3.2.1") == std::string::npos);
}

TEST_CASE("a trace that is only a header has no stack to show") {
	// Reachable when the header write succeeded and the stack write did not.
	// The header used to come out under both headings.
	const std::string header = "CedarLogic 3.2.1 (32-bit) on Linux";
	CHECK(crashVersionLine(header) == header);
	CHECK(crashTraceBody(header) == "");

	const std::string issue = crashIssueBody(header);
	CHECK(issue.find(header) != std::string::npos);
	// Once, under Version -- not again inside the code fence.
	CHECK(issue.find(header) == issue.rfind(header));
}

TEST_CASE("the title names the faulting frame, and degrades when there is none") {
	CHECK(crashIssueUrl(kTrace).find("title=Crash%3A%20Unhandled%20exception") !=
	      std::string::npos);
	CHECK(crashIssueUrl("CedarLogic 3.2.1").find("title=Crash&") != std::string::npos);
}

TEST_CASE("percent-encoding leaves only the unreserved set alone") {
	CHECK(urlEncode("aZ09-_.~") == "aZ09-_.~");
	CHECK(urlEncode(" ") == "%20");
	CHECK(urlEncode("\n") == "%0A");
	CHECK(urlEncode("&=#?") == "%26%3D%23%3F");
	// High bytes are encoded per byte, not mangled or dropped.
	CHECK(urlEncode("\xC3\xA9") == "%C3%A9");
}

TEST_CASE("a long body is cut on a character boundary, never inside one") {
	// A localised OS name or symbol makes the trace non-ASCII, and the URL copy
	// is capped. Cutting mid-sequence yields bytes no decoder will accept.
	//
	// Where the cap falls depends on the template length, so shift the payload
	// by one byte at a time: one of these must put a sequence across the cut.
	for (int pad = 0; pad < 4; pad++) {
		CAPTURE(pad);
		std::string trace = "CedarLogic 3.2.1 on Linux\n\n";
		trace.append(pad, 'x');
		for (int i = 0; i < 4000; i++) trace += "\xE2\x9C\x93";  // 3 bytes each

		const std::string url = crashIssueUrl(trace);
		const size_t at = url.find("&body=");
		REQUIRE(at != std::string::npos);

		std::string decoded;
		const std::string encoded = url.substr(at + 6);
		for (size_t i = 0; i < encoded.size();) {
			if (encoded[i] == '%') {
				decoded += static_cast<char>(std::stoi(encoded.substr(i + 1, 2), nullptr, 16));
				i += 3;
			} else {
				decoded += encoded[i++];
			}
		}

		size_t chars = 0;
		for (size_t i = 0; i < decoded.size(); chars++) {
			const unsigned char c = decoded[i];
			const size_t len = (c < 0x80) ? 1 : (c >> 5) == 0x6 ? 2
			                 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
			REQUIRE(len != 0);                  // never starts on a continuation byte
			REQUIRE(i + len <= decoded.size()); // never runs off the end
			for (size_t k = 1; k < len; k++) {
				REQUIRE((static_cast<unsigned char>(decoded[i + k]) & 0xC0) == 0x80);
			}
			i += len;
		}
		CHECK(chars > 0);
	}
}

TEST_CASE("a short body is not truncated at all") {
	const std::string url = crashIssueUrl(kTrace);
	CHECK(url.find(urlEncode("guiWire.cpp:1069")) != std::string::npos);
}
