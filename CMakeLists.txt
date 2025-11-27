cmake_minimum_required(VERSION 3.14)

project(coro
  VERSION 1.0.0
  DESCRIPTION "A stackful coroutines library for C"
  LANGUAGES C
)

option(ASAN "Build with address sanitizer" OFF)
option(UBSAN "Build with undefined behavior sanitizer" OFF)
option(TSAN "Build with thread sanitizer" OFF)

set(CMAKE_C_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(ASAN)
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)
endif()

if(UBSAN)
  add_compile_options(-fsanitize=undefined)
  add_link_options(-fsanitize=undefined)
endif()

if(TSAN)
  add_compile_options(-fsanitize=thread)
  add_link_options(-fsanitize=thread -rdynamic)
endif()

add_library(co
  libco/libco.c
)

target_include_directories(co PUBLIC libco)

add_library(coro
  src/coro.c
)

target_compile_options(coro
  PRIVATE
    -Wall
    -Wextra
    -pedantic
)

target_include_directories(coro PUBLIC include)

target_link_libraries(coro
  PRIVATE
    co
)

add_executable(example
  examples/example.c
)

target_link_libraries(example
  PRIVATE
    coro
)
