# .NET SpotActorTransfer E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-10-spot-actor-relocation.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| ST-A1 | 구현 | 동일 stable type과 node-wide placement weight를 사용한 local join 승인 순서와 packet handler evidence를 실제 process `logs/20260727-230711-505861`에서 검증했다. |
| ST-A2 | 구현 | local join 거절 뒤 actor 상태와 handler side effect가 없음을 검증한다. |
| ST-A3 | 구현 | target joined callback 완료 전 packet dispatch가 시작되지 않음을 검증한다. |
| ST-B1 | 전환 대상 | 기존 remote transfer 시나리오를 relocation admission, immutable payload, authority commit과 target Ready 순서로 전환해야 한다. |
| ST-B2 | 전환 대상 | source cleanup 실패 뒤 committed relocation을 유지하고 durable cleanup을 재개하는 현재 계약으로 전환해야 한다. |
| ST-B3 | 전환 대상 | adapter 없는 Actor는 Recreate policy에서만 허용하는 현재 relocation 계약으로 전환해야 한다. |
| ST-B4 | 전환 대상 | 명시적 empty state를 Snapshot adapter의 정상 payload로 처리하는 현재 relocation 계약으로 전환해야 한다. |
| ST-C1 | 구현 | target admission 뒤 source process 종료 시 transfer 실패와 복구를 검증한다. |
| ST-C2 | 구현 | target commit 뒤 source process 종료 시 target actor가 유지됨을 검증한다. |
| ST-C3 | 구현 | callback 실패와 transfer 실패의 public error 분류를 검증한다. |
| ST-D1 | 구현 | target commit 전후 location row의 owner와 generation 전환 시점을 검증한다. |
| ST-D2 | 구현 | stale source release가 새 generation location을 제거하지 못함을 검증한다. |
| ST-E1 | 구현 | remote transfer가 Actor의 ObjectGeneration을 유지하고 canonical authority와 bound session route를 target owner로 전환한 뒤 push를 계속 전달하는지 검증한다. 실제 process 실행은 `logs/20260725-094700-2778437`에서 통과했다. |
| ST-E1A | 구현 | bound Actor를 destroy하고 같은 ActorId를 새 ObjectGeneration으로 만든 뒤 이전 binding의 request가 `ActorLocationStale`로 끝나는지 검증한다. 새 generation은 explicit bind 뒤에만 등록되며, 같은 Session에 bind된 다른 Actor의 route와 push도 유지되는지 함께 확인한다. 실제 process 실행은 `logs/20260725-094700-2778437`에서 통과했다. |
| ST-E2 | 구현 | 실패한 transfer가 기존 bound session route를 바꾸지 않음을 검증한다. |
| ST-F1 | 구현 | handoff 중 도착한 packet의 순서와 target replay를 검증한다. |
| ST-F2 | 구현 | direct packet이 handoff backlog를 추월하지 않음을 검증한다. |
| ST-F3 | 구현 | 같은 session의 `S1,S2,S3,S4` 순서와 다른 session 진행 격리를 묶음 반복 `logs/20260720-042454-2074240`, `logs/20260720-042515-2075011`, `logs/20260720-042523-2076425`에서 검증했다. |
| ST-F3A | 미구현 | Session owner pause와 owner lease fence를 실제 process에서 검증하는 시나리오가 없다. |
| ST-F4 | fixture 구현·process 재검증 대기 | Transport delivery gate가 public resolver가 commit 전에 선택한 delivery 두 건을 operation ID별로 지연한다. Caller는 global Actor ID만 사용한다. Duration 안 release는 target application handler가 정확히 한 번 처리하고 source handler는 처리하지 않는지 확인한다. Duration 뒤 release는 `ActorLocationStale`이고 target handler는 0회인지 확인한다. Runtime log marker는 완료 증거로 사용하지 않는다. |
| ST-F5 | 부분 구현·process 재검증 대기 | 첫 owner에서 선택한 delivery를 지연한 뒤 세 node로 두 번 relocation하고 release한다. Public `Find`가 반환한 final owner handler가 정확히 한 번 처리하고 이전 두 owner handler는 처리하지 않는지 확인한다. Duration 뒤 stale terminal도 확인한다. Route entry의 실제 제거와 next-hop 내부값은 public black-box로 관찰할 수 없으므로 process 완료 증거가 아니며 provider/runtime contract test가 별도로 필요하다. |
| ST-F6 | 구현 | handoff 중 request의 원래 caller completion 상관관계와 늦은 reply timeout 격리를 같은 세 실행에서 검증했다. |
| ST-G1 | 미구현 | SpotWide·PerActor의 yielded continuation과 모든 실행 lane을 포함한 relocation barrier E2E가 없다. |
| ST-G2 | 미구현 | Actor 10,000개 inventory chunk와 typed capacity aggregate all-or-none E2E가 없다. |
| ST-G3 | 미구현 | PerActor Spot authority 선전환과 Actor별 source·target route 분할 E2E가 없다. |
| ST-G4 | 미구현 | 이동 중 stale `ToActor` Message Follow와 target queue 순서를 검증하는 E2E가 없다. |
| ST-G5 | 미구현 | Entry·PerActor Actor relocation interruption 1초 목표와 초과 시 계속 진행을 검증하는 E2E가 없다. |
| ST-G6 | 미구현 | `ApplicationSignaled` readiness와 completion callback의 source·target owner를 검증하는 E2E가 없다. |
| ST-H1 | 미구현 | Deferred Join 등록, immutable request와 Actor queue barrier E2E가 없다. |
| ST-H2 | 미구현 | Join completion outcome, 128-bit operation ID와 crash recovery E2E가 없다. |
| ST-H3 | 미구현 | Context identity와 relocation 이후 source fence E2E가 없다. |
| ST-H4 | 미구현 | 허용 execution context, 중복 등록과 relocation error parity E2E가 없다. |
| ST-H4A | 미구현 | Deferred Join 등록량·payload·timeout 경계와 Relocate·Shutdown race E2E가 없다. |
| ST-H4B | 미구현 | Join 뒤 Yield, awaited cycle과 reply terminal E2E가 없다. |
| ST-H5 | 미구현 | MessageContext와 Actor handler signature parity를 실제 transport로 검증하는 E2E가 없다. |
| ST-I1 | 부분 구현·diagnostic only | Actor는 4 KiB·64 KiB·8 MiB·64 MiB, Instance Spot과 SpotWide는 64 KiB·1 MiB·32 MiB·64 MiB profile을 사용한다. 모두 실제 cross-node relocation 뒤 `Restore`, public location, deterministic SHA와 opaque Store read-back을 확인한다. 이전 Instance activation-only 결과는 relocation 증거에서 제외했다. Actor adapter가 정확히 64 MiB를 반환하는 독립 selector도 있다. SpotWide 64 MiB 실행 `20260728-003937-1922103`은 typed failure 없이 외부 5분 제한에 도달했다. 수정한 Instance 실행 `20260728-022204-146992`도 네 fixture 생성 뒤 source `Capture` 3건, target `Restore` 0건에서 5분 relocation deadline과 HTTP 499로 끝났다. 완료 증거가 아니다. Queue·journal·timer, permit contention, 320 MiB·5-participant aggregate가 미구현이므로 전체 `ST-I1`은 `diagnostic_only`로 표시한다. |
| ST-I2 | 부분 구현·diagnostic only | `ST-I2-RECREATE`와 `ST-I2-SNAPSHOT`을 분리했다. 각 selector는 fresh host process에서 10,000/180초·64 units/s와 1,000/90초·16 units/s를 독립 계산한다. Moving target request·one-way가 0건이면 실패한다. Actor request는 original operation과 connection-scoped correlation을 일대일로 대조하고 duplicate 0건을 확인한다. Handler evidence는 sequence 집합이 아니라 도착 순서로 검증한다. Public terminal·최종 location·`ObjectGeneration`·final owner admission도 확인한다. 1초 interruption, encoded bytes/s, payload latency, CPU, peak RSS와 Store byte는 아직 측정하지 않으므로 두 selector는 `diagnostic_only`다. 현재 정본 규모는 creation-reservation production blocker를 우회하지 않고 재검증해야 한다. |
| ST-I3 | 부분 구현·diagnostic only | Instance Spot 1,000개와 SpotWide 100개×Actor 100개, control traffic 측정 scenario를 연결했다. Spot message flow는 relocation traffic 시작 전 flow ID watermark 이후의 received·replied pair만 사용한다. Public terminal·최종 location·generation과 final owner admission은 확인하지만, SpotWide final owner equality를 atomic publication 증거로 사용하지 않는다. Commit 전 participant 0개 공개와 commit 뒤 전체 공개를 관찰하는 수단이 없어 `spotwide_pre_post_visibility` blocker를 출력한다. 1초 interruption과 resource·Store 측정도 남아 있어 두 selector는 `diagnostic_only`다. |
| ST-I4 | Actor matrix 부분 구현·process 재검증 대기 | Operation ID별 transport gate로 commit 뒤 `MF-AO-FOLLOW`와 `MF-AR-FOLLOW`를 실행한다. Final owner의 one-way·request handler가 각각 정확히 한 번 실행되고 source handler는 0회인지 확인한다. Request marker와 reply marker가 같고 original timeout 안에 끝나는지 확인한다. Runtime log marker는 제거했다. `MF-AO-QUEUE`, host relocation의 `MF-AR-HOLD`, Spot 네 case와 `MF-PA-SPLIT`은 남아 있다. |
| ST-I5 | Actor safety 부분 구현·process 재검증 대기 | Pre-resolved Actor request를 역순 release하여 correlation A/B와 reply marker를 대조한다. Original absolute deadline의 timeout, late reply 미전달, duration 뒤 `ActorLocationStale`와 target handler 0회를 public 결과로 확인한다. Runtime route marker는 사용하지 않는다. Duplicate, 이전 generation, loop, 8-hop과 1,024-message·16 MiB bound 및 Spot 조합은 남아 있다. |
| ST-I6 | Actor multi-hop production blocker | 첫 source에서 선택한 request delivery를 두 번 relocation 뒤 release한다. Public `Find`로 확인한 final owner handler가 정확히 한 번 처리하고 source·중간 owner handler는 0회인지 확인한다. 내부 relay·route cleanup marker assertion은 제거했다. 첫 hop의 benign lease-renewal StoreVersion conflict는 재시도하도록 고쳤지만 `20260728-003822-1911426`에서 두 번째 target restore·location commit 뒤 source cleanup completion이 끝나지 않았다. Route cleanup 내부값, handler operation ID, one-way, 세 번째 relocation, recovery와 Instance·SpotWide 조합도 남아 있다. |

## Message Follow case별 구현 상태

Config 10의 Message Follow 전용 section은 case별 evidence를 요구한다. 현재 `.NET` process E2E의
구현 상태는 다음과 같다. Runtime route 구현이나 unit test 통과를 process E2E 통과로 계산하지 않는다.

| 대상·경계 | One-way | Request |
|---|---|---|
| Actor, seal 전 queue | `MF-AO-QUEUE` 미구현 | Track F accepted journal과 분리 검증 필요 |
| Actor, commit 전 host ingress hold | 미구현 | `MF-AR-HOLD` 미구현 |
| Actor, commit 직후 Message Follow | assertion 연결·process 재검증 대기 | assertion 연결·process 재검증 대기 |
| Actor, duration 0·route 없음·만료 | drop evidence 미구현 | 만료 assertion 연결·process 재검증 대기. Duration 0과 route 없음은 미구현 |
| Actor, duplicate·generation·loop·hop·bound | 미구현 | Correlation·deadline assertion 연결·process 재검증 대기. 나머지는 미구현 |
| Actor, multi-hop·route cleanup | 미구현 | Final owner exactly-once black-box assertion은 연결했다. 두 번 relocation은 production blocker이며 route entry cleanup은 public observation이 없어 별도 contract test가 필요하다. Recovery도 미구현이다. |
| Instance·`SpotWide` Spot, 모든 authority 경계 | 미구현 | 미구현 |
| `PerActor` Spot·Actor split | `MF-PA-SPLIT` 미구현 | `MF-PA-SPLIT` 미구현 |

Spot public call은 global Spot ID만 받으므로, commit 전에 Framework가 선택한 delivery를 process 밖
transport에서 지연하는 harness가 필요하다. 새 owner를 commit 뒤 다시 resolve해 보내는 call은 정상
direct delivery이며 Message Follow 증거가 아니다.

이전 전체 실행 `logs/20260720-044205-2109114`는 당시 기본 17개 시나리오와
별도 process generation을 사용하는 `ST-B2`, `ST-C2`, `ST-C1`이 모두 통과했다.
`ST-C1`은 공통 스펙에 따라 target의 `pending_admission_expired` marker를
30초 이내에서 기다리며, 전체 runtime drain을 admission 정리 증거로 대신 사용하지 않는다.

현재 `run_e2e.sh all`은 19개 시나리오만 실행한다. Transport delivery fixture를 연결한
`ST-F4/F5`, 부분 구현인 `ST-I1~I6`,
`ST-F3A`, ST-G·ST-H는 실행하지 않는다. 따라서 위 과거 실행과 현재
selector 모두 현행 Config 10 전체 완료 증거가 아니다. Runtime M6 gate가 끝난 뒤
전환 대상 시나리오를 현재 공개 API로 고치고, 미구현 행을 추가한 다음 전체 selector와
scenario registration을 다시 고정한다.

`ST-I1~I6`의 개별 selector도 현재 연결된 일부 case만 실행하므로 결과를
`diagnostic_only`로 출력한다. 각 case를 독립 selector와 evidence로 분리해 Config 10
matrix를 모두 채우기 전에는 selector exit 0을 Track I 완료로 해석하지 않는다.

## 현재 diagnostic transport fixture와 남은 교체

기존 transport에는 resolver가 선택한 delivery 하나만 멈추는 fault injection 표면이
없었다. Backend node 전체를 decorator로 감싸면 multipart frame, request completion과
socket 오류 변환을 fixture가 다시 구현해야 한다. 이 방식은 transport 내부 지식을 E2E에
중복시킨다.

현재 diagnostic fixture는 Actor resolver가 route를 확정한 뒤 실제 submit 직전에 호출하는 internal
gate다. E2E host만 friend assembly로 gate를 등록한다. Application call은 global Actor
ID와 일반 metadata만 사용한다. Gate와 HTTP 응답에는 owner RID, `ActorRef`,
ObjectGeneration과 Message Follow hop이 없다. 등록하지 않은 production host에서는
delivery를 변경하지 않는다. 이 fixture는 public-only 완료 증거가 아니다. 정본 E2E는
process 밖 transport harness가 같은 delivery를 지연·복제하는 방식으로 교체해야 한다.

## 현재 실행 blocker

Public opaque Store 위에 provider-backed authority repository를 연결했다. Redis↔in-memory
authority·generation parity와 실제 Redis 2-process focused test가 통과했고,
AutomaticTurnDispatch는 `TD-A1~TD-C5`를 연속 통과했다.

SpotActorTransfer의 User Spot actor handler는 sample과 같은 `Configure()` public
registration으로 고쳤다. Reservation 기반 Actor creation commit의 authority를 local
ownership coordinator에 연결하고 Actor publish를 그 뒤로 옮겼다. Actor와 User Spot을
같은 owner에 만든 `ST-A1`은 `logs/20260727-224418-4175123`에서 통과했다.

현재 User Spot fixture는 세 Actor node에 같은 stable type을 등록한다. Create DTO의
`TargetNodeRid`는 제거했고 public node-wide placement weight를 바꾼 뒤 Location descriptor에
반영됐는지 확인하고 global create를 실행한다. `ST-A1`은 이 경계에서 통과했다.
`ST-B1`은 Actor를 actor-a, Spot을 actor-b에 자동 배치하고 deferred Join, target restore,
authority convergence와 후속 probe를 검증한다. Source identity, canonical phase progress,
source ownership release와 unbound completion fence를 고친 뒤
`logs/20260727-235500-1282505`, `logs/20260727-235556-1284429`에서 연속 통과했다.

Track I의 Relocate 기반 host workload에는 public lifecycle 구현 차이도 남아 있다.
정식 .NET 계약은 mode를 받는 `RelocateAsync(...)`와 별도 `ShutdownAsync(...)`를
정의하며 production singleton과 DI 연결도 완료됐다. Actor·Spot scheduler도
`PlannedMaintenance`의 same-version과 `RollingUpdate`의 caller 지정 exact-version을
preflight와 실제 relocation에 동일하게 적용한다. Track I host workload도 연결했지만
이전 축소 process 실행의 relocation nonterminal 원인 가운데 target replay가 일반
dispatch guard에 막히던 문제를 수정했다. 최신 runtime으로 service continuity와 정본
규모를 다시 검증해야 한다.

추가 E2E 리뷰 뒤 scale 실행은 `diagnostic_only`로 분리하고 request와 one-way를 각각
독립 open-loop pacer로 바꿨다. 생성 count를 완료 수로 출력하던 부분도 제거했다.
Process E2E는 public host terminal, 최종 location과 terminal 이후 handler owner를
검증한다. Exact authority commit·target admission 순서와 SpotWide aggregate CAS는
provider·runtime contract test가 검증한다. 이 두 검증 계층이 모두 통과하기 전에는
정본 완료 증거로 사용하지 않는다. Actor transport gate도 internal metadata와
E2E friend assembly에 의존하므로 외부 transport harness로 교체해야 한다.

Relocation payload 측정용 wrapper는 구현했다. Wrapper는 private envelope나 Store key를
해석하지 않고 public `IZLinkRelocationStore`의 opaque blob 크기와 SHA-256만 기록한다.
ActorNode와 Client project build는 warning 0, error 0이다. Instance Spot 네 profile은
과거 activation-only process에서만 통과했다. 현재 relocation selector는 Capture 3/4,
Restore 0에서 timeout되어 완료 증거가 아니다. `ST-I1`은 SpotWide commit 직후 public
lookup과 남은 workload profile을 해결하기 전에는 완료로 표시하지 않는다.

현재 fixture는 Object role에 허용되지 않는 fixed RID·manual peer를 제거했다. Framework가
`actor-a`·`actor-b`·`actor-c`와 Session prefix에 UUID suffix를 붙여 full RID를 발급하며,
runner는 실제 automatic peer 연결과 owner RID를 관측한다. ActorNode·SessionGateway·Client는
warning·error 0으로 build된다. 같은 stable type·caller 비지정 배치와 handler registration을
정렬한 process 실행이 통과하기 전에는 Config 10 완료 증거가 아니다.
