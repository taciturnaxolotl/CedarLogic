cmake_minimum_required(VERSION 3.11)

project(Logic VERSION 0.0.0)

# specify the C++ standard BEFORE creating targets
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

configure_file(Logic_version.hpp.in Logic_version.hpp)

file(GLOB SRC_FILES
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/include/*.h"
)

add_library(Logic STATIC "${SRC_FILES}")

# Ensure C++11 is used for this target
target_compile_features(Logic PUBLIC cxx_std_11)

# To build this library you must include it's headers
include_directories(
	"${PROJECT_SOURCE_DIR}/include/"
)

# Make it so anyone using this library (and CMake) knows to include the library's headers.
target_include_directories(Logic PUBLIC "${PROJECT_SOURCE_DIR}/include")

# Bring in the Catch2 framework
Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)

FetchContent_MakeAvailable(Catch2)

add_library(XMLParser STATIC "../src/gui/XMLParser.cpp")
target_include_directories(XMLParser PUBLIC "../include/gui/")
target_compile_options(XMLParser PUBLIC -Wall -pedantic)

# Add a executable to run the tests
add_executable(test_logic tests/test.cpp)

# Add the libraries the test executable will need to run
target_link_libraries(test_logic PRIVATE Catch2::Catch2WithMain Logic XMLParser)
