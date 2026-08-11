# Unit tests for the wire router (Workstream H). The router is engine-free, so it
# compiles standalone into a tiny doctest exe with no app/wx dependencies.
if(BUILD_TESTING)
	include(FetchContent)
	if(NOT TARGET doctest::doctest)
		FetchContent_Declare(
			doctest
			GIT_REPOSITORY https://github.com/doctest/doctest.git
			GIT_TAG        v2.4.11
			GIT_SHALLOW    1
		)
		# doctest 2.4.x pins an ancient cmake_minimum_required that CMake >= 4.0
		# refuses; opt back in until doctest ships a newer minimum.
		set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
		FetchContent_MakeAvailable(doctest)
	endif()

	add_executable(test_route
		test_route.cpp
		"${PROJECT_SOURCE_DIR}/src/gui/route/TrunkRouter.cpp")
	target_include_directories(test_route PRIVATE "${PROJECT_SOURCE_DIR}/include/gui")
	target_link_libraries(test_route PRIVATE doctest::doctest)

	enable_testing()
	add_test(NAME route_tests COMMAND test_route)
endif()
