cmake_minimum_required(VERSION 3.16)

# Unit test for the orthogonal-polyline -> segment-tree translator (phase 3.2c).
# The translator is pure geometry -- no wxWidgets, OpenGL, or libavoid -- so this
# builds and runs on a bare CI runner with just the header.

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
	FetchContent_MakeAvailable(doctest)
endif()

add_executable(test_polyline
	"${CMAKE_CURRENT_SOURCE_DIR}/test_polyline.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/avoid/PolylineToSegments.cpp"
)
target_include_directories(test_polyline PRIVATE
	"${PROJECT_SOURCE_DIR}/include/gui"
)
target_link_libraries(test_polyline PRIVATE doctest::doctest)

if(ENABLE_SANITIZERS AND NOT MSVC)
	target_compile_options(test_polyline PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(test_polyline PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME polyline_tests COMMAND test_polyline)
