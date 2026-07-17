cmake_minimum_required(VERSION 3.28)
project(QL1K LANGUAGES CXX)

if(NOT MSVC OR NOT WIN32 OR CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "QL1K requires 32-bit MSVC: configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(FETCHCONTENT_SOURCE_DIR_ZYDIS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zydis" CACHE PATH "" FORCE)
set(SAFETYHOOK_FETCH_ZYDIS ON CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/safetyhook EXCLUDE_FROM_ALL)

add_library(ql_fps_patch SHARED src/ql_patch_core.cpp src/ql_patch.def)
target_include_directories(ql_fps_patch PRIVATE src)
target_compile_definitions(ql_fps_patch PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(ql_fps_patch PRIVATE /W4 /WX /permissive- /sdl /EHsc /Oi)
target_link_options(ql_fps_patch PRIVATE /WX /Brepro)
target_link_libraries(ql_fps_patch PRIVATE safetyhook bcrypt)
set_target_properties(ql_fps_patch PROPERTIES
    OUTPUT_NAME ql_fps_patch
    MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

add_executable(ql_fps_injector src/injector.cpp)
target_compile_definitions(ql_fps_injector PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(ql_fps_injector PRIVATE /W4 /WX /permissive- /sdl /EHsc /Oi)
target_link_options(ql_fps_injector PRIVATE /WX /Brepro)
set_target_properties(ql_fps_injector PROPERTIES
    MSVC_RUNTIME_LIBRARY MultiThreaded)

include(CTest)
if(BUILD_TESTING)
    add_executable(hitreg_state_tests tests/hitreg_state_tests.cpp)
    target_include_directories(hitreg_state_tests PRIVATE src)
    target_compile_options(hitreg_state_tests PRIVATE /W4 /WX /permissive- /sdl /EHsc /Oi)
    target_link_options(hitreg_state_tests PRIVATE /WX /Brepro)
    add_test(NAME hitreg_state_tests COMMAND hitreg_state_tests)
endif()
