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

# --- GGUF Model Embedding (optional) ---
# Set JARVIS_EMBED_MODEL to a .gguf file path to embed the model in .rodata
# If not set, the build still compiles (CI compatible) without model data.
option(JARVIS_EMBED_MODEL "Path to GGUF model to embed" "")

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
    src/ai/gguf_parser.c
    src/ai/llama_load.c
    src/ai/llama_forward.c
    src/ai/inference.c
    src/ai/gguf_vocab.c
    src/ai/llama_quant.c
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

# -Wall instead of -Werror during active development (new inference code may warn)
target_compile_options(jarvis-rootserver PRIVATE -Wall -g -O2)

# --- Conditional model embedding via objcopy ---
# objcopy encodes the input filename into symbol names. We symlink the model
# to "model.gguf" in the build dir so symbols are always:
#   _binary_model_gguf_start, _binary_model_gguf_end, _binary_model_gguf_size
if(NOT "${JARVIS_EMBED_MODEL}" STREQUAL "")
    if(EXISTS "${JARVIS_EMBED_MODEL}")
        message(STATUS "JARVIS: Embedding GGUF model: ${JARVIS_EMBED_MODEL}")
        set(MODEL_LINK "${CMAKE_CURRENT_BINARY_DIR}/model.gguf")
        set(MODEL_OBJ "${CMAKE_CURRENT_BINARY_DIR}/model_data.o")
        add_custom_command(
            OUTPUT "${MODEL_OBJ}"
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${JARVIS_EMBED_MODEL}" "${MODEL_LINK}"
            COMMAND objcopy -I binary -O elf64-x86-64
                --rename-section .data=.rodata,alloc,load,readonly,data,contents
                "${MODEL_LINK}" "${MODEL_OBJ}"
            DEPENDS "${JARVIS_EMBED_MODEL}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Embedding GGUF model (objcopy -> model_data.o)"
            VERBATIM
        )
        add_custom_target(jarvis_model_data DEPENDS "${MODEL_OBJ}")
        add_dependencies(jarvis-rootserver jarvis_model_data)
        target_link_libraries(jarvis-rootserver PRIVATE "${MODEL_OBJ}")
        target_compile_definitions(jarvis-rootserver PRIVATE JARVIS_HAS_MODEL=1)
    else()
        message(WARNING "JARVIS_EMBED_MODEL set but file not found: ${JARVIS_EMBED_MODEL}")
    endif()
endif()

# Note: math functions (expf, sqrtf, sinf, cosf, powf) are provided by
# musl's libc.a which is already linked as 'muslc'. Do NOT add -lm here
# as it would pull in the host glibc libm and cause link errors.

include(rootserver)
DeclareRootserver(jarvis-rootserver)
