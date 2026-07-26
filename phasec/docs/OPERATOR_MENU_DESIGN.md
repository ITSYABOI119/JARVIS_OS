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

---

## 10. v1 RESULT (2026-07-25) — BUILT, all 7 options, read-only: PASS

Shipped as `phasec/scripts/jarvis_menu.bat` (**the ENTRY POINT** — a `.ps1` cannot be
double-clicked) + `phasec/scripts/jarvis_menu.ps1` (**the implementation**), following the
`start_receiver.bat`/`.ps1` precedent rather than inventing a second convention. All seven §6
options implemented, no additions. The strategist's rulings on OQ1–OQ7 were adopted as written.

### The invariant, and how to verify it

**Nothing in v1 constructs a dd write operand.** This is not a promise, it is a grep over exactly
two files:

```
grep -nE 'of=|seek=' phasec/scripts/jarvis_menu.ps1 phasec/scripts/jarvis_menu.bat   # must print NOTHING
```

Measured: **no matches**, with a positive control confirming the check is not vacuous (`if=` and
`skip=` *are* present — the reads are real). The verification command is deliberately **not written
inside the scripts**: the first draft documented it in the `.ps1` header, and the file then matched
its own pattern, which would have made the invariant permanently un-checkable. The recipe lives here
and in CLAUDE.md; the scripts state the property only.

### Two design points that were decided during the build

**The `dd | python3` pipe runs ON THE BOX, not on the Main PC.** Raw sector data never crosses a
PowerShell pipeline, because PowerShell pipelines carry text and would corrupt it — the same class
of bug that silently mangled ~12% of generated 32-byte keys until `os.O_BINARY` was added. Only text
crosses ssh. This also means no intermediate file is written anywhere, which is what keeps the
invariant above true — a local-parse design would have needed a temporary file and a write operand.
**Residual, stated honestly:** this uses the BOX clone's parser, so a stale box clone parses with a
stale parser. `-Check` verifies each remote parser exists and prints the resolved path.

**Output volume (the gap the strategist raised, not in the OQs).** Default is **write the full
output to a file and show the last N lines** (`-Lines`, default 40; `-Lines 0` prints everything).
Nothing is lost — the header block that a naive `tail` would cut is preserved in the file — and the
console stays readable. Dumps go to `%USERPROFILE%\.jarvis\menu\`, **outside the repo**, so
control-IN conversation text cannot be committed by accident. The control-IN option says so on
screen before it runs.

### What -Check validates (it is the CI substitute — no ci.yml step exists or can exist)

`ssh` on PATH · `start_receiver.ps1` present · **all six store constants derived from the headers**
(printed with the macro they came from) · box state · each remote parser present · dumpcap presence
(informational) · transcript directory writable · then **the exact command every option would run**,
including option 4's probe chain. Measured: **PASS, exit 0**, all four stores resolving to the
documented values — telemetry-log 4000794624/2701 · episodic 21100000/8193 · control-IN
21140000/4097 · JACT 21120000/4097.

### PROVEN on real hardware

The box was on Ubuntu, so v1 was run against it rather than only self-tested:

- **A real store read end to end** — option 8 (JACT), through the shipped `Invoke-StoreRead` body:
  `exit=0`, 143 lines, `Checksum: 0x4A41434C (OK)`, 134 real records decoded, tail limit applied
  (`showing the last 6 of 143`), full dump written to `%USERPROFILE%\.jarvis\menu\`.
- **The transcript line**, in the ruled format, recording the RESOLVED command and never the label:
  `2026-07-25T19:55:19+10:00 | read-jact | ssh -o BatchMode=yes … | exit=0`.
- **State detection** resolving `UBUNTU -- ssh answered`.

### Three defects the build found — all fixed, all found by RUNNING it

1. **The self-matching grep** (above): documenting the invariant's own pattern inside the file it
   checks makes the check useless forever.
2. **An infinite loop on redirected stdin.** With stdin not a console, `Read-Host` returns empty
   *immediately* instead of blocking, and the menu loop spun — measured **~10,000 iterations and a
   232,569-line log in three minutes**. Now guarded by `[Console]::IsInputRedirected` (exit 6, in
   0.6 s, naming `-Check` as the non-interactive entry point), plus a consecutive-empty-read guard
   for the console-EOF case that `IsInputRedirected` reports as false.
3. **`-Lines` never worked.** PowerShell variable names are **case-insensitive**, so the local
   `$lines = @($out)` overwrote the `$Lines` parameter and `$lines.Count -le $Lines` compared the
   array against itself — always true, so the full dump always printed. This is exactly the failure
   the output-volume ruling exists to prevent, it shipped looking correct, and **`-Check` could
   never have caught it** because `-Check` never reads a store. Renamed to `$outLines`; re-measured
   `showing the last 6 of 143`.

### UNVERIFIED, stated rather than glossed

- **The interactive loop itself is not automatable, by construction.** The stdin guard that fixes
  defect 2 also makes piped end-to-end drive impossible — which is the correct trade, and it is the
  concrete reason `-Check` is the substitute (R5). The loop's dispatch was exercised by loading the
  shipped function bodies via the PowerShell AST and calling them under the same `StrictMode`; the
  `Read-Host` menu rendering itself was verified by eye, not by a harness.
- **Options 1–3 were not launched** during verification (they open a receiver window and, in send
  mode, a UAC prompt). Their resolved commands are pinned by `-Check`; the launcher's own behaviour
  is covered by its own `-Check`, which v1 deliberately does not duplicate.
- **The dumpcap fallback probe was not exercised** — the box was on Ubuntu, so the ssh branch
  answered first. dumpcap presence was confirmed; the capture path itself is untested.
- **The JARVIS and UNKNOWN state branches were not exercised** for the same reason.

### Honest ceiling — unchanged, and now more relevant

§8 stands verbatim: wrapping a dangerous command in a menu does not make it less dangerous, it makes
it easier to reach. v1 sidesteps that only because it is read-only. The moment a destructive option
lands, §8 becomes the operative section, and defect 3 above is the standing argument for why each
one needs to be exercised against real hardware rather than trusted because `-Check` printed it.

---

## 11. v2 (2026-07-26) — the first destructive slice, in a SEPARATE FILE: `jarvis_admin.ps1`

§10 ended by saying that the moment a destructive option lands, §8 becomes the operative section.
It has landed: the control-IN **re-key**. This section records the one structural decision that
shaped everything about it, and the corrections the build produced.

### 11.1 Why it is not a menu option — the invariant would have died

v1's safety property is **not a promise, it is a grep** (§10, "The invariant"): neither
`jarvis_menu.ps1` nor `jarvis_menu.bat` contains a `dd` output operand or a seek operand anywhere,
so "v1 cannot write to the box" is mechanically checkable and a single match is the proof that it
broke. **Re-keying requires both of those operands by definition.** Adding it to the menu would have
traded a checkable invariant for a claim — and it would have done so silently, since the grep would
simply start matching and nobody would notice that the thing it was protecting had changed meaning.

So the destructive work went into a **separate file whose name says what it does**, and the
invariant became **two-sided** — which says strictly more than it did before:

```
grep -nE 'of=|seek=' phasec/scripts/jarvis_menu.ps1 phasec/scripts/jarvis_menu.bat   -> NOTHING
grep -nE 'if=|skip=' phasec/scripts/jarvis_menu.ps1                                  -> 2 (positive control)
grep -nE 'of=|seek=' phasec/scripts/jarvis_admin.ps1                                 -> MATCHES (incl. the write)
grep -nE 'of=|seek=' phasec/scripts/jarvis_admin.bat                                 -> NOTHING (4-line wrapper)
```

**Device writes live in exactly one file, and that file's name says so.** Neither menu file was
modified by the v2 commit — that is itself part of the evidence. This is the read-only/destructive
separation §6 anticipated, arriving in the shape §6 predicted.

### 11.2 What v2 encodes

The full re-key procedure, performed **by hand on 2026-07-26** and validated on real hardware
(box `boot_id=44`, `answered=1 blocked=0 dropped=0 err=0`). That manual run is the reason to
automate: it surfaced roughly ten checks the runbook does not have, and they are exactly the checks
a human skips on the fifth repetition.

Three modes, and **no default** — a destructive tool must do nothing on a bare invocation, so no
arguments prints usage and exits 2. `-Check` validates and prints; `-Rekey` runs the procedure
behind a typed `REKEY-WRITE-NOW`; `-Rollback` restores the previous pair from the `.BAK` artifacts.

The gates that were **not** in the runbook, with why each earns its place:

| gate | what it catches |
|---|---|
| **P4** | the two halves must already MATCH before starting. If they do not, the channel is *already* broken and re-keying is the WRONG action — it would overwrite the only copy of the box's current key |
| **B1/B2** | backups verified by **fingerprint**, not size — a 512-byte file of zeros passes a size check. This is what makes the write safe to take |
| **G4** | the key *inside* the slot must equal the key file. Without it a mismatched pair provisions "successfully" and the channel is dead with **no error anywhere** until a query gets no reply |
| **T2** | whole-slot md5 equal on both sides, not "512 bytes arrived" — this project has silently corrupted binary through a text channel three times |
| **W1** | read back from `/dev/nvme0n1`, **not** from the file that was just written |
| **W3** | neighbours must survive: `LBA+1/+2` = JFLR, `LBA+3` = JCON. One wrong digit in `seek=` lands on the replay floor, the console address, the JACT audit store or the control-IN conversation store — damage that would not show up in the target sector at all |

Every LBA, magic and version is **parsed from the firmware headers** (`control_key.h`,
`control_floor.h`, `control_console.h`) — including the neighbour *offsets*, which are expressions
(`CTRL_KEY_BASE_LBA + 1ULL`) and are parsed out of the expression rather than assumed. A constant
typed into a script can drift from the firmware; a parsed one cannot.

### 11.3 Correction to RUNBOOK-FULL-EXERCISE §5.1

§5.1 says to "confirm the backup is exactly 32 bytes" and applies that to the **slot**. The key file
is 32 B; the **slot is 512 B**. Checking a slot for 32 bytes would pass on a truncated backup — i.e.
the check as written could green-light an unrestorable backup, which is the one thing a backup check
exists to prevent. v2 encodes the correct size per artifact and fingerprints both.

### 11.4 Cleanup is a gate, not a courtesy

The manual run left six stray artifacts. The one that mattered was **neither old key**: it was the
`scp`'d scratch slot in the **box's** home directory — a plaintext copy of the **currently live** key
on a dual-booting LAN machine. The security model is that the key exists in exactly two places, the
JKEY sector and the Main PC key file; a third readable copy widens the live key's exposure for no
benefit. Old-key artifacts are inert by comparison, because the box no longer accepts that key and a
frame signed with it is dropped at HMAC.

**Cleanup runs only after the final gate passes**, so an aborted run never destroys its own rollback
path. `-KeepBackups` retains the old pair; delete is the default, because the rollback purpose is
discharged the moment the new pair verifies on both halves. Stated plainly in the output: `rm`
unlinks, it does not securely erase, and on an SSD overwriting would not guarantee erasure either.

### 11.5 Two defects the build found — again, by RUNNING it

Same lesson as §10's three: `-Check` is worth writing because it is worth *running*.

1. **Embedded double quotes do not survive PowerShell 5.1's native-argument escaping.**
   `tr -d " \n"` and `cut -d" " -f1` reached the box with their quotes eaten and **silently became
   no-ops** — the first `-Check` printed a slot magic of `59 45 4b 4a` (unstripped) against an
   expected `59454b4a`. It failed *loudly* here only by luck of comparing an exact string; a check
   written to compare a substring would have passed vacuously forever. Fixed by removing every
   embedded double quote from every remote command: strip whitespace **locally**, take fixed-width
   hash prefixes with `cut -c` instead of a delimiter, and leave `$HOME/<name>` unquoted (the path
   has no spaces, and `$VAR` expansion — unlike tilde — happens anywhere in an unquoted word,
   including after `if=`).
2. **The all-zero-key gate could itself have been vacuous.** It counts non-`\000` bytes, and if the
   `tr` no-op'd the count would be non-zero for an all-zero key — i.e. the gate would pass on exactly
   the input it exists to reject. Proven non-vacuous with a **positive and negative control** against
   real sectors: an unused sector (md5 `bf619eac…` = 512 zero bytes) returns **0**, the live key
   sector returns **32**.
3. **Every remote read is a PIPELINE, so its exit status was `cut`/`od`/`wc`'s — not `dd`'s.**
   Measured by pointing the gates at a nonexistent device: `dd` fails, and the gate still returns
   **exit 0**. Every gate still failed *closed*, because each compares against a concrete expected
   value — but two **MISDIAGNOSED**, which in an incident-response tool is its own defect: a failed
   read reported *"the key inside the box slot is ALL ZERO"* (the broken pipe yields an empty stream
   and `wc -c` honestly counts 0), and W1 reported a mismatch against **`e3b0c442`** — which is
   `sha256("")[:8]`, i.e. the signature of a read that returned no bytes at all. Fixed by prefixing
   all six pipeline reads with **`set -o pipefail`** (verified: failed `dd` → exit 1; healthy reads
   unchanged, still `a38577f1`), splitting the read-failure and all-zero verdicts into separate
   messages, and naming `e3b0c442` explicitly wherever a fingerprint is reported. The two commands
   whose exit codes actually matter — the `stat` size check and **the `dd` write itself** — are not
   pipelines and always returned true status (confirmed: exit 1 on a missing file).

### 11.6 Honest ceiling — §8, now operative

Wrapping a destructive command in a script does not make it less destructive; **it makes it easier
to reach.** Nothing about `jarvis_admin.ps1` changes that. What makes it safe is that every gate is a
**hard abort** rather than a warning, that the write sits behind a typed confirmation, and that
nothing live is touched until the new pair is fully verified in a scratch directory. `-Check` is not
only the CI substitute (a Windows `.ps1` cannot run in this repo's Linux CI) but the **anti-rot**
mechanism: re-keying is incident response, not rotation, so this script will sit unused for long
stretches and must be verifiable on demand — and it verifies by really reading the box, not by
printing.
