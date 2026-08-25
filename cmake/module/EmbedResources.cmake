#
# This module defines:
# - embed_resources(<out_cpp_var> <root> <file>...)
#
# The app carries the resources it cannot start without -- the gate library and
# the toolbar icons -- inside the executable, so nothing at runtime has to work
# out where the program was installed. That question has no good answer for a
# build run in place: wxStandardPaths infers a prefix from a /bin/ in the
# executable's path, and a build tree has none.
#
# Generation happens at configure time and the sources are listed as configure
# dependencies, so editing a resource re-runs CMake and regenerates the blob.
#

function(embed_resources out_cpp_var root)
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedRes.cpp")

    set(blobs "")
    set(entries "")
    set(index 0)
    foreach(file ${ARGN})
        set(path "${root}/${file}")
        if(NOT EXISTS "${path}")
            message(FATAL_ERROR "embed_resources: no such file '${path}'")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")

        file(READ "${path}" hex HEX)
        string(LENGTH "${hex}" hexLen)
        math(EXPR size "${hexLen} / 2")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
        # Wrap at 16 bytes a line: one 300KB literal line compiles, slowly, and
        # is unreadable in a diff or a compiler error.
        string(REGEX REPLACE "((0x..,){16})" "\\1\n\t" bytes "${bytes}")

        string(APPEND blobs
            "const unsigned char kBlob${index}[] = {\n\t${bytes}\n};\n\n")
        string(APPEND entries
            "\t{ \"${file}\", kBlob${index}, ${size} },\n")
        math(EXPR index "${index} + 1")
    endforeach()

    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbeddedRes.cpp.in"
                   "${generated}" @ONLY)
    set(${out_cpp_var} "${generated}" PARENT_SCOPE)
endfunction()
