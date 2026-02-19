cmake_minimum_required(VERSION 3.11)

project(CedarLogicWASM VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Define _PRODUCTION_ to disable file-based logging (not available in WASM)
add_definitions(-D_PRODUCTION_)

# Collect logic library sources
set(LOGIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../logic")

file(GLOB LOGIC_SRC
	"${LOGIC_DIR}/src/*.cpp"
)

# Build the WASM module
add_executable(cedarlogic
	bindings.cpp
	${LOGIC_SRC}
)

target_include_directories(cedarlogic PRIVATE
	"${LOGIC_DIR}/include"
)

# Emscripten link flags
set_target_properties(cedarlogic PROPERTIES
	SUFFIX ".js"
	LINK_FLAGS "\
		--bind \
		-s MODULARIZE=1 \
		-s EXPORT_NAME='CedarLogic' \
		-s ALLOW_MEMORY_GROWTH=1 \
		-s ENVIRONMENT='web,worker,node' \
		-s NO_EXIT_RUNTIME=1 \
		-s WASM=1 \
		-O2 \
	"
)
