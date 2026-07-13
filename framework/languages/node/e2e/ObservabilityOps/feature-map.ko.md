# Node.js ObservabilityOps 검증표

이 fixture는 공개 framework와 connector 표면만 사용한다. 자동 turn 배포의 실제 다중 노드
STREAM→Spot→actor 경로, 실제 actor transfer 배포, metric reader contract와 drain lifecycle contract를
한 runner에서 묶어 Config 11의 운영 불변식을 검증한다.

| 시나리오 | 실제 검증 경로 |
|----------|----------------|
| OBS-A1~A4 | `AutomaticTurnDispatch` 전체 runner의 session/play/delay flow 로그와 timer 발원 로그 |
| OBS-B1~B4 | `runtime-metrics.test.js`의 meter reader, reconnect 소유권, 비활성 계측 검증 |
| OBS-C1, C3~C5 | `drain-control.test.js`의 공유 drain, 두 Spot 정책, 강제 종료와 session-closing 순서 |
| OBS-C2 | `SpotActorTransfer` 전체 runner의 실제 이동 및 bound-session 연속성 |

`run_e2e.sh`는 모든 하위 runner와 contract test가 성공하고, flow UUIDv7 및 동일 root 전파를 로그에서
다시 확인한 뒤에만 `observability-ops e2e result=passed`를 출력한다.
