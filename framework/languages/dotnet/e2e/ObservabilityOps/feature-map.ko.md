# ObservabilityOps .NET feature map

이 표는 공통 Config 11의 scenario ID가 `.NET` runner에서 어떤 증거로 검증되는지 기록한다.

| ID | 상태 | .NET 구현 |
|----|------|-----------|
| OBS-A1 | 구현 | Bingo session request가 actor/API, Play와 room Spot을 같은 flow id로 통과하고 시간 순서가 유지되는지 검증한다. |
| OBS-A2 | 구현 | 미등록 packet의 received/error가 같은 flow id를 사용하고 protocol error가 trigger에 반환되는지 검증한다. |
| OBS-A3 | 구현 | connector가 flow를 만들고 tracing off API node가 기록 없이 flow를 Play로 전달하는지 검증한다. |
| OBS-A4 | 구현 | ShoppingMall projection fan-out 두 갈래가 같은 flow id를 사용하고 Bingo room timer가 timer origin flow를 만드는지 검증한다. |
| OBS-B1 | 구현 | 세 connector의 active/opened/closed 증감과 reconnect attempt counter를 정확한 meter sample로 검증한다. |
| OBS-B2 | 구현 | Entry/user Spot queue 계열, actor transfer count/duration과 pending request sample을 검증한다. |
| OBS-B3 | 구현 | fanout 1:2, lease renew lateness, 닫힌 label과 고카디널리티 label 부재를 검증한다. |
| OBS-B4 | 구현 | reader를 켜지 않은 node가 metric raw sample을 보관하지 않는지 검증한다. |
| OBS-C1 | 구현 | drain 시작 시 readiness와 typed draining row가 바뀌고 기존 연결과 active room이 유지되는지 검증한다. |
| OBS-C2 | 구현 | actor handoff 뒤 location owner가 바뀌고 bound session push가 계속 같은 client에 도달하는지 검증한다. |
| OBS-C3 | 구현 | drain-natural과 release-and-recreate가 각각 room 제거와 checkpoint replay 계약을 지키는지 검증한다. |
| OBS-C4 | 구현 | server drain, idle timeout, heartbeat timeout과 protocol error close reason이 connector에 노출되는지 검증한다. |
| OBS-C5 | 구현 | target이 없는 동시 drain에서 actor가 source에 deadline까지 유지되고 source readiness가 차단되는지 검증한다. |

