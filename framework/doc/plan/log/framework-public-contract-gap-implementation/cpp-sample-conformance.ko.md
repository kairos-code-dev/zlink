# C++ 정본 샘플 ↔ 공통 sample spec 대조 원장 (G5-b)

기준 문서: `framework/doc/framework/common/sample/` (README + 시나리오별 문서).
언어별 샘플 문서는 계약 기준이 아니며, 이 원장은 C++ 샘플 6종이 공통 시나리오와
일치하는지 항목별로 추적한다. 상태는 `열림`, `수정중`, `닫힘`, `문서 drift`(공통 문서 쪽
정정 필요) 중 하나다.

기준일: 2026-07-13. 대조 방식: 공통 spec의 역할 분리 / 메시지 이름·필드 / codec /
서버 간 연결 / 자동 turn / dispatch 오류 로그 / runner Redis 격리 / self-check 순서.

## 1. 전 샘플 공통

| ID | 항목 | 현재 | 처리 | 상태 |
|----|------|------|------|------|
| SMP-COMMON-001 | runner cleanup은 `docker rm -fv`(익명 볼륨 제거) | 6개 `run_sample.sh`의 exit trap이 `docker rm -f` | 전 러너를 `-fv`로 정렬 | 닫힘 |
| SMP-COMMON-002 | Redis host port는 Docker가 배정(`127.0.0.1::6379`) | Bingo만 고정 포트 매핑 사용 | Bingo를 helper 기본 배정으로 정렬 | 닫힘 |
| SMP-COMMON-003 | Redis 이미지 pin 일관성 | Bingo `redis:7.2-alpine`, 나머지 `redis:7-alpine` | 전 샘플 `redis:7-alpine`로 통일 | 닫힘 |
| SMP-COMMON-004 | 샘플 handler는 dispatch 오류를 다시 잡아 로그 후 rethrow하지 않는다 | ShoppingMall `SHOPPINGMALL_HANDLER` 매크로와 handler가 catch→log→rethrow | 매크로 제거, framework dispatch 경계에 위임 | 닫힘 |
| SMP-COMMON-005 | dispatch 오류 로그는 framework message-flow가 남긴다(수기 위조 금지) | SupportChat/ShoppingMall/일부 stub 프로세스가 `"message flow role=..."` 줄을 직접 파일에 기록하고 runner가 그 문자열을 grep | SupportChat api는 실제 framework 서버로 전환, probe의 수기 flow 로그 제거(실제 self-check 수행). ShoppingMall 잔여분 정리 예정 | 수정중 |

## 2. Bingo

| ID | 항목 | 편차 | 상태 |
|----|------|------|------|
| SMP-BINGO-001 | payload codec = Protobuf | `.proto`를 두고 protoc 코드젠으로 실제 protobuf wire를 싣는다(서버 간·client stream 모두). codec extension도 진짜 protobuf 직렬화로 교체 | 닫힘 |
| SMP-BINGO-002 | Play↔Play spot pub/sub, Session→Play router는 location store 자동 연결 | `connect_peer_pub`/`connect_router` 수동 endpoint 배선 제거. 두 연결 모두 location store 자동 연결에 맡긴다 | 닫힘 |
| SMP-BINGO-003 | `BingoRoomState.Status` = `WaitingForPlayers`/`Running`/`Finished` | wire 값을 정본 대소문자로 교체 | 닫힘 |
| SMP-BINGO-004 | 미사용 wire 계약 없음 | `BingoStateNotify` 계약·codec 등록·이름 상수 삭제(정본 메시지 목록에 없음) | 닫힘 |
| SMP-BINGO-005 | self-check 순서(3 클라이언트 선인증) + 보상 알림 `RoomId` 검증 | 세 client 인증을 매칭 이전으로 모음. 보상 알림 `RoomId`는 wait 필터로 이미 강제 중임을 확인 | 닫힘 |
| SMP-BINGO-D01 | 공통 문서의 `BingoRewardAcquiredEvent` | 공통 README의 이름 규칙(wire는 `Event` 금지)과 Bingo 표가 상충. 코드는 규칙에 맞는 `BingoRewardAcquiredMsg` | 문서 drift(Bingo 표 정정 대상) |

## 3. TicTacToe

| ID | 항목 | 편차 | 상태 |
|----|------|------|------|
| SMP-TTT-001 | API↔Play는 **수동 endpoint** 연결(정본이 수동 scale-out을 보여 준다) | `all_play_endpoints()`/`all_api_endpoints()`로 두 peer를 `enable_client(endpoint)` 직접 연결. sample parity 계약 테스트도 수동 배선을 단언하도록 갱신 | 닫힘 |
| SMP-TTT-002 | room route store는 `redis-plus-plus` 사용, RESP 직접 구현 금지 | `sw::redis::Redis` 기반으로 재작성(raw RESP 인코더/파서 제거) | 닫힘 |
| SMP-TTT-003 | room route 레코드 = `{RouteChannelId, OwnerNodeRid, SpotRid, SpotKind}` | `tictactoe:rooms:{RoomId}` hash에 4개 field로 저장/조회 | 닫힘 |
| SMP-TTT-004 | `JoinGameReq {RoomId}` — player 정보는 actor가 채운다 | `join_game_req_t`에서 `player` 제거. entry spot이 actor에 보관한 `PlayerInfo`로 `TicTacToeGameJoinReq`를 구성 | 닫힘 |
| SMP-TTT-005 | client inbound observer(step 12) | player 2인은 이미 등록되어 있었고 observer 커넥터만 누락 → 추가하고 runner가 `stream-inbound` 마커를 단언 | 닫힘 |
| SMP-TTT-006 | `EnsurePlayerActorReq/Res`는 Bingo 전용 | TicTacToe에서 계약·핸들러 삭제. 인증 응답의 `PlayerInfo`를 그대로 actor 생성 payload로 사용 | 닫힘 |
| SMP-TTT-D01 | 공통 문서의 `PlayerWinMilestoneEvent` | 이름 규칙상 `Msg`가 맞고 코드도 `PlayerWinMilestoneMsg` | 문서 drift |

## 4. SupportChat

| ID | 항목 | 편차 | 상태 |
|----|------|------|------|
| SMP-SC-001 | 역할 분리 Session/Api/Support | Api 서버를 실제 framework 서버로 구현: 사용자 디렉터리 기반 `AuthenticateUserReq/Res`, `OpenConversationApiReq/Res`(→ Support `AllocateConversationReq/Res`). Session의 위조 인증 제거(API 채널 요청), Entry Spot은 배정된 대화에 join만 수행 | 닫힘 |
| SMP-SC-002 | `AuthenticateUserReq/Res`, `OpenConversationApiReq/Res`, `AllocateConversationReq/Res` | 세 쌍 모두 실제 wire 경로에서 사용 | 닫힘 |
| SMP-SC-003 | idle timer(3s)와 close grace(2s)는 Spot timer | conversation Spot에 500ms tick timer 신설 — idle deadline 경과 시 `WaitingForClose`+idle 알림, close grace(2s) 경과 시 종료+closed 알림. 클라이언트의 idle 흉내(`CloseConversationReq{reason:"idle"}`) 제거 | 닫힘 |
| SMP-SC-004 | idle/closed 알림은 양쪽 참가자에게 | `broadcast()`로 고객·상담원 모두에게 발송 | 닫힘 |
| SMP-SC-005 | multi-room/상담원 capacity, reconnect, closed 대화 오류 | client 시나리오를 spec §17로 확장: 두 번째 고객 방(같은 상담원)·방별 MessageSeq 독립·같은 token 재접속 후 재join(상태 보존)·명시적 close·closed 대화 재close/전송 오류. 도메인 `close()`에 closed 가드 추가 | 닫힘 |
| SMP-SC-006 | notify wire shape: `State`는 중첩 객체 | `SUPPORTCHAT_NOTIFY_JSON`이 `{ConversationId, State}`로 직렬화하도록 수정 | 닫힘 |
| SMP-SC-007 | 클라이언트는 Session stream만 사용 | client에서 HTTP 의존 제거. 서버 불변식 self-check는 probe 프로세스가 수행하고 runner가 probe 증거를 단언 | 닫힘 |
| SMP-SC-008 | Docker Redis 실패 시 즉시 실패 | socketless fallback 제거(포트 확보 실패도 즉시 실패) | 닫힘 |

## 5. DeliveryDispatch

| ID | 항목 | 편차 | 상태 |
|----|------|------|------|
| SMP-DD-001 | `AssignDeliveryMsg`(one-way send) | `AssignDelivery` + `AssignDeliveryResult`(request/reply, 금지 접미어) | 닫힘 |
| SMP-DD-002 | 역할 = Dispatch/CourierSession/CourierSpotNode×2/Tracking/CustomerGateway | Dispatch를 HTTP edge + DispatchWorker 한 프로세스로 합치고 CourierGateway를 제거했다. courier session route는 courier actor가 기억한다 | 닫힘 |
| SMP-DD-003 | DispatchWorker가 배차 큐·선택 정책·timeout 재시도를 소유 | worker가 작업 큐(hosted service)·배송원 선택 정책·offer 요청 timeout을 소유한다. 노드의 결정 랑데부는 여전히 blocking wait이며, 이를 없애려면 외부에서 완료시키는 awaitable(다른 언어의 TaskCompletionSource/CompletableFuture 대응)이 C++ public 표면에 필요하다 — 계약 결정 전까지 draft 후보로 남긴다 | 부분(계약 후보) |
| SMP-DD-004 | `FindCourierActorReq/Res`, `FindCustomerActorReq/Res` | 두 계약을 추가하고, CourierSession·DispatchWorker·Tracking이 ensure 이전에 find를 먼저 부른다 | 닫힘 |
| SMP-DD-005 | Tracking→CustomerEntrySpot `DeliveryStatusUpdatedMsg`→actor→`DeliveryStatusNotify` | fanout 채널을 걷어내고 Tracking이 고객 actor에 one-way로 보낸다. push는 actor가 bound session으로 한다. Tracking의 죽은 ensure 핸들러도 삭제 | 닫힘 |
| SMP-DD-006 | `DeliveryStatusChangedReq` 필드 = `{DeliveryId, Status, CourierId, OccurredAt}` | wire DTO에서 `CustomerId` 제거(Tracking은 쓰지 않았음) | 닫힘 |
| SMP-DD-007 | self-check의 node 배치 검증(courier-a=node-1, courier-b=node-2) | `bind_courier`가 bind 응답의 actor node rid를 기대 노드와 대조 | 닫힘 |

## 6. ShoppingMall

| ID | 항목 | 편차 | 상태 |
|----|------|------|------|
| SMP-SM-001 | event store 레코드(`StoredOrderEvent`)와 typed 도메인 event | `stored_order_event_t`(EventId/SourceCommandId/EventType/Payload/Version)로 기록하고 이벤트별 payload를 담는다 | 닫힘 |
| SMP-SM-002 | aggregate는 event stream fold로 rehydrate | `fold()`가 스트림을 접어 aggregate를 만들고, 다음 단계 판정도 그 결과가 한다. 기록은 기대 Version 검사를 통과해야 한다 | 닫힘 |
| SMP-SM-003 | projection은 stream만으로 재생성 가능 | rebuild가 스트림 재생만으로 조회 모델을 복원한다(read model·하드코딩 의존 제거) | 닫힘 |
| SMP-SM-004 | inventory/payment 모듈 port(`ReserveInventoryCommand` 등) | 재고·결제 모듈을 CommerceStateStore 시드 위에 구현했다. 결정적 ReservationId/PaymentId로 멱등이며, 재개는 최초 결과를 그대로 돌려받는다 | 닫힘 |
| SMP-SM-005 | CommerceApi는 event store/projection을 직접 쓰지 않는다 | 중간 상태는 owner 루프가 읽는 stop hook이 만든다. API가 스트림을 자르지 않는다 | 닫힘 |
| SMP-SM-006 | `StartOrderRes`는 신규 주문에 `Created` 즉시 반환(Continue는 owner가 비동기 진행) | 시작은 루프를 Created까지만 돌리고 응답하며, 재개는 기다리지 않고 one-way로 예약한다. 클라이언트는 폴링으로 종료를 확인한다 | 닫힘 |
| SMP-SM-007 | 서버 간 연결은 location store 자동 연결 | 수동 endpoint 배선 제거, route mesh·spot router 모두 자동 연결 | 닫힘 |
| SMP-SM-008 | handler catch→log→rethrow 금지 | 매크로로 전 handler에 적용 | 닫힘 |

## 7. GameQuest

| ID | 항목 | 편차 | 상태 |
|----|------|------|------|
| SMP-GQ-001 | owner spot은 event-sourced aggregate(append/replay/snapshot) | `quest_event_store_t`(append-only)를 두고 projection은 player별 stream을 fold해서 만든다. sync/get/notify가 모두 replay 결과를 쓴다. snapshot은 아직 없다 | 닫힘(snapshot 제외) |
| SMP-GQ-002 | `GameplayMsg`(one-way send) | one-way send로 전환. client에는 event id만 즉시 돌려주고 진행은 notify로 돌아온다 | 닫힘 |
| SMP-GQ-003 | notify는 location store session binding으로 현재 노드에 전달 | raw HTTP를 걷어내고 `resolve_actor_spot_handle`로 player의 현재 노드를 찾아 그 노드의 entry spot으로 route한다. **막혔던 이유**: spot 노드는 route 채널 **하나에만** bridge를 붙이므로(`attach_spot_route_bridge`), 별도 notify mesh를 두면 그 mesh로 온 프레임이 spot에 닿지 못하고 route-mesh 메시지로 떨어진다. 해법은 같은 spot route mesh를 양방향으로 쓰는 것 | 닫힘 |
| SMP-GQ-004 | `QuestProgress`에 `Version`, `LastSourceEventId` | 두 필드를 스펙 이름·의미대로 채운다(fold 버전, 반영한 gameplay event id) | 닫힘 |
| SMP-GQ-005 | status = `Active`/`Completed`/`RewardGranted` | 세 상태를 모두 두고, fold가 QuestCompleted→QuestRewardGranted 순서로 상태를 옮긴다 | 닫힘 |
| SMP-GQ-006 | 서버 간 연결은 location store 자동 연결 | 수동 endpoint 배선 제거(route mesh·spot router 모두 자동 연결) | 닫힘 |
| SMP-GQ-007 | self-check: projection 재생성·rehydrate·reconnect·reset 보정·reward 멱등 | reconnect(다른 노드 재접속 시 notify가 따라오는지)와 reward 멱등(완료 quest에 같은 event 재적용 시 진행 미변화)을 추가. projection은 매 조회가 stream replay라 재생성이 항상 성립한다. rehydrate(노드 재시작)와 reset 보정(GameplayStateStore drift)은 별도 store가 없어 미적용 | 닫힘(rehydrate·reset 제외) |

## 8. 진행 원칙

- 수정은 공통 spec을 기준으로 한다. 공통 spec이 현재 public contract로 표현 불가능한
  기능을 요구하면 샘플에서 우회하지 않고 계약 gap으로 분리한다.
- 각 항목은 개별 `run_sample.sh`와 통합 `run_samples.sh` 통과로 닫는다.
- 공통 문서 자체가 규칙과 어긋나는 항목(`문서 drift`)은 코드를 규칙에 맞춘 뒤 공통 문서
  정정을 별도로 제안한다.

## 9. 집합 실행에서만 재현되는 core 결함(gate 영향)

개별 `run_sample.sh`는 전 샘플이 반복 통과하지만(각 3회 이상), 통합 `run_samples.sh`처럼
샘플을 연달아 돌려 머신 부하가 높을 때 아래 두 가지가 간헐 재현된다. 두 건 모두 이번
sample spec 정렬로 생긴 것이 아니라 부하가 드러낸 core 경로의 문제다.

| ID | 증상 | 근본 원인(추적 결과) | 소유 |
|----|------|----------------------|------|
| CPP-SPOT-SUB-ACT-001 | Bingo가 `native spot subscription activation failed for 'bingo.room' ... (errno=95)`로 실패. owner room과 observer room 양쪽에서 관측 | `spot_node_t::update_logical_spot_subscription`이 internal receiver 생성 실패를 **원인과 무관하게 `ENOTSUP`으로 덮어쓴다**(`core/src/runtime/services/spot/node/spot_node_summary.cpp:264-269`). 실제 실패는 `create_spot_sub_with_defaults`의 attachment 생성/`wait_facade_peer` 핸드셰이크가 부하에서 타임아웃한 것(`spot_node_defaults.cpp:72-84`). 즉 "pub/sub 미지원"이 아니라 일시적 타임아웃인데 영구 실패로 보고된다 | core |
| CPP-AUTOCONNECT-CFG-001 | DeliveryDispatch `courier-session` 프로세스가 `terminate called after throwing an instance of 'zlink::config_error_t' what(): Unknown error 702 (errno=22)`로 abort(SIGABRT). 러너는 cleanup에서 status 134로 실패 처리 | native config 호출이 던진 `config_error_t`가 어느 loop에서도 잡히지 않고 `std::terminate`까지 전파. auto-connect 스캔이 활발한 구간에서만 관측 | core/framework |

| CPP-CORE-SPOTDESTROY-002 | SpotService 종료 단계에서 play 노드가 10초 안에 끝나지 않아 러너가 강제 kill(137) → config FAIL. 시나리오는 전부 통과한 뒤였다 | 백트레이스: 메인 스레드가 `zlink_spot_node_destroy` → `wait_for_closing_sockets` → `ctx_t::wait_for_socket_removal`에서 대기. **정지가 아니라 느린 종료**(유예 60초로 돌리면 강제 kill 없이 exit=0). core의 소켓 제거 완료 대기가 부하에서 10초를 넘긴다 | core(성능) |

| CPP-ATD-TIMER-RESUME-001 | E2E AutomaticTurnDispatch **ATD-C3B**(가끔 **ATD-D2**)가 간헐 실패한다. client가 받는 오류는 `End of file` — 세션 서버가 client stream을 닫는다 | **해결(2026-07-14).** stream connector는 server liveness ping의 pong을 `dispatch()` 경로에서만 썼다. ATD client는 async submit으로 응답을 기다리는 동안 `dispatch()`를 부르지 않으므로, 수신 pump가 ping을 읽어 `heartbeat_pong_due`만 세우고 pong은 나가지 않았다. 응답이 heartbeat 창보다 오래 걸리는 정상 요청에서 서버가 세션을 heartbeat timeout으로 끊었고, client는 그것을 `End of file`로 봤다. 수정: 수신 pump(async 체제)는 pong을 write 큐에 싣고, 동기 request 루프(sync 체제)는 자기 문맥에서 바로 답한다. 두 체제를 섞어 쓰면(io 스레드에서 동기 write) 같은 소켓에서 in-flight async write와 바이트가 섞여 abort까지 났다. 회귀 테스트는 `test_cpp_stream_connector`의 manual-dispatch pong 케이스. 동행 수정: 세션 픽스처의 중첩 timeout(3000ms → 15000ms), 러너 종료 유예(8s → 45s, `ZLINK_CPP_E2E_PROCESS_SHUTDOWN_TIMEOUT_SECONDS`) | framework/connector(완료) |

남은 과제: ATD 세션 픽스처가 actor relay 실패를 오류 응답이 아니라 **stream close**로 표현한다.
계약상 실패는 error 프레임으로 돌려야 한다. **시도해 본 것(2026-07-14)**: detached thread를 걷어내고
handler에서 `co_await relay_request` → 예외 → framework가 error reply를 쓰게 바꾸면 **ATD-B1의 marker
순서가 달라진다**(detached relay가 turn 진행을 위해 필요했다). 제대로 고치려면 relay 완료 시점에
error 프레임을 쓸 수 있는 stream error-reply 표면이 필요한데 framework에 없다 — 열림.

ATD-C3B 재현 조건(추가 확인): C1/C2/C3는 **같은 timer spot**(`timer_spot_rid`)을 공유하고
runner의 `all` 모드에서 C1 → C2 → C3 순으로 한 프로세스 안에서 돈다. `run_e2e.sh ATD-C3`로
C3만 돌리면(=C1/C2 미실행) 통과하므로, 앞선 timer 시나리오가 그 spot의 직렬 큐(
`serial_execution_queue_t`) 상태를 남기고 그 뒤의 await 재개가 큐에서 픽업되지 않는다.
다음 단계는 C1/C2 실행 후 큐의 `_active`/`_draining`/`_active_turns` 스냅샷을 찍어
released turn 회계가 어긋나는 지점을 특정하는 것이다.

두 건은 sample 계약 편차가 아니므로 이 문서의 SMP-* 항목과 분리해 둔다. gate 판정에는
영향이 있으므로(통합 러너 실패) plan §13.3에 상태를 남긴다.
