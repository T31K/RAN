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
