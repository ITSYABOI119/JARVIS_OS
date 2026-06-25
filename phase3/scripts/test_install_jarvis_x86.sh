#!/bin/bash
# =============================================================================
# Host test for install_jarvis_x86.sh — runs on CI with NO box / NO devices.
#
# Sources the installer (source-guarded, so main() does not run) and exercises
# the pure helper functions plus a full --dry-run. Prints PASS/FAIL per check
# and exits nonzero if any check fails.
# =============================================================================

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALLER="$SCRIPT_DIR/install_jarvis_x86.sh"

PASS=0
FAIL=0

ok()   { echo "PASS: $1"; PASS=$((PASS + 1)); }
bad()  { echo "FAIL: $1"; FAIL=$((FAIL + 1)); }

# Assert a command succeeds (rc 0).
assert_rc0() {
    local desc="$1"; shift
    if "$@"; then ok "$desc"; else bad "$desc (expected rc 0, got $?)"; fi
}
# Assert a command fails (rc != 0).
assert_rcN() {
    local desc="$1"; shift
    if "$@"; then bad "$desc (expected nonzero, got 0)"; else ok "$desc"; fi
}
# Assert HAYSTACK contains NEEDLE.
assert_contains() {
    local desc="$1" haystack="$2" needle="$3"
    if printf '%s' "$haystack" | grep -qF -- "$needle"; then
        ok "$desc"
    else
        bad "$desc (missing: '$needle')"
    fi
}

# Source the installer to get its helper functions (main does NOT run).
# shellcheck source=/dev/null
source "$INSTALLER"
# The installer sets `set -euo pipefail`; the test deliberately runs commands
# that return nonzero, so turn OFF errexit/pipefail for the test body.
set +e +o pipefail

echo "== install_jarvis_x86.sh helper + dry-run tests =="
echo ""

# ---- T1: --help ----
HELP_OUT="$(bash "$INSTALLER" --help 2>&1)"; HELP_RC=$?
if [ "$HELP_RC" -eq 0 ]; then ok "T1 --help exits 0"; else bad "T1 --help exits 0 (got $HELP_RC)"; fi
assert_contains "T1 --help shows Usage"      "$HELP_OUT" "Usage"
assert_contains "T1 --help shows --usb"      "$HELP_OUT" "--usb"
assert_contains "T1 --help shows --model"    "$HELP_OUT" "--model"
assert_contains "T1 --help shows --target"   "$HELP_OUT" "--target"
assert_contains "T1 --help shows --dry-run"  "$HELP_OUT" "--dry-run"

# ---- T2: an UNKNOWN --target errors (usb/esp/disk are ALL implemented now) ----
BOGUS_OUT="$(bash "$INSTALLER" --target bogus --dry-run --skip-build --skip-model 2>&1)"; BOGUS_RC=$?
if [ "$BOGUS_RC" -ne 0 ]; then ok "T2 --target bogus exits nonzero"; else bad "T2 --target bogus exits nonzero (got 0)"; fi
assert_contains "T2 --target bogus says Unknown --target" "$BOGUS_OUT" "Unknown --target"

# ---- T3: jarvis_is_unsafe_usb_target ----
assert_rc0 "T3 /dev/nvme0n1 is unsafe"        jarvis_is_unsafe_usb_target /dev/nvme0n1
# /dev/sdz must NOT be flagged by the nvme rule. The root-disk check may match
# in odd CI mounts, but in practice /dev/sdz is not the root disk in CI.
assert_rcN "T3 /dev/sdz not flagged (nvme rule)" jarvis_is_unsafe_usb_target /dev/sdz

# ---- T4: jarvis_model_shortname_ok ----
assert_rc0 "T4 GEMMA2B.GUF ok"                       jarvis_model_shortname_ok "GEMMA2B.GUF"
assert_rc0 "T4 path/GEMMA2B.GUF ok"                  jarvis_model_shortname_ok "/some/dir/GEMMA2B.GUF"
assert_rcN "T4 gemma-4-E2B-it-Q4_K_M.gguf rejected"  jarvis_model_shortname_ok "gemma-4-E2B-it-Q4_K_M.gguf"
assert_rcN "T4 model.gguf rejected"                  jarvis_model_shortname_ok "model.gguf"

# ---- T5: jarvis_partition_start_ok ----
GOOD_PARTED="$(cat <<'EOF'
Model: NVMe Device (nvme)
Disk /dev/nvme0n1: 4000797360s
Number  Start        End          Size         File system  Name         Flags
 1      2048s        1050623s     1048576s     fat32        EFI          boot, esp
 4      32768s       4000797326s  3999764559s  fat32        JARVIS_DATA
EOF
)"
assert_rc0 "T5 JARVIS_DATA @ 32768s ok" jarvis_partition_start_ok "$GOOD_PARTED"

BAD_PARTED="$(cat <<'EOF'
Number  Start        End          Size         File system  Name         Flags
 4      34816s       4000797326s  3999762559s  fat32        JARVIS_DATA
EOF
)"
assert_rcN "T5 JARVIS_DATA @ 34816s rejected" jarvis_partition_start_ok "$BAD_PARTED"

# ---- T6: full --dry-run ----
DRY_OUT="$(bash "$INSTALLER" --dry-run --usb /dev/sdz --skip-build --skip-model 2>&1)"; DRY_RC=$?
if [ "$DRY_RC" -eq 0 ]; then ok "T6 --dry-run exits 0"; else bad "T6 --dry-run exits 0 (got $DRY_RC)"; fi
assert_contains "T6 --dry-run prints 'would:'" "$DRY_OUT" "would:"
assert_contains "T6 checklist: CSM"            "$DRY_OUT" "CSM"
assert_contains "T6 checklist: Secure Boot"    "$DRY_OUT" "Secure Boot"
assert_contains "T6 checklist: Other OS"       "$DRY_OUT" "Other OS"
# IOMMU or AMD-Vi (the installer prints "IOMMU / AMD-Vi").
if printf '%s' "$DRY_OUT" | grep -qE 'IOMMU|AMD-Vi'; then
    ok "T6 checklist: IOMMU/AMD-Vi"
else
    bad "T6 checklist: IOMMU/AMD-Vi (missing)"
fi
assert_contains "T6 checklist: 32768"             "$DRY_OUT" "32768"
assert_contains "T6 checklist: parse_nvme_log"    "$DRY_OUT" "parse_nvme_log"
assert_contains "T6 checklist: telemetry_receiver" "$DRY_OUT" "telemetry_receiver"

# ---- T7: --dry-run executes nothing destructive ----
STUB_DIR="$(mktemp -d)"
SENTINEL_DIR="$(mktemp -d)"
for tool in parted mkfs.fat grub-install dd; do
    cat > "$STUB_DIR/$tool" <<EOF
#!/bin/bash
touch "$SENTINEL_DIR/${tool}_was_called"
exit 0
EOF
    chmod +x "$STUB_DIR/$tool"
done
# Run a dry-run with the stub dir prepended to PATH.
PATH="$STUB_DIR:$PATH" bash "$INSTALLER" --dry-run --usb /dev/sdz --skip-build --skip-model >/dev/null 2>&1 || true
SENTINELS_FOUND="$(find "$SENTINEL_DIR" -type f 2>/dev/null | wc -l | tr -d ' ')"
if [ "$SENTINELS_FOUND" -eq 0 ]; then
    ok "T7 --dry-run ran no destructive tool (no sentinels)"
else
    bad "T7 --dry-run executed a destructive tool ($SENTINELS_FOUND sentinel(s) found)"
fi
rm -rf "$STUB_DIR" "$SENTINEL_DIR"

# ---- T8: --target esp dry-run (implemented; reversible dual-boot; touches nothing) ----
ESP_DRY="$(bash "$INSTALLER" --target esp --esp /dev/nvme0n1p4 --dry-run --skip-build --skip-model 2>&1)"; ESP_DRY_RC=$?
if [ "$ESP_DRY_RC" -eq 0 ]; then ok "T8 --target esp --dry-run exits 0"; else bad "T8 --target esp --dry-run exits 0 (got $ESP_DRY_RC)"; fi
assert_contains "T8 esp dry-run: copies to EFI/jarvis"      "$ESP_DRY" "EFI/jarvis"
assert_contains "T8 esp dry-run: grub-mkstandalone"         "$ESP_DRY" "grub-mkstandalone"
assert_contains "T8 esp dry-run: efibootmgr --create"       "$ESP_DRY" "efibootmgr --create"
assert_contains "T8 esp dry-run: keeps Ubuntu default"      "$ESP_DRY" "Ubuntu"
assert_contains "T8 esp dry-run: stale-delete is confirmed" "$ESP_DRY" "DELETE-STALE-ESP-MODEL"
# esp is implemented now — it must NOT print the disk 'not yet implemented' message.
if printf '%s' "$ESP_DRY" | grep -qF "not yet implemented"; then
    bad "T8 esp dry-run must NOT say 'not yet implemented'"
else
    ok "T8 esp dry-run does not say 'not yet implemented'"
fi

# T8c: esp dry-run executes NOTHING destructive (stub esp + usb tools; assert no sentinel).
STUB2="$(mktemp -d)"; SENT2="$(mktemp -d)"
for tool in grub-mkstandalone efibootmgr mount rm parted mkfs.fat grub-install; do
    cat > "$STUB2/$tool" <<EOF
#!/bin/bash
touch "$SENT2/${tool}_was_called"
exit 0
EOF
    chmod +x "$STUB2/$tool"
done
PATH="$STUB2:$PATH" bash "$INSTALLER" --target esp --esp /dev/nvme0n1p4 --dry-run --skip-build --skip-model >/dev/null 2>&1 || true
SENT2_FOUND="$(find "$SENT2" -type f 2>/dev/null | wc -l | tr -d ' ')"
if [ "$SENT2_FOUND" -eq 0 ]; then
    ok "T8 esp dry-run ran no destructive tool (no sentinels)"
else
    bad "T8 esp dry-run executed a destructive tool ($SENT2_FOUND sentinel(s) found)"
fi
rm -rf "$STUB2" "$SENT2"

# T8d: --target esp with NO --esp errors clearly.
NOESP_OUT="$(bash "$INSTALLER" --target esp --dry-run --skip-build --skip-model 2>&1)"; NOESP_RC=$?
if [ "$NOESP_RC" -ne 0 ]; then ok "T8 --target esp without --esp exits nonzero"; else bad "T8 --target esp without --esp exits nonzero (got 0)"; fi
assert_contains "T8 --target esp without --esp says --esp required" "$NOESP_OUT" "requires --esp"

# ---- T9: add_jarvis_boot_entry keeps Ubuntu default when it's the ONLY prior UEFI entry (F3 regression) ----
# F3 (shipped in d57a793, hit live on-box 2026-06-25): when BootOrder is just ubuntu+jarvis, grep -v
# matched all ids -> rest empty -> grep exit 1 -> under `set -euo pipefail` the rest= assignment ABORTED
# before `efibootmgr -o`, leaving JARVIS the default. The function calls the real efibootmgr (stub it),
# and the bug only aborts under errexit (the test body runs with set +e), so drive it in a set -e subshell.
STUB9="$(mktemp -d)"; SENT9="$(mktemp -d)"
cat > "$STUB9/efibootmgr" <<EOF
#!/bin/bash
case "\$1" in
  --create) exit 0 ;;
  -o)       printf '%s' "\$2" > "$SENT9/order"; exit 0 ;;
  -b)       exit 0 ;;
  *)        printf 'Boot0000* JARVIS seL4\n'
            printf 'Boot0001* ubuntu\n'
            printf 'BootOrder: 0000,0001\n'
            exit 0 ;;
esac
EOF
chmod +x "$STUB9/efibootmgr"
# JARVIS_BOOT_LABEL is already set by sourcing the installer (inherited here); add_jarvis_boot_entry
# does not read DRY_RUN — so neither is re-set (avoids a spurious SC2034 unused warning).
( set -euo pipefail
  PATH="$STUB9:$PATH" add_jarvis_boot_entry /dev/nvme0n1 4 ) >/dev/null 2>&1
T9_RC=$?
if [ "$T9_RC" -eq 0 ]; then ok "T9 add_jarvis_boot_entry did not abort under set -e (F3 fixed)"; else bad "T9 add_jarvis_boot_entry aborted under set -e (F3 regression, rc=$T9_RC)"; fi
T9_ORDER="$(cat "$SENT9/order" 2>/dev/null || echo MISSING)"
if [ "$T9_ORDER" = "0001,0000" ]; then ok "T9 BootOrder set Ubuntu-first (0001,0000)"; else bad "T9 BootOrder = '$T9_ORDER' (expected 0001,0000 = ubuntu first)"; fi
rm -rf "$STUB9" "$SENT9"

# ---- T10: --target disk dry-run (full single-OS wipe; implemented; touches nothing) ----
DISK_DRY="$(bash "$INSTALLER" --target disk --disk /dev/sdz --dry-run --skip-build --skip-model 2>&1)"; DISK_DRY_RC=$?
if [ "$DISK_DRY_RC" -eq 0 ]; then ok "T10 --target disk --dry-run exits 0"; else bad "T10 --target disk --dry-run exits 0 (got $DISK_DRY_RC)"; fi
assert_contains "T10 disk dry-run: JARVIS_DATA"             "$DISK_DRY" "JARVIS_DATA"
assert_contains "T10 disk dry-run: start sector 32768s"     "$DISK_DRY" "32768s"
assert_contains "T10 disk dry-run: WIPE-AND-INSTALL-JARVIS" "$DISK_DRY" "WIPE-AND-INSTALL-JARVIS"
assert_contains "T10 disk dry-run: mklabel gpt"             "$DISK_DRY" "mklabel gpt"
if printf '%s' "$DISK_DRY" | grep -qF "not yet implemented"; then
    bad "T10 disk dry-run must NOT say 'not yet implemented'"
else
    ok "T10 disk dry-run does not say 'not yet implemented'"
fi

# T10c: disk dry-run executes NOTHING destructive (stub every wipe/partition tool; assert no sentinel).
STUB10="$(mktemp -d)"; SENT10="$(mktemp -d)"
for tool in parted mkfs.fat grub-install sgdisk wipefs mount rm partprobe; do
    cat > "$STUB10/$tool" <<EOF
#!/bin/bash
touch "$SENT10/${tool}_was_called"
exit 0
EOF
    chmod +x "$STUB10/$tool"
done
PATH="$STUB10:$PATH" bash "$INSTALLER" --target disk --disk /dev/sdz --dry-run --skip-build --skip-model >/dev/null 2>&1 || true
SENT10_FOUND="$(find "$SENT10" -type f 2>/dev/null | wc -l | tr -d ' ')"
if [ "$SENT10_FOUND" -eq 0 ]; then
    ok "T10 disk dry-run ran no destructive tool (no sentinels)"
else
    bad "T10 disk dry-run executed a destructive tool ($SENT10_FOUND sentinel(s) found)"
fi
rm -rf "$STUB10" "$SENT10"

# T10d: --target disk with NO --disk errors clearly.
NODISK_OUT="$(bash "$INSTALLER" --target disk --dry-run --skip-build --skip-model 2>&1)"; NODISK_RC=$?
if [ "$NODISK_RC" -ne 0 ]; then ok "T10 --target disk without --disk exits nonzero"; else bad "T10 --target disk without --disk exits nonzero (got 0)"; fi
assert_contains "T10 --target disk without --disk says --disk required" "$NODISK_OUT" "requires --disk"

# T10e: guard refuses a PARTITION path (must be a WHOLE disk).
PART_OUT="$(bash "$INSTALLER" --target disk --disk /dev/sdz1 --dry-run --skip-build --skip-model 2>&1)"; PART_RC=$?
if [ "$PART_RC" -ne 0 ]; then ok "T10 --target disk refuses a partition path (/dev/sdz1)"; else bad "T10 --target disk accepted a partition path (should refuse)"; fi
assert_contains "T10 partition-path refusal is a REFUSED guard" "$PART_OUT" "REFUSED"

# T10f: guard refuses the booted/root disk (if derivable in this env).
ROOTDISK="$(root_fs_disk)"
if [ -n "$ROOTDISK" ]; then
    ROOT_OUT="$(bash "$INSTALLER" --target disk --disk "$ROOTDISK" --dry-run --skip-build --skip-model 2>&1)"; ROOT_RC=$?
    if [ "$ROOT_RC" -ne 0 ]; then ok "T10 --target disk refuses the booted/root disk ($ROOTDISK)"; else bad "T10 --target disk accepted the root disk $ROOTDISK (should refuse)"; fi
    assert_contains "T10 root-disk refusal is a REFUSED guard" "$ROOT_OUT" "REFUSED"
else
    ok "T10 root-disk refusal skipped (root_fs_disk empty in this env)"
fi

# ---- Results ----
echo ""
echo "== Results: $PASS PASS, $FAIL FAIL =="
[ "$FAIL" -eq 0 ]
