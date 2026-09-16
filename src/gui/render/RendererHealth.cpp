// RendererHealth -- see RendererHealth.h.

#include "render/RendererHealth.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace cl {
namespace render {

namespace {

std::string gVendor, gRenderer, gVersion;
std::string gFailure;

bool containsNoCase(const std::string& haystack, const char* needle) {
	std::string h = haystack, n = needle;
	for (char& c : h) c = (char)std::tolower((unsigned char)c);
	for (char& c : n) c = (char)std::tolower((unsigned char)c);
	return h.find(n) != std::string::npos;
}

}  // namespace

bool looksLikeSoftwareGL(const std::string& renderer, const std::string& version) {
	if (renderer.empty()) return true;   // nothing answered at all

	// A GL version string starts with "major.minor" and may carry anything after
	// it. Skia needs shaders, so anything before 2.0 is a stand-in by definition.
	const int major = std::atoi(version.c_str());
	if (major > 0 && major < 2) return true;

	// The handful of names the software rasterisers go by. A version alone is not
	// enough: Mesa's llvmpipe honestly reports a modern GL that it emulates in
	// software, fast enough to pass a version check and far too slow to draw with.
	static const char* const kSoftwareNames[] = {
		"gdi generic",         // Windows' built-in OpenGL 1.1 fallback
		"microsoft basic",     // Basic Render Driver: a VM with no 3D at all
		"llvmpipe", "softpipe", "swrast",   // Mesa's software rasterisers
		"software rasterizer",
	};
	for (const char* name : kSoftwareNames) {
		if (containsNoCase(renderer, name)) return true;
	}
	return false;
}

void noteGLImplementation(const char* vendor, const char* renderer,
                          const char* version) {
	if (!gRenderer.empty()) return;   // the first answer is the one that counts
	if (renderer == nullptr || *renderer == '\0') return;
	gRenderer = renderer;
	gVendor = (vendor != nullptr) ? vendor : "";
	gVersion = (version != nullptr) ? version : "";
}

std::string glImplementation() {
	if (gRenderer.empty()) return "unknown";
	std::string s = gRenderer;
	if (!gVendor.empty()) s += " (" + gVendor + ")";
	if (!gVersion.empty()) s += ", OpenGL " + gVersion;
	return s;
}

void noteRendererFailure(const char* what) {
	if (!gFailure.empty()) return;
	gFailure = (what != nullptr) ? what : "unknown";
	// Also on stderr, which is where it lands when the app is run from a terminal
	// on macOS or Linux. On Windows the app has no console, so the message a user
	// actually sees is the one rendererFailureMessage builds.
	std::fprintf(stderr, "CedarLogic: drawing on the processor (%s). GL is %s.\n",
	             gFailure.c_str(), glImplementation().c_str());
}

bool rendererFailed() { return !gFailure.empty(); }

const std::string& rendererFailureReason() { return gFailure; }

std::string rendererFailureMessage() {
	if (gFailure.empty()) return std::string();

	// Two very different situations end up here and they want opposite advice.
	// Guessing between them is how someone with a perfectly good graphics card
	// gets sent off to reinstall a driver that was never at fault, so decide
	// from what the driver actually said about itself.
	if (looksLikeSoftwareGL(gRenderer, gVersion)) {
		return
			"CedarLogic ran into a rendering issue; more information is below\n\n"
			"Graphics: " + glImplementation() + "\n\n"
			"This computer has no working 3D graphics, so CedarLogic is drawing "
			"with the processor instead. Circuits still work, but the display "
			"will be slow. Turn on 3D acceleration for the virtual machine, or "
			"install a graphics driver.";
	}

	return
		"CedarLogic ran into a rendering issue; more information is below\n\n"
		"Graphics: " + glImplementation() + "\n"
		"Error: " + gFailure + "\n\n"
		"CedarLogic is drawing with the processor instead, which is slow. This "
		"is a bug in CedarLogic. Please report it with the two lines above.";
}

bool forceGLFailure() {
	// Empty, "0", "false" and "no" all mean off, so that clearing the variable
	// with CEDAR_FORCE_GL_FAILURE= does what it looks like it does rather than
	// silently switching the fallback on.
	static const bool forced = [] {
		const char* v = std::getenv("CEDAR_FORCE_GL_FAILURE");
		if (v == nullptr || *v == '\0') return false;
		std::string s = v;
		for (char& c : s) c = (char)std::tolower((unsigned char)c);
		return s != "0" && s != "false" && s != "no";
	}();
	return forced;
}

}  // namespace render
}  // namespace cl
