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

`.NET` e2e는 첫 실패에서 중단한다. 따라서 실패 시나리오 뒤의 항목은 "실패"가 아니라
**아직 검증되지 않음**이다. 아래 표는 그 구분을 유지한다.

| 스위트 | 통과 | 막고 있는 시나리오 | 그 뒤 미검증 |
|---|---|---|---|
| PubSub | 7 / 7 | — | — |
| RegistrationCodec | 11 / 11 | — | — |
| AutomaticTurnDispatch | 19 / 27 | **TD-E3** 동시 반대 join 중 하나가 `accepted=False` | TD-F1~F6, TD-G1 |
| ObservabilityOps | 8 / 21 | **OBS-C1** `host-state` evidence 미관측 | OBS-C2~C12 |
| SpotActorTransfer | 7 / 31 | **ST-C3** leave 실패 case의 완료 판정 | ST-B2~I6 |
| SpotService | 7 / 55 | **SM-B6** bound session이 handoff를 따라가지 않음 | SM-A1~G5 |
| LocationMessaging | 5 / 15 | **RM-B2** drain 중 select-one이 재선택하지 않음 | RM-B3, RM-C1~C9 |
| ToActorMessaging | 3 / 7 | **TA-A4** destroy 뒤 `NotFound` 대신 `Unavailable` | TA-B1~B3 |
| RuntimeMonitoring | 2 / 7 | **MON-A5** location store 이벤트 미관측 | MON-C1, MON-D1 |
| ResilienceLifecycle | 1 / 20 | **RL-A2** SIGKILL 뒤 descriptor가 lease 만료로 제외되지 않음 | RL-A3~D5 |
| StoreFailure | 0 / 10 | **SF-B1** store 정지 중 요청이 408 | SF-A1~E1 |
| SubmitAdmission | 0 / 8 | readiness에서 peer를 모름 | 전체 |
| ChannelEgressRouting | 부분 | 선택자 4개 미구현 | CH-E2E-03, CH-E2E-08, CH-REG-02, CH-REG-05 |

합계 **70 / 234** 통과다(2026-08-01 검증 실행 기준). 막고 있는 시나리오는 11개이므로, 그 11개를
풀면 나머지 164개가 비로소 검증 대상이 된다. 지금 수치는 "164개가 실패한다"는 뜻이 아니다.

Feature map의 요구 ID 수가 시나리오 파일 수보다 많은 스위트가 있다(`StoreFailure` 28,
`ResilienceLifecycle` 39, `PubSub` 21). 시나리오가 전부 통과해도 그 차이는 별도 검증으로 남는다.

`SpotService`와 `ToActorMessaging`은 시나리오별 통과를 stdout에 남기지 않는다. 두 스위트의 통과
수는 evidence와 실행 관찰에서 얻은 값이다. 진행 상황을 파일에서 바로 읽으려면 다른 스위트처럼
시나리오마다 한 줄씩 남기도록 harness를 맞추는 편이 낫다.

단위 테스트는 1375/1376이다. 남은 1건
(`LocationRuntimeQueryTests.Status_Reports_The_Most_Recent_Success_Across_Reads_And_Lease_Renewal`)은
순서 의존 기존 결함이며 오늘 변경과 무관함을 별도 worktree에서 확인했다.

### 2.1 남은 blocker의 성격

오늘 하루 원인을 열어본 결과, 성격이 셋으로 갈린다. **시나리오 코드가 v11 이전 기대를 들고 있는
경우가 가장 많다.** v11 전환에서 runtime과 시나리오 spec만 갱신했고 e2e 코드는 그대로 남았기
때문이다. 그래서 새 blocker를 만나면 runtime을 파기 전에 해당 config 문서와 코드를 먼저 대조한다.

| 성격 | 오늘 확인 | 예 |
|---|---|---|
| 시나리오 코드가 낡음 | 6건 | endpoint로 peer 식별(spec 24 §7이 감춤), deferred ack에 caller success 단언, `Fetch`를 금지 terminator로 오인, deferred join 완료 전 연결 절단 |
| runtime 결함 | 4건 | reply 정합, completion pair budget, 원격 disconnect 신원, 이동할 unit이 없는 host를 target 없다고 차단 |
| e2e 문서가 spec과 충돌 | 1건 | config 5가 `Unavailable`, spec 06 결과표는 `NotFound` |

특히 되풀이된 함정 하나를 적어 둔다. **join과 destroy 요청의 응답은 deferred intent의 확인일
뿐이다.** 거기에 대고 성공·실패를 단언하면 항상 통과하거나 항상 실패한다. 실제 결과는 completion
callback이나 evidence가 나른다. SM-B6와 ST-C3가 같은 이유로 막혀 있었다.

### 2.2 원인이 확정된 미해결 항목

**SM-B6 — cross-node join이 session route commit을 거치지 않는다.** Join 자체는 통과한다. Actor는
entry node를 떠나 target node에서 `spot-actor-joined`까지 간다. Session route seal도 성립한다.

```
session-a : route_seal_drain    active_frames=0 waits=False
session-a : route_seal_replying ack=True
session-a : session_binding_removed  by=UnbindSessionActor   (transport 절단 뒤 cleanup)
```

Seal 뒤에 commit이 오지 않는다. Session 쪽 binding은 sealed 상태로 남고 계속 source node를
가리킨다. 그래서 disconnect 통지가 actor가 실제로 join한 spot이 아니라 원래 node로 간다.

Commit을 보내는 곳은 `CompleteRoutedActorHandoffAsync`이고, 이 메서드는 결과를
`session_route_commit_acknowledged` 또는 `_not_required`로 남긴다. Target node의 handoff log에는
`capture_entry`와 `location_committed`는 있는데 이 표시가 **없다.** 즉 cross-node join은
`CompleteRoutedActorHandoffAsync`를 거치지 않는 다른 경로로 actor를 옮긴다.

다음은 그 경로를 찾아 session route commit을 같은 자리에 붙이는 것이다. Commit 자체는 구현되어
있고(`ZLinkSessionRouteCommitHandler` → `CommitSessionActorRoute`) target actor ref로 binding route를
갱신하므로, 빠진 것은 호출이지 기능이 아니다.

**RM-B2 — mesh 계층에 drain 표시가 구현되어 있지 않다.** Provider가 drain 중일 때 select-one이 그
member를 고르고 `Rejected`(admission sealed)를 caller에게 그대로 돌려준다. 원인을 끝까지 보면 표시
자체가 없다.

```
MeshNodeState.Draining  — 대입하는 코드 0곳 (읽는 곳만 있음)
MeshPeerState.Draining  — 대입하는 코드 0곳 (읽는 곳만 있음)
```

두 enum 값 모두 정의되어 있고 status 매핑과 wrapper에서 읽히지만, 어디에서도 설정되지 않는다.
그래서 `ChannelSelectionFailureReason`의 `_state == MeshNodeState.Draining` 분기는 죽은 코드이고,
`SnapshotChannelTargets`는 `peer.Admitted`만 보므로 drain 중인 peer가 후보로 남는다.

Spec 28 §567은 "`Draining` descriptor를 게시해 새 selection과 placement에서 제외한다"고 정한다.
Config 1 RM-B2는 "`Draining` 전파 구간을 포함해 모두 정상 reply로 끝난다"를 요구한다. 따라서 필요한
것은 세 가지다. 종료를 시작한 node가 자신을 `Draining`으로 표시하고, 그 사실을 peer에게 알리고,
select-one 후보에서 제외하는 것이다. 전파 구간의 경합까지 덮으려면 admission 전 거부에서 재선택도
필요하다. Spec 06 §679가 성공한 admission 전까지 재선택을 허용한다.

**TA-A4.** Actor destroy 뒤 같은 ID 호출이 `Unavailable`로 끝난다. Config 9는 `NotFound`로 분류할
것을 요구하고, spec 06 결과표도 authority가 없으면 `NotFound`다.

**SubmitAdmission.** `ReceiverGate`가 transport pair 두 연결을 모두 나르도록 고쳤으나 readiness는
아직 통과하지 않는다. 남은 용의자는 caller router socket의 `SendHighWaterMark = 1`이 application
lane에 그대로 남아 있는 것이다.

**OBS-C1.** `host-state|` evidence가 관측되지 않는다. Observer는 등록되어 있고 host status stream은
구독 시 현재 상태를 한 번 쓴다. Evidence는 HTTP로만 노출되므로 로그 파일 grep으로는 판정할 수
없다. 다음은 `/evidence`를 직접 읽어 host-state 항목 유무를 확인하는 것이다.

### 2.2 배포 경로 (해소)

Core 수정 두 건이 소스에만 있던 상태를 닫았다. 절차는 `AGENTS.md`와
[`scripts/local-package/README.ko.md`](../../../../scripts/local-package/README.ko.md)가 정한 그대로다.

| 단계 | 산출물 |
|---|---|
| candidate manifest | `.artifacts/v11/evidence/V11-M3-CORE-VERIFY/candidate-reply-match-completion-hwm-20260801.json` |
| 자체 R2 review | `.artifacts/v11/evidence/V11-R2/core-candidate-reply-match-completion-hwm-review-20260801.json` |
| Core package | `.artifacts/wsl/install/zlink-core/11.1.0` (clean C consumer 통과) |
| `.NET` package | `.artifacts/wsl/nuget/Systems.Zlink.11.1.0.nupkg` (isolated NuGet consumer 통과) |

독립 리뷰어(codex)를 쓸 수 없어 자체 리뷰로 대체했고, evidence의 `independent: false`와
`independenceWaiver`에 그 사실을 남겼다.

소비자 쪽에서도 확인했다. 추출된 package를 지우고 nupkg에서 새로 풀린 native로 RegistrationCodec를
돌려 11개 전량이 통과한다. 이 통과에는 reply 정합 수정이 필요하므로, 구성상 그럴 것이라는 추정이
아니라 실제 확인이다. Core ctest는 경합 없는 실행에서 84/84다.

`.artifacts`는 gitignore 대상이므로 이 산출물은 저장소에 들어가지 않는다. 다른 기계에서 다시
만들려면 위 네 단계를 같은 순서로 실행한다.

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
