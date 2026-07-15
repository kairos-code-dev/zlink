# ObservabilityOps Java feature map

이 표는 공통 Config 11의 시나리오와 Java evidence verifier의 대응을 기록한다. verifier는 배포된
framework 프로세스가 남긴 증거만 읽으며, 런타임 증거를 임의로 만들지 않는다.

| ID | verifier가 확인하는 증거 | 현재 실행 상태 |
|----|--------------------------|----------------|
| OBS-A1 | connector outbound부터 STREAM, relay, Spot dispatch까지 같은 flow와 순서 | PASS |
| OBS-A2 | received와 server dispatch error 라인의 같은 flow | partial — runtime error flow event 부재(E2E-JV-19) |
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

각 PASS는 실제 Java 역할을 기동한 strict runner와 scenario별 verifier 성공을 뜻한다. OBS-A2는
client가 missing-handler 오류를 받아도 server flow log에 같은 flow의 error event가 없어 strict
runner가 실패하므로 완료로 판정하지 않는다. C5의 serving과
zero-target 분기는 같은 프로세스를 임의로 재사용하지 않고 각각 새 토폴로지에서 검증한다.
