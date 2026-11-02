cmake_minimum_required(VERSION 3.20)
project(workbench VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Raven: installed (the raven-dev package, or `cmake --install` of a checkout),
# or built here from a checkout given with -DRAVEN_SOURCE_DIR=/path/to/Raven.
# That is the only development dependency; nothing is vendored.
find_package(raven CONFIG QUIET)
if(NOT TARGET raven::raven AND RAVEN_SOURCE_DIR)
	add_subdirectory("${RAVEN_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/raven" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET raven::raven)
	message(FATAL_ERROR "Raven not found. Install it (raven-dev, or `cmake --install` of a checkout), or configure with -DRAVEN_SOURCE_DIR=/path/to/Raven")
endif()

# What the application pane and the About box show.
string(TIMESTAMP WORKBENCH_BUILD_DATE "%Y-%m-%d")

# The instrument pane lives in its own folder: this project carries everything
# it needs except Raven itself, so the folder can be copied to a new project.
add_executable(workbench main.cpp config.cpp json.cpp schema.cpp preferences.cpp
	panel/panel.cpp)
target_include_directories(workbench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/panel")
target_link_libraries(workbench PRIVATE raven::raven)

enable_testing()
add_test(NAME workbench COMMAND workbench --test)
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../workbench.jsonc" WORKBENCH_CONFIG_FILE)
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../help" WORKBENCH_HELP_FOLDER)
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../workbench.schema.json" WORKBENCH_SCHEMA_FILE)

target_compile_definitions(workbench PRIVATE
	WORKBENCH_VERSION="${PROJECT_VERSION}"
	WORKBENCH_BUILD_DATE="${WORKBENCH_BUILD_DATE}"
	WORKBENCH_DEFAULT_CONFIG="${WORKBENCH_CONFIG_FILE}"
	WORKBENCH_HELP_DIR="${WORKBENCH_HELP_FOLDER}"
	WORKBENCH_SCHEMA="${WORKBENCH_SCHEMA_FILE}")
