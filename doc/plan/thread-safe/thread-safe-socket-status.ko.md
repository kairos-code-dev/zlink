# Thread-Safe Socket 상태 추적

이 문서는
[`thread-safe-socket-plan.ko.md`](./thread-safe-socket-plan.ko.md)
대비 현재 구현/검증 상태를 추적한다.

## 상태 요약

| 항목 | implemented | verified | remaining |
| --- | --- | --- | --- |
| raw socket hot path/lifecycle | 예 | 예 | perf scaling, stress/TSan lane |
| raw runtime read (`EVENTS`, `LAST_ENDPOINT`) | 예 | 예 | `RCVMORE` 전용 경합 확장 |
| gateway hot path/lifecycle | 예 | 예 | perf scaling, stress/TSan lane |
| gateway runtime read (`routing_id`, snapshot) | 예 | 예 | attach/query ordering matrix |
| spot / spot_node hot path/lifecycle | 예 | 예 | perf scaling, stress/TSan lane |
| spot runtime read (monitor snapshot) | 예 | 예 | parent-child 상세 선형화 matrix |
| discovery / registry lifecycle | 예 | 예 | full control-path ordering matrix |
| discovery / registry control-path correctness | 예 | 부분 | discovery-side full matrix, registry matrix 추가 확장 |
| monitor open/close/reopen/destroy | 예 | 부분 | parent destroy race 확장 |
| 문서/헤더 tiered contract | 예 | 예 | 추가 policy coverage 확장 가능 |
| perf acceptance (1/4/16/64 handle) | 예 | 부분 | threshold 실측과 lane 정착 |
| stress 10^3 / TSan lane | 예 | 부분 | TSAN 실빌드 검증, lane 대상 확장 |

## 이번 반영

- gateway same-handle concurrent send 중 `routing_id`/snapshot runtime read 회귀 추가
- spot same-handle concurrent publish 중 monitor snapshot runtime read 회귀 추가
- registry same-handle concurrent `set_id` + `topology_query` 회귀 추가
- split test registration 갱신
- thread-safe contract stress runner 추가
- raw/gateway/spot 1/4/16/64 handle scaling perf-contract test 추가
- perf runner / TSan 전용 build runner 추가

## 남은 항목

- discovery/registry control-path ordering full matrix
- parent-child open/destroy, callback self-close 세부 matrix
- perf threshold 장비별 기준선 수집
- TSAN lane 실빌드 결과를 기준으로 대상 케이스 확장
