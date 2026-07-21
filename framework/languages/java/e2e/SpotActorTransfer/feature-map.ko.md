# Java SpotActorTransfer E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-10-spot-actor-transfer.ko.md`

이 문서는 Config 10의 계약 시나리오와 Java E2E의 현재 검증 범위를 연결한다. `run_e2e.sh all`은
아래 20개 ID를 각각 별도 실행하며, client도 같은 ID를 직접 선택한다.

| 시나리오 | 상태 | 현재 검증 범위 |
|----------|------|----------------|
| ST-A1 | 10.0.0 전환 대상 | target admission 뒤 source leave, location commit, target joined, 성공 응답 순서를 검증해야 한다. 현재 Java runtime은 target joined를 source leave보다 먼저 기록하므로 E2E-JV-31이 추적한다. |
| ST-A2 | 구현 | local join 거절 뒤 source membership을 유지하고 leave·joined 부수 효과가 없는지 확인한다. |
| ST-A3 | 구현 | joined callback 대기 중 packet 완료를 차단하고, callback 이후 target에서 처리되는지 확인한다. |
| ST-B1 | 구현 | remote transfer와 state 복원, 역할별 callback 순서, transfer id로 연결된 typed flow marker와 commit ack를 확인한다. |
| ST-B2 | 구현 | commit ack 뒤 source process를 종료해도 target actor와 성공 결과가 유지되는지 확인한다. |
| ST-B3 | 구현 | transfer adapter가 없는 actor가 기본 빈 state로 transfer되는지 확인한다. |
| ST-B4 | 구현 | 명시적인 빈 transfer state와 target domain state를 확인하고, 역할별 callback과 typed flow marker를 같은 transfer id로 연결한다. |
| ST-C1 | 구현 | target admission 뒤 source process를 종료하고, commit되지 않은 target actor와 callback 부수 효과가 없는지 확인한다. |
| ST-C2 | 구현 | target commit 뒤 source process를 종료해도 target actor가 유지되는지 확인한다. |
| ST-C3 | 구현 | transfer-out, leave, transfer-in, joined callback 실패를 각각 주입하고 실패 evidence를 확인한다. |
| ST-D1 | 구현 | joined callback 대기 중에는 target location이 공개되지 않고 callback 완료 뒤 target owner로 바뀌는지 확인한다. |
| ST-D2 | 구현 | stale source release가 새 generation의 target location을 제거하지 못하는지 확인한다. |
| ST-E1 | 구현 | remote transfer 전후 bound session push가 source에서 target으로 이어지는지 확인한다. |
| ST-E2 | 구현 | 실패한 transfer 뒤 bound session push가 기존 source actor로 전달되는지 확인한다. |
| ST-F1 | 구현 | moving 중 packet을 target backlog에 적재하고 arrival index 순서대로 handler에서 다시 처리하는지 확인한다. target `backlog_enqueued` evidence 뒤 replay되는 경로까지 검증한다. |
| ST-F2 | 구현 | handoff backlog를 location publish 전에 target에 적재하고, target direct packet보다 먼저 처리하는지 확인한다. |
| ST-F3 | 구현 | bound session packet이 transfer 전후 순서를 유지하며 target에서 다시 처리되는지 확인한다. |
| ST-F4 | 구현 | forwarding window 안의 stale ref는 전달하고 window 뒤에는 `ACTOR_LOCATION_STALE`로 실패하는지 확인한다. |
| ST-F5 | 구현 | 연속 transfer 뒤 이전 두 source의 forwarding mapping이 제거되는지 확인한다. |
| ST-F6 | 구현 | handoff 중 request reply correlation, 원래 timeout, late reply의 단일 처리를 확인한다. |

## 남은 검증 갭

- 위 `ST-A1`의 callback과 location commit 순서는 E2E-JV-31에서 추적한다. 성공 응답 marker가 있다는
  사실만으로 10.0.0 순서 계약을 완료로 판정하지 않는다.
