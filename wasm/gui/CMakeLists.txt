cmake_minimum_required(VERSION 3.11)

# The CedarLogic core, built for the browser.
#
# Nothing in this list is web-specific: it is the same gate model, geometry,
# parsing, and Scene-emitting code the desktop compiles. What makes the build
# work is that none of it includes wxWidgets any more -- shim/ supplies the
# handful of types (wxString, wxDocument, wxCommand, the mutexes) that the core
# still spells in wx's vocabulary, and shim/gl_shim.h stands in for the GL
# scalar typedefs the geometry uses.
#
# If a file added here fails to compile, the fix is almost always to break an
# include in src/gui rather than to widen the shim: the shim exists to name what
# the core legitimately still uses, not to reimplement a toolkit.

project(CedarLogicGUIWASM VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Extra link flags for debugging a browser crash, e.g.
#   -DCEDAR_WASM_EXTRA_LINK="-sASSERTIONS=2 -g2"
# which turns emscripten's out-of-bounds aborts into named C++ stack traces.
set(CEDAR_WASM_EXTRA_LINK "" CACHE STRING "Extra emcc link flags")

# _PRODUCTION_ disables the core's file-based logging, which has nowhere to go
# in a browser.
add_definitions(-D_PRODUCTION_)

set(CORE_SRC
	"${REPO}/src/gui/CircuitPage.cpp"
	"${REPO}/src/gui/GUICircuit.cpp"
	"${REPO}/src/gui/guiGate.cpp"
	"${REPO}/src/gui/guiWire.cpp"
	"${REPO}/src/gui/wireSegment.cpp"
	"${REPO}/src/gui/klsCollisionChecker.cpp"
	"${REPO}/src/gui/gl_defs.cpp"
	"${REPO}/src/gui/GateLibrary.cpp"
	"${REPO}/src/gui/LibraryParse.cpp"
	"${REPO}/src/gui/XMLParser.cpp"
	"${REPO}/src/gui/Settings.cpp"
	"${REPO}/src/gui/SimBridge.cpp"
	"${REPO}/src/gui/route/TrunkRouter.cpp"
)

file(GLOB LOGIC_SRC "${REPO}/logic/src/*.cpp")
file(GLOB FORMAT_SRC "${REPO}/format/*.cpp")

add_executable(cedarlogic-gui
	bindings.cpp
	headless.cpp
	text_metrics.cpp
	${CORE_SRC}
	${LOGIC_SRC}
	${FORMAT_SRC}
)

target_include_directories(cedarlogic-gui PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/shim"
	"${REPO}/include"
	"${REPO}/include/gui"
	"${REPO}/include/gui/command"
	"${REPO}/include/gui/render"
	"${REPO}/logic/include"
	"${REPO}/format"
)

# Stand in for <GL/gl.h> everywhere, the way src/gui/tests does for its headless
# suites: the geometry still names GLfloat, but nothing calls OpenGL.
target_compile_options(cedarlogic-gui PRIVATE
	"SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/shim/gl_shim.h")

set_target_properties(cedarlogic-gui PROPERTIES
	SUFFIX ".js"
	LINK_FLAGS "\
		--bind \
		--embed-file ${REPO}/res/cl_gatedefs.xml@/res/cl_gatedefs.xml \
		-s MODULARIZE=1 \
		-s EXPORT_ES6=1 \
		-s EXPORT_NAME='CedarLogicGUI' \
		-s ALLOW_MEMORY_GROWTH=1 \
		-s ENVIRONMENT='web,worker,node' \
		-s NO_EXIT_RUNTIME=1 \
		-s EXPORTED_RUNTIME_METHODS=['HEAPF32'] \
		-s WASM=1 \
		${CEDAR_WASM_EXTRA_LINK} \
		-O2 \
	"
)
