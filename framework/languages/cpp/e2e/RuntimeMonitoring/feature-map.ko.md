# C++ RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

## 10.0.0 목표 판정

Config 7은 C++ framework public `route_mesh_runtime_t`의 snapshot과 typed event를 기준으로 판정한다.
기존 native socket, location source와 Spot source 증거는 관련 canonical scenario의 부분 증거며,
trigger-only marker나 message-flow trace로 빈 runtime field를 대신하지 않는다.

| 시나리오 | 상태 | 현재 증거 | 남은 gap |
|---|---|---|---|
| MON-A1 | 10.0.0 전환 대상 | `location-runtime` source가 MeshNode descriptor 변경과 service summary를 기록한다. | 하나의 snapshot에 MeshNode·peer·channel·multicast·claim·location·drain이 함께 있는지와 sequence·불변성을 검증한다. |
| MON-A2 | 10.0.0 전환 대상 | Service가 native `Connected`·`ConnectionReady`·`Disconnected`와 location topology의 peer RID를 수집한다. | `peer_changed` event와 snapshot의 generation·descriptor revision·admission·ready·last failure를 재시작 전후로 대조한다. |
| MON-A3 | 10.0.0 전환 대상 | Drain·restore에서 admission 변경 marker를 수집한다. | Weight 0·100 전파, channel event, ready member 수·selectable과 실제 ChannelName request 선택 결과를 같이 단언한다. |
| MON-A4 | 10.0.0 전환 대상 | 같은 RID의 다른 endpoint 재시작과 두 번의 강제 종료·재시작에서 endpoint 교체, route down/up, 후속 request를 확인한다. | 정상 replacement와 fresh topology의 `SIGKILL`·lease 만료를 나누고 각 event 뒤 generation·ready peer·ready member를 최신 snapshot과 대조한다. |
| MON-A5 | 10.0.0 전환 대상 | Location runtime `StatusChanged`와 Redis-backed topology 경로가 있다. | Redis 정지·failure grace·복구에서 `location.store_changed`, location state·last success·last failure와 current owner token 재검증을 단언한다. |
| MON-B1 | 10.0.0 전환 대상 | 해당 Logical Multicast 증거가 없다. | 막힌 remote ROUTER target에 대해 backpressure·timeout result, backpressured event와 target count snapshot을 비교한다. |
| MON-B2 | 10.0.0 전환 대상 | 해당 Logical Multicast 증거가 없다. | local target의 수락·drop 결과, dropped event와 remote·local snapshot/admitted/dropped 수를 비교한다. |
| MON-C1 | 10.0.0 전환 대상 | Monitoring handler 예외를 error sink에 남긴 뒤 후속 request가 성공한다. | Application gate·느린 observer·정상 observer를 함께 실행해 claim progress, request completion, coalescing·sequence gap과 snapshot resync를 단언한다. |
| MON-D1 | 10.0.0 전환 대상 | 중복 socket source, 비양수 location interval, 없는 Spot·socket source 구성 검증과 두 번의 비정상 재시작 증거가 있다. | 등록하지 않은 MeshName·0 이하 capacity 오류를 검증하고 비정상 종료·lease 만료·재시작 3회의 sequence·snapshot·event field 제한을 확인한다. |

## 실행 경계

- `run_e2e.sh`는 Redis-capable C++ build 디렉터리를 사용하고 scenario별 key prefix를 나눈다.
- Service는 MeshNode ROUTER endpoint 하나를 공개하며 Spot Logical Multicast도 같은 MeshNode 연결을
  사용한다.
- 전환 대상 행은 목표 topology와 public runtime 증거가 구현된 뒤에만 완료로 바꾼다.
