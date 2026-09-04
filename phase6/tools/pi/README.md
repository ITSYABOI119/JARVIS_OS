# Pi capture host — soak telemetry capture + dated snapshots

The Pi (`ssh pi`, wired `eth0` 192.168.68.66 / WiFi `wlan0` 192.168.68.61) is the dedicated always-on
capture host for a JARVIS soak run. It captures the box's UDP broadcast telemetry (`:51000`) so the run
has a wire record independent of the box's own durable log, which wraps in minutes.

Two units live here, and they do different jobs:

| Unit | What it is | Why |
|---|---|---|
| `jarvis-soak-capture.service` | the CONTINUOUS ring — 50 × 100 MB files in `/home/pi/soak/` | full-fidelity wire history while it lasts |
| `jarvis-soak-snapshot.timer` → `.service` → `jarvis_snap.sh` | ONE dated 10-frame snapshot per day in `/home/pi/soak-snapshots/` | the DURABLE artefact — see below |

## Why the snapshot exists, and why it is not inside the ring

**The ring restarts at `pcap00` after any service restart or reboot.** That is not hypothetical: the
2026-08 soak lost its first ~21 h that way (`phase6/docs/SOAK_2026-08_FINAL_REPORT.md`). A ring is a
*window*, not an archive — it can only ever prove the recent past, and it silently discards the rest.

A dated snapshot written **outside** `/home/pi/soak/` is never in the ring's write path and is never
overwritten by it. **STANDING RULE: the snapshot directory must never be placed inside `/home/pi/soak/`.**
Putting it there would hand the ring the authority to delete the one artefact that outlives it.

`Persistent=true` on the timer is the other half: a run missed while the Pi was down fires at the next
boot, so an outage produces a late snapshot rather than a silent hole.

## Install

```bash
# from the repo, on the Main PC
scp phase6/tools/pi/jarvis_snap.sh pi:/tmp/
scp phase6/tools/pi/jarvis-soak-snapshot.service phase6/tools/pi/jarvis-soak-snapshot.timer pi:/tmp/

# on the Pi
sudo mkdir -p /home/pi/bin
sudo install -o root -g root -m 0755 /tmp/jarvis_snap.sh              /home/pi/bin/jarvis_snap.sh
sudo install -o root -g root -m 0644 /tmp/jarvis-soak-snapshot.service /etc/systemd/system/
sudo install -o root -g root -m 0644 /tmp/jarvis-soak-snapshot.timer   /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now jarvis-soak-snapshot.timer
systemctl list-timers jarvis-soak-snapshot.timer
```

Verify the copies match rather than assuming the transfer was clean — this repo has corrupted a file
through a text channel three separate times:

```bash
md5sum /home/pi/bin/jarvis_snap.sh /etc/systemd/system/jarvis-soak-snapshot.*   # on the Pi
md5sum phase6/tools/pi/jarvis_snap.sh phase6/tools/pi/jarvis-soak-snapshot.*    # in the repo
```

## The two proofs (run BOTH before trusting the timer)

Neither needs the JARVIS box. Run them after any change to the script.

**Negative control — the box is parked, so there is no telemetry.** `sudo systemctl start
jarvis-soak-snapshot.service`. It must return after ~300 s having written **nothing**, with the unit
`inactive (dead)` and `Result=success` — a snapshot that cannot find telemetry is a normal night, not a
failure, and a `failed` unit would train the operator to ignore it. Measured 2026-09-04:

```
jarvis-snap: no telemetry within 300 s (box off?), nothing written (rc=124, frames=0)
```

**Positive control — synthetic frames, from the Pi itself.** Start the service in the background, wait
~3 s for tcpdump to attach, then broadcast to `:51000` from the Pi (`-i any` captures outgoing frames
too, so no second host is needed):

```bash
sudo systemctl start jarvis-soak-snapshot.service &
sleep 3
python3 -c "
import socket, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
for i in range(12):
    s.sendto(b'JARVIS-SNAP-TEST', ('255.255.255.255', 51000)); time.sleep(0.5)
"
wait
```

It must finish in seconds. Measured 2026-09-04:

```
jarvis-snap: wrote /home/pi/soak-snapshots/snap_2026-09-04_2244.pcap (10 frames, 824 bytes, tcpdump rc=0)
```

**DELETE the synthetic file afterwards** — it must never sit beside real snapshots, where a later reader
would count it as box telemetry.

**The timer then proved itself unattended**, which is the property the whole thing exists for. Its first
scheduled run fired at 03:00:05 on 2026-09-05 with nobody present and the box parked, and cost nothing:

```
Sep 05 03:05:05 pi jarvis_snap.sh[7465]: jarvis-snap: no telemetry within 300 s (box off?), nothing written (rc=124, frames=0)
Sep 05 03:05:05 pi systemd[1]: Finished jarvis-soak-snapshot.service - JARVIS soak: one dated telemetry snapshot (outside the capture ring).
```

The POSITIVE control also shows the dual-homing duplication in the raw capture, worth seeing once: five
sends produced ten frames, `eth0 Out` and `wlan0 B` for each. `-i any` captures a broadcast once per
interface, so raw frame counts are ~2× what the box sent. That is accepted, not a defect — see below.

## `[ -s "$F" ]` does not mean "captured something" (measured, not theorised)

The first version of `jarvis_snap.sh` decided success with `[ -s "$F" ]`. **tcpdump writes the 24-byte
pcap FILE HEADER before it captures anything**, so a zero-packet run leaves a non-empty file and `-s`
reports a successful capture. The first negative control caught it exactly:

```
jarvis-snap: wrote /home/pi/soak-snapshots/snap_2026-09-04_2238.pcap (24 bytes, tcpdump rc=124)
```

— a "snapshot" of nothing, left on disk, from a run that had captured zero frames. The same artefact is
already in the soak archive as the 24-byte `pcap00`. The script now counts frames with `tcpdump -r`,
which measures the thing the snapshot exists for instead of a proxy for it. **Do not revert that to a
size test**, and do not swap it for `rc`: a partial capture (3 real frames, then the 300 s timeout) also
exits 124, and those frames are worth keeping.

## `jarvis-soak-capture.service` is a COPY, kept for provenance

`jarvis-soak-capture.service` here is a **byte-exact copy of the unit running on the Pi since
2026-08-13** (md5 `2a92ccbd6ee8e0130f5627e20b22b02c`), committed 2026-09-04 so the live host can be
diffed against version control:

```bash
diff <(ssh pi 'systemctl cat jarvis-soak-capture.service | tail -n +2') phase6/tools/pi/jarvis-soak-capture.service
```

(`tail -n +2` drops systemd's `# /etc/systemd/system/...` header line, which is not part of the unit.)

Because it is a provenance snapshot it is **copied, never corrected**. It carries one stale figure — its
sizing comment reads `1 Hz x ~372 B x 2 interfaces`, and the measured stream rate is ~2 Hz at deployed
query rates (2.08 Hz across the 2026-08 soak; the 1 Hz keepalive is the floor, and the `q%100` STATS
emission outruns it). The conclusion it supports is unaffected and was confirmed by the run itself: the
month cost ~2.1 GB against a 5 GB hard cap. Editing the copy to match the docs would break the `diff`
above, which is the only thing this file is for. If the live unit is ever changed, change it on the Pi
and re-copy it here.

## What NOT to touch

- `/home/pi/soak-archive-2026-08/` — the 2026-08 soak's nine archived ring files (652 MB), the sole
  wire history of that run outside the second copy on the Main PC. Never inside the live ring's path.
- The live `jarvis-soak-capture.service` — mirror it, do not reinvent it. `-i any` is deliberate: the Pi
  is dual-homed and a named interface would go silent if that one link dropped, producing a gap with no
  error anywhere.
