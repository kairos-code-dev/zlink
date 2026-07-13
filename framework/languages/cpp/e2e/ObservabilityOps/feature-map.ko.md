# Config 11 — ObservabilityOps (C++) feature map

정본 시나리오: [config-11-observability-ops.ko.md](../../../../doc/framework/common/e2e/config-11-observability-ops.ko.md)

| 시나리오 | 상태 | 비고 |
|----------|------|------|
| OBS-A1 flow 관통 | 구현 | connector 발원 flow가 session inbound→room-spot dispatch까지 `flow=` 동일, `origin=application` |
| OBS-A2 error 라인 flow | 구현 | 미등록 packet dispatch error 라인에 `flow=` |
| OBS-A3 create-if-absent·off 전파 | 구현 | `offnode` 페이즈: play-a trace off — flow 무생성·무로그, 하류(play-b)에 같은 pair 도달 |
| OBS-A4 fan-out 트리·timer 발원 | 구현 | `fanout` 페이즈: publish 트리 구독자 라인 동일 flow, timer tick publish는 `origin=timer` 신규 flow |
| OBS-B1 CCU·재접속 | 구현(부분) | 서버측 `connections.active/opened/closed(close_reason)` 실측. connector `reconnects` 계기는 공개 표면 spec 확정 대기 |
| OBS-B2 SPOT 큐·transfer 계기 | 구현 | `spot.queue.depth/wait.duration(kind)` + `actor.transfers/transfer.duration/pending_requests.count`(핸드오프 페이즈 실측) |
| OBS-B3 fanout·lease 계기 | 구현(부분) | `fanout.published/received(topic)` 1:N 실측·dropped 미방출. lease 갱신 지연은 redis 측 지연 주입 인프라 필요 |
| OBS-B4 비활성 최소 비용 | 구현 | reader 무등록 노드(`offnode` 페이즈 play-a) 메시징 불변+계기 무적재. 핫패스 clock 생략은 unit(RMETRIC-001/009) 소유 |
| OBS-B(부분) spot/channel 계기 | 구현 | `spot.created/count(kind)`·`channel.request.duration(s)`·고카디널리티 라벨 부재 |
| OBS-C1 draining 마커 | 구현 | 마커 게시+연결 유지+readiness flip+생성 거부+peer 격리+terminal Drained |
| OBS-C2 actor 핸드오프 | 구현 | `handoff` 페이즈: 일반 join(admission/transfer/commit) 완주, `drain.actors.handed_off`+transfer 계기, post-move ping 연속성. bound-session push 세부는 ping 연속성으로 대체 |
| OBS-C3 SPOT 정책 | 구현 | `policy` 페이즈: release-and-recreate row 해제 → 타 노드 GetOrCreate 재구성, `rooms.drained{policy}` |
| OBS-C4 강제 종료 통지 | 구현 | `force` 페이즈: ForceStopped 시 활성 세션 `session-closing(server_drain)`+종료, connector 공개 `closeReason` 확인 |
| OBS-C5 롤아웃/zero-target | 구현 | (a) rolling drain은 무강제 Drained, (b) zero-target은 deadline 강제+`drain.forced{actor,session}` |

실행: `./run_e2e.sh [all|flow|metrics|fanout|drain|handoff|force|policy|offnode]`

잔여(외부 의존): connector `stream.reconnects` 계기(spec 표면 확정), lease 지연 주입(redis 인프라),
cross-language fanout wire 대조(G6 — ledger CPP-FANOUT-WIRE-001).
