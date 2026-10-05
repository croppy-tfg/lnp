cmake_minimum_required(VERSION 3.20)
project(LNP C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(lnp STATIC
        src/address.c
        src/frame.c
        src/routing.c
)

target_include_directories(lnp PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

add_executable(lnp_test tests/main.c)

target_link_libraries(lnp_test PRIVATE lnp)