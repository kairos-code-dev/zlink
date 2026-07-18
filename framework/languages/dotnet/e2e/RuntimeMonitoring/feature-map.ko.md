# .NET RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

## 10.0.0 목표 판정

Config 7은 MeshNode·peer·ChannelName readiness, Logical Multicast의 backpressure·drop,
claim progress와 location health를 공개 runtime snapshot과 typed event로 검증한다. Socket·location·Spot
source marker는 관련 행의 부분 증거로만 기록하며, operation result·typed event·후속 snapshot을
같이 단언하지 않으면 10.0.0 완료 증거로 사용하지 않는다.

| 시나리오 | 상태 | 현재 증거 | 남은 gap |
|---|---|---|---|
| MON-A1 | 구현 | Mesh event와 snapshot evidence를 10.0.0 runtime 표면으로 확인했다. | 최신 전체 실행 통과. |
| MON-A2 | 구현 | Typed peer event와 snapshot의 연결 상태를 확인했다. | 최신 전체 실행 통과. |
| MON-A3 | 구현 | Spot subject 추적과 관련 event evidence를 확인했다. | 최신 전체 실행 통과. |
| MON-A4 | core 대기 | Weight 전파와 drain·restore 구간은 통과했지만 같은 RID replacement의 최종 재승인이 실패한다. | core 재승인 결함 수정 뒤 다시 실행한다. |
| MON-A5 | core 대기 | 무자격 TCP의 handshake 실패는 peer 항목이 없어 현재 binding 표면으로 관찰할 수 없다. | core mesh monitor event의 binding 공개가 필요하다. |
| MON-B1 | 구현 | 구성한 kind filter가 필요한 event만 전달하는지 확인했다. | 최신 전체 실행 통과. |
| MON-B2 | 구현 | 잘못된 monitoring 등록이 startup validation에서 거부되는지 확인했다. | 최신 전체 실행 통과. |
| MON-C1 | 구현 | Monitoring handler 실패 뒤에도 messaging과 정상 observer가 계속되는지 확인했다. | 최신 전체 실행 통과. |
| MON-D1 | core 대기 | 비정상 종료 뒤 같은 endpoint replacement의 재승인이 이뤄지지 않아 반복 복구가 중단된다. | core 재승인 결함 수정 뒤 다시 실행한다. |
