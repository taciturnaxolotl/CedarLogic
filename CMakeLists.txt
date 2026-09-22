# Note: Below 3.11 breaks the FetchContent capability used to pull in Catch2,
# the unit testing framework.
cmake_minimum_required(VERSION 3.11)

# Name project, and set version. If updating version, change it here, and then re-generate your Visual
# Studio solution.
project(CedarLogic VERSION 3.2.2)

# The Windows build ships 32-bit and the Skia dist is built target_cpu="x86". A
# 64-bit configure succeeds and only fails at link, with a wall of unresolved
# Skia symbols, so reject it here instead.
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "CedarLogic builds 32-bit on Windows. Configure with -A Win32 on Visual "
        "Studio generators, or point CMake at a 32-bit toolchain.")
endif()

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
include(EmbedResources)
include(VsSourceGroups)
include(GNUInstallDirs)

# Skia rendering engine (Workstream G). Required: it is the renderer.
include(SetupSkia)

##
# Testing options. The logic/ and format/ subdirectories build their own
# doctest suites; enabling BUILD_TESTING here lets `ctest` aggregate them.
##
option(BUILD_TESTING "Build the logic/format test suites" ON)
option(ENABLE_SANITIZERS "Instrument tests with ASan/UBSan (GCC/Clang only)" OFF)
if(BUILD_TESTING)
    enable_testing()
endif()

##
# Connect external libraries
##

# Use static runtime libs.
# The Skia lib is built /MT, so the whole app must link /MT too or the link
# fails (LNK2038/LNK2005). CMAKE_MSVC_RUNTIME_LIBRARY only works under policy
# CMP0091, which cmake_minimum_required(3.11) leaves OLD and which did not take
# effect reliably here -- so rewrite /MD -> /MT directly in the flag variables
# (the pre-CMP0091 idiom). This reaches the app and every subdirectory.
if(MSVC)
    foreach(flag_var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
        if(${flag_var} MATCHES "/MD")
            string(REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
        endif()
    endforeach()
endif()
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
    if(WIN32)
        # A prebuilt system wxWidgets links its own libjpeg, which collides
        # (LNK2005 on _jpeg_*) with the libjpeg-turbo Skia's PDF backend needs.
        # The only fix is to drop wx's JPEG (wxUSE_LIBJPEG=OFF), which requires a
        # from-source wx -- impossible with a prebuilt system one. Fail clearly
        # rather than at link time.
        message(FATAL_ERROR
            "Windows requires the bundled wxWidgets "
            "(-DUSE_SYSTEM_WXWIDGETS=OFF): a prebuilt system wxWidgets links a "
            "libjpeg that collides with Skia's (LNK2005).")
    endif()
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

    # Skia's PDF backend needs libjpeg-turbo, whose symbols duplicate wxWidgets'
    # bundled libjpeg on Windows (LNK2005 on _jpeg_*). Drop wx's copy so Skia's
    # is the only libjpeg; the app exposes PNG/SVG/PDF export instead of JPEG (a
    # poor format for line-art schematics anyway). wxTIFF pulls jpeglib.h for
    # JPEG-in-TIFF, so it must go too -- the app never reads/writes TIFF.
    #
    # Off on every platform, not just Windows. On Linux wx links the system
    # libjpeg/libtiff, which put libjpeg.so.8 and libtiff.so.6 in the shipped
    # binary's DT_NEEDED for image formats nothing in the app ever touches.
    # Distros disagree about that soname (Debian ships libjpeg.so.62), so the
    # release archive refused to start on Debian until the user hand-installed
    # Ubuntu's libjpeg8.
    #
    # macOS is the reason this must stay off rather than merely being tidy.
    # There the two libjpegs link without complaint and the linker takes half of
    # each: 3.1.2 shipped jpeg_CreateDecompress and jpeg_read_header from wx's
    # libjpeg 9d and jpeg_start_decompress from Skia's libjpeg-turbo 2.1.4. The
    # two disagree on the layout of jpeg_decompress_struct (v9 added
    # color_transform and split min_DCT_scaled_size in two), so a struct built by
    # one half was read by the other and every JPEG decode segfaulted -- which
    # took out the help viewer, the only thing in the app that reads JPEG.
    # Turning JPEG back on for one platform re-arms that.
    set(wxUSE_LIBJPEG OFF)
    set(wxUSE_LIBTIFF OFF)

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
    # 0.9.0 is the first release that can verify EdDSA signatures. On 0.8.1 the
    # downloader had no EdDSA support at all and, with no DSA key configured
    # either, logged "Using unsigned updates!" and ran the payload anyway -- so
    # every Windows update was installed unauthenticated.
    set(WINSPARKLE_VERSION "0.9.4")
    FetchContent_Declare(
        WinSparkle
        URL "https://github.com/vslavik/winsparkle/releases/download/v${WINSPARKLE_VERSION}/WinSparkle-${WINSPARKLE_VERSION}.zip"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(WinSparkle)

    # Find the real dist root rather than assuming it is the extract directory.
    # CMake only strips an archive's leading directory when the archive holds
    # exactly one top-level entry. The 0.9.x zips were packaged on a Mac and
    # carry a stray __MACOSX/ alongside WinSparkle-<version>/, which makes two,
    # so the contents land one level deeper than they did for 0.8.1. Locating
    # the header keeps this working either way, and if upstream cleans up its
    # packaging nothing here has to change.
    file(GLOB_RECURSE WINSPARKLE_HEADER
         "${winsparkle_SOURCE_DIR}/*/include/winsparkle.h"
         "${winsparkle_SOURCE_DIR}/include/winsparkle.h")
    if(NOT WINSPARKLE_HEADER)
        message(FATAL_ERROR
            "WinSparkle ${WINSPARKLE_VERSION}: no include/winsparkle.h under "
            "'${winsparkle_SOURCE_DIR}'. The dist layout must have changed.")
    endif()
    list(GET WINSPARKLE_HEADER 0 WINSPARKLE_HEADER)
    # <root>/include/winsparkle.h -> <root>
    get_filename_component(WINSPARKLE_DIR "${WINSPARKLE_HEADER}" DIRECTORY)
    get_filename_component(WINSPARKLE_DIR "${WINSPARKLE_DIR}" DIRECTORY)
    message(STATUS "WinSparkle ${WINSPARKLE_VERSION} dist: ${WINSPARKLE_DIR}")

    # The WinSparkle zip ships a separate import lib + DLL per architecture.
    # Pick the one matching the target, or the link fails with LNK4272
    # (machine-type conflict) on any non-x86 build. Note the x86 directory was
    # renamed from Release/ to Win32/Release/ in 0.9.0.
    if(CMAKE_GENERATOR_PLATFORM MATCHES "[Aa][Rr][Mm]64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]64")
        set(WINSPARKLE_ARCH_DIR "ARM64/Release")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(WINSPARKLE_ARCH_DIR "x64/Release")
    else()
        set(WINSPARKLE_ARCH_DIR "Win32/Release")
    endif()

    # Fail here, with the path, rather than at link time with LNK1104.
    if(NOT EXISTS "${WINSPARKLE_DIR}/${WINSPARKLE_ARCH_DIR}/WinSparkle.lib")
        message(FATAL_ERROR
            "WinSparkle ${WINSPARKLE_VERSION}: no WinSparkle.lib at "
            "'${WINSPARKLE_DIR}/${WINSPARKLE_ARCH_DIR}'.")
    endif()
endif()

###
### Build Config.
###

# Our sources are UTF-8. Without this MSVC reads them in whatever ANSI code page
# the build machine happens to use, which turns non-ASCII characters in string
# literals into mojibake on screen and non-ASCII in comments into warnings. It
# sits below the wxWidgets fetch on purpose so it only reaches our own targets.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Glob all the source files to be added when CedarLogic executable is added later.
file(GLOB SRC_FILES CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/logic/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/command/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/route/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/wire/*.cpp"
	"${PROJECT_SOURCE_DIR}/include/*.h"
	"${PROJECT_SOURCE_DIR}/include/gui/*.h"
	"${PROJECT_SOURCE_DIR}/include/gui/render/*.h"
	"${PROJECT_SOURCE_DIR}/include/gui/route/*.h"
	"${PROJECT_SOURCE_DIR}/include/gui/wire/*.h"
	"${PROJECT_SOURCE_DIR}/include/logic/*.h"
	"${PROJECT_SOURCE_DIR}/include/gui/command/*.h"
)

# Skia backend translation units (SkiaScene/SkiaBackend). Only these TUs include
# Skia and need C++17, so raise the standard per-source rather than on the whole
# target: the legacy app code keeps its historical standard, avoiding C++17
# clashes there (e.g. std::byte vs windows.h's 'byte' under `using namespace
# std`). They stay in the app target so their Skia symbols get linked.
file(GLOB SKIA_SRC_FILES "${PROJECT_SOURCE_DIR}/src/gui/render/*.cpp")
list(APPEND SRC_FILES ${SKIA_SRC_FILES})
set_source_files_properties(${SKIA_SRC_FILES} PROPERTIES
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/std:c++17,-std=c++17>")

# The gate library and toolbar icons are compiled into the executable (see
# include/gui/EmbeddedRes.h); everything else in res/ is still read from disk.
file(GLOB EMBED_ICONS RELATIVE "${PROJECT_SOURCE_DIR}/res" "${PROJECT_SOURCE_DIR}/res/icons/*.svg")
embed_resources(EMBEDDED_RES_CPP "${PROJECT_SOURCE_DIR}/res"
    cl_gatedefs.xml ${EMBED_ICONS})
list(APPEND SRC_FILES "${EMBEDDED_RES_CPP}")

# macOS: Add Objective-C++ sources
if(APPLE)
    list(APPEND SRC_FILES
        "${PROJECT_SOURCE_DIR}/src/gui/SparkleUpdater.mm"
        "${PROJECT_SOURCE_DIR}/src/gui/NativeIcons.mm"
        "${PROJECT_SOURCE_DIR}/src/gui/MacAppearance.mm"
    )
endif()

if(APPLE)
    add_executable(CedarLogic MACOSX_BUNDLE "${SRC_FILES}")
else()
    add_executable(CedarLogic WIN32 "${SRC_FILES}")
endif()

# Keep debug symbols on the app so a crash address can be turned back into a
# function and a line -- in-process on Windows, from an archived debug file
# everywhere else. App target only -- does not touch wxWidgets, so no dependency
# rebuild, and the code stays optimized; this only emits/links symbol info.
if(MSVC)
    # Windows: emit a .pdb in Release so writeCrashTrace resolves file:line.
    target_compile_options(CedarLogic PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(CedarLogic PRIVATE $<$<CONFIG:Release>:/DEBUG>)
    # Make the process DPI-aware by merging a windowsSettings fragment into the
    # linker's embedded application manifest (which already carries the wx
    # Common-Controls v6 dependency -- so we merge, not add a second manifest,
    # which would conflict). See res/dpiaware.manifest.
    target_link_options(CedarLogic PRIVATE
        /MANIFEST:EMBED
        "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/res/dpiaware.manifest")
else()
    # POSIX: -g emits DWARF for the app's own sources. Nothing walks the stack
    # in-process here -- the handler records the signal and lets the OS write
    # the report -- so this exists to symbolize that report afterwards, on a
    # machine that is not the one that crashed.
    target_compile_options(CedarLogic PRIVATE -g)
    if(NOT APPLE)
        # -rdynamic exports symbols to the dynamic table, so frames are still
        # named without the debug file. The build ID is what ties a stripped
        # binary back to the debug file split out of it below; ld normally
        # writes one already, but a crash report is a bad place to discover
        # that this toolchain did not.
        target_link_options(CedarLogic PRIVATE -rdynamic -Wl,--build-id=sha1)
    endif()
endif()

# Collect the debug info somewhere that outlives the build tree.
#
# Release only: a local debug build already has everything a debugger needs, and
# these steps only matter for a binary that gets handed to someone else.
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
    if(APPLE)
        # -g leaves the DWARF in the .o files and links only a debug map
        # pointing at them, so the symbols die with the build tree. In 3.2.1's
        # shipped .dmg those entries name /Users/runner/work/..., and atos
        # resolves guiWire::setSubState with no file and no line at all.
        # dsymutil gathers them into a bundle that can be archived and used
        # months later. It sits beside the .app and is deliberately not
        # installed, so it stays out of the .dmg.
        add_custom_command(TARGET CedarLogic POST_BUILD
            COMMAND dsymutil "$<TARGET_FILE:CedarLogic>"
                    -o "$<TARGET_BUNDLE_DIR:CedarLogic>.dSYM"
            COMMENT "Collecting DWARF into CedarLogic.app.dSYM"
            VERBATIM)
    else()
        # Linux links the DWARF into the executable instead, which works but
        # ships it to every user: 3.2.1's binary is 69.5MB, ~27MB of that debug
        # info nobody downloading it will ever read. Split it into a sidecar the
        # way Windows already does with its .pdb. Run from the output directory
        # so the debuglink records a bare filename rather than a build path.
        if(NOT CMAKE_OBJCOPY OR NOT CMAKE_STRIP)
            # Better to stop here than to ship a release whose only debug info
            # was silently dropped, which is how 3.0.2's crash reports became
            # permanently unreadable.
            message(FATAL_ERROR
                "objcopy and strip are required to split debug info for a "
                "Release build; without them a crash report cannot be "
                "symbolized after the fact.")
        endif()
        add_custom_command(TARGET CedarLogic POST_BUILD
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:CedarLogic>"
            COMMAND ${CMAKE_OBJCOPY} --only-keep-debug
                    "$<TARGET_FILE_NAME:CedarLogic>"
                    "$<TARGET_FILE_NAME:CedarLogic>.debug"
            COMMAND ${CMAKE_STRIP} --strip-debug "$<TARGET_FILE_NAME:CedarLogic>"
            COMMAND ${CMAKE_OBJCOPY}
                    "--add-gnu-debuglink=$<TARGET_FILE_NAME:CedarLogic>.debug"
                    "$<TARGET_FILE_NAME:CedarLogic>"
            COMMENT "Splitting debug info into CedarLogic.debug"
            VERBATIM)
    endif()
endif()

# Add the external libraries for CedarLogic
target_link_libraries(CedarLogic PRIVATE
    Logic
    CircuitFile
    ${OPENGL_opengl_LIBRARY}
    ${OPENGL_glu_LIBRARY}
    ${wxWidgets_LIBRARIES}
)

# Skia carries its own includes, static libs, system deps, and the WITH_SKIA/
# SK_GL defines.
target_link_libraries(CedarLogic PRIVATE cedar_skia)

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
    target_link_libraries(CedarLogic PRIVATE "${WINSPARKLE_DIR}/${WINSPARKLE_ARCH_DIR}/WinSparkle.lib")
    # UpdateInfo.cpp reads the appcast with WinINet for the crash dialog's update
    # offer. Linked explicitly rather than relying on wininet.h's self-link
    # pragma, so the Windows build cannot turn on a missing-library surprise.
    target_link_libraries(CedarLogic PRIVATE wininet)
    # Stage the DLL beside the exe. It was only handled by install(), so a plain
    # build produced a binary that could not start ("error while loading shared
    # libraries") until the DLL was copied by hand.
    add_custom_command(TARGET CedarLogic POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WINSPARKLE_DIR}/${WINSPARKLE_ARCH_DIR}/WinSparkle.dll"
            "$<TARGET_FILE_DIR:CedarLogic>/WinSparkle.dll"
        COMMENT "Staging WinSparkle.dll next to the executable...")
endif()

# This is for internal headers
include_directories(
	"${PROJECT_SOURCE_DIR}/include/"
	"${PROJECT_SOURCE_DIR}/include/gui/"
	"${PROJECT_SOURCE_DIR}/include/logic/"
	"${PROJECT_SOURCE_DIR}/include/gui/command/"
	"${PROJECT_SOURCE_DIR}/format/"
	"${CMAKE_CURRENT_BINARY_DIR}"
)

add_subdirectory(
    "${PROJECT_SOURCE_DIR}/logic/"
)

# format/ is the wx-free v3 .cdl model, legacy reader, and migration layer; the
# app links it to load circuits and surface migration notices. It also builds its
# own doctest suite under BUILD_TESTING.
add_subdirectory("${PROJECT_SOURCE_DIR}/format/")

if(BUILD_TESTING)
    # Headless differential test for the collision checker's spatial grid.
    add_subdirectory("${PROJECT_SOURCE_DIR}/src/gui/tests/")
    # Unit tests for the wire router seam (Workstream H).
    add_subdirectory("${PROJECT_SOURCE_DIR}/src/gui/route/tests/")
endif()


target_compile_definitions(CedarLogic PRIVATE "_PRODUCTION_")

# Linux: Define GL_GLEXT_PROTOTYPES for OpenGL extension functions
if(UNIX AND NOT APPLE)
    target_compile_definitions(CedarLogic PRIVATE "GL_GLEXT_PROTOTYPES")
endif()

if (WIN32)
    # Compile icon.rc into the exe. Without it the binary carries no RT_GROUP_ICON
    # and Explorer/taskbar/Alt-Tab fall back to the generic executable icon.
    #
    # It is generated from icon.rc.in so the EdDSA public key can ride along in
    # the same resource script. WinSparkle looks for a resource named "EdDSAPub"
    # of type "EDDSA" and, when it finds one, verifies every downloaded update
    # against it before running the installer. A build with no key omits the
    # resource entirely rather than emitting an empty one.
    enable_language(RC)
    set(CEDARLOGIC_ICON "${PROJECT_SOURCE_DIR}/res/icon.ico")
    if(SPARKLE_ED_PUBLIC_KEY)
        # Braces matter: `EdDSAPub EDDSA "..."` makes rc.exe treat the quoted
        # text as a *filename* to load, so the build fails looking for a file
        # named after the key. `{"..."}` is an inline raw-data block, which is
        # the form winsparkle-tool prints and what the library reads back.
        set(WINSPARKLE_EDDSA_RESOURCE "EdDSAPub EDDSA {\"${SPARKLE_ED_PUBLIC_KEY}\"}")
    else()
        set(WINSPARKLE_EDDSA_RESOURCE "")
        message(STATUS "No SPARKLE_ED_PUBLIC_KEY: Windows updates will not be signature-verified")
    endif()
    configure_file("${PROJECT_SOURCE_DIR}/icon.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/icon.rc" @ONLY)
    target_sources(CedarLogic PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/icon.rc")

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
    install(FILES "${WINSPARKLE_DIR}/${WINSPARKLE_ARCH_DIR}/WinSparkle.dll" DESTINATION ".")
    # The debug symbols, beside the exe. The in-app crash handler symbolizes
    # against whatever .pdb sits next to the program (see CrashTrace.cpp), so
    # shipping it is the difference between a crash report
    # naming a function and file:line, and one that is a column of hex offsets
    # nobody can act on. The linker records the .pdb's BUILD path, which on a CI
    # runner is a directory that never existed on the user's machine, so without
    # this there is nothing for the handler to find.
    if(MSVC)
        install(FILES "$<TARGET_PDB_FILE:CedarLogic>" DESTINATION "." OPTIONAL)
    endif()
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
# The name of the installed thing, with no version in it. CPack would otherwise
# default both of these to "CedarLogic <version>", which is what made every
# release install itself beside the last one instead of over it: the install
# directory, the Start Menu folder and the Add/Remove Programs key were all
# version-stamped, so 3.2.0 shared nothing with 3.1.2 and could not see it.
# CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL looks the old copy up by exactly
# this registry key, so with the version in it the lookup always missed and the
# "uninstall the old one first" step silently did nothing.
#
# Add/Remove Programs still shows the version: it comes from DisplayVersion,
# which CPack writes separately from CPACK_PACKAGE_VERSION.
set(CPACK_PACKAGE_INSTALL_DIRECTORY "CedarLogic")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "CedarLogic")
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
# That setting only finds a previous install under the registry key above, which
# no release before this one used. Clear out the versioned installs the old
# naming scheme left scattered across the machine.
set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS
    "!include \\\"${CMAKE_SOURCE_DIR}/cmake/cpack\\\\RemoveLegacyVersions.nsh\\\"
    \\\${RemoveLegacyVersions}"
)
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
    # The Applications symlink comes for free, but on its own it is only an icon
    # sitting next to another icon: nothing says to drag one onto the other, and
    # opening the app where it lies is the easier thing to do. That copy runs
    # from the read-only image, so it cannot update itself and dies outright if
    # the image is ejected. Hence a background with an arrow on it, and a script
    # that puts the icons where the arrow points.
    set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_SOURCE_DIR}/res/macos/dmg-background.tiff")
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_SOURCE_DIR}/res/macos/dmg-setup.applescript")
endif()

include(CPack)
