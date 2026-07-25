# Operator Menu — DESIGN (plan-first, no code)

**Status: PLAN-FIRST. No code, no flag, no CI, no box operation.** Authored 2026-07-25, after the
`start_receiver.bat`/`gen_control_key.py` access-UX work and C/M1b-1. Deliverable for strategist
review; v1 lands only after that review.

Every `file:line`, script name and command below was re-verified against the repo at `6b24a0d`.
Where the brief's inventory was wrong, §2.1 says so with the evidence — per the standing rule that
a design doc which only agrees with its prompt did not do its job.

---

## 1. The problem, stated honestly

Every box operation today is a command the operator must find in CLAUDE.md and retype correctly:
an `ssh jarvis 'bash -lc "…"'` to build, a `dd | parse_*.py` pipe per store, an `efibootmgr
--bootnext` + reboot to reach JARVIS, an `install_jarvis_x86.sh` incantation to deploy. This
session alone produced a half-re-key hazard, a recurring git-lock footgun, and a "which flags for
a stability run" checklist — all **operator-friction** failures, none of them code bugs.

The ask: a menu — pick an option, it runs the right thing.

**Calibration (from the brief, and correct):** the failure modes here are *"typed the wrong
device"* and *"ran the wrong thing while the box was in the wrong state"*, not *"hung the seL4
boot."* This is a tight design doc, not a K/M2-scale adversarial pre-mortem.

---

## 2. Ground truth — the VERIFIED operator task list

Read from `phase3/scripts/`, `phasec/scripts/` and CLAUDE.md's command reference.

### 2.1 Corrections to the brief's inventory (evidence first)

1. **"read the semantic store (21,110,000 / 4097)" — THERE IS NO TOOL.** `ls phase3/scripts/`
   contains `parse_nvme_log.py`, `parse_episodic.py`, `parse_action_audit.py` and **no
   `parse_semantic.py`**. `parse_episodic.py` cannot stand in: it validates the header magic and
   errors out (`parse_episodic.py:129`, `expected 0x4A455049` "JEPI") — the semantic store is
   "JSEM". So this option **cannot be built by wrapping**; it needs a new parser first. ⇒ moved to
   DEFERRED (§6), not v1.
2. **"KVM smoke (`qemu_test.sh`)" — that is not the command any recent gate actually used.**
   `qemu_test.sh` passes **no `-smp`** and **attaches no NVMe drive** (it boots kernel+initrd
   only). `qemu_test_nvme.sh` does attach NVMe but points at a *different* image
   (`/tmp/jarvis_data.img`) and defaults to a **Llama-1B** model path. Every gate this session
   (C/M1b-1, the HUD fix, the monitor gate) used a hand-written invocation: `-enable-kvm -cpu host
   -smp 6 -snapshot -serial file:… -drive file=$HOME/nvme_test.img … -device nvme,…`. ⇒ Wrapping
   `qemu_test.sh` would wrap the *wrong* command. See §6 and OQ4.
3. **R3's detection cannot simply bind UDP :51000 on the Main PC.** CLAUDE.md records that "the
   receiver's :51000 bind is Hyper-V-excluded" (the 6-7 soak capture section) — the documented
   workarounds are dumpcap/Npcap L2 capture or reading an already-running receiver's `/events`.
   A menu that "listens for telemetry" as its liveness probe would fail on the very machine it
   runs on. See §5.
4. **Ping cannot distinguish "on JARVIS" from "off".** Under JARVIS the box answers no ICMP and no
   ARP — CLAUDE.md states plainly "The box has NO ARP: the reply dst is PROVISIONED, never
   resolved". Inbound is control-IN unicast on :51001 only. So an unreachable box is *ambiguous*
   by construction, which §5 must handle rather than paper over.

### 2.2 Confirmed correct in the brief

- `parse_episodic.py` **does** correctly read the control-IN store: it derives the slot count from
  the input length (`slots = (len(data) // 512) - 1`, `parse_episodic.py:81`), so `dd … count=4097
  | parse_episodic.py` yields 4096 slots. One tool, two stores. (I suspected this was broken; the
  code proves it is not.)
- The LBA/count constants as listed (episodic 21,100,000/8193 · JACT 21,120,000/4097 · control-IN
  21,140,000/4097 · telemetry log 4000794624/2701).
- `install_jarvis_x86.sh` already has the confirmation shape to copy — `guard_esp_device`,
  `guard_disk_device`, typed confirmations (`ADD-JARVIS`, `WIPE-AND-INSTALL-JARVIS`).
- Passwordless sudo on the box holds today (this session used `sudo systemd-run`, `sudo mount`,
  `sudo dd` over ssh with no prompt).

### 2.3 The task list v1 designs against

| # | Task | Class | Runs today |
|---|---|---|---|
| 1 | Start receiver (send) | safe | `phasec/scripts/start_receiver.bat` |
| 2 | Start receiver (display-only) | safe | `start_receiver.bat -DisplayOnly` |
| 3 | Preflight check | safe | `start_receiver.bat -Check` |
| 4 | Box state | safe | *(no tool today — §5)* |
| 5 | Read telemetry log | safe (read) | `ssh jarvis 'sudo dd if=/dev/nvme0n1 bs=512 skip=4000794624 count=2701' \| parse_nvme_log.py` |
| 6 | Read episodic store | safe (read) | `… skip=21100000 count=8193 \| parse_episodic.py` |
| 7 | Read control-IN store | safe (read) | `… skip=21140000 count=4097 \| parse_episodic.py` |
| 8 | Read JACT audit | safe (read) | `… skip=21120000 count=4097 \| parse_action_audit.py` |
| 9 | Build | changes box | `ssh jarvis 'bash -lc "cd ~/Desktop/JARVIS_OS/phase3/scripts && bash build_jarvis_x86.sh ~/Desktop/JARVIS_OS"'` |
| 10 | KVM smoke | changes box(VM) | **see §2.1(2)** — no faithful script exists |
| 11 | Deploy to ESP | **destructive** | `install_jarvis_x86.sh --target esp --esp /dev/nvme0n1p4` |
| 12 | Boot into JARVIS | **destructive-ish** | `ssh jarvis 'sudo efibootmgr --bootnext <id> && sudo reboot'` |
| 13 | Generate + provision key | **destructive** | `gen_control_key.py` → `dd … seek=21130000 count=1 conv=fsync` → reboot |
| 14 | Clear telemetry log | **destructive** | `clear_nvme_log.sh` |
| 15 | Read semantic store | — | **no tool (§2.1(1))** |

---

## 3. Design rules

**R1 — thin wrapper, NEVER a reimplementation. ADOPTED as a hard rule.** Every option shells out
to the existing proven script; the menu owns *selection and sequencing*, never logic. The moment it
contains its own copy of the deploy or `dd` logic there are two implementations, they drift, and
the wrong one runs at 2am. Concretely: option 11 calls `install_jarvis_x86.sh` (it must never
assemble its own `cp`/`grub-mkstandalone`), option 14 calls `clear_nvme_log.sh`, options 1-3 call
`start_receiver.bat`. **Enforcement, not just intent:** `-Check` prints the exact command string
each option would run (§R5), so a reimplementation is visible in the self-test output.

**R2 — read-only and destructive visually separate; destructive confirms.** Two rendered sections
with a hard rule between them. Destructive options follow the `install_jarvis_x86.sh` precedent
(typed confirmation, not `y/N`) rather than inventing a style. The typed token names the *action
and target*, e.g. `DEPLOY-ESP-nvme0n1p4`, so muscle memory cannot carry a confirmation from one
option to another.

**R3 — state-aware.** See §5; this is the feature that actually prevents mistakes.

**R4 — never claim success you have not verified.** "Deployed ✓" only after the deploy flow's own
md5 read-back; "key provisioned ✓" only after reading the sector back and matching the magic; "log
cleared ✓" only after re-reading the header. If a verification step is absent, the menu reports
**"ran, unverified"** — never a bare ✓. This is the project's honesty rule applied to tooling.

**R5 — `-Check` self-test, reusing the `start_receiver.ps1` convention.** A Windows PowerShell
script cannot run in the Linux CI, so there is deliberately **no ci.yml step** and `-Check` is the
substitute: validate every prerequisite (py -3, scripts present, ssh reachable, key file exactly
32 bytes, Npcap) and **print what each option WOULD run without running it**. Reuse the flag name
and the distinct-exit-code discipline; do not invent a second convention.

---

## 4. Traps

**T1 — a wrong `dd` target is unrecoverable. Make it structurally impossible, not discouraged.**
Three layers, in order of strength:
1. **The operator never types an offset.** Options are *named stores*; LBA/count are internal.
2. **Constants are DERIVED FROM THE HEADERS, not retyped.** `clear_nvme_log.sh:34-41` already does
   exactly this (`grep`s `NVME_LOG_BASE_LBA` out of `nvme_log.h`, computes `COUNT = MAX_ENTRIES+1`)
   — that is the precedent, and it means a menu constant cannot drift from the box's truth.
   Sources: `nvme_log.h`, `episodic_store.h`, `action_audit.h`, `control_key.h`.
3. **Reads are `if=`, writes are a separate code path with a typed confirmation naming the store.**
   The read options physically cannot write: they never construct an `of=` argument.
Residual risk stated honestly: the *device* (`/dev/nvme0n1`) is still a constant the menu supplies.
Mitigation is a pre-flight identity check (`lsblk` model/serial match) before any write, refusing on
mismatch — the `guard_esp_device` pattern.

**T2 — do NOT bundle build + deploy + reboot.** Adopted. Each is a separate option; each prints its
result and returns to the menu. There is **no "do everything" option in v1 or ever** — a single
button is exactly how a bad build reaches the ESP. The menu may *suggest* the next step in text
("build succeeded — deploy is option 11") but never chains automatically.

**T3 — the key path must never print key bytes, and the menu must weaken no receiver guard.**
`gen_control_key.py` already never prints the key (host-tested: "the key NEVER appears in stdout").
The menu adds nothing to that path but a confirmation and a read-back; it must not echo file
contents, must not pass the key on a command line, and must not add flags to `telemetry_receiver.py`.
The receiver's loopback pin, per-request peer check and CSRF/rebinding guards are untouched — the
menu only *launches* it via `start_receiver.bat`.

**T4 — passwordless sudo is assumed; make its absence legible.** True today (verified §2.2). If it
ever is not, `ssh … sudo` hangs waiting for a password on a non-tty. Mitigation: run remote sudo
with `sudo -n` (non-interactive) so it *fails fast* with a clear message, and surface exactly
**"sudo refused on the box"** rather than a silent hang or a generic timeout.

---

## 5. State detection (R3) — the hard part

Three states, and only one of them supports most options:

| State | ssh | Telemetry | What is valid |
|---|---|---|---|
| **Ubuntu** | works | none | build, deploy, all store reads, key provision, log clear, boot-into-JARVIS |
| **JARVIS** | **impossible** (seL4 has no shell/sshd) | live UDP :51000 broadcast | receiver/console options only |
| **Off** | no | no | nothing box-side |

**Detection, in order:**
1. **`ssh -o BatchMode=yes -o ConnectTimeout=3 jarvis true`** → success ⇒ **Ubuntu**. This is
   unambiguous and cheap.
2. If ssh fails, look for **telemetry**. This is where §2.1(3) bites: the menu **cannot bind
   :51000** on the Main PC (Hyper-V exclusion). Two viable probes, in preference order:
   a. **An already-running receiver's SSE** — `GET http://127.0.0.1:<port>/events`, take one
      record. Cheap, no elevation, and it is what the operator usually has open anyway.
   b. **dumpcap/Npcap L2 capture** for a few seconds, filtered `udp port 51000`. Works without the
      receiver but needs Npcap; `-Check` already validates Npcap presence.
   A telemetry record ⇒ **JARVIS**.
3. Neither ⇒ **UNKNOWN**, *not* "off".

**Degradation when detection is ambiguous — the part that matters.** "Off" and "JARVIS with no
receiver running and no Npcap" and "network/cable problem" are **indistinguishable** from the Main
PC, because under JARVIS the box answers no ping and no ARP (§2.1(4)). The design therefore:
- reports **UNKNOWN**, never guesses "off";
- in UNKNOWN, presents **safe options only** and greys the rest with the reason shown
  (*"box state unknown — ssh unreachable and no telemetry seen; start the receiver or check the
  box"*);
- **never blocks the operator**: an explicit `--assume-ubuntu` / `--assume-jarvis` override exists,
  because a menu that refuses to act when its own probe is broken is a worse failure than one that
  lets an informed operator proceed. The override is *stated* in the transcript so the log records
  that detection was bypassed.

**An option that silently fails because the box is in the wrong mode is worse than one that is not
offered** — so invalid options are shown, disabled, *with the reason*, rather than hidden (hiding
them makes the menu look different run-to-run and teaches nothing).

---

## 6. v1 scope, and what is deferred

**v1 — the highest friction-removed per unit of risk (7 options):**

| # | Option | Why v1 |
|---|---|---|
| 1-3 | Receiver: send / display-only / `-Check` | Already wrapped; pure win; zero box risk |
| 4 | Box state | Enables everything else; the single most-asked question |
| 5 | Read telemetry log | The headless box's only persistent record; run at every check-in |
| 6-8 | Read episodic / control-IN / JACT | Same `dd | parse` shape, three constants, read-only |

That is **read-only plus launching a Main-PC process**. It removes most of the daily friction and
**cannot damage the box** — nothing in v1 constructs an `of=`.

**DEFERRED, with reasons:**
- **Deploy to ESP (11), boot into JARVIS (12), key provision (13), clear log (14)** — all
  destructive, all worth wrapping *eventually*, but each needs its own confirmation + verification
  design and they should not ride v1's first outing. Deploy and key-provision are also the two that
  most need R4 read-back proof.
- **Build (9)** — deferred not for risk but because a build is long-running and needs streamed
  output and a cancel story; wrapping it badly is worse than the current one-liner.
- **KVM smoke (10)** — blocked on §2.1(2): there is no faithful script to wrap. Wrapping
  `qemu_test.sh` would enshrine the wrong invocation. Fix the script first (OQ4).
- **Read semantic store (15)** — blocked on §2.1(1): no parser exists.

---

## 7. Where it lives

`phasec/scripts/jarvis_menu.ps1`, alongside `start_receiver.ps1`/`.bat`, with a
`phasec/scripts/jarvis_menu.bat` entry point — because a `.ps1` cannot be double-clicked
(the lesson `start_receiver.bat` exists to encode).

**It CALLS `start_receiver.ps1`; it does not replace or absorb it.** R1 requires this, and the
launcher has its own tested behaviour (UAC elevation for send-mode only, the `explorer.exe`
non-elevated browser hand-off, fail-closed preflight). Duplicating any of that would be exactly the
drift R1 forbids. The launcher remains independently usable.

---

## 8. Honest ceiling — what this does NOT make safer

**Wrapping a dangerous command in a menu does not make it less dangerous. It makes it easier to
reach.** That is the whole point and also the whole risk. Specifically:

- A menu-driven `dd` to LBA 21,130,000 destroys the key exactly as thoroughly as a typed one. The
  menu removes *typos*, not *consequences*.
- Making deploy one keystroke away makes deploying **more likely**, including when it should not
  happen. T2 (no bundling) and R2 (typed confirmation) exist to add friction back where it belongs.
- State-awareness reduces *category* errors ("ran an ssh option while on JARVIS"); it does nothing
  about *intent* errors ("re-keyed when I meant to read the key slot").
- It does not make the box more reliable, does not reduce any risk the underlying scripts carry,
  and adds a new Main-PC component that can itself be wrong. Its failure mode is offering a
  correct-looking option that runs the wrong command — which is why R1 (thin wrapper) and R5
  (`-Check` prints the command) are the two rules that carry the design.

---

## 9. Open questions for the strategist

1. **Is v1's read-only scope right, or too timid?** *Recommendation: as scoped.* It removes the
   daily friction with zero box-damage potential and lets the state-detection design prove itself
   before anything destructive rides on it. Deploy/key are the two I would add next, together, with
   their read-back proofs.
2. **UNKNOWN-state behaviour: block, or allow with an explicit override?**
   *Recommendation: allow with `--assume-*` + a transcript note.* A tool that refuses to work when
   its own probe is broken (Npcap missing, receiver not running) trains operators to bypass it.
3. **Telemetry probe: require Npcap, or accept "receiver already running" as the only probe?**
   *Recommendation: try `/events` first, fall back to dumpcap, and treat "no probe available" as
   UNKNOWN rather than a hard error* — so the menu is useful on a machine without Npcap.
4. **Fix the KVM smoke scripts before wrapping them?** *Recommendation: yes, separately.* Neither
   `qemu_test.sh` nor `qemu_test_nvme.sh` matches how gates are actually run (§2.1(2)); a small
   commit making one of them the real invocation (`-smp`, `-snapshot`, `nvme_test.img`) is worth
   more than a menu entry, and unblocks option 10.
5. **Write a `parse_semantic.py`?** *Recommendation: only if the semantic store is actually being
   read.* `JARVIS_SEMANTIC` is default-0 and the store's live yield was ~1 fact; a parser for a
   dormant store is speculative. Revisit if Phase C makes it live.
6. **Does the menu need a transcript/log of what it ran?** *Recommendation: yes, minimal* — an
   append-only local file of `timestamp | option | exact command | exit code`. It costs little and
   turns "what did I run at 2am" into a fact rather than a memory. It must never log key material.
7. **One menu, or separate read-only and admin menus?** *Recommendation: one menu with R2's visual
   split.* Two tools means two entry points and the admin one gets launched "just to look".
