#! /bin/sh

set -e

. ${0%/*}/functions.sh

exec ${SCRIPT_DIR}/build_sel4.sh rpi4_jarvis projects/jarvis-sel4
