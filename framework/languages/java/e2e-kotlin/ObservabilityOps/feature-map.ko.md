# ObservabilityOps Kotlin feature map

이 표는 공통 Config 11의 시나리오와 Kotlin trigger, 공유 Java runtime,
evidence verifier의 대응을 기록한다. verifier는 배포된 framework 프로세스가 남긴
증거만 읽으며, 런타임 증거를 임의로 만들지 않는다.

| ID | verifier가 확인하는 증거 | 현재 실행 상태 |
|----|--------------------------|----------------|
| OBS-A1 | connector outbound부터 STREAM, relay, Spot dispatch까지 같은 flow와 순서 | PASS |
| OBS-A2 | received와 error reply 라인의 같은 flow | PASS |
| OBS-A3 | tracing Off 노드의 기록 억제와 하류의 같은 flow | PASS |
| OBS-A4 | fanout 분기와 timer 발원 flow | PASS |
| OBS-B1 | STREAM active/opened/closed/reconnect와 닫힌 종료 사유 | PASS |
| OBS-B2 | Spot queue와 actor transfer 계기 | PASS |
| OBS-B3 | fanout/lease 계기와 고카디널리티 label 부재 | PASS |
| OBS-B4 | reader 미등록 traffic의 무보관과 messaging 정합 | PASS |
| OBS-C1 | readiness, typed draining row, 기존 연결, lease 갱신 | PASS |
| OBS-C2 | takeover, bound push, pending request, handed-off 계기 | PASS |
| OBS-C3 | 두 Spot drain 정책과 replay 재구성 | PASS |
| OBS-C4 | force stopping, session-closing, server_drain, forced 계기 | PASS |
| OBS-C5 | serving target 롤아웃과 zero-target deadline 결과 | PASS |

각 PASS는 Kotlin 공개 adapter를 사용하는 trigger와 공유 Java runtime 역할을 기동한
strict runner에서 scenario별 verifier가 성공했음을 뜻한다. 2026-07-14 `all` 실행으로
OBS-A1~A4, OBS-B1~B4, OBS-C1~C5를 확인했다. runner는 각 selector를 새 Redis와 새
토폴로지에서 실행한다. 따라서 drain과 역할 재기동으로 생긴 상태가 다음 selector에
남지 않는다. C5의 serving과 zero-target 분기도 각각 새 토폴로지에서 검증한다.

OBS-C2는 동일 routing id를 사용하는 Play 역할의 재기동, pending request 완료와
handed-off 계기를 확인한다. OBS-C3는 두 Spot의 drain 정책과 replay 재구성을 확인한다.
두 selector를 포함한 Config 11 전체 실행이 종료 코드 0으로 통과했다.
