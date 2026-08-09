#
# JARVIS AI-OS x86-64 Rootserver (Process A)
#
# Spawns the inference server (Process B) from a CPIO-archived ELF.
# Process A handles: decision cache, SHIELD, IPC dispatch, drivers.
# Process B handles: model loading, quantized inference, tokenizer.
#
# Pattern follows sel4test: sel4test-driver spawns sel4test-tests.
#

cmake_minimum_required(VERSION 3.16.0)

project(sel4test-driver C)

set(configure_string "")

find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)

musllibc_setup_build_environment_with_sel4runtime()
sel4_import_libsel4()
util_libs_import_libraries()
sel4_libs_import_libraries()

add_config_library(sel4test-driver "${configure_string}")

# --- Build inference server (Process B) and pack into CPIO ---
add_subdirectory(../jarvis-inference jarvis-inference)
add_subdirectory(../jarvis-input jarvis-input)
include(cpio)
MakeCPIO(archive.o "$<TARGET_FILE:jarvis-inference>;$<TARGET_FILE:jarvis-input>")

# --- Process A: Rootserver executable ---
# Includes CPIO archive containing Process B's ELF
add_executable(sel4test-driver EXCLUDE_FROM_ALL
    archive.o
    src/main.c
    src/drivers/vga_text.c
    src/drivers/pci.c
    src/drivers/nvme.c
    src/drivers/nvme_log.c
    src/drivers/fat32.c
    src/drivers/framebuffer.c
    src/drivers/nic_i211.c
    src/drivers/net_udp.c
    src/drivers/jarvis_telemetry.c
    # Decision cache + patterns (stays in rootserver)
    src/ai/decision_cache.c
    src/ai/episodic_store.c
    src/ai/shared_context.c
    src/ai/g3_retrieval.c
    src/ai/cache_growth.c
    src/ai/shield_learn.c
    src/ai/semantic_store.c
    src/ai/semantic_distill.c
    src/ai/action_allowlist.c
    src/ai/shield_action.c
    src/ai/action_audit.c
    src/ai/km2b_trigger.c
    src/ai/km2b_miss.c
    src/ai/km2b_fault.c
    src/ai/monitors.c
    src/ai/wake.c
    src/ai/behaviors.c
    src/ai/route.c
    src/ai/ctrl_epi_index.c
    src/ai/pb_health.c
    src/ai/pb_progress.c
    src/ai/route_veto.c
    src/ai/embed_project.c
    src/ai/embed_store.c
    src/ipc/embed_region.c
    src/ai/query_shield.c
    src/net/control_reply.c
    src/net/control_console.c
    src/net/control_floor.c
    src/net/control_verify.c
    src/net/control_ratelimit.c
    src/net/control_replay.c
    src/net/control_parser.c
    src/crypto/hmac_sha256.c
    src/crypto/sha256.c
    src/ai/cache_patterns.c
    # SHIELD (keyword check in rootserver, model check forwarded to Process B)
    src/ai/shield.c
    # Model scaling state machine
    # Shared IPC (rootserver sends requests, receives responses)
    src/ipc/shmem_ipc.c
    src/drivers
    src/ipc/ring_buffer.c
    src/drivers
    src/ipc/dual_ring_buffer.c
    src/drivers
    # Self-test support (tensor ops, dequant, tokenizer, sampling still needed for 5/5 self-tests)
    src/ai/tensor_ops.c
    src/ai/dequant.c
    src/ai/tokenizer.c
    src/ai/sampling.c
)

target_include_directories(sel4test-driver PRIVATE
    "src"
    "src/ai"
    "src/ipc" "src/drivers" "src/crypto" "src/net"
    src/drivers
)

target_link_libraries(sel4test-driver
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

# -Wall during development
target_compile_options(sel4test-driver PRIVATE -Wall -g -O2 -mavx2 -mfma -mf16c -isystem /usr/lib/gcc/x86_64-linux-gnu/13/include)
target_compile_definitions(sel4test-driver PRIVATE JARVIS_CONTROL_IN=1)
target_compile_definitions(sel4test-driver PRIVATE JARVIS_SEL4)

# Note: Model embedding is now in Process B (jarvis-inference), NOT here.
# Pass -DJARVIS_EMBED_MODEL=/path/to/model.gguf to cmake — it propagates
# to the jarvis-inference target via the shared cache variable.

# Note: math functions provided by musl's libc.a (linked as 'muslc').
# Do NOT add -lm.

include(rootserver)
DeclareRootserver(sel4test-driver)
