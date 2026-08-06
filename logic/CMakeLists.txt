cmake_minimum_required(VERSION 3.16)

project(Logic VERSION 0.0.0)

# specify the C++ standard BEFORE creating targets
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

configure_file(Logic_version.hpp.in Logic_version.hpp)

# CONFIGURE_DEPENDS re-globs on rebuild so newly added sources are picked up.
file(GLOB SRC_FILES CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/include/*.h"
)

add_library(Logic STATIC "${SRC_FILES}")

# Ensure C++11 is used, and make consumers include this library's headers.
target_compile_features(Logic PUBLIC cxx_std_11)
target_include_directories(Logic PUBLIC "${PROJECT_SOURCE_DIR}/include")

# ---------------------------------------------------------------------------
# Tests (doctest). Guarded so app/release builds don't pay for them.
# ---------------------------------------------------------------------------
option(BUILD_TESTING "Build the test suite" ON)
option(ENABLE_SANITIZERS "Build tests with ASan/UBSan (GCC/Clang only)" OFF)
option(ENABLE_COVERAGE "Build tests with gcov coverage instrumentation (GCC/Clang only)" OFF)

if(ENABLE_SANITIZERS AND NOT MSVC)
	target_compile_options(Logic PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(Logic PUBLIC -fsanitize=address,undefined)
endif()

if(ENABLE_COVERAGE AND NOT MSVC)
	target_compile_options(Logic PRIVATE --coverage -O0 -g)
	target_link_options(Logic PUBLIC --coverage)
endif()

if(BUILD_TESTING)
	include(FetchContent)

	# doctest: fast-compiling, header-only test framework. Reuse the target if a
	# parent project (the top-level CedarLogic build) already fetched it.
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

	# XMLParser is compiled here (from the GUI tree) purely so it can be unit tested.
	add_library(XMLParser STATIC "../src/gui/XMLParser.cpp")
	target_include_directories(XMLParser PUBLIC "../include/gui/")
	if(NOT MSVC)
		target_compile_options(XMLParser PRIVATE -Wall -pedantic)
	endif()

	add_executable(test_logic tests/test.cpp tests/test_circuit.cpp)
	target_link_libraries(test_logic PRIVATE doctest::doctest Logic XMLParser)

	# Inject the fixture path so the XML reading test is independent of cwd.
	target_compile_definitions(test_logic PRIVATE
		"TESTXML_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/testxml.txt\"")

	if(ENABLE_SANITIZERS AND NOT MSVC)
		target_compile_options(XMLParser PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
		target_compile_options(test_logic PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
		target_link_options(test_logic PRIVATE -fsanitize=address,undefined)
	endif()

	if(ENABLE_COVERAGE AND NOT MSVC)
		target_compile_options(XMLParser PRIVATE --coverage -O0 -g)
		target_compile_options(test_logic PRIVATE --coverage -O0 -g)
		target_link_options(test_logic PRIVATE --coverage)
	endif()

	enable_testing()
	add_test(NAME logic_tests COMMAND test_logic)
endif()
