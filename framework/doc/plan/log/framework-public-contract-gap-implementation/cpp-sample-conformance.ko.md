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
| SMP-BINGO-001 | payload codec = Protobuf | `.proto` 없음. `protobuf_codec_extension_t`가 실제로는 JSON을 직렬화(extension이 `from_json`/`parse_json` 사용, media type만 protobuf) | 열림(extension 자체가 JSON 기반 — framework codec extension 트랙과 함께 판정 필요) |
| SMP-BINGO-002 | Play↔Play spot pub/sub, Session→Play router는 location store 자동 연결 | `connect_peer_pub`/`connect_router`로 수동 endpoint 연결 | 열림 |
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
| SMP-DD-002 | 역할 = Dispatch/CourierSession/CourierSpotNode×2/Tracking/CustomerGateway | Dispatch가 Api/Center 2프로세스로 분리, 스펙에 없는 **CourierGateway**(session registry) 추가 | 열림 |
| SMP-DD-003 | DispatchWorker가 배차 큐·선택 정책·timeout 재시도를 소유 | courier-a/b 하드코딩, offer timeout이 courier actor node의 blocking `condition_variable::wait_for` | 열림 |
| SMP-DD-004 | `FindCourierActorReq/Res`, `FindCustomerActorReq/Res` | 없음(항상 ensure 경로) | 열림 |
| SMP-DD-005 | Tracking→CustomerEntrySpot `DeliveryStatusUpdatedMsg`→actor→`DeliveryStatusNotify` | fanout 채널로 우회, entry-spot 구간 없음. `EnsureCustomerActorReq/Res` 핸들러가 Tracking에 있고 아무도 호출 안 함 | 열림 |
| SMP-DD-006 | `DeliveryStatusChangedReq` 필드 = `{DeliveryId, Status, CourierId, OccurredAt}` | wire DTO에서 `CustomerId` 제거(Tracking은 쓰지 않았음) | 닫힘 |
| SMP-DD-007 | self-check의 node 배치 검증(courier-a=node-1, courier-b=node-2) | `bind_courier`가 bind 응답의 actor node rid를 기대 노드와 대조 | 닫힘 |

## 6. ShoppingMall

| ID | 항목 | 편차 | 상태 |
|----|------|------|------|
| SMP-SM-001 | event store 레코드(`StoredOrderEvent`)와 typed 도메인 event | event가 `{type, createdAtUnixMs}` 문자열 수준, 도메인 event 타입 없음 | 열림 |
| SMP-SM-002 | aggregate는 event stream fold로 rehydrate | projection을 진실의 원천으로 사용 | 열림 |
| SMP-SM-003 | projection은 stream만으로 재생성 가능 | rebuild가 살아남은 read model과 하드코딩 값에 의존 | 열림 |
| SMP-SM-004 | inventory/payment 모듈 port(`ReserveInventoryCommand` 등) | 모듈 없음 — 금액 범위·문자열 비교로 성공/실패 흉내 | 열림 |
| SMP-SM-005 | CommerceApi는 event store/projection을 직접 쓰지 않는다 | API가 event stream 절단·read model 삭제를 직접 수행 | 열림 |
| SMP-SM-006 | `StartOrderRes`는 신규 주문에 `Created` 즉시 반환(Continue는 owner가 비동기 진행) | API가 Continue를 inline await 후 `Confirmed` 반환, 클라이언트도 `Confirmed` 기대 | 열림 |
| SMP-SM-007 | 서버 간 연결은 location store 자동 연결 | `enable_client(endpoint)`/`connect_router(endpoint)` 수동 배선 | 열림 |
| SMP-SM-008 | handler catch→log→rethrow 금지 | 매크로로 전 handler에 적용 | 닫힘 |

## 7. GameQuest

| ID | 항목 | 편차 | 상태 |
|----|------|------|------|
| SMP-GQ-001 | owner spot은 event-sourced aggregate(append/replay/snapshot) | 노드 전역 in-memory `std::map` 현재값, 이벤트 스토어 없음 | 열림 |
| SMP-GQ-002 | `GameplayMsg`(one-way send) | `ApplyGameplayEventReq/Res`(request/reply)로 대체, 봉투 필드도 상이 | 열림 |
| SMP-GQ-003 | notify는 location store session binding으로 현재 노드에 전달 | owner spot이 raw HTTP POST `/internal/notify`로 특정 API 노드에 전달(다른 노드 바인딩이면 push 유실) | 열림 |
| SMP-GQ-004 | `QuestProgress`에 `Version`, `LastSourceEventId` | `Version` 없음, `lastEventId`로 개명 | 열림 |
| SMP-GQ-005 | status = `Active`/`Completed`/`RewardGranted` | `Completed` 없음 | 열림 |
| SMP-GQ-006 | 서버 간 연결은 location store 자동 연결 | 일부 채널이 `enable_client(endpoint)`/`connect_router(endpoint)` 수동 | 열림 |
| SMP-GQ-007 | self-check: projection 재생성·rehydrate·reconnect·reset 보정·reward 멱등 | 미커버 | 열림 |

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

| CPP-ATD-TIMER-RESUME-001 | E2E AutomaticTurnDispatch **ATD-C3B**(가끔 **ATD-D2**)가 간헐 실패한다 | **깨끗한 worktree(HEAD)에서 재검증한 결과**(작업 트리에는 다른 담당 세션의 커넥터 미커밋 변경이 있어 이전 관측 일부가 오염돼 있었다): (1) client가 받는 오류는 **`End of file`** — 즉 세션 서버가 client stream을 닫는다. (2) ATD 세션 픽스처는 control 요청을 route로 전달할 때 timeout을 **3000ms**로 두는데, play측 evidence-wait 핸들러는 marker를 **최대 3000ms**까지 기다린다. 두 값이 같아, 정상이지만 조금 느린 응답에도 전달이 timeout→핸들러 예외→**stream 종료**로 이어진다(중첩 timeout 결함, 이번에 15000ms로 수정). (3) 그래도 간헐 실패가 남는다 — outbound 채널 요청은 보통 ~400ms인데 드물게 ~1.5s가 걸리며(transport 회전은 없음), timer 재개가 그만큼 늦어지면 marker가 늦게 뜬다. 남은 조사는 이 지연의 출처(요청 재시도/readiness 폴링)와, 픽스처가 실패를 오류 응답 대신 stream close로 표현하는 문제다 | framework/fixture(열림) |
경합 시 건너뛰게 해도 증상은 그대로였다(요청 timeout 여전히 미발동) — 대기 루프가 그
mutex에 막힌 것은 아니다. 따라서 detached thread는 `request()` 진입 이전/직후(예: 클라이언트
`_mutex`, `sync_connections()`, endpoint provider) 어딘가에서 막혀 있을 가능성이 높다.
**채널 trace(`ZLINK_CPP_CHANNEL_TRACE=1`)로 얻은 결정적 단서**: C3B의 delay 요청이 제출된
(`client request candidates=1`) 직후부터 play-a의 **route-channel 디스패치가 멈춘다**. 클라이언트가
보낸 evidence-wait 요청(`route-channel recv ... requestSeq=23` → `dispatch-submit`)이 그대로 걸려 있고,
기다리던 delay 응답(`client request reply parts=2`)과 그 evidence-wait의 `dispatch-complete`가
**둘 다 프로세스 종료 시퀀스(`host-stop-*`, `loop-join-workers-begin`) 중에야** 처리된다. 즉 개별
요청의 문제가 아니라, timer 핸들러가 await하는 동안 **route-channel 디스패치의 공유 자원(worker/executor)이
막힌다**. 다음 담당자는 route-channel worker 풀과 handler invocation executor의 점유를 이 구간에서
확인할 것(같은 시점에 actor 요청은 stream 경로로 들어와 정상 처리되므로 route 경로 전용 자원이 유력) | framework(열림) |

ATD-C3B 재현 조건(추가 확인): C1/C2/C3는 **같은 timer spot**(`timer_spot_rid`)을 공유하고
runner의 `all` 모드에서 C1 → C2 → C3 순으로 한 프로세스 안에서 돈다. `run_e2e.sh ATD-C3`로
C3만 돌리면(=C1/C2 미실행) 통과하므로, 앞선 timer 시나리오가 그 spot의 직렬 큐(
`serial_execution_queue_t`) 상태를 남기고 그 뒤의 await 재개가 큐에서 픽업되지 않는다.
다음 단계는 C1/C2 실행 후 큐의 `_active`/`_draining`/`_active_turns` 스냅샷을 찍어
released turn 회계가 어긋나는 지점을 특정하는 것이다.

두 건은 sample 계약 편차가 아니므로 이 문서의 SMP-* 항목과 분리해 둔다. gate 판정에는
영향이 있으므로(통합 러너 실패) plan §13.3에 상태를 남긴다.
