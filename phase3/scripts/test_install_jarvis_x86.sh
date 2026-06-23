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

# ---- T2: --target esp / disk not implemented ----
ESP_OUT="$(bash "$INSTALLER" --target esp --usb /dev/sdz --skip-build --skip-model 2>&1)"; ESP_RC=$?
if [ "$ESP_RC" -ne 0 ]; then ok "T2 --target esp exits nonzero"; else bad "T2 --target esp exits nonzero (got 0)"; fi
assert_contains "T2 --target esp says not yet implemented" "$ESP_OUT" "not yet implemented"

DISK_OUT="$(bash "$INSTALLER" --target disk --usb /dev/sdz --skip-build --skip-model 2>&1)"; DISK_RC=$?
if [ "$DISK_RC" -ne 0 ]; then ok "T2 --target disk exits nonzero"; else bad "T2 --target disk exits nonzero (got 0)"; fi
assert_contains "T2 --target disk says not yet implemented" "$DISK_OUT" "not yet implemented"

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

# ---- Results ----
echo ""
echo "== Results: $PASS PASS, $FAIL FAIL =="
[ "$FAIL" -eq 0 ]
