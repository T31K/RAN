# Native macOS compat-layer tests. Build + run: port/scripts/run-tests.sh
cmake_minimum_required(VERSION 3.24)
project(ran_port_tests CXX)
# C++14 like the game itself (VS2022 v143 default, no LanguageStandard set in the .vcxproj files):
# the compat layer must compile in the same mode as the code that includes it.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
enable_testing()

get_filename_component(PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(DXVK_INCLUDE "${PORT_DIR}/third_party/dxvk-install/include/dxvk" CACHE PATH "DXVK native headers")

add_compile_options(-Wall)
include_directories(${PORT_DIR}/compat/include ${DXVK_INCLUDE})
link_libraries(iconv)   # win32/codepage.h (macOS ships iconv as a separate library)

foreach(t compat_types_test cstring_test afx_types_test win32_kernel_test win32_files_test strsafe_test fpu_test codepage_test winsock_test win32_system_test mmio_test wchar16_test)
  add_executable(${t} ${t}.cpp)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
# The file test uses std::filesystem to set up fixtures (C++17); every other test keeps the
# compat headers honest in C++14.
set_target_properties(win32_files_test PROPERTIES CXX_STANDARD 17)

# D3DX replacement (Phase 3): math library.
add_executable(d3dx_math_test d3dx_math_test.cpp ${PORT_DIR}/d3dx/d3dx9_math.cpp)
add_test(NAME d3dx_math_test COMMAND d3dx_math_test)

# Platform layer (Phase 2): SDL3 scancode -> DirectInput DIK table, checked against the SDK header.
find_path(SDL3_INCLUDE_DIR SDL3/SDL_scancode.h HINTS /opt/homebrew/include /usr/local/include)
if(SDL3_INCLUDE_DIR)
  add_executable(input_map_test input_map_test.cpp)
  target_include_directories(input_map_test PRIVATE ${SDL3_INCLUDE_DIR} ${PORT_DIR}/../Dependency/directx)
  add_test(NAME input_map_test COMMAND input_map_test)
endif()

# DirectInput 8 stand-in fed by the SDL event pump.
add_executable(dinput_sdl_test dinput_sdl_test.cpp ${PORT_DIR}/platform/dinput_sdl.cpp)
target_include_directories(dinput_sdl_test PRIVATE ${PORT_DIR}/../Dependency/directx)
target_compile_definitions(dinput_sdl_test PRIVATE INITGUID)   # GUID_SysKeyboard etc. (dxguid.lib on Windows)
add_test(NAME dinput_sdl_test COMMAND dinput_sdl_test)
