# Skia integration (Workstream G).
#
# Skia is REQUIRED to build the GUI app: it is the renderer, not an option. This
# module produces a single INTERFACE target, `cedar_skia`, carrying Skia's
# include dirs, static libraries, required system dependencies, and the
# WITH_SKIA / SK_GL compile definitions. Link it onto the app target.
#
# WITH_SKIA survives only as a compile definition, for the `#ifdef WITH_SKIA`
# guards in the Skia TUs; it is not a build option.
#
# Where Skia comes from mirrors the USE_SYSTEM_WXWIDGETS split:
#   USE_SYSTEM_SKIA=OFF  consume a dist tree at SKIA_ROOT: the CI artifact from
#                        .github/workflows/skia.yml (`task skia` fetches one), or
#                        the flake's re-cut skia-aseprite, which is the same
#                        milestone. This is the path for Windows, macOS, CI, and
#                        Nix -- everything that pins aseprite-m124.
#   USE_SYSTEM_SKIA=ON   find a system Skia through pkg-config, with SKIA_ROOT as
#                        a fallback. For distro packages, which track whatever
#                        milestone they track; expect API drift.

if(CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(USE_SYSSKIA_DEF TRUE)
else()
    set(USE_SYSSKIA_DEF FALSE)
endif()
set(USE_SYSTEM_SKIA "${USE_SYSSKIA_DEF}" CACHE BOOL
    "Find a system Skia instead of consuming the CI-built dist")

set(SKIA_ROOT "" CACHE PATH
    "Root of a Skia dist (contains include/ and lib/); required when \
USE_SYSTEM_SKIA=OFF, optional fallback when ON")

# NB: Skia (m124) requires C++17, raised per-source on the Skia TUs in the
# top-level CMakeLists; C++17 is source-compatible with the existing C++11 code.

add_library(cedar_skia INTERFACE)
target_compile_definitions(cedar_skia INTERFACE WITH_SKIA SK_GL)

set(_skia_found FALSE)

if(USE_SYSTEM_SKIA)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SKIA_PC QUIET skia)
        if(SKIA_PC_FOUND)
            target_include_directories(cedar_skia INTERFACE ${SKIA_PC_INCLUDE_DIRS})
            target_link_libraries(cedar_skia INTERFACE ${SKIA_PC_LINK_LIBRARIES})
            set(_skia_found TRUE)
            message(STATUS "Skia: using system package (pkg-config)")
        endif()
    endif()
endif()

# Fall back to (or, when USE_SYSTEM_SKIA=OFF, require) a dist tree at SKIA_ROOT.
if(NOT _skia_found)
    if(NOT SKIA_ROOT)
        message(FATAL_ERROR
            "Skia was not found. Set -DSKIA_ROOT=<dir> to a "
            "Skia dist containing include/ and lib/ (build one with the Skia CI "
            "workflow), or set -DUSE_SYSTEM_SKIA=ON with a pkg-config 'skia'.")
    endif()
    if(NOT EXISTS "${SKIA_ROOT}/include/core/SkCanvas.h")
        message(FATAL_ERROR
            "SKIA_ROOT='${SKIA_ROOT}' does not look like a Skia dist "
            "(missing include/core/SkCanvas.h).")
    endif()

    # Static archives, module libs first so GNU ld resolves core symbols after
    # its dependents. --start-group makes the order robust regardless.
    file(GLOB _skia_libs "${SKIA_ROOT}/lib/*.a" "${SKIA_ROOT}/lib/*.lib")
    if(NOT _skia_libs)
        message(FATAL_ERROR "No Skia static libraries under '${SKIA_ROOT}/lib'.")
    endif()
    list(SORT _skia_libs)  # deterministic; 'skia' sorts after module libs

    # Skia's own headers include each other root-relative (e.g.
    # #include "include/core/SkTypes.h"), so the dist root itself must be on the
    # search path -- alongside include/ for our own "core/..."-style includes.
    target_include_directories(cedar_skia INTERFACE "${SKIA_ROOT}" "${SKIA_ROOT}/include")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE AND NOT MSVC)
        target_link_libraries(cedar_skia INTERFACE
            -Wl,--start-group ${_skia_libs} -Wl,--end-group)
    else()
        target_link_libraries(cedar_skia INTERFACE ${_skia_libs})
    endif()
    message(STATUS "Skia: using dist at ${SKIA_ROOT}")
endif()

# A few of Skia's public headers still #include internal "src/..." headers that
# are unused in the public API (docs/SkPDFDocument.h pulls src/base/SkTime.h even
# though its Metadata carries its own DateTime). The trimmed dist ships no src/
# tree, so those includes fail to resolve. Add empty shims LAST, so a real Skia
# checkout (system/Nix) that ships the genuine headers is searched first.
target_include_directories(cedar_skia INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/../skia-shim")

# System libraries the static Skia needs at final link. Skia's vendored deps
# (freetype, harfbuzz, icu, image codecs, zlib, expat) ship as their own static
# archives beside libskia and are picked up by the glob above; these are the
# OS-level pieces layered on top.
if(WIN32)
    target_link_libraries(cedar_skia INTERFACE opengl32 user32 gdi32)
elseif(APPLE)
    target_link_libraries(cedar_skia INTERFACE
        "-framework CoreFoundation" "-framework CoreGraphics"
        "-framework CoreText" "-framework OpenGL")
else()
    # Skia's GL backend (and wxGLCanvas) use GLX; link legacy libGL, which
    # provides glX* -- under GLVND OPENGL_opengl_LIBRARY is libOpenGL, which does
    # not. Bare "GL" defers to -lGL at link time (find_package(OpenGL) runs after
    # this module).
    find_package(Fontconfig QUIET)
    find_package(PkgConfig QUIET)   # may already be found above; harmless
    target_link_libraries(cedar_skia INTERFACE GL ${CMAKE_DL_LIBS} pthread)
    if(Fontconfig_FOUND)
        target_link_libraries(cedar_skia INTERFACE Fontconfig::Fontconfig)
        # Skia's fontconfig font manager is how the app finds a system face on
        # Linux -- hardcoded /usr/share/fonts paths only ever matched Debian and
        # Ubuntu, and match nothing on NixOS. The factory gained a font-scanner
        # argument after m124 (pinned dist: one arg; nixpkgs' newer skia: two),
        # so compile-probe both rather than assume a milestone. Neither hit
        # leaves the app on its path list, which still works where it works.
        include(CheckCXXSourceCompiles)
        get_target_property(_skia_incs cedar_skia INTERFACE_INCLUDE_DIRECTORIES)
        set(CMAKE_REQUIRED_INCLUDES ${_skia_incs} ${Fontconfig_INCLUDE_DIRS})
        set(CMAKE_REQUIRED_QUIET TRUE)
        # Compile only -- the definition lives in libskia, which is not on the
        # probe's link line. SkFontMgr must be complete for sk_sp's destructor,
        # hence the core header alongside the port one.
        set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
        set(CMAKE_REQUIRED_FLAGS -std=c++17)   # Skia's headers require it
        check_cxx_source_compiles("
            #include \"core/SkFontMgr.h\"
            #include \"ports/SkFontMgr_fontconfig.h\"
            sk_sp<SkFontMgr> f() { return SkFontMgr_New_FontConfig(nullptr); }
        " SKIA_FONTCONFIG_PLAIN)
        if(NOT SKIA_FONTCONFIG_PLAIN)
            check_cxx_source_compiles("
                #include \"core/SkFontMgr.h\"
                #include \"ports/SkFontMgr_fontconfig.h\"
                #include \"ports/SkFontScanner_FreeType.h\"
                sk_sp<SkFontMgr> f() {
                    return SkFontMgr_New_FontConfig(nullptr,
                                                    SkFontScanner_Make_FreeType());
                }
            " SKIA_FONTCONFIG_SCANNER)
        endif()
        unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
        unset(CMAKE_REQUIRED_FLAGS)
        unset(CMAKE_REQUIRED_INCLUDES)
        unset(CMAKE_REQUIRED_QUIET)
        if(SKIA_FONTCONFIG_PLAIN OR SKIA_FONTCONFIG_SCANNER)
            target_compile_definitions(cedar_skia INTERFACE CEDAR_SKIA_FONTCONFIG)
            message(STATUS "Skia: system fonts via fontconfig")
        else()
            message(STATUS "Skia: no usable fontconfig manager; "
                           "falling back to well-known font paths")
        endif()
        if(SKIA_FONTCONFIG_SCANNER)
            target_compile_definitions(cedar_skia INTERFACE
                CEDAR_SKIA_FONTCONFIG_SCANNER)
        endif()
    endif()
    # wxWidgets' shared libs pull system DSOs (zlib, png, ...) that modern ld
    # won't resolve indirectly (--no-copy-dt-needed-entries default). Let a
    # linked .so's DT_NEEDED satisfy them instead of listing each by hand.
    target_link_options(cedar_skia INTERFACE "LINKER:--copy-dt-needed-entries")

    # Skia's own third-party dependencies. The CI dist vendors them: they sit as
    # archives beside libskia and the glob above already picked them up. A Skia
    # built against SYSTEM libraries (the Nix flake, a distro package) ships no
    # such archives, and the app is what links libskia.a, so its references land
    # here. wxWidgets' DT_NEEDED covers the common ones (png, jpeg, zlib, expat,
    # freetype); these two it does not pull, and their absence only shows up at
    # the very end of the app link:
    #   libwebpdemux    <- webp_decode.SkWebpCodec.o
    #   harfbuzz-subset <- pdf.SkPDFSubsetFont.o (font subsetting for PDF export)
    if(PkgConfig_FOUND)
        if(NOT EXISTS "${SKIA_ROOT}/lib/libwebp.a")
            pkg_check_modules(SKIA_WEBP QUIET libwebpdemux libwebp)
            if(SKIA_WEBP_FOUND)
                target_link_libraries(cedar_skia INTERFACE ${SKIA_WEBP_LINK_LIBRARIES})
            endif()
        endif()
        if(NOT EXISTS "${SKIA_ROOT}/lib/libharfbuzz.a")
            pkg_check_modules(SKIA_HB QUIET harfbuzz-subset)
            if(SKIA_HB_FOUND)
                target_link_libraries(cedar_skia INTERFACE ${SKIA_HB_LINK_LIBRARIES})
            endif()
        endif()
    endif()
endif()
