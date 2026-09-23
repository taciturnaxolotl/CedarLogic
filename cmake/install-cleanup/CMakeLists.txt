# Undo the install rules sentry-native's static build fires into our prefix:
# crashpad's libraries and its cmake package directory. See the call site in the
# top-level CMakeLists for why this is a separate directory.
#
# CedarLogic installs nothing to lib/ on any platform, so everything there came
# from the dependency. That is the whole rule -- naming the individual libraries
# only works until the next version adds one.
install(CODE [[
    file(REMOVE_RECURSE "${CMAKE_INSTALL_PREFIX}/lib")
    # crashpad puts its handler in bin/ too. On Linux that is where ours goes,
    # so only the copies nothing looks at are removed.
    if(NOT CMAKE_INSTALL_PREFIX STREQUAL "")
        file(GLOB _ours "${CMAKE_INSTALL_PREFIX}/bin/CedarLogic*")
        if(NOT _ours)
            file(REMOVE_RECURSE "${CMAKE_INSTALL_PREFIX}/bin")
        endif()
    endif()
]])
