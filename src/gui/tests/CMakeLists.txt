cmake_minimum_required(VERSION 3.16)

# Headless unit test for klsCollisionChecker. The checker is free of wxWidgets
# and only borrows the GLfloat type from the GL wrapper, so gl_shim.h is
# force-included to stand in for it -- letting this build and run on a CI runner
# with no OpenGL headers.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FetchContent)
if(NOT TARGET doctest::doctest)
	FetchContent_Declare(
		doctest
		GIT_REPOSITORY https://github.com/doctest/doctest.git
		GIT_TAG        v2.4.11
		GIT_SHALLOW    1
	)
	set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
	# doctest installs its headers and cmake config by default, which CPack
	# then sweeps into the installer -- users were getting include/doctest and
	# lib/cmake/doctest dropped into Program Files. It is a test dependency;
	# it has no business in the package.
	set(DOCTEST_NO_INSTALL ON)
	FetchContent_MakeAvailable(doctest)
endif()

set(_shim "${CMAKE_CURRENT_SOURCE_DIR}/gl_shim.h")

add_executable(test_collision
	"${CMAKE_CURRENT_SOURCE_DIR}/test_collision.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/klsCollisionChecker.cpp"
)
target_include_directories(test_collision PRIVATE
	"${PROJECT_SOURCE_DIR}/include/gui"
)
target_link_libraries(test_collision PRIVATE doctest::doctest)

# Force-include the GLfloat shim so both the test and klsCollisionChecker.cpp
# compile without the real GL/OS headers.
target_compile_options(test_collision PRIVATE
	"$<$<CXX_COMPILER_ID:MSVC>:/FI${_shim}>"
	"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:SHELL:-include ${_shim}>"
)

if(ENABLE_SANITIZERS AND NOT MSVC)
	target_compile_options(test_collision PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(test_collision PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME collision_tests COMMAND test_collision)

# Typed command serialization (Workstream D). Free of wxWidgets and OpenGL, so
# it just needs the command header and IDType -- no gl_shim.
add_executable(test_cmdserialize
	"${CMAKE_CURRENT_SOURCE_DIR}/test_cmdserialize.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/command/cmdSerialize.cpp"
)
target_include_directories(test_cmdserialize PRIVATE
	"${PROJECT_SOURCE_DIR}/include/gui/command"
	"${PROJECT_SOURCE_DIR}/logic/include"
)
target_link_libraries(test_cmdserialize PRIVATE doctest::doctest)

if(ENABLE_SANITIZERS AND NOT MSVC)
	target_compile_options(test_cmdserialize PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(test_cmdserialize PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME cmdserialize_tests COMMAND test_cmdserialize)

# How CedarLogic tells a software OpenGL stand-in from a real graphics driver,
# which decides what it advises someone whose canvas will not draw. Free of
# wxWidgets, OpenGL, and Skia, so it just needs the one source file.
add_executable(test_rendererhealth
	"${CMAKE_CURRENT_SOURCE_DIR}/test_rendererhealth.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/render/RendererHealth.cpp"
)
target_include_directories(test_rendererhealth PRIVATE
	"${PROJECT_SOURCE_DIR}/include/gui"
)
target_link_libraries(test_rendererhealth PRIVATE doctest::doctest)

if(ENABLE_SANITIZERS AND NOT MSVC)
	target_compile_options(test_rendererhealth PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(test_rendererhealth PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME rendererhealth_tests COMMAND test_rendererhealth)

# Which GUI params a circuit file may carry: the rule that keeps a gate's
# drawing boxes (hit boxes, keypad cells) out of saved circuits, so the library
# can retune them without old files pinning the originals. Checked against the
# real shipped cl_gatedefs.xml. LibraryParse reaches for wxWidgets in one place,
# so stub/ shadows that header (and MainApp.h) and the test builds with no
# wxWidgets present.
add_executable(test_gatelibrary
	"${CMAKE_CURRENT_SOURCE_DIR}/test_gatelibrary.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/LibraryParse.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/GateLibrary.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/XMLParser.cpp"
)
# stub/ comes first so its MainApp.h wins over the real one.
target_include_directories(test_gatelibrary PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/stub"
	"${PROJECT_SOURCE_DIR}/include/gui"
	"${PROJECT_SOURCE_DIR}/logic/include"
)
target_link_libraries(test_gatelibrary PRIVATE doctest::doctest)
target_compile_definitions(test_gatelibrary PRIVATE
	"GATEDEFS_PATH=\"${PROJECT_SOURCE_DIR}/res/cl_gatedefs.xml\"")

if(ENABLE_SANITIZERS AND NOT MSVC)
	target_compile_options(test_gatelibrary PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(test_gatelibrary PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME gatelibrary_tests COMMAND test_gatelibrary)

# Segment-map tidying (issue #110). wireSegment only borrows GLfloat/GLPoint2f
# from the GL wrapper, so the same gl_shim.h the collision test uses is enough.
add_executable(test_segmentmap
	"${CMAKE_CURRENT_SOURCE_DIR}/test_segmentmap.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/SegmentMap.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/wireSegment.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/klsCollisionChecker.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gl_defs.cpp"
)
target_include_directories(test_segmentmap PRIVATE
	"${PROJECT_SOURCE_DIR}/include/gui"
	"${PROJECT_SOURCE_DIR}/logic/include"
)
target_link_libraries(test_segmentmap PRIVATE doctest::doctest)
target_compile_options(test_segmentmap PRIVATE
	"$<$<CXX_COMPILER_ID:MSVC>:/FI${_shim}>"
	"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:SHELL:-include ${_shim}>"
)

if(ENABLE_SANITIZERS AND NOT MSVC)
	target_compile_options(test_segmentmap PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(test_segmentmap PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME segmentmap_tests COMMAND test_segmentmap)

# Crash-report issue text. Pure string work: no wxWidgets, no platform headers.
add_executable(test_crashissue
	"${CMAKE_CURRENT_SOURCE_DIR}/test_crashissue.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/CrashIssue.cpp"
)
target_include_directories(test_crashissue PRIVATE
	"${PROJECT_SOURCE_DIR}/include/gui"
)
target_link_libraries(test_crashissue PRIVATE doctest::doctest)

if(ENABLE_SANITIZERS AND NOT MSVC)
	target_compile_options(test_crashissue PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(test_crashissue PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME crashissue_tests COMMAND test_crashissue)
