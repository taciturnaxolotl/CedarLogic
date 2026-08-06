cmake_minimum_required(VERSION 3.16)

project(Simulator VERSION 0.0.0)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ---------------------------------------------------------------------------
# NOTE: circuit.cpp / network.hpp currently `#include "entities.hpp"`, a header
# that does not exist anywhere in the tree (it was never committed -- see the
# "tests are broken, else good work" commit). Until that header is restored the
# circuit/network layer cannot compile, which is why nothing has ever built the
# simulator. The test build below is therefore scoped to the parts that do
# compile and that the tests actually exercise: the logic primitives, gates,
# and junctions. Add circuit.cpp/event.cpp/network back here once entities.hpp
# is restored.
# ---------------------------------------------------------------------------
set(SIMULATOR_TESTABLE_SOURCES
	"${PROJECT_SOURCE_DIR}/logic.cpp"
	"${PROJECT_SOURCE_DIR}/gate.cpp"
	"${PROJECT_SOURCE_DIR}/junction.cpp"
)

option(BUILD_TESTING "Build the test suite" ON)
option(ENABLE_SANITIZERS "Build tests with ASan/UBSan (GCC/Clang only)" OFF)

if(BUILD_TESTING)
	include(FetchContent)

	# Reuse doctest if the top-level build already fetched it.
	if(NOT TARGET doctest::doctest)
		FetchContent_Declare(
			doctest
			GIT_REPOSITORY https://github.com/doctest/doctest.git
			GIT_TAG        v2.4.11
			GIT_SHALLOW    1
		)
		# doctest 2.4.x declares an ancient cmake_minimum_required that CMake >= 4.0
		# refuses; opt back in until doctest ships a release with a newer minimum.
		set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
		FetchContent_MakeAvailable(doctest)
	endif()

	add_executable(test_Simulator
		tests/test_logic.cpp
		tests/test_gate.cpp
		tests/test_junction.cpp
		${SIMULATOR_TESTABLE_SOURCES}
	)
	target_include_directories(test_Simulator PRIVATE "${PROJECT_SOURCE_DIR}/")
	target_link_libraries(test_Simulator PRIVATE doctest::doctest)

	if(ENABLE_SANITIZERS AND NOT MSVC)
		target_compile_options(test_Simulator PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
		target_link_options(test_Simulator PRIVATE -fsanitize=address,undefined)
	endif()

	enable_testing()
	add_test(NAME simulator_tests COMMAND test_Simulator)
endif()
