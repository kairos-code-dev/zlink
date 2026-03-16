# Thread-Safe Socket 상태 추적

이 문서는
[`thread-safe-socket-plan.ko.md`](./thread-safe-socket-plan.ko.md)
대비 현재 구현/검증 상태를 추적한다.

## 상태 요약

| 항목 | implemented | verified | remaining |
| --- | --- | --- | --- |
| raw socket hot path/lifecycle | 예 | 예 | stress lane 반복 실적 정리, TSan 대상 확대 |
| raw runtime read (`EVENTS`, `LAST_ENDPOINT`) | 예 | 예 | `RCVMORE` 전용 경합 확장 |
| gateway hot path/lifecycle | 예 | 예 | stress lane 반복 실적 정리, TSan 대상 확대 |
| gateway runtime read (`routing_id`, snapshot) | 예 | 예 | attach/query ordering 추가 matrix 확장 |
| spot / spot_node hot path/lifecycle | 예 | 예 | stress lane 반복 실적 정리, TSan 대상 확대 |
| spot runtime read (monitor snapshot) | 예 | 예 | parent-child 상세 선형화 matrix |
| discovery / registry lifecycle | 예 | 예 | discovery-side matrix 추가 확장 |
| discovery / registry control-path correctness | 예 | 예 | discovery-side full matrix, registry matrix 추가 확장 |
| monitor open/close/reopen/destroy | 예 | 예 | reopen/snapshot 세부 matrix 확장 |
| 문서/헤더 tiered contract | 예 | 예 | 추가 policy coverage 확장 가능 |
| perf acceptance (1/4/16/64 handle) | 예 | 부분 | 20% 기준선 실측 축적, 장비별 baseline 정리 |
| stress 10^3 / TSan lane | 예 | 부분 | 10^3 반복 실적 정리, TSan 실빌드 범위 확대 |

## 이번 반영

- gateway same-handle concurrent send 중 `routing_id`/snapshot runtime read 회귀 추가
- spot same-handle concurrent publish 중 monitor snapshot runtime read 회귀 추가
- registry same-handle concurrent `set_id` + `topology_query` 회귀 추가
- gateway same-handle `attach_discovery` 중 `routing_id`/monitor query ordering 회귀 추가
- discovery same-handle `set_routing_id` 중 `routing_id` read와 destroy 수렴 회귀 추가
- discovery destroy 경로의 socket close/drain을 `close_socket_and_wait()` 기반으로 보강
- gateway send-ready callback self-close `EBUSY` 회귀 추가
- spot send-ready callback self-close `EBUSY` 회귀 추가
- spot parent destroy vs open monitor child `EBUSY` 회귀 추가
- stress/TSan lane에 discovery/gateway/spot 신규 회귀 추가
- split test registration 갱신
- thread-safe contract stress runner 추가
- raw/gateway/spot 1/4/16/64 handle scaling perf-contract test 추가
- perf runner / TSan 전용 build runner 추가
- `spot` destroy timeout의 core root cause 수정
- `spot` internal control/data-plane teardown과 pipe termination 정합성 보강
- 64-handle scaling 계약 수행을 위한 default socket cap 상향

## 남은 항목

- raw `RCVMORE` 전용 runtime read 경합 회귀 추가
  현재 blocker: 이 build surface에서는 recv API export/link 경로가 노출되지 않아
  `RCVMORE` 경합 회귀를 public test로 바로 추가하기 어렵다.
- gateway attach/query ordering full matrix
- discovery-side full control-path ordering matrix
- monitor reopen/snapshot, parent-child open/destroy 세부 matrix
- callback 종류별 self-close 세부 matrix
- perf 64-handle 기준 20% acceptance의 장비별 기준선 수집
- stress 10^3 반복 실행 결과와 TSan 실빌드 범위 확장
