#
# JARVIS AI-OS — Inference Server Process (Process B)
#
# Separate seL4 process for LLM inference. Built as a standalone ELF,
# packed into a CPIO archive, and embedded in the rootserver (Process A).
#
# NOTE: This is included via add_subdirectory from the rootserver's
# CMakeLists.txt. The seL4 packages are already imported — do NOT call
# find_package or import_libraries here (causes duplicate target errors).
#

cmake_minimum_required(VERSION 3.16.0)

project(jarvis-inference C)

add_executable(jarvis-inference EXCLUDE_FROM_ALL
    src/main.c
    # AI inference modules
    src/ai/llama_quant.c
    src/ai/qdot.c
    src/ai/pb_progress.c
    src/ai/shared_context.c
    src/ai/threadpool_sel4.c
    src/ai/ssm.c
    src/ai/llama_forward.c
    src/ai/llama_load.c
    src/ai/gguf_parser.c
    src/ai/gguf_vocab.c
    src/ai/dequant.c
    src/ai/tensor_ops.c
    src/ai/tokenizer.c
    src/ai/sampling.c
    src/ai/inference.c
    # IPC
    src/ipc/shmem_ipc.c
)

target_include_directories(jarvis-inference PRIVATE
    "src"
    "src/ai"
    "src/ipc"
)

target_link_libraries(jarvis-inference
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

target_compile_options(jarvis-inference PRIVATE -Wall -g -O2 -mavx2 -mfma -mf16c -isystem /usr/lib/gcc/x86_64-linux-gnu/13/include)
target_compile_definitions(jarvis-inference PRIVATE JARVIS_EMBED=1)
target_compile_definitions(jarvis-inference PRIVATE JARVIS_SEL4_SMP)

# --- Conditional GGUF model embedding ---
# JARVIS_EMBED_MODEL is a shared cache variable set by the parent cmake invocation
if(NOT "${JARVIS_EMBED_MODEL}" STREQUAL "")
    if(EXISTS "${JARVIS_EMBED_MODEL}")
        message(STATUS "JARVIS-INFERENCE: Embedding GGUF model: ${JARVIS_EMBED_MODEL}")
        set(MODEL_LINK "${CMAKE_CURRENT_BINARY_DIR}/model.gguf")
        set(MODEL_OBJ "${CMAKE_CURRENT_BINARY_DIR}/model_data.o")
        add_custom_command(
            OUTPUT "${MODEL_OBJ}"
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${JARVIS_EMBED_MODEL}" "${MODEL_LINK}"
            COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_BINARY_DIR}"
                objcopy -I binary -O elf64-x86-64
                --rename-section .data=.rodata,alloc,load,readonly,data,contents
                model.gguf model_data.o
            DEPENDS "${JARVIS_EMBED_MODEL}"
            COMMENT "Embedding GGUF model into inference process"
            VERBATIM
        )
        add_custom_target(inference_model_data DEPENDS "${MODEL_OBJ}")
        add_dependencies(jarvis-inference inference_model_data)
        target_link_libraries(jarvis-inference PRIVATE "${MODEL_OBJ}")
        target_compile_definitions(jarvis-inference PRIVATE JARVIS_HAS_MODEL=1)
    else()
        message(WARNING "JARVIS_EMBED_MODEL set but file not found: ${JARVIS_EMBED_MODEL}")
    endif()
endif()
