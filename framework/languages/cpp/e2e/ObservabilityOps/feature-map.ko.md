# Config 11 — ObservabilityOps (C++) feature map

정본 시나리오: [config-11-observability-ops.ko.md](../../../../doc/framework/common/e2e/config-11-observability-ops.ko.md)

| 시나리오 | 상태 | 비고 |
|----------|------|------|
| OBS-A1 flow 관통 | 구현 | connector 발원 flow가 session inbound→room-spot dispatch까지 `flow=` 동일, `origin=application` |
| OBS-A2 error 라인 flow | 구현 | 미등록 packet dispatch error 라인에 `flow=` |
| OBS-A3 create-if-absent·off 전파 | 대기 | off 중간 노드 토폴로지 필요(unit은 MFLOW-EXT-003/008 케이스가 소유) |
| OBS-A4 fan-out 트리·timer 발원 | 대기 | publish fan-out flow 배선·timer origin e2e 관측 잔여 |
| OBS-B1 CCU·재접속 | 구현(부분) | 서버측 `connections.active/opened/closed(close_reason)` 실측. connector `reconnects` 계기 잔여 |
| OBS-B2 SPOT 큐·transfer 계기 | 구현(부분) | `spot.queue.depth/wait.duration(kind)` 실측. `actor.transfer*` 계기 잔여 |
| OBS-B3 fanout·lease 계기 | 대기 | `fanout.*`/`location.*` 계기 미배선 |
| OBS-B4 비활성 최소 비용 | 부분 | emitter fold는 unit이 소유(RMETRIC-001/009), e2e 장시간 검증 잔여 |
| OBS-B(부분) spot/channel 계기 | 구현 | `spot.created/count(kind)`·`channel.request.duration(s)`·고카디널리티 라벨 부재 |
| OBS-C1 draining 마커 | 구현(부분) | 마커 게시+연결 유지+readiness flip+생성 거부+terminal Drained. 전파 지연 창 request 무오류 검증은 잔여 |
| OBS-C2 actor 핸드오프 | 대기 | drain 핸드오프 실행 미구현 |
| OBS-C3 SPOT 정책 | 대기 | `release-and-recreate` 실행 미구현(선언 표면만) |
| OBS-C4 강제 종료 통지 | 대기 | 활성 세션 `session-closing` 발신 orchestration 잔여(신규 연결 거부 통지는 구현) |
| OBS-C5 롤아웃/zero-target | 대기 | 핸드오프 의존 |

실행: `./run_e2e.sh [all|flow|metrics|drain]`
