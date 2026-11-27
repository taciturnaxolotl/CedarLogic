# Undo the install rules sentry-native's static build fires into our prefix:
# crashpad's libraries and its cmake package directory. See the call site in the
# top-level CMakeLists for why this is a separate directory.
#
# CedarLogic installs nothing to lib/ on any platform, so everything there came
# from the dependency. That is the whole rule -- naming the individual libraries
# only works until the next version adds one.
#
# Paths go through DESTDIR: a staged install (a distro build, or CPack's .deb
# and .rpm) has a prefix of /usr, and without it this would sweep the build
# machine's own /usr/lib.
install(CODE [[
    set(_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
    file(REMOVE_RECURSE "${_root}/lib")
    # crashpad puts its handler in bin/ too. On Linux that is where ours goes,
    # so only the copies nothing looks at are removed.
    if(NOT CMAKE_INSTALL_PREFIX STREQUAL "")
        file(GLOB _ours "${_root}/bin/CedarLogic*")
        if(NOT _ours)
            file(REMOVE_RECURSE "${_root}/bin")
        endif()
    endif()
]])
