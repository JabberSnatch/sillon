cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(sillon-package)

find_package(Vulkan COMPONENTS shaderc_combined)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/loader/cmake")
include(loader)
add_subdirectory(loader)

add_library(vktk STATIC vktk.cc)
target_link_libraries(vktk PRIVATE Vulkan::Vulkan Vulkan::shaderc_combined)
set_property(TARGET vktk PROPERTY CXX_STANDARD 20)

add_library(sillon MODULE engine.cc _glyphs.cc)
target_link_libraries(sillon PRIVATE vktk loader_interface)
set_property(TARGET sillon PROPERTY CXX_STANDARD 20)

hotreload_setup(sillon)

if (UNIX)
  target_compile_options(vktk PRIVATE "-fPIC")
  target_compile_options(sillon PRIVATE "-fvisibility=hidden")
endif()
