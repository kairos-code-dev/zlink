# .NET SpotActorTransfer E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-10-spot-actor-relocation.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| ST-A1 | runtime evidence gap | Same-node join을 강제하고 `admission -> authority_committed -> leave -> joined -> success_reply`와 Relocation Store artifact 0건을 요구한다. Client와 ActorNode는 warning·error 0으로 build됐지만 실제 process `logs/20260728-042012-2770643`은 same-node authority commit marker가 없어 실패했다. Actor별 Message Follow route가 생성되지 않았다는 사실도 현재 public observation으로 판정할 수 없다. 이전 순서 일부만 확인한 실행은 현재 계약의 완료 증거로 사용하지 않는다. |
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
| ST-F4 | 외부 transport process 통과 | 세 ActorNode의 실제 ROUTER는 서로 다른 loopback 주소에 bind하고 public `SetAdvertiseHost`로 외부 TCP proxy를 게시한다. Proxy는 Framework protocol을 해석하지 않고 application payload의 고유 marker 바이트만 streaming 검색한다. Third-node caller가 relocation 전에 제출한 one-way와 request를 proxy가 보류한다. Commit 뒤 one-way는 final owner handler가 정확히 한 번 처리하고 이전 owner handler는 처리하지 않는다. Message Follow 만료 뒤 request는 handler에 도달하지 않고 public `Unavailable`로 끝난다. `ObjectGeneration` mismatch의 `InvalidOperation`과 구분한다. 최종 회귀 증거는 `logs/20260729-032359-2949302`이다. |
| ST-F5 | 외부 transport multi-hop process 통과 | ST-F4와 같은 외부 proxy에서 third-node caller의 delivery를 보류한 뒤 A→B→C relocation을 실행한다. User Spot handler가 등록한 deferred Join은 제출한 Spot의 serial turn에서 실행될 때 Spot activation context도 함께 복원한다. 따라서 두 번째 source의 committed leave callback이 같은 queue를 다시 기다리지 않는다. Commit 뒤 one-way는 A와 B의 Message Follow를 거쳐 C handler에서 정확히 한 번 처리되고 이전 owner handler는 0회다. 만료 뒤 request는 handler에 도달하지 않고 public `Unavailable`로 끝난다. Diagnostic-only marker를 제거한 최종 코드로 `logs/20260729-032242-2945859`, `logs/20260729-032307-2947057`, `logs/20260729-032331-2948131`에서 3회 연속 통과했다. Route entry의 실제 제거와 next-hop 내부값은 public black-box로 관찰할 수 없어 provider/runtime contract test가 별도로 필요하다. |
| ST-F6 | 구현 | handoff 중 request의 원래 caller completion 상관관계와 늦은 reply timeout 격리를 같은 세 실행에서 검증했다. |
| ST-G1 | 미구현 | SpotWide·PerActor의 yielded continuation과 모든 실행 lane을 포함한 relocation barrier E2E가 없다. |
| ST-G2 | 미구현 | Actor 10,000개 inventory chunk와 typed capacity aggregate all-or-none E2E가 없다. |
| ST-G3 | 구현·process 통과 | Actor 100개가 속한 `PerActor` User Spot을 actor-a에서 actor-b로 이전한다. Spot·Actor의 ObjectGeneration 유지와 최종 owner, Actor별 Capture payload와 Restore payload의 byte 수·SHA 일치, Actor별 `transfer_in` 정확히 1회, 이동 뒤 Actor 100건과 Spot 1건의 target dispatch를 검증했다. Fresh process 증거는 `logs/20260728-075736-1950230`이다. |
| ST-G4 | 미구현 | 이동 중 stale `ToActor` Message Follow와 target queue 순서를 검증하는 E2E가 없다. |
| ST-G5 | Entry Actor·SpotWide 10 continuity 통과·나머지 부분 구현 | 공통 production histogram은 `zlink.relocation.interruption`과 `unit_kind`·선택형 `execution_mode`를 사용한다. Entry Actor selector는 relocation 전·중·후 request·one-way를 계속 보내고 metric duration과 source 마지막 handler→target 첫 handler/reply gap, loss·duplicate를 수치로 남긴다. 같은 종류의 operation은 한 제출 흐름에서 순서대로 제출해 FIFO 기준을 명확히 한다. Fresh `ST-G5-SMALL` 실행 `logs/20260728-092319-3148154`는 interruption 0.388267초, application handler gap 377 ms, request 71건·one-way 80건, loss 0·duplicate 0과 원래 operation ID·deadline·request correlation 보존을 통과했다. Command 34 single-flight 수정 뒤 SpotWide 10 Actor required continuity gate를 `logs/20260728-233944-3394603`과 `logs/20260728-234157-3450332`에서 연속 두 번 실행했다. 두 실행 모두 Spot·Actor가 각각 64 KiB state를 Capture·Restore하고 실행별 request 286건과 one-way 318건을 처리했다. Relocation interruption은 0.597029초와 0.569657초, source 마지막 handler에서 target 첫 handler까지의 application gap은 896 ms와 876 ms다. 두 실행 모두 loss·duplicate 0, Actor FIFO, global SpotId trace와 request correlation pair를 검증하고 `required_gate=spotwide_service_continuity status=passed`로 끝났다. 이전 physical route를 public API로 고정한 뒤 release할 수 없어 exact stale-route Spot Message Follow는 별도 gap으로 남긴다. Commit 전 participant 0개와 commit 뒤 전체 공개를 함께 관찰하는 public API도 없어 atomic publication은 별도 gap이다. `ACTORS-100`의 canonical scale gate와 다른 unit selector도 남아 있으므로 ST-G5 전체는 완료가 아니다. |
| ST-G6 | 미구현 | `ApplicationSignaled` readiness와 completion callback의 source·target owner를 검증하는 E2E가 없다. |
| ST-H1 | 미구현 | Deferred Join 등록, immutable request와 Actor queue barrier E2E가 없다. |
| ST-H2 | 미구현 | Join completion outcome, 128-bit operation ID와 crash recovery E2E가 없다. |
| ST-H3 | 미구현 | Context identity와 relocation 이후 source fence E2E가 없다. |
| ST-H4 | 미구현 | 허용 execution context, 중복 등록과 relocation error parity E2E가 없다. |
| ST-H4A | 미구현 | Deferred Join 등록량·payload·timeout 경계와 Relocate·Shutdown race E2E가 없다. |
| ST-H4B | 미구현 | Join 뒤 Yield, awaited cycle과 reply terminal E2E가 없다. |
| ST-H5 | 미구현 | MessageContext와 Actor handler signature parity를 실제 transport로 검증하는 E2E가 없다. |
| ST-I1 | 부분 구현·diagnostic only | Actor는 4 KiB·64 KiB·8 MiB·64 MiB, Instance Spot과 SpotWide는 64 KiB·1 MiB·32 MiB·64 MiB profile을 사용한다. Actor와 SpotWide small은 실제 cross-node relocation 뒤 `Restore`, public location, deterministic SHA와 opaque Store read-back을 확인했다. Actor adapter가 정확히 64 MiB를 반환하는 독립 selector도 있다. SpotWide 64 MiB 실행 `20260728-003937-1922103`은 typed failure 없이 외부 5분 제한에 도달했다. 수정한 Instance 실행 `20260728-022204-146992`도 네 fixture 생성 뒤 source `Capture` 3건, target `Restore` 0건에서 5분 relocation deadline과 HTTP 499로 끝났다. 따라서 Instance와 SpotWide 전체 profile은 완료 증거가 아니다. 이전 Instance activation-only 결과도 relocation 증거에서 제외했다. Queue·journal·timer, permit contention, 320 MiB·5-participant aggregate가 미구현이므로 전체 `ST-I1`은 `diagnostic_only`로 표시한다. |
| ST-I2 | 부분 구현·diagnostic only | `ST-I2-RECREATE`와 `ST-I2-SNAPSHOT`을 분리했다. 각 selector는 fresh host process에서 10,000/180초·64 units/s와 1,000/90초·16 units/s를 독립 계산한다. Moving target request·one-way가 0건이면 실패한다. Actor request는 original operation과 connection-scoped correlation을 일대일로 대조하고 duplicate 0건을 확인한다. Handler evidence는 sequence 집합이 아니라 도착 순서로 검증한다. Public terminal·최종 location·`ObjectGeneration`·final owner admission도 확인한다. 1초 interruption, encoded bytes/s, payload latency, CPU, peak RSS와 Store byte는 아직 측정하지 않으므로 두 selector는 `diagnostic_only`다. 현재 정본 규모는 creation-reservation production blocker를 우회하지 않고 재검증해야 한다. |
| ST-I3 | 부분 구현·diagnostic only | Instance Spot 1,000개와 SpotWide 100개×Actor 100개, control traffic 측정 scenario를 연결했다. Spot message flow는 relocation traffic 시작 전 flow ID watermark 이후의 received·replied pair만 사용한다. Public terminal·최종 location·generation과 final owner admission은 확인하지만, SpotWide final owner equality를 atomic publication 증거로 사용하지 않는다. Commit 전 participant 0개 공개와 commit 뒤 전체 공개를 관찰하는 수단이 없어 `spotwide_pre_post_visibility` blocker를 출력한다. 1초 interruption과 resource·Store 측정도 남아 있어 두 selector는 `diagnostic_only`다. |
| ST-I4 | Actor matrix 부분 구현·focused process 통과 | Operation ID별 transport gate로 commit 뒤 `MF-AO-FOLLOW`와 `MF-AR-FOLLOW`를 실행한다. Final owner의 one-way·request handler가 각각 정확히 한 번 실행되고 source handler는 0회인지 확인한다. Source transport reply admission fixture가 backpressure를 반환하는 동안 request terminal이 끝나지 않고, release 뒤 original marker·correlation으로 한 번만 완료되는 것도 확인했다. `20260728-035609-2262342`에서 ST-I4·I5 focused process가 통과했다. `MF-AO-QUEUE`, host relocation의 `MF-AR-HOLD`, Spot 네 case와 `MF-PA-SPLIT`은 남아 있다. |
| ST-I5 | Actor safety 부분 구현·focused process 통과 | Pre-resolved Actor request를 역순 release하여 correlation A/B와 reply marker를 대조한다. Source reply admission을 original 2초 deadline까지 backpressure한 뒤 `TimeoutException` terminal 하나를 유지하고, gate를 늦게 release해도 결과가 바뀌지 않는지 확인했다. 기존 late reply 미전달, duration 뒤 `ActorLocationStale`와 target handler 0회도 유지한다. Runtime route marker와 private pending state는 사용하지 않는다. `20260728-035609-2262342`에서 focused process가 통과했다. Duplicate, 이전 generation, loop, 8-hop과 1,024-message·16 MiB bound 및 Spot 조합은 남아 있다. |
| ST-I6 | 외부 transport 대체 완료·internal selector 정리 필요 | 같은 multi-hop 동작은 public API와 외부 TCP proxy만 사용하는 ST-F5로 대체했다. A→B→C 뒤 final owner exactly-once, 이전 owner handler 0회, one-way와 만료 request terminal을 3회 연속 검증했다. 기존 ST-I6 selector는 internal delivery gate를 사용하므로 정본 증거가 아니며 제거 또는 외부 fixture 전환이 남아 있다. Route cleanup 내부값, recovery와 Instance·SpotWide 조합도 별도 gap이다. |

## Message Follow case별 구현 상태

Config 10의 Message Follow 전용 section은 case별 evidence를 요구한다. 현재 `.NET` process E2E의
구현 상태는 다음과 같다. Runtime route 구현이나 unit test 통과를 process E2E 통과로 계산하지 않는다.

| 대상·경계 | One-way | Request |
|---|---|---|
| Actor, seal 전 queue | `MF-AO-QUEUE` 미구현 | Track F accepted journal과 분리 검증 필요 |
| Actor, commit 전 host ingress hold | 미구현 | `MF-AR-HOLD` 미구현 |
| Actor, commit 직후 Message Follow | 외부 transport one-way 통과·request 부분 구현 | ST-F4 external proxy에서 final owner의 one-way exactly-once와 previous owner handler 0회를 확인했다. Internal reply-admission fixture를 쓰는 request matrix는 교체가 남아 있다. Seal 전 queue와 host ingress hold도 별도 case다. |
| Actor, duration 0·route 없음·만료 | 만료 request 외부 transport 통과 | ST-F4에서 만료된 request가 handler에 도달하지 않고 public `Unavailable`로 끝나는 것을 확인했다. Duration 0과 route 없음은 미구현이다. |
| Actor, duplicate·generation·loop·hop·bound | 부분 구현 | Correlation, reply backpressure deadline과 late terminal 불변은 실제 process에서 확인했다. Duplicate, generation, loop, hop과 bound는 미구현이다. |
| Actor, multi-hop·route cleanup | 외부 transport multi-hop 통과·route cleanup 부분 구현 | ST-F5에서 relocation 전 선택한 one-way를 A→B→C 두 Message Follow route로 전달하고 final owner exactly-once와 이전 owner handler 0회를 확인했다. 만료 request도 public `Unavailable`로 끝났다. Route entry의 실제 제거는 public observation이 없어 별도 provider/runtime contract test가 필요하며 recovery는 미구현이다. |
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

ST-F4/F5는 process 밖 TCP proxy로 교체했다. ActorNode는 별도 loopback 주소에 ROUTER를
bind하고 public `SetAdvertiseHost`로 proxy endpoint를 게시한다. Proxy는 application
payload의 고유 marker 바이트만 streaming 검색한다. Marker가 TCP read 사이에서 나뉘어도
같은 connection의 후속 바이트를 보류하며 service wire, owner, generation과 Message Follow
hop은 해석하지 않는다.

ST-I4~I6에는 Actor resolver 뒤의 internal gate와 E2E friend assembly가 아직 남아 있다.
Reply admission backpressure도 internal fixture다. 이 세 selector를 외부 proxy로 바꾸고
실제 process를 통과하기 전에는 public-only 완료 증거가 아니다.

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
정본 완료 증거로 사용하지 않는다. ST-I4~I6 Actor transport gate도 internal metadata와
E2E friend assembly에 의존하므로 ST-F4/F5에 적용한 외부 transport harness로 교체해야 한다.

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
