# .NET SpotActorTransfer E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-10-spot-actor-relocation.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| ST-A1 | 구현 | local join 승인 순서와 packet handler evidence를 client가 검증한다. |
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
| ST-F4 | 전환 대상 | 이전 시나리오는 public Actor client에 old `ActorRef`를 직접 넘겼다. 현 계약에서는 caller가 global Actor ID만 사용하고 transport fixture가 이전 owner route의 delivery를 지연해야 한다. |
| ST-F5 | 전환 대상 | 이전 시나리오는 old `ActorRef` 직접 주입에 의존했다. Transport fixture로 stale-source delivery와 node별 next-hop mapping 축출을 검증하도록 전환해야 한다. |
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

이전 전체 실행 `logs/20260720-044205-2109114`는 당시 기본 17개 시나리오와
별도 process generation을 사용하는 `ST-B2`, `ST-C2`, `ST-C1`이 모두 통과했다.
`ST-C1`은 공통 스펙에 따라 target의 `pending_admission_expired` marker를
30초 이내에서 기다리며, 전체 runtime drain을 admission 정리 증거로 대신 사용하지 않는다.

현재 `run_e2e.sh all`은 ST-G·ST-H와 ST-F3A를 실행하지 않는다. 따라서 위 과거 실행은
현행 Config 10 전체 완료 증거가 아니다. Runtime M6 gate가 끝난 뒤 전환 대상 시나리오를 현재
공개 API로 고치고, 미구현 행을 추가한 다음 전체 selector와 scenario registration을 다시 고정한다.
