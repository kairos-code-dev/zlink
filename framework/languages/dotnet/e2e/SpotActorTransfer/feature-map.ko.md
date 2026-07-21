# .NET SpotActorTransfer E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-10-spot-actor-transfer.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| ST-A1 | 구현 | local join 승인 순서와 packet handler evidence를 client가 검증한다. |
| ST-A2 | 구현 | local join 거절 뒤 actor 상태와 handler side effect가 없음을 검증한다. |
| ST-A3 | 구현 | target joined callback 완료 전 packet dispatch가 시작되지 않음을 검증한다. |
| ST-B1 | 구현 | remote transfer의 admission, state 복원, joined, reply 순서를 검증한다. |
| ST-B2 | 구현 | source cleanup 실패 뒤에도 committed target transfer가 성공으로 유지됨을 검증한다. |
| ST-B3 | 구현 | transfer adapter가 없는 actor의 기본 빈 state transfer를 검증한다. |
| ST-B4 | 구현 | 빈 state를 명시한 remote transfer를 검증한다. |
| ST-C1 | 구현 | target admission 뒤 source process 종료 시 transfer 실패와 복구를 검증한다. |
| ST-C2 | 구현 | target commit 뒤 source process 종료 시 target actor가 유지됨을 검증한다. |
| ST-C3 | 구현 | callback 실패와 transfer 실패의 public error 분류를 검증한다. |
| ST-D1 | 구현 | target commit 전후 location row의 owner와 generation 전환 시점을 검증한다. |
| ST-D2 | 구현 | stale source release가 새 generation location을 제거하지 못함을 검증한다. |
| ST-E1 | 구현 | remote transfer 뒤 bound session push가 새 owner에서 계속 전달됨을 검증한다. |
| ST-E2 | 구현 | 실패한 transfer가 기존 bound session route를 바꾸지 않음을 검증한다. |
| ST-F1 | 구현 | handoff 중 도착한 packet의 순서와 target replay를 검증한다. |
| ST-F2 | 구현 | direct packet이 handoff backlog를 추월하지 않음을 검증한다. |
| ST-F3 | 구현 | 같은 session의 `S1,S2,S3,S4` 순서와 다른 session 진행 격리를 묶음 반복 `logs/20260720-042454-2074240`, `logs/20260720-042515-2075011`, `logs/20260720-042523-2076425`에서 검증했다. |
| ST-F4 | 구현 | forwarding window 안의 `straggler_forward`와 window 뒤 `stale_fail_fast`/`ActorLocationStale`를 같은 세 실행에서 검증했다. |
| ST-F5 | 구현 | node별 단일 next-hop mapping, 축출, stale ref 거부와 최종 target 전달을 같은 세 실행에서 검증했다. |
| ST-F6 | 구현 | handoff 중 request의 원래 caller completion 상관관계와 늦은 reply timeout 격리를 같은 세 실행에서 검증했다. |

최신 전체 실행 `logs/20260720-044205-2109114`에서 기본 17개 시나리오와
별도 process generation을 사용하는 `ST-B2`, `ST-C2`, `ST-C1`이 모두 통과했다.
`ST-C1`은 공통 스펙에 따라 target의 `pending_admission_expired` marker를
30초 이내에서 기다리며, 전체 runtime drain을 admission 정리 증거로 대신 사용하지 않는다.

`run_e2e.sh all`은 모든 행을 실행한다. process 종료가 필요한 `ST-B2`, `ST-C1`, `ST-C2`는
각각 별도 server generation을 시작하며, 나머지 행도 client selector와 evidence marker를 통해
누락 없이 실행한다.
