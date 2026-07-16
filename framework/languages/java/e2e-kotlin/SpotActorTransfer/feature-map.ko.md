# Kotlin SpotActorTransfer E2E feature map

기준 문서는 [Config 10 — Spot·Actor transfer](../../../../doc/framework/common/e2e/config-10-spot-actor-transfer.ko.md)다.
Kotlin lane은 Java와 shared transfer fixture를 사용하더라도 Kotlin client와 server entry point에서
각 정식 시나리오를 독립적으로 증명해야 한다.

| 시나리오 | 상태 | 검증 대상 |
|---|---|---|
| `ST-A1` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: local admission accept와 callback 순서. |
| `ST-A2` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: local admission reject의 무효과. |
| `ST-A3` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: joined callback 완료 전 packet dispatch 차단. |
| `ST-B1` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: remote transfer 성공과 state 복원. |
| `ST-B2` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: commit 뒤 source cleanup. |
| `ST-B3` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: adapter 미등록 실패. |
| `ST-B4` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: custom empty state transfer. |
| `ST-C1` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: commit 전 source 종료. |
| `ST-C2` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: commit 뒤 source 종료. |
| `ST-C3` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: callback 단계별 failure 분류. |
| `ST-D1` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: location commit 공개 시점. |
| `ST-D2` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: stale generation fencing. |
| `ST-E1` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: transfer 성공 뒤 bound session push. |
| `ST-E2` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: transfer 실패 때 기존 session binding 유지. |
| `ST-F1` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: moving backlog FIFO. |
| `ST-F2` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: location publish 전 replay 순서. |
| `ST-F3` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: bound session의 cross-move FIFO. |
| `ST-F4` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: bounded straggler forwarding window. |
| `ST-F5` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: forwarding map 교체와 만료 뒤 축출. |
| `ST-F6` | 전환 필요 | MeshNode topology와 공개 API로 검증할 대상: request correlation과 caller timeout 뒤 late reply 격리. |

현재 Kotlin source와 runner에는 목표 MeshNode topology와 정식 `ST-*` marker가 없으므로 위 행은
구현 완료 표시가 아니다. 구현
단계에서 각 `ST-* result=passed` marker와 최종
`spot-actor-transfer e2e result=passed` marker를 남기고, 실제 bindings package 이름, version과
경로 및 제거 대상 topology의 정적 검사 결과를 함께 출력해야 한다.
