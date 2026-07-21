# C++ RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

## 10.0.0 목표 판정

Config 7은 C++ framework public `route_mesh_runtime_t`의 snapshot과 typed event를 기준으로 판정한다.
기존 native socket, location source와 Spot source 증거는 관련 canonical scenario의 부분 증거며,
trigger-only marker나 message-flow trace로 빈 runtime field를 대신하지 않는다.

| 시나리오 | 상태 | 현재 증거 | 남은 gap |
|---|---|---|---|
| MON-A1 | 구현 | 하나의 public snapshot에서 MeshNode·peer·channel·multicast·claim·location·drain과 증가하는 sequence를 확인한다. | 없음 |
| MON-A2 | 구현 | `peer_changed` event와 snapshot의 generation·descriptor revision·admission·ready·last failure를 대조한다. | 없음 |
| MON-A3 | 구현 | Weight 0·100 전파 뒤 `channel_changed`, ready member 수·selectable과 실제 request 선택 결과를 대조한다. | 없음 |
| MON-A4 | 구현 | 정상 종료 replacement와 `SIGKILL` recovery에서 event sequence, 새 generation, 단일 ready lifetime, channel readiness와 follow-up request를 확인한다. | 없음 |
| MON-A5 | 구현 | Redis pause·복구에서 `zlink.runtime.location.store_changed`, degraded·ready snapshot, 성공·실패 시각과 기존 peer·request 지속을 확인한다. | 없음 |
| MON-B1 | 구현 | 막힌 remote target의 public backpressure result, typed event와 후속 snapshot에서 remote target 수 `2/1/1`을 대조한다. | 없음 |
| MON-B2 | 구현 | 용량이 없는 local target과 수락 target을 분리하고 drop result, typed event와 후속 snapshot의 local 수 `2/1/1`을 대조한다. | 없음 |
| MON-C1 | 구현 | RouteMesh application gate 중 request completion, claim event, 정상·느린·예외 observer 격리, sequence gap과 snapshot resync를 확인한다. | 없음 |
| MON-D1 | 구현 | 잘못된 MeshName snapshot·observer와 0 capacity를 거부하고 세 번의 crash·restart 뒤 event sequence와 최신 ready snapshot을 확인한다. | 없음 |

## 실행 경계

- `run_e2e.sh`는 Redis-capable C++ build 디렉터리를 사용하고 scenario별 key prefix를 나눈다.
- Service는 MeshNode ROUTER endpoint 하나를 공개하며 Spot Logical Multicast도 같은 MeshNode 연결을
  사용한다.
- 각 구현 행은 public operation result, typed event와 후속 snapshot을 함께 사용하는 runner 증거를
  기준으로 판정한다.
