cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(sillon-package)

find_package(Vulkan COMPONENTS shaderc_combined glslangValidator glslc)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/loader/cmake")
include(loader)
add_subdirectory(loader)
target_include_directories(loader_interface INTERFACE toolkits)

add_library(vktk STATIC vktk.cc)
target_link_libraries(vktk PUBLIC Vulkan::Vulkan PRIVATE Vulkan::shaderc_combined)
set_property(TARGET vktk PROPERTY CXX_STANDARD 20)

add_library(sillon MODULE engine.cc _glyphs.cc _icosphere.cc _voxels.cc _editor.cc)

set(SHADER_SOURCES
  shaders/sparse_region.frag.glsl
  shaders/atmosphere_transmittance.frag.glsl
  shaders/atmosphere_first_order.frag.glsl)


# === SHADER BUILD SETUP ===

set(SHADER_BINARIES)
foreach(SHADER ${SHADER_SOURCES})

  set(BINARY shaders/${SHADER}.spv)
  set(DEPFILE ${BINARY}.d)
  set(SHADER ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})

  cmake_path(GET DEPFILE PARENT_PATH DEPFILE_DIR)
  file(MAKE_DIRECTORY ${DEPFILE_DIR})
  file(TOUCH ${DEPFILE})
  add_custom_command(
	OUTPUT ${BINARY}
	COMMAND $<TARGET_FILE:Vulkan::glslc> ${SHADER} -M -o ${DEPFILE}
	COMMAND $<TARGET_FILE:Vulkan::glslangValidator> -V100 ${SHADER} -o ${BINARY} -x
	MAIN_DEPENDENCY ${SHADER}
	BYPRODUCTS ${DEPFILE}
	DEPFILE ${DEPFILE}
	VERBATIM
  )

  list(APPEND SHADER_BINARIES ${BINARY})

endforeach()

add_custom_target(shaders_target DEPENDS ${SHADER_BINARIES})
add_library(shaders INTERFACE)
target_include_directories(shaders INTERFACE shaders)
target_sources(shaders
  INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/shaders
  FILES ${SHADER_BINARIES})
add_dependencies(shaders shaders_target)

# ===


target_link_libraries(sillon PRIVATE vktk loader_interface shaders)
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
