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
| ST-F4 | fixture 구현·process 재검증 대기 | Internal transport delivery gate가 public resolver가 commit 전에 선택한 delivery 두 건을 operation ID별로 지연한다. Caller는 global Actor ID만 사용한다. Duration 안 release는 relay하고 route 제거 뒤 release는 `ActorLocationStale`로 끝나는지 검증한다. |
| ST-F5 | fixture 구현·process 재검증 대기 | 첫 owner에서 선택한 delivery를 지연한 뒤 세 node로 두 번 relocation하고 release한다. 두 source의 next-hop Message Follow와 route 제거 뒤 stale 결과를 검증한다. Application은 owner RID나 ObjectGeneration을 제출하지 않는다. |
| ST-F6 | 구현 | handoff 중 request의 원래 caller completion 상관관계와 늦은 reply timeout 격리를 같은 세 실행에서 검증했다. |
| ST-G1 | 미구현 | SpotWide·PerActor의 yielded continuation과 모든 실행 lane을 포함한 relocation barrier E2E가 없다. |
| ST-G2 | 미구현 | Actor 10,000개 inventory chunk와 typed capacity aggregate all-or-none E2E가 없다. |
| ST-G3 | 미구현 | PerActor Spot authority 선전환과 Actor별 source·target route 분할 E2E가 없다. |
| ST-G4 | 미구현 | 이동 중 stale `ToActor` relay와 target queue 순서를 검증하는 E2E가 없다. |
| ST-G5 | 미구현 | Entry·PerActor Actor relocation interruption 1초 목표와 초과 시 계속 진행을 검증하는 E2E가 없다. |
| ST-G6 | 미구현 | `ApplicationSignaled` readiness와 completion callback의 source·target owner를 검증하는 E2E가 없다. |
| ST-H1 | 미구현 | Deferred Join 등록, immutable request와 Actor queue barrier E2E가 없다. |
| ST-H2 | 미구현 | Join completion outcome, 128-bit operation ID와 crash recovery E2E가 없다. |
| ST-H3 | 미구현 | Context identity와 relocation 이후 source fence E2E가 없다. |
| ST-H4 | 미구현 | 허용 execution context, 중복 등록과 relocation error parity E2E가 없다. |
| ST-H4A | 미구현 | Deferred Join 등록량·payload·timeout 경계와 Relocate·Shutdown race E2E가 없다. |
| ST-H4B | 미구현 | Join 뒤 Yield, awaited cycle과 reply terminal E2E가 없다. |
| ST-H5 | 미구현 | MessageContext와 Actor handler signature parity를 실제 transport로 검증하는 E2E가 없다. |
| ST-I1 | 부분 구현·profile 확대 대기 | Actor와 Instance Spot의 4 KiB·64 KiB·8 MiB·64 MiB, SpotWide의 64 KiB·1 MiB·32 MiB·64 MiB profile, deterministic SHA, opaque Store 총 byte·checksum과 peak RSS 측정을 연결했다. Instance Spot 네 profile은 `20260727-232318-914304`, SpotWide 64 KiB는 `20260728-000859-1518859`에서 통과했다. Actor adapter가 정확히 64 MiB를 반환하는 독립 selector도 추가했다. 첫 실행 `20260728-004839-2032371`에서 fixture header 74 bytes를 잘못 더한 문제를 고쳤지만 수정 뒤 process 증거는 아직 없다. SpotWide 64 MiB 실행 `20260728-003937-1922103`은 typed failure 없이 외부 5분 제한에 도달했다. Queue·journal·timer, permit contention, 320 MiB·5-participant aggregate도 미검증이다. |
| ST-I2 | 부분 구현·process blocker | 10,000 Recreate Actor와 1,000 Snapshot Actor, control Actor·Spot traffic과 throughput·p99 측정 scenario를 연결했다. Request와 one-way를 독립 open-loop로 제출하고 operation ID·absolute deadline·correlation, relocation 전후 public 위치와 terminal callback을 대조한다. 축소 smoke `20260728-000226-1408117`은 baseline traffic을 오류 없이 처리했지만 host relocation이 terminal 없이 반복 retry했다. 정본 규모와 relocation 중·후 결과는 아직 검증하지 못했다. Public ref에 authority owner generation이 없어 해당 검증은 명시적인 gap으로 실패한다. |
| ST-I3 | 부분 구현·process blocker | Instance Spot 1,000개와 SpotWide 100개×Actor 100개, control traffic 측정 scenario를 연결했다. Public 위치와 terminal callback, 모든 SpotWide member의 최종 owner를 대조한다. 축소 Instance smoke `20260728-000501-1448286`은 baseline을 통과했지만 Capture만 반복하고 target restore와 relocation terminal에 도달하지 못했다. Spot request correlation과 aggregate 단일 CAS publication은 현재 public 관측 표면으로 증명할 수 없어 명시적인 gap으로 실패한다. |
| ST-I4 | Actor matrix 부분 통과 | Operation ID별 transport gate로 commit 뒤 `MF-AO-FOLLOW`와 `MF-AR-FOLLOW`를 실행한다. Source dispatch와 reply relay를 고친 뒤 `20260728-001043-1662047`에서 held one-way·request, Message Follow route 등록·relay·제거가 통과했다. `MF-AO-QUEUE`, host relocation의 `MF-AR-HOLD`, Spot 네 case와 `MF-PA-SPLIT`은 남아 있다. |
| ST-I5 | Actor safety 부분 통과 | `20260728-002118-1750005`에서 pre-resolved Actor request의 역순 release correlation A/B, original absolute deadline, late reply 폐기와 route expiry stale rejection이 통과했다. Duplicate, 이전 generation, loop, 8-hop과 1,024 record·16 MiB bound 및 Spot 조합은 남아 있다. |
| ST-I6 | Actor multi-hop production blocker | 첫 source에서 선택한 request delivery를 두 번 relocation 뒤 release하고 source·중간 node relay와 route cleanup을 검증한다. 첫 hop의 benign lease-renewal StoreVersion conflict는 재시도하도록 고쳤지만 `20260728-003822-1911426`에서 두 번째 target restore·location commit 뒤 source cleanup completion이 끝나지 않았다. Handler operation ID, one-way, 세 번째 relocation, recovery와 Instance·SpotWide 조합도 남아 있다. |

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

## Transport delivery fixture 경계

기존 transport에는 resolver가 선택한 delivery 하나만 멈추는 fault injection 표면이
없었다. Backend node 전체를 decorator로 감싸면 multipart frame, request completion과
socket 오류 변환을 fixture가 다시 구현해야 한다. 이 방식은 transport 내부 지식을 E2E에
중복시킨다.

선택한 방식은 Actor resolver가 route를 확정한 뒤 실제 submit 직전에 호출하는 internal
gate다. E2E host만 friend assembly로 gate를 등록한다. Application call은 global Actor
ID와 일반 metadata만 사용한다. Gate와 HTTP 응답에는 owner RID, `ActorRef`,
ObjectGeneration과 Message Follow hop이 없다. 등록하지 않은 production host에서는
delivery를 변경하지 않는다.

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
축소 process 실행에서 relocation terminal이 끝나지 않아 service continuity 완료 증거는 남아 있다.

추가 E2E 리뷰 뒤 scale 실행은 `diagnostic_only`로 분리하고 request와 one-way를 각각
독립 open-loop pacer로 바꿨다. 생성 count를 완료 수로 출력하던 부분도 제거했다.
실제 per-unit terminal·admission 순서와 SpotWide member 최종 owner·aggregate 검증은
아직 없다. 이 항목을 고치기 전에는 scenario가 terminal을 반환하더라도 정본 완료
증거로 사용하지 않는다. Actor transport gate도 internal metadata와
E2E friend assembly에 의존하므로 외부 transport harness로 교체해야 한다.

Relocation payload 측정용 wrapper는 구현했다. Wrapper는 private envelope나 Store key를
해석하지 않고 public `IZLinkRelocationStore`의 opaque blob 크기와 SHA-256만 기록한다.
ActorNode와 Client project build는 warning 0, error 0이다. Instance Spot 네 profile은
process에서 통과했다. `ST-I1`은 SpotWide commit 직후 public lookup과 남은 workload
profile을 해결하기 전에는 완료로 표시하지 않는다.

현재 fixture는 Object role에 허용되지 않는 fixed RID·manual peer를 제거했다. Framework가
`actor-a`·`actor-b`·`actor-c`와 Session prefix에 UUID suffix를 붙여 full RID를 발급하며,
runner는 실제 automatic peer 연결과 owner RID를 관측한다. ActorNode·SessionGateway·Client는
warning·error 0으로 build된다. 같은 stable type·caller 비지정 배치와 handler registration을
정렬한 process 실행이 통과하기 전에는 Config 10 완료 증거가 아니다.
