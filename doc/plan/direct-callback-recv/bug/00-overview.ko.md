# Spot Shutdown / Teardown 버그 개요

---

## 타임라인

| 시점 | 이슈 | 상태 |
|------|------|------|
| `17e69e79` 이후 | split ctest timeout, ctx_term 무기한 블로킹 | 분석 시작 |
| `73dfc80f` | BUG-01 수정: wait_drained socket tracking 보존 | **해결** |
| `73dfc80f` 이후 | `_term_acks > 0` assertion (stress) | BUG-02 분석 완료 |

---

## 버그 목록

### [BUG-01](01-wait-drained-socket-tracking-loss.ko.md): wait_drained 소켓 추적 유실

- **상태**: 수정 완료
- **계층**: service lifecycle (`service_runtime_base_t`)
- **요약**: `wait_drained()` 타임아웃 시 `_closing_sockets`를 swap으로 빼간 뒤
  로컬 변수가 drop되면서 소켓 추적이 유실된다. abortive 경로가 no-op이 된다.

### [BUG-02](02-async-mailbox-reaper-data-race.ko.md): async mailbox vs reaper data race

- **상태**: 분석 완료, 수정 대기
- **계층**: socket core (`socket_base_t`, `own_t`)
- **직접 트리거**: direct callback의 async mailbox가 I/O 스레드에서 소켓 명령을
  처리하는 동안 reaper 스레드가 같은 소켓의 `process_term()`을 실행하여
  `_pipes`/`_term_acks`에 동시 접근한다 (data race).
- **구조적 배경**: 이 race는 더 깊은 이중 종료 모델 문제의 트리거다.
  아래 "구조적 원인 분석" 참조.

---

## 구조적 원인 분석

### 왜 하나 고치면 다른 문제가 나오는가

개별 버그를 수정해도 다른 형태의 실패가 반복되는 이유는
**단일 코드 결함이 아니라 종료 모델 자체가 이중 구조**라서다.

### 이중 종료 모델

현재 종료 완료 판정이 두 곳에서 독립적으로 이루어진다.

```
[spot 계층]                              [core 계층]
service_runtime_base_t                   own_t / socket_base_t / pipe_t
 ├─ owned_sockets                        ├─ _term_acks (pipe + owned 혼합)
 ├─ closing_sockets                      ├─ _pipes
 ├─ wait_drained() → "종료 완료"         ├─ _owned
 └─ force_wait_remaining()               ├─ check_term_acks() → "destroy 가능"
                                         └─ ctx_t::destroy_socket()
```

spot은 "내 tracked socket이 다 사라졌는가"를 자체적으로 판단하고,
core는 "pipe termination graph가 수렴했는가"를 따로 판단한다.

두 판단이 어긋나면:
- **timeout 방향**: spot은 종료했다고 보는데 core는 아직 socket을 들고 있음 → ctx_term hang
- **assertion 방향**: pipe completion이 core ack 카운터와 맞지 않음 → `_term_acks > 0`

이 둘은 같은 구조적 문제의 양면이다.

### `_term_acks` 혼합 카운터 문제

`own_t::_term_acks`가 두 가지를 같은 카운터로 센다.

| 등록 경로 | 해제 경로 |
|-----------|-----------|
| `register_term_acks(_pipes.size())` | `pipe_terminated()` → `unregister_term_ack()` |
| `register_term_acks(_owned.size())` | `process_term_ack()` → `unregister_term_ack()` |
| `attach_pipe()` 중 `register_term_acks(1)` | (같은 pipe의 terminated) |

pipe completion과 owned-object completion이 한 카운터에 섞여 있어
어느 쪽에서 불일치가 생겼는지 구분하기 어렵다.

### async mailbox race는 트리거

[BUG-02](02-async-mailbox-reaper-data-race.ko.md)에서 분석한
I/O 스레드 vs reaper 간 data race는 이 구조 위에서 작동하는 **직접 트리거**다.

async mailbox race가 없더라도 spot의 teardown 순서가 조금만 흔들리면
같은 계열의 ack 불일치가 다른 경로로 드러날 수 있다:

- data plane thread가 아직 활성인 상태에서 attachment close
- `term_endpoint()` 선호출로 pipe 상태가 미리 변경
- `ctx_t::terminate()` pending inproc 해소 중 late pipe attach

---

## 해결 방향 요약

### 즉시 적용 (BUG-02 트리거 차단)

`close()` 전에 async mailbox를 완전히 quiesce하여 data race를 막는다.
이것만으로 현재 재현되는 assertion과 timeout의 직접 트리거를 차단할 수 있다.

→ 상세: [BUG-02 해결 방향](02-async-mailbox-reaper-data-race.ko.md#해결-방향)

### 구조 개선 (반복 방지)

같은 계열의 문제가 다른 경로로 다시 드러나지 않으려면
종료 모델 자체를 정리해야 한다:

1. **core를 종료 완료의 단일 source of truth로** —
   spot은 "무엇을 언제 멈출지"만 결정하고, 실제 종료 완료는 core가 판정

2. **pipe ack와 owned-object ack 분리** —
   `own_t::_term_acks`에서 pipe termination을 분리하여
   `socket_base_t` 전용 pending pipe state로 이동

3. **teardown 순서 quiesce-first** —
   data plane join → handle destroy → socket removal wait 순서로 통일

4. **destroy strict fail-fast** —
   graceful/abortive 모두 수렴 실패 시 error 반환,
   `ctx_term()`으로 실패를 밀어내지 않음

→ 상세: [구조 개선안](codex-spot-shutdown-structural-rework.ko.md),
  [lifecycle contract 분석](deterministic-lifecycle-root-cause-review.ko.md)

---

## 문서 목록

| 문서 | 내용 |
|------|------|
| [BUG-01](01-wait-drained-socket-tracking-loss.ko.md) | wait_drained 추적 유실 — **수정 완료** |
| [BUG-02](02-async-mailbox-reaper-data-race.ko.md) | async mailbox/reaper data race — 트리거 분석 |
| [구조 개선안](codex-spot-shutdown-structural-rework.ko.md) | core 이중 모델 해소, teardown 순서 개선 |
| [lifecycle contract](deterministic-lifecycle-root-cause-review.ko.md) | 전체 서비스 lifecycle 공통 계약 부재 분석 |
