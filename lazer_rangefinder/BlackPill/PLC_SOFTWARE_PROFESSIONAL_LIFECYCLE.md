# Professional PLC Software: Planning and Delivery Sequence

This document outlines a practical, industry-aligned sequence for specifying, designing, implementing, testing, and maintaining PLC application software. It assumes IEC 61131-3 style projects (e.g. ladder, FBD, ST) and environments such as vendor IDEs or OpenPLC Editor / Runtime.

---

## 1. Charter and scope

1. **Define the machine or process** — What physical system is controlled? Boundaries (what is in scope vs. operator, MES, safety system).
2. **Success criteria** — Cycle time, throughput, alarm philosophy, recovery after fault, required modes (auto/manual/maintenance).
3. **Stakeholders** — Who approves logic, who accepts on site, who maintains.
4. **Deliverables list** — Software, drawings, I/O list, test records, manuals, training.

*Output:* one-page scope + acceptance criteria signed or acknowledged.

---

## 2. Requirements and constraints

1. **Functional requirements** — Sequences, interlocks, permissives, setpoints, recipes, HMI expectations.
2. **Non-functional** — Scan time budget, determinism, power-fail behavior, language mix allowed (ST vs. ladder policy).
3. **Regulatory and safety** — Category / PL / SIL if applicable; what **must** stay in a certified safety PLC vs. standard PLC.
4. **Interfaces** — Fieldbus, OPC UA, Modbus, Ethernet/IP; who owns each signal.

*Output:* requirements document or backlog with unique IDs traceable to tests.

---

## 3. System design (before coding)

1. **Architecture** — Partition into **equipment modules** (zones, stations). Assign one coherent “owner” program per module where possible.
2. **State machines** — Identify major states (Idle, Running, Fault, Setup). Document transitions; avoid implicit “flag soup.”
3. **I/O and addressing** — Master **I/O list**: tag name, description, type, panel/device location, scaling, default safe state.
4. **Data model** — Global vs. local variables; structures for recipes; naming convention (prefixes for inputs/outputs/memory).
5. **Alarm and event strategy** — What is an alarm vs. a warning; reset rules; logging expectations.

*Output:* I/O list, state diagrams or tables, naming standard, memory map draft.

---

## 4. Detailed design

1. **Control narrative** — Step-by-step description of normal operation and fault handling (readable by non-programmers).
2. **Logic diagrams** — Causes-and-effects, ladder sketches, or FBD plans for critical interlocks.
3. **Timing** — Timers, debounce, anti-tie-down, minimum ON/OFF times.
4. **HMI mapping** — Which tags drive which screens, faceplates, permissions.

*Output:* narrative + diagrams linked to requirement IDs.

---

## 5. Implementation (coding)

1. **Version control** — Project in Git or vendor archive with **tags** per release; never only “last good copy” on a USB stick.
2. **Coding standard** — Languages allowed per layer (e.g. ST for algorithms, ladder for interlocks); comment rules; forbidden patterns (e.g. duplicated one-shots everywhere).
3. **Modularity** — Function blocks / UDFBs for repeated equipment; single writer principle for shared data where possible.
4. **Determinism** — Avoid unbounded loops; keep scan predictable; document fast vs. slow tasks if the platform supports multiple.
5. **Documentation in project** — POUs named clearly; variable comments; revision block in main program.

*Output:* buildable project matching detailed design.

---

## 6. Verification and validation (V-model alignment)

1. **FAT (Factory Acceptance Test)** — Against requirements: I/O simulation, sequence tests, alarm tests, recovery tests. **Record pass/fail** with requirement ID.
2. **Code review** — Peer review of interlocks, timers, and safety-related paths.
3. **SAT (Site Acceptance Test)** — Real I/O, production-like material, operator sign-off.
4. **Regression** — After any change, rerun a **minimal smoke test** plus tests for affected requirements.

*Output:* signed test sheets or digital trace (tickets + results).

---

## 7. Commissioning and handover

1. **As-built** — Final I/O list, final logic export, parameter backup.
2. **Runbooks** — Startup, shutdown, fault recovery, seasonal adjustments.
3. **Training** — Operators (HMI + alarms); maintenance (where logic lives, how to update).
4. **Cyber and access** — User accounts, VPN/plant network rules, patch policy for PC-based runtimes.

*Output:* handover package checklist completed.

---

## 8. Operation and change management

1. **Change request** — Every logic change has a reason, risk note, and retest scope.
2. **Backup before flash** — Store previous version and **document** what changed.
3. **Periodic review** — Unused alarms, dead code, obsolete modes.
4. **Obsolescence** — Plan for PLC/editor/runtime upgrades (e.g. OpenPLC Editor/Runtime version pairs).

---

## 9. Suggested order of work (summary checklist)

| Phase | Key actions |
|-------|-------------|
| 1 | Scope, acceptance criteria, stakeholders |
| 2 | Requirements + interfaces + safety split |
| 3 | Architecture, states, I/O list, naming |
| 4 | Control narrative, diagrams, HMI map |
| 5 | Implement with standards + version control |
| 6 | FAT → review → SAT → regression discipline |
| 7 | As-built, runbooks, training, handover |
| 8 | Change control, backups, periodic cleanup |

---

## 10. Quality habits that separate “hobby” from “professional”

- **Traceability** — Requirement → design paragraph → code location → test case.
- **Single source of truth** — One official I/O list; one official program export per release.
- **Explicit fault behavior** — Every automatic action has a defined safe state and operator path.
- **No magic numbers** — Constants, named parameters, documented timers.
- **Test before production** — Simulate I/O where possible; never “try it live” for interlocks.

---

*This guide is generic; adapt depth to project size (small machine vs. line vs. plant). For regulated industries, add formal design controls, validation protocols, and electronic signatures as required.*
