# SpotActorTransfer Node E2E feature map

공통 정본은 `framework/doc/framework/common/e2e/config-10-spot-actor-transfer.ko.md`다.
consumer는 Node HTTP client wrapper로 역할 서버 endpoint를 호출하고, bound session 검증에는 stream
connector를 사용한다. actor node 두 개는 같은 Redis location store와 Spot mesh를 공유한다.

| 시나리오 | 구현 | 검증 |
| --- | --- | --- |
| ST-A1/A2/A3 | 구현 | local accept 순서, reject 무효과, joined 대기 중 dispatch 차단 |
| ST-B1/B2/B3/B4 | 구현 | state 복원, source 종료 후 성공 유지, adapter 미등록과 custom 빈 state |
| ST-C1/C2/C3 | 구현 | commit 전·후 source 종료, callback 단계별 실패 분류 |
| ST-D1/D2 | 구현 | joined 완료 전 location 비공개, stale source generation fencing |
| ST-E1/E2 | 구현 | remote transfer 뒤 기존 connector push, 실패 시 기존 binding 유지 |
| ST-F1/F2/F3 | 구현 | moving backlog 순서, publish 전 replay, bound session cross-move FIFO |
| ST-F4/F5 | 구현 | bounded straggler forward, cutoff fail-fast, hop별 mapping 교체·축출 |
| ST-F6 | 구현 | request id·flags 보존, 원 caller reply correlation, caller timeout·late reply 처리 |

## 증거 경계

- callback과 transfer 순서는 actor node가 실제 callback에서 남긴 evidence를 transfer id로 연결한다.
- location commit은 Redis key를 client가 직접 읽지 않고 location monitoring event의 row update로 확인한다.
- ST-C1의 pending admission은 31초 뒤 target actor가 materialize되지 않은 공개 결과와, deadline 뒤 같은
  transfer id의 late commit을 거부하는 in-process contract test를 함께 사용해 검증한다.
- ST-B2의 source cleanup은 target success가 source 종료 뒤에도 유지되는 배포형 결과와, location remove가
  한 번 실패한 뒤 같은 generation으로 재시도되는 in-process contract test를 함께 사용해 검증한다.
- bound session은 stream connector가 받은 push의 actor id, node rid, state version을 확인한다. target은
  commit ack 전에 session gateway에 ownership generation을 보내므로 첫 target push보다 먼저 도착한 stale
  source push도 gateway에서 폐기된다.
- Track F는 source의 `handoff_backlog`, target의 `backlog_enqueued`, location의 `location_committed` 순서를
  actor별 arrival index로 대조한다. forwarding window는 E2E에서 500ms로 줄여 window 안 forward와 축출 뒤
  `ActorLocationStale` fail-fast를 한 실행에서 확인한다.
- ST-F6은 moving 중 request의 stream header에 있던 request sequence와 flags를
  `handoff_request_frame`으로 남긴다. 충분히 긴 timeout의 request는 target reply가 원 caller에 한 번만
  도착해야 하고, 짧은 timeout의 request는 normal timeout 뒤 늦은 reply가 다음 request를 방해하지 않아야 한다.

기존 Track A~E 전체 실행 증거는 `log/20260710-152609-2661347`이다. Track F 전체 실행 증거는
`log/20260710-200221-3864800`이며 ST-F1~ST-F6이 모두 통과했다.
