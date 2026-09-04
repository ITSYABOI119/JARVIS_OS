#!/bin/bash
# JARVIS soak: one dated telemetry snapshot, outside the capture ring's write path.
# Exits 0 in BOTH outcomes; the journal line says which. Box off => no telemetry => timeout => no file.
#
# WHY A FRAME COUNT AND NOT [ -s "$F" ]: tcpdump writes the 24-byte pcap FILE HEADER before it
# captures anything, so a zero-packet run still leaves a NON-EMPTY file and -s calls it a success.
# Not hypothetical - the 2026-08 soak archive carries exactly such a 24-byte pcap00 artifact, and the
# first negative control of this script (2026-09-04, box parked, 300 s) logged
# "wrote ... (24 bytes, tcpdump rc=124)" and left the empty file behind. Counting frames measures the
# thing the snapshot exists for; file size measures whether tcpdump started.
set -u
D=/home/pi/soak-snapshots
mkdir -p "$D"; chown pi:pi "$D"
F="$D/snap_$(date +%F_%H%M).pcap"
/usr/bin/timeout 300 /usr/bin/tcpdump -i any -n -s 0 -c 10 -Z pi -w "$F" 'udp port 51000'
rc=$?
n=0
[ -f "$F" ] && n=$(/usr/bin/tcpdump -r "$F" 2>/dev/null | wc -l)
if [ "$n" -gt 0 ]; then
    echo "jarvis-snap: wrote $F ($n frames, $(stat -c %s "$F") bytes, tcpdump rc=$rc)"
else
    rm -f "$F"; echo "jarvis-snap: no telemetry within 300 s (box off?), nothing written (rc=$rc, frames=$n)"
fi
find "$D" -name 'snap_*.pcap' -mtime +90 -delete
exit 0
