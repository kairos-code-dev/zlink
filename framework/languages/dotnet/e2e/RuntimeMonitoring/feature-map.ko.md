# .NET RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

## 10.0.0 목표 판정

Config 7은 MeshNode·peer·ChannelName readiness, Logical Multicast의 backpressure·drop,
claim progress와 location health를 공개 runtime snapshot과 typed event로 검증한다. Socket·location·Spot
source marker는 관련 행의 부분 증거로만 기록하며, operation result·typed event·후속 snapshot을
같이 단언하지 않으면 10.0.0 완료 증거로 사용하지 않는다.

| 시나리오 | 상태 | 현재 증거 | 남은 gap |
|---|---|---|---|
| MON-A1 | 10.0.0 전환 대상 | `location-runtime` source가 topology·service summary payload를 기록한다. | 하나의 MeshNode snapshot에 topology·multicast·claim·location·drain이 함께 있는지, sequence와 불변 snapshot을 검증한다. |
| MON-A2 | 10.0.0 전환 대상 | Socket 연결·해제 marker와 topology 변경 marker가 있다. | Typed peer event와 snapshot의 RID·generation·descriptor revision·admission·ready·last failure를 재시작 전후로 대조한다. |
| MON-A3 | 10.0.0 전환 대상 | Drain·restore 중 `PeerAdmissionChanged`가 기록된다. | Weight 0·100 전파 전후 channel event, ready member 수, selectable과 실제 ChannelName request 선택 결과를 함께 단언한다. |
| MON-A4 | 10.0.0 전환 대상 | `svc-b` stop·restart 뒤 request 성공과 topology remove·re-add marker를 관측한다. | 정상 replacement와 fresh topology의 `SIGKILL`·lease 만료를 나누고 generation·endpoint·ready member가 최신 snapshot으로 수렴하는지 검증한다. |
| MON-A5 | 10.0.0 전환 대상 | Location runtime과 Spot 상태 marker 경로가 있다. | Redis 정지·복구 전후 `zlink.runtime.location.store_changed`, location state·last success·last failure와 owner token 재검증을 단언한다. |
| MON-B1 | 10.0.0 전환 대상 | 해당 Logical Multicast 증거가 없다. | `NoDrop = true` target queue를 막아 backpressure·timeout result, backpressured event와 dropped=0인 후속 snapshot count를 비교한다. |
| MON-B2 | 10.0.0 전환 대상 | 해당 Logical Multicast 증거가 없다. | `NoDrop = false`에서 수락 target과 drop target을 만들고 operation result, dropped event, remote·local snapshot/admitted/dropped 수를 비교한다. |
| MON-C1 | 10.0.0 전환 대상 | Monitoring handler 예외가 task failure로 보고된 뒤 messaging이 계속되는 marker가 있다. | Application gate와 느린 observer를 함께 만들어 application·infrastructure claim, request completion, 정상 observer, coalescing·sequence gap 후 snapshot 재조회를 검증한다. |
| MON-D1 | 10.0.0 전환 대상 | 중복 source, 비양수 interval, 없는 Spot·socket source 구성 오류와 한 번의 stop·restart 경로가 있다. | 등록하지 않은 MeshName, 0 이하 observer capacity를 public error로 단언하고 비정상 종료·lease 만료·재시작을 3회 반복해 sequence와 최신 snapshot을 확인한다. |
