# .deb and .rpm packages, built with `cpack -G "DEB;RPM"`. Included before
# include(CPack), which reads these.

set(CPACK_PACKAGE_CONTACT "Kieran Klukas <me@dunkirk.sh>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Digital logic simulator for education")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/taciturnaxolotl/CedarLogic")

# Neither format accepts the "-" in a prerelease like 3.5.0-rc1: dpkg reads it
# as a Debian revision, which sorts the prerelease after the final release, and
# rpmbuild rejects it outright. "~" sorts before the release in both.
if(NOT CPACK_PACKAGE_VERSION)
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
endif()
string(REPLACE "-" "~" _cl_pkg_version "${CPACK_PACKAGE_VERSION}")
# Both also want a leading digit, which a dry-run release ("dev") lacks.
if(NOT _cl_pkg_version MATCHES "^[0-9]")
    set(_cl_pkg_version "0~${_cl_pkg_version}")
endif()

set(CPACK_DEBIAN_PACKAGE_NAME "cedarlogic")
set(CPACK_DEBIAN_PACKAGE_VERSION "${_cl_pkg_version}")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "electronics")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_RPM_PACKAGE_NAME "cedarlogic")
set(CPACK_RPM_PACKAGE_VERSION "${_cl_pkg_version}")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
# Directories the distro already owns. Claiming them too makes rpm refuse the
# install as a conflict with the filesystem package.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications
    /usr/share/icons
    /usr/share/icons/hicolor
    /usr/share/icons/hicolor/64x64
    /usr/share/icons/hicolor/64x64/apps
    /usr/share/mime
    /usr/share/mime/packages)
# The debug info is already split out at link time and goes to Sentry, so there
# is nothing for rpmbuild's debuginfo machinery to do, and the /usr/lib/.build-id
# links it adds would collide with any other package built the same way.
set(CPACK_RPM_SPEC_MORE_DEFINE "%define _build_id_links none")
