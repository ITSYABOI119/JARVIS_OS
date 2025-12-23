#!/usr/bin/env python3
"""
JARVIS AI-OS - SHIELD Action Database
Week 16: SHIELD Expansion

This module defines the comprehensive action database with 100 action types
across 10 categories, including base risk scores, reversibility flags, and
metadata for context-aware risk scoring.

Author: JARVIS AI-OS Team
Date: November 24, 2025
"""

from typing import Dict, List, NamedTuple
from enum import Enum


class ActionScope(Enum):
    """Scope of action impact"""
    LOCAL = "local"           # Single file/process
    MODERATE = "moderate"     # Multiple files/processes
    GLOBAL = "global"         # System-wide impact


class ActionMetadata(NamedTuple):
    """Metadata for an action type"""
    name: str
    category: str
    base_risk: float          # 0.0-1.0
    reversible: bool
    hardware_impact: bool
    scope: ActionScope
    description: str
    patterns: List[str]       # Wildcard patterns for matching


# Action Database (100 types across 10 categories)
ACTION_DATABASE: Dict[str, ActionMetadata] = {
    # ===== FILE OPERATIONS (10 types) =====
    "file_read": ActionMetadata(
        name="file_read",
        category="file_operations",
        base_risk=0.1,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Read contents of a file",
        patterns=["read_*", "cat_*", "view_*"]
    ),

    "file_write": ActionMetadata(
        name="file_write",
        category="file_operations",
        base_risk=0.3,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Write or modify a file",
        patterns=["write_*", "edit_*", "modify_*"]
    ),

    "file_delete": ActionMetadata(
        name="file_delete",
        category="file_operations",
        base_risk=0.7,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Delete a file",
        patterns=["delete_*", "rm_*", "remove_*"]
    ),

    "file_move": ActionMetadata(
        name="file_move",
        category="file_operations",
        base_risk=0.4,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Move or rename a file",
        patterns=["move_*", "mv_*", "rename_*"]
    ),

    "file_copy": ActionMetadata(
        name="file_copy",
        category="file_operations",
        base_risk=0.2,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Copy a file",
        patterns=["copy_*", "cp_*", "duplicate_*"]
    ),

    "dir_create": ActionMetadata(
        name="dir_create",
        category="file_operations",
        base_risk=0.2,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Create a directory",
        patterns=["mkdir_*", "create_dir_*"]
    ),

    "dir_delete": ActionMetadata(
        name="dir_delete",
        category="file_operations",
        base_risk=0.8,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Delete a directory (recursive)",
        patterns=["rmdir_*", "delete_dir_*", "rm_rf_*"]
    ),

    "file_chmod": ActionMetadata(
        name="file_chmod",
        category="file_operations",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Change file permissions",
        patterns=["chmod_*", "permissions_*"]
    ),

    "file_chown": ActionMetadata(
        name="file_chown",
        category="file_operations",
        base_risk=0.6,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Change file ownership",
        patterns=["chown_*", "ownership_*"]
    ),

    "file_link": ActionMetadata(
        name="file_link",
        category="file_operations",
        base_risk=0.3,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Create symbolic or hard link",
        patterns=["link_*", "ln_*", "symlink_*"]
    ),

    # ===== SYSTEM CONTROL (10 types) =====
    "system_shutdown": ActionMetadata(
        name="system_shutdown",
        category="system_control",
        base_risk=0.9,
        reversible=False,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Shutdown the system",
        patterns=["shutdown_*", "poweroff_*", "halt_*"]
    ),

    "system_reboot": ActionMetadata(
        name="system_reboot",
        category="system_control",
        base_risk=0.85,
        reversible=False,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Reboot the system",
        patterns=["reboot_*", "restart_*"]
    ),

    "system_suspend": ActionMetadata(
        name="system_suspend",
        category="system_control",
        base_risk=0.3,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Suspend system to RAM (S3)",
        patterns=["suspend_*", "sleep_*"]
    ),

    "system_hibernate": ActionMetadata(
        name="system_hibernate",
        category="system_control",
        base_risk=0.7,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Hibernate system to disk",
        patterns=["hibernate_*"]
    ),

    "kernel_module_load": ActionMetadata(
        name="kernel_module_load",
        category="system_control",
        base_risk=0.75,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Load kernel module",
        patterns=["modprobe_*", "insmod_*", "load_module_*"]
    ),

    "kernel_module_unload": ActionMetadata(
        name="kernel_module_unload",
        category="system_control",
        base_risk=0.8,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Unload kernel module",
        patterns=["rmmod_*", "unload_module_*"]
    ),

    "system_time_set": ActionMetadata(
        name="system_time_set",
        category="system_control",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Set system time/date",
        patterns=["set_time_*", "set_date_*"]
    ),

    "system_hostname_set": ActionMetadata(
        name="system_hostname_set",
        category="system_control",
        base_risk=0.4,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Change system hostname",
        patterns=["set_hostname_*", "hostname_*"]
    ),

    "system_locale_set": ActionMetadata(
        name="system_locale_set",
        category="system_control",
        base_risk=0.3,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Change system locale/language",
        patterns=["set_locale_*", "locale_*"]
    ),

    "system_timezone_set": ActionMetadata(
        name="system_timezone_set",
        category="system_control",
        base_risk=0.3,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Change system timezone",
        patterns=["set_timezone_*", "timezone_*"]
    ),

    # ===== PROCESS MANAGEMENT (10 types) =====
    "process_start": ActionMetadata(
        name="process_start",
        category="process_management",
        base_risk=0.4,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Start a new process",
        patterns=["start_*", "exec_*", "run_*", "launch_*"]
    ),

    "process_kill": ActionMetadata(
        name="process_kill",
        category="process_management",
        base_risk=0.6,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Kill a running process",
        patterns=["kill_*", "terminate_*", "stop_*"]
    ),

    "process_pause": ActionMetadata(
        name="process_pause",
        category="process_management",
        base_risk=0.3,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Pause/suspend a process",
        patterns=["pause_*", "suspend_process_*"]
    ),

    "process_resume": ActionMetadata(
        name="process_resume",
        category="process_management",
        base_risk=0.2,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Resume a paused process",
        patterns=["resume_*", "continue_*"]
    ),

    "process_nice": ActionMetadata(
        name="process_nice",
        category="process_management",
        base_risk=0.4,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Change process priority",
        patterns=["nice_*", "renice_*", "priority_*"]
    ),

    "process_affinity": ActionMetadata(
        name="process_affinity",
        category="process_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.LOCAL,
        description="Set CPU affinity for process",
        patterns=["affinity_*", "taskset_*", "cpu_bind_*"]
    ),

    "process_rlimit": ActionMetadata(
        name="process_rlimit",
        category="process_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Set resource limits for process",
        patterns=["ulimit_*", "limit_*", "rlimit_*"]
    ),

    "process_list": ActionMetadata(
        name="process_list",
        category="process_management",
        base_risk=0.1,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="List running processes",
        patterns=["ps*", "list_processes*", "top*", "ps", "top", "process_list"]
    ),

    "process_info": ActionMetadata(
        name="process_info",
        category="process_management",
        base_risk=0.1,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Get process information",
        patterns=["pinfo*", "proc_info*", "process_status*", "process_info", "process_status"]
    ),

    "process_trace": ActionMetadata(
        name="process_trace",
        category="process_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Trace/debug a process",
        patterns=["strace_*", "trace_*", "debug_*"]
    ),

    # ===== NETWORK OPERATIONS (10 types) =====
    "network_connect": ActionMetadata(
        name="network_connect",
        category="network_operations",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Establish network connection",
        patterns=["connect_*", "tcp_connect_*", "socket_connect_*"]
    ),

    "network_listen": ActionMetadata(
        name="network_listen",
        category="network_operations",
        base_risk=0.6,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Listen on network port",
        patterns=["listen_*", "bind_*", "server_start_*"]
    ),

    "network_send": ActionMetadata(
        name="network_send",
        category="network_operations",
        base_risk=0.4,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Send data over network",
        patterns=["send_*", "transmit_*", "upload_*"]
    ),

    "network_receive": ActionMetadata(
        name="network_receive",
        category="network_operations",
        base_risk=0.3,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Receive data from network",
        patterns=["receive_*", "recv_*", "download_*"]
    ),

    "firewall_rule_add": ActionMetadata(
        name="firewall_rule_add",
        category="network_operations",
        base_risk=0.7,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Add firewall rule",
        patterns=["firewall_add_*", "iptables_add_*", "fw_rule_*"]
    ),

    "firewall_rule_delete": ActionMetadata(
        name="firewall_rule_delete",
        category="network_operations",
        base_risk=0.8,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Delete firewall rule",
        patterns=["firewall_delete_*", "iptables_delete_*"]
    ),

    "network_interface_up": ActionMetadata(
        name="network_interface_up",
        category="network_operations",
        base_risk=0.5,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.MODERATE,
        description="Bring network interface up",
        patterns=["ifup_*", "interface_up_*", "net_enable_*"]
    ),

    "network_interface_down": ActionMetadata(
        name="network_interface_down",
        category="network_operations",
        base_risk=0.7,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.MODERATE,
        description="Bring network interface down",
        patterns=["ifdown_*", "interface_down_*", "net_disable_*"]
    ),

    "network_route_add": ActionMetadata(
        name="network_route_add",
        category="network_operations",
        base_risk=0.6,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Add network route",
        patterns=["route_add_*", "ip_route_*"]
    ),

    "network_dns_set": ActionMetadata(
        name="network_dns_set",
        category="network_operations",
        base_risk=0.7,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Set DNS servers",
        patterns=["dns_set_*", "nameserver_*", "resolv_*"]
    ),

    # ===== MEMORY MANAGEMENT (10 types) =====
    "memory_allocate": ActionMetadata(
        name="memory_allocate",
        category="memory_management",
        base_risk=0.3,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Allocate memory",
        patterns=["malloc_*", "alloc_*", "mmap_*"]
    ),

    "memory_free": ActionMetadata(
        name="memory_free",
        category="memory_management",
        base_risk=0.4,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Free allocated memory",
        patterns=["free_*", "munmap_*", "dealloc_*"]
    ),

    "memory_read": ActionMetadata(
        name="memory_read",
        category="memory_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Read from memory address",
        patterns=["mem_read_*", "peek_*"]
    ),

    "memory_write": ActionMetadata(
        name="memory_write",
        category="memory_management",
        base_risk=0.7,
        reversible=False,
        hardware_impact=True,
        scope=ActionScope.LOCAL,
        description="Write to memory address",
        patterns=["mem_write_*", "poke_*"]
    ),

    "swap_enable": ActionMetadata(
        name="swap_enable",
        category="memory_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Enable swap space",
        patterns=["swapon_*", "swap_enable_*"]
    ),

    "swap_disable": ActionMetadata(
        name="swap_disable",
        category="memory_management",
        base_risk=0.6,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Disable swap space",
        patterns=["swapoff_*", "swap_disable_*"]
    ),

    "cache_drop": ActionMetadata(
        name="cache_drop",
        category="memory_management",
        base_risk=0.5,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Drop filesystem caches",
        patterns=["drop_caches_*", "clear_cache_*"]
    ),

    "huge_pages_set": ActionMetadata(
        name="huge_pages_set",
        category="memory_management",
        base_risk=0.4,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Configure huge pages",
        patterns=["hugepages_*", "thp_*"]
    ),

    "oom_adjust": ActionMetadata(
        name="oom_adjust",
        category="memory_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Adjust OOM killer score",
        patterns=["oom_score_*", "oom_adj_*"]
    ),

    "memory_compact": ActionMetadata(
        name="memory_compact",
        category="memory_management",
        base_risk=0.4,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Trigger memory compaction",
        patterns=["compact_*", "defrag_*"]
    ),

    # ===== SERVICE MANAGEMENT (10 types) =====
    "service_start": ActionMetadata(
        name="service_start",
        category="service_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Start a system service",
        patterns=["systemctl_start_*", "service_start_*"]
    ),

    "service_stop": ActionMetadata(
        name="service_stop",
        category="service_management",
        base_risk=0.6,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Stop a system service",
        patterns=["systemctl_stop_*", "service_stop_*"]
    ),

    "service_restart": ActionMetadata(
        name="service_restart",
        category="service_management",
        base_risk=0.6,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Restart a system service",
        patterns=["systemctl_restart_*", "service_restart_*"]
    ),

    "service_enable": ActionMetadata(
        name="service_enable",
        category="service_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Enable service at boot",
        patterns=["systemctl_enable_*", "service_enable_*"]
    ),

    "service_disable": ActionMetadata(
        name="service_disable",
        category="service_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Disable service at boot",
        patterns=["systemctl_disable_*", "service_disable_*"]
    ),

    "service_reload": ActionMetadata(
        name="service_reload",
        category="service_management",
        base_risk=0.4,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Reload service configuration",
        patterns=["systemctl_reload_*", "service_reload_*"]
    ),

    "service_status": ActionMetadata(
        name="service_status",
        category="service_management",
        base_risk=0.1,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Check service status",
        patterns=["systemctl_status*", "service_status*", "service_status", "systemctl_status"]
    ),

    "daemon_reload": ActionMetadata(
        name="daemon_reload",
        category="service_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Reload systemd daemon",
        patterns=["daemon_reload_*", "systemctl_daemon_reload_*"]
    ),

    "service_mask": ActionMetadata(
        name="service_mask",
        category="service_management",
        base_risk=0.7,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Mask service (prevent start)",
        patterns=["systemctl_mask_*", "service_mask_*"]
    ),

    "service_unmask": ActionMetadata(
        name="service_unmask",
        category="service_management",
        base_risk=0.4,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Unmask service",
        patterns=["systemctl_unmask_*", "service_unmask_*"]
    ),

    # ===== HARDWARE CONTROL (10 types) =====
    "cpu_frequency_set": ActionMetadata(
        name="cpu_frequency_set",
        category="hardware_control",
        base_risk=0.6,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Set CPU frequency/governor",
        patterns=["cpufreq_*", "governor_*", "cpu_freq_*"]
    ),

    "gpu_frequency_set": ActionMetadata(
        name="gpu_frequency_set",
        category="hardware_control",
        base_risk=0.6,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Set GPU frequency",
        patterns=["gpu_freq_*", "nvidia_smi_*"]
    ),

    "backlight_set": ActionMetadata(
        name="backlight_set",
        category="hardware_control",
        base_risk=0.2,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.LOCAL,
        description="Set screen brightness",
        patterns=["backlight_*", "brightness_*"]
    ),

    "fan_speed_set": ActionMetadata(
        name="fan_speed_set",
        category="hardware_control",
        base_risk=0.7,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Set fan speed",
        patterns=["fan_*", "pwm_*"]
    ),

    "power_profile_set": ActionMetadata(
        name="power_profile_set",
        category="hardware_control",
        base_risk=0.4,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Set power profile",
        patterns=["power_profile_*", "performance_*", "powersave_*"]
    ),

    "usb_device_reset": ActionMetadata(
        name="usb_device_reset",
        category="hardware_control",
        base_risk=0.5,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.LOCAL,
        description="Reset USB device",
        patterns=["usb_reset_*", "usb_power_*"]
    ),

    "disk_spindown": ActionMetadata(
        name="disk_spindown",
        category="hardware_control",
        base_risk=0.4,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.LOCAL,
        description="Spin down disk",
        patterns=["hdparm_*", "disk_sleep_*"]
    ),

    "audio_volume_set": ActionMetadata(
        name="audio_volume_set",
        category="hardware_control",
        base_risk=0.2,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Set audio volume",
        patterns=["volume_*", "alsa_*", "amixer_*"]
    ),

    "bluetooth_power": ActionMetadata(
        name="bluetooth_power",
        category="hardware_control",
        base_risk=0.5,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Control Bluetooth power",
        patterns=["bluetooth_*", "bt_power_*"]
    ),

    "wifi_power": ActionMetadata(
        name="wifi_power",
        category="hardware_control",
        base_risk=0.6,
        reversible=True,
        hardware_impact=True,
        scope=ActionScope.GLOBAL,
        description="Control WiFi power",
        patterns=["wifi_*", "rfkill_*"]
    ),

    # ===== USER MANAGEMENT (10 types) =====
    "user_create": ActionMetadata(
        name="user_create",
        category="user_management",
        base_risk=0.7,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Create user account",
        patterns=["useradd_*", "adduser_*", "create_user_*"]
    ),

    "user_delete": ActionMetadata(
        name="user_delete",
        category="user_management",
        base_risk=0.8,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Delete user account",
        patterns=["userdel_*", "deluser_*", "remove_user_*"]
    ),

    "user_modify": ActionMetadata(
        name="user_modify",
        category="user_management",
        base_risk=0.6,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Modify user account",
        patterns=["usermod_*", "moduser_*"]
    ),

    "user_password_set": ActionMetadata(
        name="user_password_set",
        category="user_management",
        base_risk=0.7,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Set user password",
        patterns=["passwd_*", "chpasswd_*", "password_*"]
    ),

    "group_create": ActionMetadata(
        name="group_create",
        category="user_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Create user group",
        patterns=["groupadd_*", "addgroup_*"]
    ),

    "group_delete": ActionMetadata(
        name="group_delete",
        category="user_management",
        base_risk=0.6,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Delete user group",
        patterns=["groupdel_*", "delgroup_*"]
    ),

    "group_modify": ActionMetadata(
        name="group_modify",
        category="user_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Modify user group",
        patterns=["groupmod_*", "modgroup_*"]
    ),

    "sudo_grant": ActionMetadata(
        name="sudo_grant",
        category="user_management",
        base_risk=0.9,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Grant sudo privileges",
        patterns=["sudo_add_*", "visudo_*", "sudoers_*"]
    ),

    "sudo_revoke": ActionMetadata(
        name="sudo_revoke",
        category="user_management",
        base_risk=0.7,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Revoke sudo privileges",
        patterns=["sudo_remove_*", "sudoers_remove_*"]
    ),

    "user_lock": ActionMetadata(
        name="user_lock",
        category="user_management",
        base_risk=0.6,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.MODERATE,
        description="Lock user account",
        patterns=["userlock_*", "passwd_lock_*"]
    ),

    # ===== PACKAGE MANAGEMENT (10 types) =====
    "package_install": ActionMetadata(
        name="package_install",
        category="package_management",
        base_risk=0.6,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Install software package",
        patterns=["apt_install_*", "yum_install_*", "pacman_install_*"]
    ),

    "package_remove": ActionMetadata(
        name="package_remove",
        category="package_management",
        base_risk=0.7,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Remove software package",
        patterns=["apt_remove_*", "yum_remove_*", "pacman_remove_*"]
    ),

    "package_update": ActionMetadata(
        name="package_update",
        category="package_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Update package database",
        patterns=["apt_update_*", "yum_update_*"]
    ),

    "package_upgrade": ActionMetadata(
        name="package_upgrade",
        category="package_management",
        base_risk=0.6,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Upgrade packages",
        patterns=["apt_upgrade_*", "yum_upgrade_*", "dist_upgrade_*"]
    ),

    "package_search": ActionMetadata(
        name="package_search",
        category="package_management",
        base_risk=0.1,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Search for packages",
        patterns=["apt_search*", "yum_search*", "package_search"]
    ),

    "package_info": ActionMetadata(
        name="package_info",
        category="package_management",
        base_risk=0.1,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Get package information",
        patterns=["apt_show*", "rpm_query*", "package_info*", "package_info", "apt_show", "rpm_query"]
    ),

    "package_autoremove": ActionMetadata(
        name="package_autoremove",
        category="package_management",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Remove unused dependencies",
        patterns=["apt_autoremove_*", "yum_autoremove_*"]
    ),

    "repository_add": ActionMetadata(
        name="repository_add",
        category="package_management",
        base_risk=0.7,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Add package repository",
        patterns=["add_apt_repository_*", "yum_add_repo_*"]
    ),

    "repository_remove": ActionMetadata(
        name="repository_remove",
        category="package_management",
        base_risk=0.6,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Remove package repository",
        patterns=["remove_apt_repository_*", "yum_remove_repo_*"]
    ),

    "package_clean": ActionMetadata(
        name="package_clean",
        category="package_management",
        base_risk=0.3,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Clean package cache",
        patterns=["apt_clean_*", "yum_clean_*"]
    ),

    # ===== MONITORING/LOGGING (10 types) =====
    "log_read": ActionMetadata(
        name="log_read",
        category="monitoring_logging",
        base_risk=0.1,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Read system logs",
        patterns=["journalctl*", "dmesg*", "tail_log*", "log_read*", "journalctl"]
    ),

    "log_clear": ActionMetadata(
        name="log_clear",
        category="monitoring_logging",
        base_risk=0.5,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Clear system logs",
        patterns=["clear_log_*", "truncate_log_*"]
    ),

    "log_rotate": ActionMetadata(
        name="log_rotate",
        category="monitoring_logging",
        base_risk=0.3,
        reversible=False,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Rotate log files",
        patterns=["logrotate_*", "rotate_log_*"]
    ),

    "syslog_config": ActionMetadata(
        name="syslog_config",
        category="monitoring_logging",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Configure syslog",
        patterns=["rsyslog_*", "syslog_conf_*"]
    ),

    "monitor_cpu": ActionMetadata(
        name="monitor_cpu",
        category="monitoring_logging",
        base_risk=0.1,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Monitor CPU usage",
        patterns=["cpu_monitor*", "mpstat*", "monitor_cpu", "read_cpu_stats"]
    ),

    "monitor_memory": ActionMetadata(
        name="monitor_memory",
        category="monitoring_logging",
        base_risk=0.1,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Monitor memory usage",
        patterns=["mem_monitor*", "vmstat*", "monitor_memory", "read_memory_stats"]
    ),

    "monitor_disk": ActionMetadata(
        name="monitor_disk",
        category="monitoring_logging",
        base_risk=0.1,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Monitor disk I/O",
        patterns=["disk_monitor*", "iostat*", "monitor_disk"]
    ),

    "monitor_network": ActionMetadata(
        name="monitor_network",
        category="monitoring_logging",
        base_risk=0.1,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Monitor network traffic",
        patterns=["net_monitor*", "netstat*", "ss*", "monitor_network"]
    ),

    "perf_trace": ActionMetadata(
        name="perf_trace",
        category="monitoring_logging",
        base_risk=0.4,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.LOCAL,
        description="Performance tracing",
        patterns=["perf_*", "ftrace_*", "ebpf_*"]
    ),

    "audit_enable": ActionMetadata(
        name="audit_enable",
        category="monitoring_logging",
        base_risk=0.5,
        reversible=True,
        hardware_impact=False,
        scope=ActionScope.GLOBAL,
        description="Enable audit logging",
        patterns=["auditctl_*", "audit_enable_*"]
    ),
}


def get_action_by_name(action_name: str) -> ActionMetadata:
    """
    Get action metadata by name.

    Args:
        action_name: Action name (e.g., "file_read")

    Returns:
        ActionMetadata or None if not found
    """
    return ACTION_DATABASE.get(action_name)


def get_actions_by_category(category: str) -> List[ActionMetadata]:
    """
    Get all actions in a category.

    Args:
        category: Category name (e.g., "file_operations")

    Returns:
        List of ActionMetadata
    """
    return [action for action in ACTION_DATABASE.values() if action.category == category]


def get_all_categories() -> List[str]:
    """
    Get list of all categories.

    Returns:
        Sorted list of category names
    """
    categories = set(action.category for action in ACTION_DATABASE.values())
    return sorted(categories)


def get_database_stats() -> Dict[str, int]:
    """
    Get database statistics.

    Returns:
        Dictionary with counts
    """
    total = len(ACTION_DATABASE)
    by_category = {}
    reversible = 0
    hardware_impact = 0

    for action in ACTION_DATABASE.values():
        by_category[action.category] = by_category.get(action.category, 0) + 1
        if action.reversible:
            reversible += 1
        if action.hardware_impact:
            hardware_impact += 1

    return {
        "total_actions": total,
        "categories": len(by_category),
        "reversible": reversible,
        "hardware_impact": hardware_impact,
        "by_category": by_category
    }


if __name__ == "__main__":
    # Test database
    print("JARVIS AI-OS - SHIELD Action Database")
    print("=" * 70)

    stats = get_database_stats()
    print(f"\nDatabase Statistics:")
    print(f"  Total actions: {stats['total_actions']}")
    print(f"  Categories: {stats['categories']}")
    print(f"  Reversible: {stats['reversible']} ({stats['reversible']*100//stats['total_actions']}%)")
    print(f"  Hardware impact: {stats['hardware_impact']} ({stats['hardware_impact']*100//stats['total_actions']}%)")

    print(f"\nActions per category:")
    for cat, count in sorted(stats['by_category'].items()):
        print(f"  {cat}: {count}")

    print(f"\nSample actions:")
    for i, (name, action) in enumerate(list(ACTION_DATABASE.items())[:5]):
        print(f"\n{i+1}. {action.name}")
        print(f"   Category: {action.category}")
        print(f"   Base risk: {action.base_risk:.2f}")
        print(f"   Reversible: {action.reversible}")
        print(f"   Hardware: {action.hardware_impact}")
        print(f"   Scope: {action.scope.value}")
        print(f"   Patterns: {', '.join(action.patterns)}")
