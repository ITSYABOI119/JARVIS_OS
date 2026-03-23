cmake_minimum_required(VERSION 3.16.0)
project(jarvis-rootserver C)

set(configure_string "")

find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)

musllibc_setup_build_environment_with_sel4runtime()
sel4_import_libsel4()
util_libs_import_libraries()
sel4_libs_import_libraries()

add_config_library(jarvis-rootserver "${configure_string}")

add_executable(jarvis-rootserver EXCLUDE_FROM_ALL
    src/main.c
    src/ai/decision_cache.c
    src/ai/cache_patterns.c
    src/ipc/ring_buffer.c
    src/ipc/dual_ring_buffer.c
    src/ipc/shmem_ipc.c
    src/ai/tensor_ops.c
    src/ai/dequant.c
    src/ai/tokenizer.c
    src/ai/sampling.c
)

target_include_directories(jarvis-rootserver PRIVATE "src" "src/ai" "src/ipc")

target_link_libraries(jarvis-rootserver
    PUBLIC
    sel4_autoconf
    muslc
    sel4
    sel4runtime
    sel4allocman
    sel4vka
    sel4utils
    sel4platsupport
    sel4muslcsys
)

target_compile_options(jarvis-rootserver PRIVATE -Werror -g -O2)

include(rootserver)
DeclareRootserver(jarvis-rootserver)
