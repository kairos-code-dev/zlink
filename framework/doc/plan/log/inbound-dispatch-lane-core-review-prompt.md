# Core clean review — byte HWM and Application/Completion connection pair

You are one of two independent reviewers for stage 2 of the plan in
`framework/doc/plan/inbound-dispatch-lane-design.ko.md` (§8.2). Review the **whole
candidate**, not just the diff. Do not assume another reviewer covers anything.

## Candidate (immutable — do not modify any file)

The coordinator fills these in per round and creates the worktree with
`git worktree add /tmp/zlink-core-candidate-<sha> <sha>`, so reviewers never read the
shared working tree.

- Candidate worktree: `/tmp/zlink-core-candidate-<candidate sha>` (read-only for you)
- Candidate commit: `<candidate sha>`
- Comparison base commit: `8bc2aa6786` (previous count-based HWM semantics)
- Full diff: run `git -C /tmp/zlink-core-candidate-<candidate sha> diff 8bc2aa6786..<candidate sha> -- core bindings/c`

You may run read-only commands (`git`, `grep`, `sed`, file reads, `ctest` is NOT
required). Do not edit, stage, commit, or build anything in the main repository at
`/home/hep7/project/kairos/zlink`.

## What the candidate is supposed to implement

Read these first; they are the contract, and the code is what must match them:

1. `framework/doc/plan/inbound-dispatch-lane-design.ko.md` — the approved design.
   Stage 1 scope is items C-01 through C-08 in §8.1 and §9.
2. `AGENTS.md`
3. `doc/principal/documentation/spec-writing-guide.ko.md`
4. `doc/principal/source-comment-principles.ko.md`
5. `doc/principal/software-design-principles.ko.md`
6. Core official spec/internals under `core/doc/` that stage 1 updated (socket
   options, context options, polling, monitoring, errors).
7. `framework/doc/plan/log/inbound-dispatch-lane-reqrep-multipart-rollback-review.ko.md`
   — an earlier review of the request/reply rollback issue plus §9, which records the
   defects already found and fixed in this candidate and the measured evidence.

Headline contract points: `ZLINK_OPT_SNDHWM`/`ZLINK_OPT_RCVHWM` are `uint64_t` byte
values (0 = unlimited, 4-byte legacy values are a configuration error); Auto HWM plans
in bytes; monitoring exposes 64-bit byte fields with a versioned ABI; `pipe_t` computes
payload + routing frame + metadata + minimum charge once per write and applies the same
number to admission, credit return, LWM, runtime HWM change, inproc summation,
reconnect, termination, multipart commit and rollback; an empty pipe admits one
oversize message bounded by `MaxMessageSize`; every peer has one Application and one
Completion connection whose handshake validates peer identity, pair identity and
generation; requests and application messages go on the Application connection while
replies and progress control go on the Completion connection; the internal PAIR
`recv_queue`, the reply payload deque and the signal socket are removed; incomplete
multiparts are never exposed to a reader.

## Questions you must answer (all five, over the whole candidate)

1. Contract fidelity: anything in the design document's instructions, formulas,
   breaking contracts, removals, error semantics, monitoring or verification items that
   is misread or not implemented? Any compatibility path or extra behaviour introduced
   that the document does not ask for?
2. POSD: shallow modules, information leakage, pass-through methods or variables,
   temporal decomposition, duplication, special-case logic mixed into general paths,
   complexity pushed onto callers.
3. DDD: is the owner of byte accounting, pipe admission, connection pair, request
   completion, ownership, lifecycle, state transition and failure invariants
   unambiguous? Are Core / bindings / Framework responsibilities mixed, or are
   transport, codec or storage details leaking into higher-level contracts?
4. Hot path: unnecessary allocation, payload re-traversal, copy, atomic, lock, system
   call, branch, queue scan or cache contention added? Room to improve memory
   amplification, throughput, CPU per message, p99 latency or multi-connection scaling?
5. Leftovers: any queue, deque, signal socket or count-compat path that was supposed to
   be removed but is still present; unused code, options or tests that blur the new
   contract.

## Severity definitions (use exactly these)

- `Critical` — data corruption, security issue, ABI/wire breakage, deadlock, or
  unbounded memory growth.
- `High` — violates a core contract or invariant, or very likely causes a large
  correctness/performance regression in a real workload.
- `Medium` — a gap, responsibility-boundary leak, concrete refactoring target or
  measurable performance risk that must be fixed before the next stage.
- `Low` — optional improvement that does not endanger the current contract or the next
  stage.

Only report what you can support with file and line evidence from the candidate. Do not
inflate severity, and do not report style preferences as `Medium`. If you find nothing
at `Medium` or above, say so explicitly — a clean verdict is a valid and useful result.

## Report format

Write your report to the output path given to you, as GitHub-flavoured markdown:

```
# <reviewer name> — Core clean review round <n>

- reviewer: <name>
- model id and version: <exact>
- reasoning level: <exact>
- candidate SHA: <candidate sha>
- comparison base: 8bc2aa6786
- run started (local time): <YYYY-MM-DD HH:MM>
- verdict: CLEAN | NOT CLEAN

## Findings

### <ID> — <one-line title>
- severity: Critical | High | Medium | Low
- category: contract | POSD | DDD | hot-path | leftover
- file:line: <path>:<line>
- evidence: <what the code does, quoted or precisely described>
- violated instruction or principle: <which document, which rule>
- impact: <concrete consequence>
- alternative A: <...>
- alternative B: <...>
- recommendation: <which alternative and why, in terms of simplicity, generality,
  performance and caller burden>

## Answers to the five questions

<one short paragraph each>

## Notes and non-findings

<what you checked and found correct; anything you could not verify>
```

For a non-trivial structural finding you must give at least two alternatives and compare
them. For a performance finding, say what to measure on the critical path first and
under what condition the added complexity should be reverted.
