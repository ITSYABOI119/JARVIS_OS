/*
 * JARVIS AI-OS - Initial Cache Patterns
 * Phase 1 Week 3
 *
 * Pre-compiled patterns for common operations
 * Target: >80% hit rate on typical user commands
 *
 * Pattern Format:
 *   query: Normalized user input (lowercase, whitespace collapsed)
 *   action: Command to execute or bytecode
 *   trust: Trust level (0=auto, 1=notify, 2=request, 3=require)
 */

#include "decision_cache.h"
#include <stdio.h>

/* Pattern definition structure */
typedef struct {
    const char *query;
    const char *action;
    trust_level_t trust;
} cache_pattern_t;

/* Initial pattern database - 50 most common commands */
static const cache_pattern_t initial_patterns[] = {
    /* System Information (TRUST_AUTO) */
    {"help", "show_help", TRUST_AUTO},
    {"status", "show_status", TRUST_AUTO},
    {"version", "show_version", TRUST_AUTO},
    {"info", "show_system_info", TRUST_AUTO},
    {"about", "show_about", TRUST_AUTO},

    /* Basic Commands (TRUST_AUTO) */
    {"hello", "print: Hello from JARVIS!", TRUST_AUTO},
    {"hi", "print: Hello! How can I assist you?", TRUST_AUTO},
    {"time", "show_current_time", TRUST_AUTO},
    {"date", "show_current_date", TRUST_AUTO},
    {"uptime", "show_system_uptime", TRUST_AUTO},

    /* File System - Read Only (TRUST_AUTO) */
    {"ls", "list_directory", TRUST_AUTO},
    {"pwd", "print_working_directory", TRUST_AUTO},
    {"ls -l", "list_directory --long", TRUST_AUTO},
    {"ls -a", "list_directory --all", TRUST_AUTO},
    {"cat readme", "display_file: README.md", TRUST_AUTO},

    /* System Monitoring (TRUST_AUTO) */
    {"ps", "list_processes", TRUST_AUTO},
    {"top", "show_process_monitor", TRUST_AUTO},
    {"free", "show_memory_usage", TRUST_AUTO},
    {"df", "show_disk_usage", TRUST_AUTO},
    {"cpu", "show_cpu_info", TRUST_AUTO},

    /* Network - Read Only (TRUST_AUTO) */
    {"ping google.com", "ping: google.com", TRUST_NOTIFY},
    {"ifconfig", "show_network_interfaces", TRUST_AUTO},
    {"netstat", "show_network_connections", TRUST_AUTO},

    /* File System - Write (TRUST_NOTIFY) */
    {"touch test.txt", "create_file: test.txt", TRUST_NOTIFY},
    {"mkdir test", "create_directory: test", TRUST_NOTIFY},
    {"echo hello", "print: hello", TRUST_AUTO},
    {"cp file1 file2", "copy_file: file1 → file2", TRUST_NOTIFY},

    /* Process Management (TRUST_REQUEST) */
    {"kill 1234", "kill_process: 1234", TRUST_REQUEST},
    {"killall chrome", "killall_process: chrome", TRUST_REQUEST},
    {"restart service", "restart_service", TRUST_REQUEST},

    /* System Configuration (TRUST_REQUEST) */
    {"set timezone est", "set_timezone: EST", TRUST_REQUEST},
    {"update system", "system_update", TRUST_REQUEST},
    {"install package", "install_package", TRUST_REQUEST},
    {"remove package", "remove_package", TRUST_REQUEST},

    /* Dangerous Operations (TRUST_REQUIRE) */
    {"rm -rf /", "BLOCKED: Dangerous operation", TRUST_REQUIRE},
    {"format disk", "format_disk", TRUST_REQUIRE},
    {"dd if=/dev/zero", "BLOCKED: Dangerous operation", TRUST_REQUIRE},
    {"chmod 777 /etc", "BLOCKED: Dangerous operation", TRUST_REQUIRE},

    /* AI-Specific Commands (TRUST_AUTO) */
    {"cache stats", "show_cache_stats", TRUST_AUTO},
    {"cache clear", "clear_decision_cache", TRUST_NOTIFY},
    {"ai status", "show_ai_status", TRUST_AUTO},
    {"model info", "show_model_info", TRUST_AUTO},

    /* Common Variations */
    {"what time is it", "show_current_time", TRUST_AUTO},
    {"what is the time", "show_current_time", TRUST_AUTO},
    {"show me the time", "show_current_time", TRUST_AUTO},
    {"list files", "list_directory", TRUST_AUTO},
    {"show files", "list_directory", TRUST_AUTO},
    {"what files are here", "list_directory", TRUST_AUTO},
    {"where am i", "print_working_directory", TRUST_AUTO},
    {"current directory", "print_working_directory", TRUST_AUTO}
};

#define NUM_INITIAL_PATTERNS (sizeof(initial_patterns) / sizeof(cache_pattern_t))

/* Extended pattern database - 150 more patterns for higher hit rate */
static const cache_pattern_t extended_patterns[] = {
    /* Git Commands (TRUST_NOTIFY) */
    {"git status", "exec: git status", TRUST_AUTO},
    {"git log", "exec: git log --oneline -10", TRUST_AUTO},
    {"git diff", "exec: git diff", TRUST_AUTO},
    {"git add .", "exec: git add .", TRUST_NOTIFY},
    {"git commit", "git_commit_interactive", TRUST_NOTIFY},
    {"git push", "exec: git push", TRUST_REQUEST},
    {"git pull", "exec: git pull", TRUST_NOTIFY},
    {"git clone", "git_clone_interactive", TRUST_REQUEST},

    /* Docker Commands (TRUST_REQUEST) */
    {"docker ps", "exec: docker ps", TRUST_AUTO},
    {"docker images", "exec: docker images", TRUST_AUTO},
    {"docker logs", "docker_logs_interactive", TRUST_AUTO},
    {"docker stop", "docker_stop_interactive", TRUST_REQUEST},
    {"docker rm", "docker_remove_interactive", TRUST_REQUEST},

    /* Package Management (TRUST_REQUEST) */
    {"apt update", "exec: sudo apt update", TRUST_REQUEST},
    {"apt upgrade", "exec: sudo apt upgrade", TRUST_REQUEST},
    {"apt install", "apt_install_interactive", TRUST_REQUEST},
    {"apt remove", "apt_remove_interactive", TRUST_REQUEST},
    {"pip install", "pip_install_interactive", TRUST_REQUEST},
    {"npm install", "exec: npm install", TRUST_REQUEST},

    /* File Operations */
    {"find file", "find_file_interactive", TRUST_AUTO},
    {"grep pattern", "grep_interactive", TRUST_AUTO},
    {"tail log", "tail_log_interactive", TRUST_AUTO},
    {"head file", "head_file_interactive", TRUST_AUTO},
    {"less file", "less_file_interactive", TRUST_AUTO},
    {"vim file", "vim_file_interactive", TRUST_AUTO},
    {"nano file", "nano_file_interactive", TRUST_AUTO},

    /* System Control */
    {"shutdown now", "shutdown_system", TRUST_REQUIRE},
    {"reboot", "reboot_system", TRUST_REQUIRE},
    {"suspend", "suspend_system", TRUST_REQUEST},
    {"logout", "logout_user", TRUST_NOTIFY},

    /* Natural Language Variations */
    {"open terminal", "launch: terminal", TRUST_AUTO},
    {"start browser", "launch: browser", TRUST_AUTO},
    {"check disk space", "show_disk_usage", TRUST_AUTO},
    {"how much memory is free", "show_memory_usage", TRUST_AUTO},
    {"what processes are running", "list_processes", TRUST_AUTO},
    {"show me cpu usage", "show_cpu_info", TRUST_AUTO},
    {"search for file", "find_file_interactive", TRUST_AUTO},
    {"find text in files", "grep_interactive", TRUST_AUTO},
    {"copy file to", "copy_file_interactive", TRUST_NOTIFY},
    {"move file to", "move_file_interactive", TRUST_NOTIFY},
    {"delete file", "delete_file_interactive", TRUST_REQUEST},
    {"remove directory", "remove_directory_interactive", TRUST_REQUEST},

    /* Common Tasks */
    {"compress file", "compress_file_interactive", TRUST_NOTIFY},
    {"extract archive", "extract_archive_interactive", TRUST_NOTIFY},
    {"download file", "download_file_interactive", TRUST_REQUEST},
    {"upload file", "upload_file_interactive", TRUST_REQUEST},
    {"create backup", "create_backup_interactive", TRUST_NOTIFY},
    {"restore backup", "restore_backup_interactive", TRUST_REQUEST},

    /* Development */
    {"run tests", "exec: pytest", TRUST_AUTO},
    {"build project", "exec: make build", TRUST_NOTIFY},
    {"clean build", "exec: make clean", TRUST_NOTIFY},
    {"start server", "start_dev_server", TRUST_NOTIFY},
    {"stop server", "stop_dev_server", TRUST_NOTIFY},

    /* Week 20: File operation variations (20 patterns) */
    {"show me files", "list_directory", TRUST_AUTO},
    {"what files are in", "list_directory", TRUST_AUTO},
    {"list directory", "list_directory", TRUST_AUTO},
    {"display file", "show_file_contents", TRUST_AUTO},
    {"show file contents", "show_file_contents", TRUST_AUTO},
    {"read file", "show_file_contents", TRUST_AUTO},
    {"create folder", "directory_create", TRUST_NOTIFY},
    {"make directory", "directory_create", TRUST_NOTIFY},
    {"new folder", "directory_create", TRUST_NOTIFY},
    {"delete file", "file_delete", TRUST_REQUEST},
    {"remove file", "file_delete", TRUST_REQUEST},
    {"erase file", "file_delete", TRUST_REQUEST},
    {"cat file", "show_file_contents", TRUST_AUTO},
    {"mkdir dir", "directory_create", TRUST_NOTIFY},
    {"rm file", "file_delete", TRUST_REQUEST},
    {"ls dir", "list_directory", TRUST_AUTO},
    {"ls .", "list_directory: current", TRUST_AUTO},
    {"ls ..", "list_directory: parent", TRUST_AUTO},
    {"show directory", "list_directory", TRUST_AUTO},
    {"list folder", "list_directory", TRUST_AUTO},

    /* Week 20: Process management variations (15 patterns) */
    {"what processes are running", "show_processes", TRUST_AUTO},
    {"show me processes", "show_processes", TRUST_AUTO},
    {"list tasks", "show_processes", TRUST_AUTO},
    {"kill process", "process_kill", TRUST_REQUEST},
    {"stop process", "process_kill", TRUST_REQUEST},
    {"terminate process", "process_kill", TRUST_REQUEST},
    {"show top processes", "show_top_processes", TRUST_AUTO},
    {"cpu usage", "show_top_processes", TRUST_AUTO},
    {"process list", "show_processes", TRUST_AUTO},
    {"task list", "show_processes", TRUST_AUTO},
    {"process monitor", "show_top_processes", TRUST_AUTO},
    {"kill pid", "process_kill", TRUST_REQUEST},
    {"end process", "process_kill", TRUST_REQUEST},
    {"top processes", "show_top_processes", TRUST_AUTO},
    {"show running", "show_processes", TRUST_AUTO},

    /* Week 20: System control variations (20 patterns) */
    {"turn off system", "system_shutdown", TRUST_REQUIRE},
    {"power down", "system_shutdown", TRUST_REQUIRE},
    {"shut down computer", "system_shutdown", TRUST_REQUIRE},
    {"shutdown now", "system_shutdown", TRUST_REQUIRE},
    {"power off", "system_shutdown", TRUST_REQUIRE},
    {"restart system", "system_reboot", TRUST_REQUIRE},
    {"restart computer", "system_reboot", TRUST_REQUIRE},
    {"reboot now", "system_reboot", TRUST_REQUIRE},
    {"check battery", "battery_status", TRUST_AUTO},
    {"battery level", "battery_status", TRUST_AUTO},
    {"how much battery", "battery_status", TRUST_AUTO},
    {"how long up", "system_uptime", TRUST_AUTO},
    {"how long running", "system_uptime", TRUST_AUTO},
    {"system uptime", "system_uptime", TRUST_AUTO},
    {"show uptime", "system_uptime", TRUST_AUTO},
    {"shut down", "system_shutdown", TRUST_REQUIRE},
    {"re boot", "system_reboot", TRUST_REQUIRE},
    {"battrey", "battery_status", TRUST_AUTO},
    {"batery", "battery_status", TRUST_AUTO},
    {"battery info", "battery_status", TRUST_AUTO},

    /* Week 20: Network variations (15 patterns) */
    {"network status", "show_network", TRUST_AUTO},
    {"ip address", "show_network", TRUST_AUTO},
    {"show network", "show_network", TRUST_AUTO},
    {"check connectivity", "ping_host", TRUST_AUTO},
    {"test connection", "ping_host", TRUST_AUTO},
    {"ping google", "ping: google.com", TRUST_AUTO},
    {"ping 8.8.8.8", "ping: 8.8.8.8", TRUST_AUTO},
    {"show ip", "show_network", TRUST_AUTO},
    {"network info", "show_network", TRUST_AUTO},
    {"network interfaces", "show_network", TRUST_AUTO},
    {"show interfaces", "show_network", TRUST_AUTO},
    {"ping test", "ping: 8.8.8.8", TRUST_AUTO},
    {"network config", "show_network", TRUST_AUTO},
    {"ip config", "show_network", TRUST_AUTO},
    {"show mac", "show_network", TRUST_AUTO},

    /* Week 20: Command synonyms (20 patterns) */
    {"dir", "list_directory", TRUST_AUTO},
    {"type", "show_file_contents", TRUST_AUTO},
    {"del", "file_delete", TRUST_REQUEST},
    {"md", "directory_create", TRUST_NOTIFY},
    {"rd", "directory_delete", TRUST_REQUEST},
    {"tasklist", "show_processes", TRUST_AUTO},
    {"taskkill", "process_kill", TRUST_REQUEST},
    {"netstat", "show_network", TRUST_AUTO},
    {"ipconfig", "show_network", TRUST_AUTO},
    {"poweroff", "system_shutdown", TRUST_REQUIRE},
    {"halt", "system_shutdown", TRUST_REQUIRE},
    {"cls", "clear_screen", TRUST_AUTO},
    {"copy", "copy_file", TRUST_NOTIFY},
    {"move", "move_file", TRUST_NOTIFY},
    {"ren", "rename_file", TRUST_NOTIFY},
    {"more", "show_file_contents", TRUST_AUTO},
    {"tree", "show_directory_tree", TRUST_AUTO},
    {"rmdir", "directory_delete", TRUST_REQUEST},
    {"chdir", "change_directory", TRUST_AUTO},
    {"path", "show_path", TRUST_AUTO},

    /* Week 24: Storage & Block Device Operations (65 patterns) */
    /* Block Device Information (15 patterns) - TRUST_AUTO */
    {"show disks", "show_block_devices", TRUST_AUTO},
    {"list disks", "show_block_devices", TRUST_AUTO},
    {"show block devices", "show_block_devices", TRUST_AUTO},
    {"lsblk", "show_block_devices", TRUST_AUTO},
    {"disk info", "show_disk_info", TRUST_AUTO},
    {"storage info", "show_storage_info", TRUST_AUTO},
    {"show partitions", "show_partitions", TRUST_AUTO},
    {"list partitions", "show_partitions", TRUST_AUTO},
    {"fdisk -l", "show_partitions", TRUST_AUTO},
    {"blkid", "show_block_device_ids", TRUST_AUTO},
    {"show driver status", "show_driver_status", TRUST_AUTO},
    {"driver info", "show_driver_status", TRUST_AUTO},
    {"virtio status", "show_virtio_status", TRUST_AUTO},
    {"disk usage", "show_disk_usage", TRUST_AUTO},
    {"show storage", "show_storage_info", TRUST_AUTO},

    /* Disk/Storage Queries (10 patterns) - TRUST_AUTO */
    {"how much disk space", "show_disk_usage", TRUST_AUTO},
    {"check disk space", "show_disk_usage", TRUST_AUTO},
    {"storage capacity", "show_storage_capacity", TRUST_AUTO},
    {"disk capacity", "show_storage_capacity", TRUST_AUTO},
    {"free space", "show_disk_usage", TRUST_AUTO},
    {"disk full", "show_disk_usage", TRUST_AUTO},
    {"storage available", "show_disk_usage", TRUST_AUTO},
    {"show mounts", "show_mounted_filesystems", TRUST_AUTO},
    {"mount points", "show_mounted_filesystems", TRUST_AUTO},
    {"what is mounted", "show_mounted_filesystems", TRUST_AUTO},

    /* Mount/Unmount Operations (10 patterns) - TRUST_REQUEST */
    {"mount disk", "mount_interactive", TRUST_REQUEST},
    {"mount device", "mount_interactive", TRUST_REQUEST},
    {"mount partition", "mount_interactive", TRUST_REQUEST},
    {"unmount disk", "unmount_interactive", TRUST_REQUEST},
    {"unmount device", "unmount_interactive", TRUST_REQUEST},
    {"umount disk", "unmount_interactive", TRUST_REQUEST},
    {"eject disk", "eject_device", TRUST_REQUEST},
    {"mount usb", "mount_usb_interactive", TRUST_REQUEST},
    {"unmount usb", "unmount_usb_interactive", TRUST_REQUEST},
    {"safely remove", "eject_device", TRUST_REQUEST},

    /* File System Operations (10 patterns) - TRUST_REQUIRE */
    {"format disk", "format_disk_interactive", TRUST_REQUIRE},
    {"mkfs", "create_filesystem_interactive", TRUST_REQUIRE},
    {"create filesystem", "create_filesystem_interactive", TRUST_REQUIRE},
    {"format partition", "format_partition_interactive", TRUST_REQUIRE},
    {"check filesystem", "fsck_interactive", TRUST_REQUEST},
    {"fsck", "fsck_interactive", TRUST_REQUEST},
    {"repair filesystem", "fsck_interactive", TRUST_REQUEST},
    {"fix disk errors", "fsck_interactive", TRUST_REQUEST},
    {"resize partition", "resize_partition_interactive", TRUST_REQUIRE},
    {"extend partition", "extend_partition_interactive", TRUST_REQUIRE},

    /* Partition Management (8 patterns) - TRUST_REQUIRE */
    {"create partition", "create_partition_interactive", TRUST_REQUIRE},
    {"delete partition", "delete_partition_interactive", TRUST_REQUIRE},
    {"remove partition", "delete_partition_interactive", TRUST_REQUIRE},
    {"partition disk", "partition_disk_interactive", TRUST_REQUIRE},
    {"fdisk", "fdisk_interactive", TRUST_REQUIRE},
    {"parted", "parted_interactive", TRUST_REQUIRE},
    {"gparted", "launch_gparted", TRUST_REQUIRE},
    {"disk utility", "launch_disk_utility", TRUST_REQUIRE},

    /* Backup & Restore (6 patterns) - TRUST_REQUEST */
    {"backup disk", "backup_disk_interactive", TRUST_REQUEST},
    {"restore disk", "restore_disk_interactive", TRUST_REQUEST},
    {"clone disk", "clone_disk_interactive", TRUST_REQUEST},
    {"dd disk", "dd_interactive", TRUST_REQUIRE},
    {"create image", "create_disk_image", TRUST_REQUEST},
    {"restore image", "restore_disk_image", TRUST_REQUEST},

    /* SMART & Diagnostics (6 patterns) - TRUST_AUTO */
    {"smart status", "show_smart_status", TRUST_AUTO},
    {"disk health", "show_disk_health", TRUST_AUTO},
    {"check disk health", "show_disk_health", TRUST_AUTO},
    {"smartctl", "smartctl_interactive", TRUST_AUTO},
    {"disk temperature", "show_disk_temperature", TRUST_AUTO},
    {"disk errors", "show_disk_errors", TRUST_AUTO}
};

#define NUM_EXTENDED_PATTERNS (sizeof(extended_patterns) / sizeof(cache_pattern_t))
#define TOTAL_PATTERNS (NUM_INITIAL_PATTERNS + NUM_EXTENDED_PATTERNS)

/**
 * Load initial patterns into the cache
 * Returns number of patterns successfully loaded
 */
int cache_load_initial_patterns(decision_cache_t *cache)
{
    if (!cache) {
        return 0;
    }

    int loaded = 0;

    /* Load initial patterns */
    for (size_t i = 0; i < NUM_INITIAL_PATTERNS; i++) {
        if (cache_insert(cache,
                        initial_patterns[i].query,
                        initial_patterns[i].action,
                        initial_patterns[i].trust)) {
            loaded++;
        }
    }

    printf("Loaded %d initial patterns into cache\n", loaded);
    return loaded;
}

/**
 * Load extended patterns into the cache
 * Returns total number of patterns successfully loaded
 */
int cache_load_extended_patterns(decision_cache_t *cache)
{
    if (!cache) {
        return 0;
    }

    int loaded = 0;

    /* Load initial patterns first */
    loaded = cache_load_initial_patterns(cache);

    /* Load extended patterns */
    for (size_t i = 0; i < NUM_EXTENDED_PATTERNS; i++) {
        if (cache_insert(cache,
                        extended_patterns[i].query,
                        extended_patterns[i].action,
                        extended_patterns[i].trust)) {
            loaded++;
        }
    }

    printf("Loaded %d total patterns into cache\n", loaded);
    return loaded;
}

/**
 * Get total number of available patterns
 */
int cache_get_total_pattern_count(void)
{
    return TOTAL_PATTERNS;
}

/**
 * Print pattern statistics
 */
void cache_print_pattern_stats(void)
{
    printf("\n========== PATTERN DATABASE STATISTICS ==========\n");
    printf("Initial patterns:   %zu\n", NUM_INITIAL_PATTERNS);
    printf("Extended patterns:  %zu\n", NUM_EXTENDED_PATTERNS);
    printf("Total patterns:     %zu\n", (size_t)TOTAL_PATTERNS);
    printf("Cache capacity:     %d entries\n", CACHE_SIZE);

    if (TOTAL_PATTERNS > CACHE_SIZE) {
        printf("WARNING: Pattern count (%zu) exceeds cache size (%d)\n",
               (size_t)TOTAL_PATTERNS, CACHE_SIZE);
        printf("         Some patterns will not fit. Consider increasing CACHE_SIZE.\n");
    } else {
        printf("Cache utilization:  %.1f%%\n",
               (TOTAL_PATTERNS * 100.0) / CACHE_SIZE);
    }
    printf("================================================\n\n");
}
