# Note: Below 3.11 breaks the FetchContent capability used to pull in Catch2,
# the unit testing framework.
cmake_minimum_required(VERSION 3.11)

# Name project, and set version. If updating version, change it here, and then re-generate your Visual
# Studio solution.
project(CedarLogic VERSION 2.3.9)

# macOS: Enable Objective-C++ for Sparkle updater (.mm files)
if(APPLE)
    enable_language(OBJCXX)
endif()

# macOS: Set deployment target (arm64 only for now)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(SPARKLE_ED_PUBLIC_KEY "" CACHE STRING "Sparkle EdDSA public key for auto-updates")
endif()

# This configure file is where we store version info that's auto-populated by cmake configure.
# This file is not considered part of the source code and it's stored in the build directory
# It is made visible as an external header when we add CMAKE_CURRENT_BINARY_DIR (build) in 
# include_directories. This may seem odd, but it's how they do it in the official cmake tutorial.
configure_file(CedarLogic.h.in CedarLogic.h)

# CMake modules, that is, files specifying functions we can use in CMake
# each is a .cmake file in the CMAKE_MODULE_PATH and we include them below
# so we can use their functions later on.
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/module")
include(ResourceTools)
include(VsSourceGroups)
include(GNUInstallDirs)

##
# Connect external libraries
##

# Use static runtime libs.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Linking to the system version of wxWidgets is usually preferred on linux, and
# it makes the build much faster.
if (CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(USE_SYSWX_DEF TRUE)
else()
    set(USE_SYSWX_DEF FALSE)
endif()
set(USE_SYSTEM_WXWIDGETS "${USE_SYSWX_DEF}" CACHE BOOL
    "Use the system version of wxWidgets or build it from soruce")

if (USE_SYSTEM_WXWIDGETS)
    find_package(wxWidgets 3.2.0 COMPONENTS html aui adv gl core base REQUIRED)
    include("${wxWidgets_USE_FILE}")
else()
    include(FetchContent)

    FetchContent_Declare(
       wxWidgets_fetch
       GIT_REPOSITORY https://github.com/wxWidgets/wxWidgets.git
       GIT_TAG v3.2.9
       GIT_SHALLOW 1
    )

    set(wxBUILD_INSTALL OFF)
    set(wxBUILD_SHARED OFF)
    set(wxUSE_GLCANVAS ON)
    set(wxBUILD_USE_STATIC_RUNTIME ON)
    set(wxUSE_OLE ON)  # Required to use wxUSE_DRAG_AND_DROP

    # Disabled because they break the windows build
    set(wxUSE_SOCKETS OFF)
    set(wxUSE_PROTOCOL OFF)
    set(wxUSE_URL OFF)
    set(wxUSE_FS_INET OFF)
    set(wxUSE_PROTOCOL_FTP OFF)
    set(wxUSE_PROTOCOL_HTTP OFF)

    FetchContent_MakeAvailable(wxWidgets_fetch)
    set(wxWidgets_LIBRARIES wx::html wx::aui wx::adv wx::gl wx::core wx::base)
endif()

find_package(OpenGL REQUIRED)

# macOS: Download and setup Sparkle framework for auto-updates
if(APPLE)
    include(FetchContent)
    set(SPARKLE_VERSION "2.6.4")
    FetchContent_Declare(
        Sparkle
        URL "https://github.com/sparkle-project/Sparkle/releases/download/${SPARKLE_VERSION}/Sparkle-${SPARKLE_VERSION}.tar.xz"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Sparkle)
    set(SPARKLE_FRAMEWORK "${sparkle_SOURCE_DIR}/Sparkle.framework")
endif()

# Windows: Download and setup WinSparkle for auto-updates
if(WIN32)
    include(FetchContent)
    set(WINSPARKLE_VERSION "0.8.1")
    FetchContent_Declare(
        WinSparkle
        URL "https://github.com/vslavik/winsparkle/releases/download/v${WINSPARKLE_VERSION}/WinSparkle-${WINSPARKLE_VERSION}.zip"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(WinSparkle)
    set(WINSPARKLE_DIR "${winsparkle_SOURCE_DIR}")
endif()

###
### Build Config.
###

# Glob all the source files to be added when CedarLogic executable is added later.
file(GLOB SRC_FILES
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/logic/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/command/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/GLFont/*.cpp"
	"${PROJECT_SOURCE_DIR}/include/*.h"
	"${PROJECT_SOURCE_DIR}/include/gui/*.h"
	"${PROJECT_SOURCE_DIR}/include/logic/*.h"
	"${PROJECT_SOURCE_DIR}/include/gui/command/*.h"
	"${PROJECT_SOURCE_DIR}/include/gui/GLFont/*.h"
)

# macOS: Add Objective-C++ sources
if(APPLE)
    list(APPEND SRC_FILES
        "${PROJECT_SOURCE_DIR}/src/gui/SparkleUpdater.mm"
        "${PROJECT_SOURCE_DIR}/src/gui/NativeIcons.mm"
    )
endif()

if(APPLE)
    add_executable(CedarLogic MACOSX_BUNDLE "${SRC_FILES}")
else()
    add_executable(CedarLogic WIN32 "${SRC_FILES}")
endif()

# Add the external libraries for CedarLogic
target_link_libraries(CedarLogic PRIVATE
    Logic
    ${OPENGL_opengl_LIBRARY}
    ${OPENGL_glu_LIBRARY}
    ${wxWidgets_LIBRARIES}
)

# macOS: Link Sparkle framework
if(APPLE)
    target_link_libraries(CedarLogic PRIVATE "${SPARKLE_FRAMEWORK}")
    target_include_directories(CedarLogic PRIVATE "${sparkle_SOURCE_DIR}")
    set_target_properties(CedarLogic PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks"
        BUILD_RPATH "@executable_path/../Frameworks"
    )
endif()

# Windows: Link WinSparkle
if(WIN32)
    target_include_directories(CedarLogic PRIVATE "${WINSPARKLE_DIR}/include")
    target_link_libraries(CedarLogic PRIVATE "${WINSPARKLE_DIR}/Release/WinSparkle.lib")
endif()

# This is for internal headers
include_directories(
	"${PROJECT_SOURCE_DIR}/include/"
	"${PROJECT_SOURCE_DIR}/include/gui/"
	"${PROJECT_SOURCE_DIR}/include/logic/"
	"${PROJECT_SOURCE_DIR}/include/gui/command/"
	"${PROJECT_SOURCE_DIR}/include/gui/GLFont/"
	"${CMAKE_CURRENT_BINARY_DIR}"
)

add_subdirectory(
    "${PROJECT_SOURCE_DIR}/logic/"
)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

target_compile_definitions(CedarLogic PRIVATE "_PRODUCTION_")

if (WIN32)
    target_compile_options(CedarLogic PRIVATE /MP)
    target_compile_definitions(CedarLogic PRIVATE
	"WIN32"
	"STRICT"
	"__WXMSW__"
	"__WX__"
	"__WXDEBUG__"
	"_WINDOWS"
	"_CRT_SECURE_NO_DEPRECATE"
    )
endif()

if(APPLE)
    # Set macOS bundle properties
    set_target_properties(CedarLogic PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "CedarLogic"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "edu.cedarville.cedarlogic"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/res/macos/Info.plist.in"
    )

    # Copy icon into Resources (not in res/ subdirectory)
    set(MACOS_ICON "${CMAKE_SOURCE_DIR}/res/macos/CedarLogic.icns")
    set_source_files_properties(${MACOS_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(CedarLogic PRIVATE ${MACOS_ICON})

    # Copy res folder contents into Resources/res (app expects res/ subdirectory)
    add_custom_command(TARGET CedarLogic POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:CedarLogic>/Contents/Resources/res"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/res/arial.glf"
            "${CMAKE_SOURCE_DIR}/res/cl_gatedefs.xml"
            "${CMAKE_SOURCE_DIR}/res/LICENSE.txt"
            "$<TARGET_BUNDLE_DIR:CedarLogic>/Contents/Resources/res/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/res/bitmaps"
            "$<TARGET_BUNDLE_DIR:CedarLogic>/Contents/Resources/res/bitmaps"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/res/help"
            "$<TARGET_BUNDLE_DIR:CedarLogic>/Contents/Resources/res/help"
    )

    # Copy Sparkle.framework into the app bundle (cp -a preserves symlinks)
    add_custom_command(TARGET CedarLogic POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:CedarLogic>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_BUNDLE_DIR:CedarLogic>/Contents/Frameworks/Sparkle.framework"
        COMMAND cp -a
            "${SPARKLE_FRAMEWORK}"
            "$<TARGET_BUNDLE_DIR:CedarLogic>/Contents/Frameworks/Sparkle.framework"
    )
endif()


###
### Dev Setup.
###

copy_resources(CedarLogic res)
vs_source_groups(src ${Sources})


###
### Installation.
###

if (WIN32)
    install(TARGETS CedarLogic RUNTIME DESTINATION ".")
    install(FILES "${WINSPARKLE_DIR}/Release/WinSparkle.dll" DESTINATION ".")
    install_resources(res)
elseif(APPLE)
    install(TARGETS CedarLogic BUNDLE DESTINATION ".")
else()
    install(TARGETS CedarLogic RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES res/CedarLogic.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install(FILES res/mime.xml DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/mime/packages RENAME CedarLogic.xml)
    install(FILES res/icon.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/64x64/apps RENAME CedarLogic.png)
    install_resources(res)
endif()

set(CPACK_PACKAGE_NAME "CedarLogic")
# This exceptionally weird line is hard to understand even with the documentation.
# CMake thinks my executable is in the 'bin' subdirectory of the installation folder.
# I don't like to put it there, so I need to manually tell it my programs are up one level.
# Also, the combination of CMAKE and NSIS requires quadruple backslashes sometimes :(
# UPDATE: You can change 'bin' to '.' by setting  CPACK_NSIS_EXECUTABLES_DIRECTORY.
set(CPACK_PACKAGE_EXECUTABLES ..\\\\CedarLogic "CedarLogic" ..\\\\Uninstall "Uninstall CedarLogic")
set(CPACK_PACKAGE_VENDOR "Cedarville University")
# set(CPACK_PACKAGE_VERSION_MAJOR "${CEDARLOGIC_MAJOR}")
# set(CPACK_PACKAGE_VERSION_MINOR "${CEDARLOGIC_MINOR}")
# set(CPACK_PACKAGE_VERSION_PATCH "${CEDARLOGIC_PATCH}")
if(APPLE)
    set(CPACK_PACKAGE_ICON ${CMAKE_SOURCE_DIR}/res/macos/CedarLogic.icns)
else()
    set(CPACK_PACKAGE_ICON ${CMAKE_SOURCE_DIR}/cmake\\\\InstallSmall.bmp)
endif()
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/res/LICENSE.txt)
set(CPACK_NSIS_MUI_ICON ${CMAKE_SOURCE_DIR}/cmake\\\\icon.ico)
set(CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP ${CMAKE_SOURCE_DIR}/cmake\\\\InstallLarge.bmp)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
    "!include \\\"${CMAKE_SOURCE_DIR}/cmake/cpack\\\\FileAssociation.nsh\\\"
    \\\${registerExtension} \\\"$INSTDIR\\\\CedarLogic.exe\\\" \\\".cdl\\\" \\\"CedarLogic Project\\\""
)
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
    "\\\${unregisterExtension} \\\".cdl\\\" \\\"CedarLogic Project\\\""
)

# macOS DMG configuration
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "CedarLogic")
    set(CPACK_DMG_FORMAT "UDZO")
    set(CPACK_BUNDLE_NAME "CedarLogic")
    set(CPACK_BUNDLE_PLIST "${CMAKE_SOURCE_DIR}/res/macos/Info.plist.in")
    set(CPACK_BUNDLE_ICON "${CMAKE_SOURCE_DIR}/res/macos/CedarLogic.icns")
endif()

include(CPack)
