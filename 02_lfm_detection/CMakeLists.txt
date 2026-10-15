cmake_minimum_required(VERSION 3.16)
project(lfm_host_test C)
set(CMAKE_C_STANDARD 99)
add_executable(lfm_host
    firmware/host_test/main_host.c
    firmware/algo/lfm_algo.c
    firmware/algo/fft_complex.c)
if(NOT MSVC)
    target_link_libraries(lfm_host PRIVATE m)
endif()
