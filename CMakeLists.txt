# glistening on Linux (EGL), macOS (CGL, Core Audio) and Windows (WGL, WASAPI).
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release
#   ctest --test-dir build -C Release --output-on-failure
# The render tests need a real GL 3.3 context; a host without one fails them.
cmake_minimum_required(VERSION 3.21)
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "14.2" CACHE STRING "Oldest macOS: Core Audio process taps need 14.2")
endif()
project(glistening VERSION 0.1.0 LANGUAGES CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)
option(GLISTENING_PROJECTM "MilkDrop mode via projectM 3.x when found (Linux only)" ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Windows: no VC++ redistributable needed by users.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive- /Zc:__cplusplus)
else()
  add_compile_options(-Wall)
endif()
if(WIN32)
  # Windows 10 1809+ APIs (VT console, ConPTY in the terminal test).
  add_compile_definitions(_WIN32_WINNT=0x0A00 _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
endif()
if(MINGW)
  add_link_options(-static)  # no MinGW runtime DLLs to ship
endif()

# Embedded shaders: regenerated whenever a shader file is added, removed or edited.
file(GLOB GLISTENING_SHADER_FILES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/scenes/* ${CMAKE_SOURCE_DIR}/shaders/*)
set(GLISTENING_GEN ${CMAKE_BINARY_DIR}/generated)
add_custom_command(
  OUTPUT ${GLISTENING_GEN}/shaders.inc
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -DOUT=${GLISTENING_GEN}/shaders.inc
          -P ${CMAKE_SOURCE_DIR}/cmake/embed-shaders.cmake
  DEPENDS ${GLISTENING_SHADER_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed-shaders.cmake
  COMMENT "Embedding shaders"
  VERBATIM)
add_custom_target(glistening_shaders DEPENDS ${GLISTENING_GEN}/shaders.inc)

# GL context per OS: glistening_gpu. Everything else OS-specific: glistening_platform.
if(WIN32)
  add_library(glistening_gpu STATIC platform/gpu_wgl.cpp)
  target_link_libraries(glistening_gpu PUBLIC opengl32 gdi32 user32)
  add_library(glistening_platform STATIC platform/windows.cpp platform/audio_wasapi.cpp)
  target_link_libraries(glistening_platform PUBLIC ole32)
elseif(APPLE)
  add_library(glistening_gpu STATIC platform/gpu_cgl.cpp)
  target_link_libraries(glistening_gpu PUBLIC "-framework OpenGL")
  add_library(glistening_platform STATIC platform/posix.cpp platform/audio_macos.mm)
  target_link_libraries(glistening_platform PUBLIC "-framework CoreAudio" "-framework AudioToolbox" "-framework Foundation")
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
  add_library(glistening_gpu STATIC platform/gpu_egl.cpp)
  target_link_libraries(glistening_gpu PUBLIC PkgConfig::EGL ${CMAKE_DL_LIBS})
  add_library(glistening_platform STATIC platform/posix.cpp)
endif()

add_executable(glistening viz.cpp)
add_dependencies(glistening glistening_shaders)
target_include_directories(glistening PRIVATE ${GLISTENING_GEN})
target_link_libraries(glistening PRIVATE glistening_platform glistening_gpu)
target_compile_definitions(glistening PRIVATE GLISTENING_VERSION="${PROJECT_VERSION}")
if(APPLE)
  # NSAudioCaptureUsageDescription: macOS asks the user before system audio is captured.
  target_link_options(glistening PRIVATE "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_SOURCE_DIR}/platform/Info.plist")
  set_property(TARGET glistening APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/platform/Info.plist)
endif()

# MilkDrop mode (projectM 3.x), Linux only: it finishes into the EGL pbuffer.
if(GLISTENING_PROJECTM AND NOT WIN32 AND NOT APPLE)
  pkg_check_modules(PROJECTM IMPORTED_TARGET libprojectM>=3)
  if(PROJECTM_FOUND)
    target_compile_definitions(glistening PRIVATE GLISTENING_PROJECTM=1)
    target_link_libraries(glistening PRIVATE PkgConfig::PROJECTM)
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS glistening RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
  find_package(Threads REQUIRED)
  add_executable(glistening_unit tests/test.cpp)
  target_link_libraries(glistening_unit PRIVATE glistening_platform Threads::Threads)
  add_executable(glistening_render tests/render.cpp)
  add_dependencies(glistening_render glistening_shaders)
  target_include_directories(glistening_render PRIVATE ${GLISTENING_GEN})
  target_link_libraries(glistening_render PRIVATE glistening_gpu)
  add_executable(glistening_ptysmoke tests/ptysmoke.cpp)
  target_link_libraries(glistening_ptysmoke PRIVATE Threads::Threads)
  if(NOT WIN32)
    find_library(UTIL_LIB util)
    if(UTIL_LIB)
      target_link_libraries(glistening_ptysmoke PRIVATE ${UTIL_LIB})
    endif()
  endif()

  add_test(NAME unit COMMAND glistening_unit)
  add_test(NAME render COMMAND glistening_render --size 320x180)
  add_test(NAME render_regressions COMMAND glistening_render --regressions)
  add_test(NAME render_wide COMMAND glistening_render --scene clockwork --size 1000x240)
  add_test(NAME version COMMAND glistening --version)
  add_test(NAME self_test COMMAND glistening --self-test)
  add_test(NAME dump_smoke COMMAND ${CMAKE_COMMAND} -DGLISTENING=$<TARGET_FILE:glistening>
           -DOUT=${CMAKE_BINARY_DIR}/smoke.ppm -P ${CMAKE_SOURCE_DIR}/cmake/dump-smoke.cmake)
  add_test(NAME terminal_blocks COMMAND glistening_ptysmoke $<TARGET_FILE:glistening> blocks)
  add_test(NAME terminal_kitty COMMAND glistening_ptysmoke $<TARGET_FILE:glistening> kitty)
  # Config writes go to the build tree, never the user's settings.
  set_tests_properties(version self_test dump_smoke terminal_blocks terminal_kitty PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config;APPDATA=${CMAKE_BINARY_DIR}/test-config")
endif()
