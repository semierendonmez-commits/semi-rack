cmake_minimum_required(VERSION 3.22)

# SEMI Family — MetaModule build.
#   cmake -B build -G Ninja -DMETAMODULE_SDK_DIR=/path/to/metamodule-plugin-sdk
#   cmake --build build      ->  ../dist/metamodule/SemiFamily.mmplugin
# Needs arm-none-eabi-gcc 12.2/12.3/15.3 on PATH (or -DTOOLCHAIN_BASE_DIR=...).

if(NOT "${METAMODULE_SDK_DIR}" STREQUAL "")
	message("METAMODULE_SDK_DIR set by CMake variable ${METAMODULE_SDK_DIR}")
elseif(DEFINED ENV{METAMODULE_SDK_DIR})
	set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
	message("METAMODULE_SDK_DIR set by environment variable ${METAMODULE_SDK_DIR}")
else()
	set(METAMODULE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../metamodule-plugin-sdk")
	message("METAMODULE_SDK_DIR set to default: ${METAMODULE_SDK_DIR}")
endif()

include(${METAMODULE_SDK_DIR}/plugin.cmake)

project(SemiFamily VERSION 2.0.0 DESCRIPTION "SEMI Family for MetaModule" LANGUAGES C CXX ASM)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

add_library(SemiFamily STATIC)

file(GLOB PLUGIN_SOURCES ${REPO_ROOT}/src/*.cpp ${REPO_ROOT}/src/modules/*.cpp)

# Each engine appends its own sources (engines/<name>/sources.cmake).
set(ENGINE_SOURCES "")
file(GLOB ENGINE_CMAKE ${REPO_ROOT}/engines/*/sources.cmake)
foreach(f ${ENGINE_CMAKE})
	include(${f})
endforeach()

target_sources(SemiFamily PRIVATE ${PLUGIN_SOURCES} ${ENGINE_SOURCES})
target_include_directories(SemiFamily PRIVATE ${REPO_ROOT}/engines)
target_compile_options(SemiFamily PRIVATE -O3 $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20>)

create_plugin(
	SOURCE_LIB      SemiFamily
	PLUGIN_NAME     SemiFamily
	PLUGIN_JSON     ${REPO_ROOT}/plugin.json
	SOURCE_ASSETS   ${CMAKE_CURRENT_LIST_DIR}/assets
	DESTINATION     ${REPO_ROOT}/dist/metamodule
)
