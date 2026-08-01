# RouteMesh 11.0.0 남은 작업과 진행 가이드

이 문서가 남은 작업의 정본이다. `route-mesh-11.0.0-execution-ledger.ko.md`(12,000줄)와
`blocked-issue-log.md`(6,700줄)는 지나온 근거로 보존하며, 진행 판단에는 사용하지 않는다. 과거
근거가 필요할 때만 해당 문서의 정확한 절을 링크한다.

작성 시점은 2026-08-01이다.

## 1. 시작 전에 반드시 확인할 것

오늘 하루에 같은 실수를 네 번 반복했다. 각각 30분에서 두 시간을 썼다. 작업을 시작하기 전에
이 절을 먼저 읽는다.

### 1.1 저장소 규칙을 먼저 읽는다

`CLAUDE.md`는 `AGENTS.md`를 단일 기준으로 지정한다. `AGENTS.md`에는 배포 절차, 문서 위치,
바인딩 사용 규칙, 설계 원칙이 모두 있다. 읽지 않고 시작하면 이미 문서화된 절차를 실측으로
다시 발견하게 된다.

### 1.2 Core를 고쳤으면 배포까지가 한 작업이다

Core 수정은 소스 커밋으로 끝나지 않는다. framework는 bindings 소스를 참조하지 않는다.

```text
core 빌드 → native/sync-local-core-libs.sh → 언어별 package 생성 → framework 참조 버전
```

이 단계를 건너뛰어도 **실패하지 않는다.** 옛 라이브러리가 그대로 쓰인다. `.NET`은
`~/.nuget/packages/systems.zlink/<ver>/runtimes/`에 이미 풀린 native를 읽으므로, 같은 버전으로
package를 다시 만들어도 추출된 캐시를 지우지 않으면 갱신되지 않는다.

**Core에 넣은 추적이 아무것도 찍히지 않으면 코드가 안 도는 것이 아니라 배포가 안 된 것을 먼저
의심한다.**

절차는 [`scripts/local-package/README.ko.md`](../../../../scripts/local-package/README.ko.md)에 있다.

### 1.3 원인은 flow 로그에서 먼저 찾는다

Framework에는 message-flow tracing이 있다(spec
[26-message-flow-tracing.ko.md](../../framework/common/spec/26-message-flow-tracing.ko.md)).
`dropped`와 `error` 단계는 기본 진단 수준에서 항상 기록되고, e2e의 `E2eMessageFlowListener`가
`<rid>-flow.log` 파일로 남긴다.

무진단 폐기 지점을 만나면 임시 `fprintf`나 debug 출력을 심지 말고, 그 자리를 flow trace로
만든다. 그러면 다음 사람이 같은 자리를 다시 파지 않는다. 오늘 심은 것은 다음 넷이다.

| trace | 무엇을 말하는가 |
|---|---|
| `session_disconnect_notify_failed` | disconnect 통지가 session node를 떠나지 못했다 |
| `session_disconnect_ignored` | disconnect frame이 현재 binding과 맞지 않아 버려졌다 |
| `session_disconnect_applied` | owner node가 disconnect를 적용했다 |
| `relay_identity_stale` | relay frame의 신원 9개 필드 중 무엇이 어긋났는가 |

Flow listener가 없는 스위트가 아직 있다. 조사 전에 해당 host에
`E2eMessageFlowListener`를 붙이는 편이 매번 다시 빌드하는 것보다 빠르다.

### 1.4 측정은 격리해서 한다

- e2e를 동시에 돌리면 결과가 오염된다. 포트는 무작위라 충돌하지 않지만 CPU 경합으로 timeout이
  난다. 병렬로 돌릴 때는 4개까지만 두고, 결과가 기존과 크게 다르면 단독으로 다시 확인한다.
- 끝나지 않은 `dotnet test`가 남아 있으면 다음 실행과 경합해 `Test Run Aborted`와 잘린 통과
  수(405, 517)를 낸다. 전체 수를 볼 때는 남은 test host가 없는지 먼저 확인한다.
- 단독 실행과 전체 실행의 결과가 다르면 순서 의존이다. 회귀로 단정하기 전에 수정 전 커밋에서
  같은 필터로 한 번 더 확인한다.

### 1.5 계약 판단은 spec이 소유한다

구현이나 e2e가 spec과 다르면 spec이 기준이다. 오늘 두 건이 e2e 기대가 낡은 경우였다
(TA-A2 session-not-bound kind, TD-A1 terminator 표면). 다만 spec에 근거가 없는 내용을 내
추론으로 채우지 않는다. 오늘 "HTTP client는 framework를 모르니 Yield를 노출할 수 없다"고 적었으나
그것은 spec에 없는 추론이었고, 실제 판단은 사용자가 내렸다.

## 2. `.NET` 남은 항목

`PubSub`와 `RegistrationCodec`는 전량 통과한다.

| 스위트 | 진행 | 막힌 지점 | 성격 |
|---|---|---|---|
| PubSub | 7 / 7 | — | 완료 |
| RegistrationCodec | 11 / 11 | — | 완료 |
| AutomaticTurnDispatch | 19 / 27 | TD-E3 동시 반대 join 중 하나가 `accepted=False`로 거부 | 조사 중 |
| SpotActorTransfer | 7 / 36 | ST-C3 transfer-out 실패가 accepted를 반환 | 미조사 |
| SpotService | 7 / 58 | SM-B6 cross-node join이 seal 단계에서 fenced | 원인 확인됨 |
| ObservabilityOps | 8 / 21 | OBS-C1 draining marker evidence | 미조사 |
| RuntimeMonitoring | 2 / 8 | HTTP 500 | 미조사 |
| ToActorMessaging | 3 / 7 | TA-A4 destroy 뒤 요청이 `NotFound` 대신 `Unavailable` | 계약 대조 필요 |
| ResilienceLifecycle | 0 / 20 | RL-A1 재시작 뒤 같은 endpoint·새 owner generation이 안 나온다 | 미조사 |
| LocationMessaging | 0 / 15 | RM-A4 v1이 terminal `Stopped`에 도달하지 않는다 | 미조사 |
| StoreFailure | 0 / 10 | SF-B1 store 정지 중 요청이 408 | fail-static 위반 |
| SubmitAdmission | 0 / 8 | readiness에서 peer를 모른다 | 원인 좁혀짐 |
| ChannelEgressRouting | 부분 / 6 | 선택자 4개 미구현 (CH-E2E-03, CH-E2E-08, CH-REG-02, CH-REG-05) | 구현 필요 |

합계는 시나리오 **64 / 234** 통과다. 통과 수는 시나리오 파일 수 기준이며, feature map의 요구
ID 수는 스위트에 따라 더 많다(예: `StoreFailure` 28, `ResilienceLifecycle` 39). 즉 시나리오가
전부 통과해도 feature map 기준으로는 추가 검증이 남는 스위트가 있다.

단위 테스트는 1375/1376이다. 남은 1건
(`LocationRuntimeQueryTests.Status_Reports_The_Most_Recent_Success_Across_Reads_And_Lease_Renewal`)은
순서 의존 기존 결함이며 오늘 변경과 무관함을 별도 worktree에서 확인했다.

### 2.1 원인이 확인된 항목

**SM-B6 cross-node join.** Entry actor는 한 play node에, spot은 다른 play node에 생긴다. Spot
쪽은 `spot-actor-admitted`까지 진행하고 join completion이 실패한다.

```
deferred_join_failed kind=InvalidOperation retriable=False
  Actor '...' session ingress seal was fenced by its binding identity.
```

`ZLinkSessionActorBindingTable.SealRouteAsync`가 binding generation·route fence·session owner
generation 일치를 요구하고, 어긋나면 `Acknowledged=false`로 답한다.

**SubmitAdmission readiness.** 원인이 둘이었고 각각 단독으로 handshake를 막았다. 하나는
`ReceiverGate`가 TCP 연결을 하나만 accept하던 것이고(수정 완료 — mesh peer 하나는 application과
completion 두 연결이다), 다른 하나는 caller router socket의 `SendHighWaterMark = 1`이다.
completion pair의 budget은 분리했으므로 남은 것은 application lane 쪽이다.

### 2.2 배포 경로 복구

Core 수정 두 건이 소스에만 있고 package에 없다.

- `core/src/api/socket/socket_request_reply_internal.hpp` — reply 정합
- `core/src/runtime/core/transport_pair_policy.hpp` — completion pair budget

`scripts/local-package/dotnet/build-wsl.sh`는 review를 통과한 `V11-M3-CORE-VERIFY` candidate
manifest를 요구하고, 봉인된 파일 hash가 현재 worktree와 정확히 같아야 한다. 최신 candidate는 이
수정들보다 앞서 봉인됐다. 새 candidate는 만들어 두었다.

```
.artifacts/v11/evidence/V11-M3-CORE-VERIFY/candidate-reply-match-completion-hwm-20260801.json
```

남은 것은 자체 R2 review evidence 작성, Core package 생성, `.NET` package 생성, 소비자 검증이다.
독립 리뷰어(codex)는 사용할 수 없으므로 자체 리뷰로 대체하고 evidence에 그 사실을 명시한다.

## 3. 다른 언어

`.NET` 완료 뒤에 진행한다. 상태는 기존 ledger의 §9.1 이후 checkpoint를 참고한다. 요지는 다음과
같다.

- Node: contract test가 파일 단위로 절반쯤 남았고 5개 파일은 hang한다(`BLK-009`).
  `backend-contract`의 남은 4건은 `BLK-006` 계약 판정 대기다.
- C++: ClientServer listener와 queue-free relocation restore checkpoint가 열려 있다.
- Java/Kotlin: checkpoint만 잡혀 있고 실제 결함 밀도는 아직 모른다.

`Yield`를 서버 HTTP request builder에 추가한 계약 변경은 다섯 언어 모두에 반영해야 한다. 오늘은
`.NET`만 구현했다.

## 4. 오늘 해결한 것

근거는 `blocked-issue-log.md`의 같은 날짜 절에 있다.

| 수정 | 위치 | 효과 |
|---|---|---|
| pending을 intent rid로 걸고 응답은 정착한 rid로 도착 | core reply 정합 | RegistrationCodec 10 → 11 전량 |
| completion pair가 application HWM을 따라 내려감 | core transport pair policy | LocationMessaging readiness 통과 |
| 원격 disconnect 통지가 relay 신원을 싣지 않음 | framework bound session | TA-A2·A3 통과, SpotService 5 → 7 |
| gate가 TCP 연결을 하나만 accept | SubmitAdmission harness | transport pair 두 연결 전달 |
| session-not-bound 기대가 spec보다 낡음 | ToActorMessaging e2e | TA-A2·TA-A3 통과 |
| `Fetch`를 금지 terminator로 오인 | AutomaticTurnDispatch e2e | 0 → 19 통과 |
| socket·threading 문서가 `.en.md`로 바뀐 뒤 경로 미갱신 | core 테스트 | ctest 84/84 복구 |
| SpotWide에서 서버 HTTP builder가 `Yield` 제공 | http-client 계약 | 신규 기능 |
