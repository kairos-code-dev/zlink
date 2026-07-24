# .NET RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

## 10.0.0 목표 판정

Config 7은 MeshNode·peer·ChannelName readiness, claim progress와 location health를 공개 runtime
snapshot과 typed event로 검증한다. Publish는 전용 snapshot·metric·runtime event를 만들지 않으므로
Track B에서는 public 관측값의 부재와 result-free terminal을 검증한다. Socket·location·Spot source
marker는 관련 행의 부분 증거로만 기록한다.

| 시나리오 | 상태 | 현재 증거 | 남은 gap |
|---|---|---|---|
| MON-A1 | 구현 | MeshNode의 peer lifecycle event와 후속 snapshot이 같은 RID·generation·endpoint를 나타내는지 확인했다. | 없음 |
| MON-A2 | 구현 | 같은 RID의 정상 교체 전후 typed peer event와 snapshot에서 이전 generation이 ready로 남지 않는지 확인했다. | 없음 |
| MON-A3 | 구현 | Spot subject 상태와 ChannelName 변경을 typed event와 snapshot으로 함께 확인했다. | 없음 |
| MON-A4 | 구현 | 정상 교체와 비정상 종료 뒤 같은 RID 재시작에서 이전 generation 제거, 새 generation 준비와 별도 request 완료를 확인했다. | 없음 |
| MON-A5 | 구현 | location store 중단·복구 중 peer·channel messaging이 유지되고 location health event와 snapshot이 일치하는지 확인했다. | 없음 |
| MON-B1 | 부분 구현 | zero-target publish를 실제로 시작한 뒤 public snapshot·event에 publish 전용 필드가 없고, framework assembly에 제거한 public type·metric·event 이름이 없음을 검사한다. | 막힌 remote target이 있어도 시작 뒤 정상 완료하며 rollback·자동 재시도가 없음을 process E2E에서 추가로 확인한다. |
| MON-B2 | 부분 구현 | local subscriber를 만든 뒤 publish하고 handler 단일 처리와 public snapshot·event의 publish 전용 관측값 부재를 검사한다. | 막힌 local target과 정상 target을 함께 두고 message-flow trace에도 target별 결과가 남지 않음을 추가로 확인한다. |
| MON-C1 | 구현 | application gate 중 별도 request와 정상 observer가 진행되고, 작은 observer queue의 coalescing·consumer 예외 뒤 snapshot resync와 messaging이 유지되는지 확인했다. | 없음 |
| MON-D1 | 구현 | 잘못된 public 호출 거부와 세 차례 비정상 종료·재시작에서 peer·channel event sequence, 최신 ready snapshot과 event field 제한을 확인했다. | 없음 |

2026-07-20 실행 기록은 당시 계약의 회귀 증거이며 `logs/20260720-004148-1606817`부터
`logs/20260720-004424-1615752`까지 보존한다. CA-D77 이후 제거된 target별 집계를 검증한 기존
MON-B1·MON-B2 결과는 현재 계약의 완료 증거로 사용하지 않는다.
