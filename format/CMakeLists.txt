cmake_minimum_required(VERSION 3.16)

project(CircuitFile VERSION 0.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# The v3 .cdl format: a GUI-independent circuit data model plus an S-expression
# reader/writer. Deliberately free of wxWidgets and the logic core so it can be
# loaded, validated, and tested headless.
add_library(CircuitFile STATIC
	"${PROJECT_SOURCE_DIR}/sexpr.cpp"
	"${PROJECT_SOURCE_DIR}/sexpr.hpp"
	"${PROJECT_SOURCE_DIR}/circuit_file.hpp"
	"${PROJECT_SOURCE_DIR}/circuit_file_io.cpp"
	"${PROJECT_SOURCE_DIR}/circuit_file_io.hpp"
	"${PROJECT_SOURCE_DIR}/legacy_cdl.cpp"
	"${PROJECT_SOURCE_DIR}/legacy_cdl.hpp"
	"${PROJECT_SOURCE_DIR}/numeric.cpp"
	"${PROJECT_SOURCE_DIR}/numeric.hpp"
	"${PROJECT_SOURCE_DIR}/migrate.cpp"
	"${PROJECT_SOURCE_DIR}/migrate.hpp"
)
target_include_directories(CircuitFile PUBLIC "${PROJECT_SOURCE_DIR}")

option(BUILD_TESTING "Build the test suite" ON)
option(ENABLE_SANITIZERS "Build tests with ASan/UBSan (GCC/Clang only)" OFF)
option(ENABLE_COVERAGE "Build tests with gcov coverage instrumentation (GCC/Clang only)" OFF)

if(ENABLE_SANITIZERS AND NOT MSVC)
	target_compile_options(CircuitFile PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(CircuitFile PUBLIC -fsanitize=address,undefined)
endif()

if(ENABLE_COVERAGE AND NOT MSVC)
	target_compile_options(CircuitFile PRIVATE --coverage -O0 -g)
	target_link_options(CircuitFile PUBLIC --coverage)
endif()

if(BUILD_TESTING)
	include(FetchContent)
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

	add_executable(test_format tests/test_format.cpp tests/test_legacy.cpp
		tests/test_migrate.cpp tests/test_fixtures.cpp)
	target_link_libraries(test_format PRIVATE doctest::doctest CircuitFile)
	# Let the fixture test find the committed real-world .cdl files by absolute path.
	target_compile_definitions(test_format PRIVATE
		"FIXTURES_DIR=\"${PROJECT_SOURCE_DIR}/tests/fixtures\"")

	if(ENABLE_SANITIZERS AND NOT MSVC)
		target_compile_options(test_format PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
		target_link_options(test_format PRIVATE -fsanitize=address,undefined)
	endif()
	if(ENABLE_COVERAGE AND NOT MSVC)
		target_compile_options(test_format PRIVATE --coverage -O0 -g)
		target_link_options(test_format PRIVATE --coverage)
	endif()

	enable_testing()
	add_test(NAME format_tests COMMAND test_format)
endif()
