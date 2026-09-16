// Which OpenGL implementations count as a software stand-in.
//
// This is the judgement behind the advice CedarLogic gives someone whose canvas
// will not draw: either "your machine has no working 3D, turn it on" or "your
// machine is fine, this is our bug." Getting it backwards sends a person to
// reinstall a driver that was never at fault, so the rule is worth pinning
// down with real driver strings rather than trusting it by eye.
//
// The strings below are what these implementations actually report.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "render/RendererHealth.h"

using cl::render::looksLikeSoftwareGL;

TEST_CASE("real graphics hardware is not mistaken for software") {
	CHECK_FALSE(looksLikeSoftwareGL("Apple M4", "4.1 Metal - 90.5"));
	CHECK_FALSE(looksLikeSoftwareGL("NVIDIA GeForce RTX 3060/PCIe/SSE2", "4.6.0 NVIDIA 550.54.14"));
	CHECK_FALSE(looksLikeSoftwareGL("Intel(R) UHD Graphics 620", "4.6.0 - Build 27.20.100.8935"));
	CHECK_FALSE(looksLikeSoftwareGL("AMD Radeon RX 6800 XT", "4.6.0 Core Profile Context"));
	// A VM with 3D acceleration switched ON passes through to the host GPU.
	CHECK_FALSE(looksLikeSoftwareGL("VMware SVGA 3D", "4.1 (Core Profile) Mesa 23.2.1"));
	CHECK_FALSE(looksLikeSoftwareGL("Virgl (AMD Radeon Graphics)", "4.3 (Core Profile) Mesa 24.0.9"));
}

TEST_CASE("the software fallbacks are recognised") {
	// Windows with no graphics driver at all. This is the one the bug reports
	// come from: a context comes up, so nothing looks wrong, but it is OpenGL
	// 1.1 from the 1990s and Skia needs shaders.
	CHECK(looksLikeSoftwareGL("GDI Generic", "1.1.0"));
	CHECK(looksLikeSoftwareGL("Microsoft Basic Render Driver", "1.1.0"));
	// Mesa's software rasterisers, which report a modern GL they emulate on the
	// CPU -- so a version check alone would wave them through.
	CHECK(looksLikeSoftwareGL("llvmpipe (LLVM 17.0.6, 256 bits)", "4.5 (Core Profile) Mesa 23.2.1"));
	CHECK(looksLikeSoftwareGL("softpipe", "3.3 (Core Profile) Mesa 22.0.5"));
	CHECK(looksLikeSoftwareGL("Software Rasterizer", "3.3.0"));
}

TEST_CASE("the version threshold is where the shaders start") {
	// Shaders arrive in OpenGL 2.0, so that is the line, whatever the name says.
	CHECK(looksLikeSoftwareGL("Some Ancient Card", "1.4.0"));
	CHECK_FALSE(looksLikeSoftwareGL("Some Ancient Card", "2.0.0"));
}

TEST_CASE("a driver that will not name itself counts as software") {
	// Better to give the "your 3D is off" advice than to tell someone their
	// invisible driver looks capable.
	CHECK(looksLikeSoftwareGL("", ""));
	CHECK(looksLikeSoftwareGL("", "4.6.0"));
}

TEST_CASE("the match ignores case and surrounding text") {
	CHECK(looksLikeSoftwareGL("gdi generic", "1.1.0"));
	CHECK(looksLikeSoftwareGL("LLVMpipe (LLVM 15.0.7, 128 bits)", "4.5"));
}
