cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(sillon-package)

find_package(Vulkan COMPONENTS shaderc_combined)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/loader/cmake")
include(loader)
add_subdirectory(loader)
target_include_directories(loader_interface INTERFACE toolkits)

add_library(vktk STATIC vktk.cc)
target_link_libraries(vktk PUBLIC Vulkan::Vulkan PRIVATE Vulkan::shaderc_combined)
set_property(TARGET vktk PROPERTY CXX_STANDARD 20)

add_library(sillon MODULE engine.cc _glyphs.cc _icosphere.cc _voxels.cc _editor.cc)
target_link_libraries(sillon PRIVATE vktk loader_interface)
target_include_directories(sillon PRIVATE toolkits)
set_property(TARGET sillon PROPERTY CXX_STANDARD 20)

hotreload_setup(sillon)

if (UNIX)
  target_compile_options(vktk PRIVATE "-fPIC")
  target_compile_options(sillon PRIVATE "-fvisibility=hidden" "-mbmi2" "-mbmi")
endif()


# === VOXTK_IMPORTERS ===

if (MSVC)
  add_compile_options(/wd4267 /wd4244 /wd5051 /wd4805)
else()
  add_compile_options(-w -fPIC)
endif()

set(WITH_PYTHON OFF CACHE BOOL "")
set(WITH_TESTS OFF CACHE BOOL "")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

add_subdirectory(voxtk-importers/laz-perf)
include_directories(voxtk-importers/laz-perf/cpp)

add_subdirectory(voxtk-importers/copc-lib)
include_directories(voxtk-importers/copc-lib/cpp/include/copc-lib)

target_link_libraries(sillon PRIVATE copc-lib-s)

# ===
