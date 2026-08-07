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
