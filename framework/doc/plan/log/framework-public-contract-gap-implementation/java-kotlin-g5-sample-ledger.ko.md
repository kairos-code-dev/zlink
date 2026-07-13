# Java/Kotlin G5 공통 sample spec ledger

## 기준과 판정 방법

이 ledger는 Java와 Kotlin 샘플이
`framework/doc/framework/common/sample/`의 공통 sample spec을 실제 코드와 runner에서
구현하는지 추적한다. 언어별 README나 과거 `.NET` 파일 대응표는 계약 기준으로 사용하지 않는다.

각 행은 다음 여섯 영역을 모두 확인한 뒤에만 `PASS`로 바꾼다.

1. 공통 spec의 서버 역할과 연결 구조
2. request, response, send, notify 메시지 이름과 필드
3. owner가 소유하는 상태와 상태 전이 순서
4. 기본 또는 확장 payload codec
5. client self-check 순서와 각 응답·push assertion
6. 개별 runner의 topology, server evidence, 완료 marker

공통 spec과 구현이 다르면 현재 public contract로 표현 가능한지 먼저 확인한다. 가능하면 sample의
공유 계약, handler, client와 runner를 함께 수정한다. 새 public API가 필요한 경우에는 sample-local
helper로 우회하지 않고 public contract gap으로 분리한다.

## 문서 분모

상위 `sample/README.ko.md`가 정본으로 지정한 구현 대상은 Bingo, TicTacToe, SupportChat,
DeliveryDispatch, ShoppingMall, GameQuest 6종이다. `event/README.ko.md`는 두 event sample의
목적을 묶어 설명하고, `languages/README.ko.md`는 언어별 추가 계약이 현재 없음을 명시한다.
runner template의 Redis 격리·실행 순서는 Java/Kotlin 공용 helper와 12개 개별 runner에 반영했다.
TicTacToe 구현 prompt는 시나리오 계약을 추가하지 않는 작성 입력이다.

ZoneWorld 문서는 구현 자체를 별도 sample 작업으로 진행한다고 명시한 설계 초안이므로 현재
Java/Kotlin sample parity의 완료 분모에 넣지 않는다. 정식 구현 대상으로 바뀌면 언어별 서버와
공용 TypeScript browser client를 별도 ledger에서 검증한다.

## 진행 현황

| 언어 | sample | 공통 spec | 역할·연결 | 메시지·필드 | 상태 전이 | codec | self-check | runner | 결과 |
|------|--------|-----------|-----------|-------------|-----------|-------|------------|--------|------|
| Java | Bingo | `sample/bingo/README.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Java | TicTacToe | `sample/tictactoe/README.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Java | SupportChat | `sample/supportchat/README.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Java | DeliveryDispatch | `sample/deliverydispatch/README.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Java | ShoppingMall | `sample/event/shoppingmall.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Java | GameQuest | `sample/event/gamequest.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Kotlin | Bingo | `sample/bingo/README.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Kotlin | TicTacToe | `sample/tictactoe/README.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Kotlin | SupportChat | `sample/supportchat/README.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Kotlin | DeliveryDispatch | `sample/deliverydispatch/README.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Kotlin | ShoppingMall | `sample/event/shoppingmall.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Kotlin | GameQuest | `sample/event/gamequest.ko.md` | PASS | PASS | PASS | PASS | PASS | PASS | PASS |

## 현재 확인된 불일치

### G5-SAMPLE-000 Java/Kotlin Bingo 공통 spec 대조

- 상태: 완료
- 두 언어 모두 Session/API/Play 각 2개 노드, Redis match queue, 자동 연결 topology와
  Protobuf schema 기반 payload를 사용한다. 공통 spec의 request, response, notify 이름은 공유
  schema와 client/server 호출부에서 모두 확인했다.
- client는 인증, matching, game start, card 제출, draw와 종료 push를 검증한다. runner는 client
  완료 marker뿐 아니라 message flow, stream notify, stream connection metric과 Spot queue metric을
  server evidence로 확인한다.
- Java와 Kotlin의 `./run_sample.sh`가 각각
  `bingo full client/server self-check completed`를 출력하고 종료 코드 0으로 통과했다.

### G5-SAMPLE-006 Java/Kotlin ShoppingMall 공통 spec 대조

- 상태: 완료
- 두 언어 모두 이중 Commerce API와 OrderWorkflow owner topology, Redis location store, 주문 event
  store와 read model store를 분리한다. 공통 spec의 client와 내부 workflow 메시지 이름 및 필드는
  공유 계약과 호출부에서 확인했다.
- client self-check는 성공, 멱등 중복, 동시 시작, pending 복구, 재고 실패, 결제 실패와 보상,
  중단 뒤 재개, projection 재생성을 검증하고 runner는 server evidence를 별도로 확인한다.
- Java runner는 `shoppingmall full client/server self-check completed`, Kotlin runner는
  `shoppingmall-server-evidence=completed`를 출력하고 모두 종료 코드 0으로 통과했다.

### G5-SAMPLE-007 Java/Kotlin GameQuest 공통 spec 대조

- 상태: 완료
- 기존 `GameplayEventEnvelope`를 공통 spec의 `GameplayMsg`로 바꾸고 wire 필드를
  `EventId`, `PlayerId`, `Type`, `Payload`, `OccurredAtUnixMs`로 정렬했다. idempotency key와
  업무 세부 값은 공유 payload codec이 `Payload` 안에서 처리하므로 owner handler에 raw byte 해석을
  노출하지 않는다.
- 두 언어 모두 이중 Game API와 player별 QuestMission owner route, append-only quest event,
  projection, reconnect와 reconcile 흐름을 유지한다. client는 중복 event, offline 진행, projection
  재생성과 유실 뒤 보정을 검증한다.
- Java와 Kotlin runner가 각각 `gamequest-server-evidence=completed`, `gamequest=completed`와 전체
  self-check marker를 출력하고 종료 코드 0으로 통과했다.

### G5-SAMPLE-001 Java/Kotlin DeliveryDispatch 공통 spec 대조

- 상태: 완료
- 두 언어 모두 Dispatch, CourierSession, Courier Spot node 2개, Tracking, CustomerGateway와 client
  역할을 분리하고 Redis location store를 통해 자동 연결한다. courier와 customer session은 actor
  위치를 먼저 조회하고, 결과가 없을 때만 actor를 생성한 뒤 현재 session route를 연결한다.
- 공통 spec의 request, response, send message 이름과 필드로 공유 계약을 정렬했다.
  `BindCourierSessionRes`를 포함한 actor 참조 필드는 sample 전용 wire DTO가 아니라 framework 공개
  타입인 `ActorRefSnapshot`을 사용한다. 기본 JSON codec이 이 공개 타입의 binary-safe `RoutingId`를
  보존하도록 framework와 stream connector 양쪽의 round-trip 계약 테스트도 추가했다.
- client는 성공 흐름의 `Assigned`, `Accepted`, `PickedUp`, `Delivered` 순서와 timeout 재배정 흐름의
  `Assigned`, `Reassigned`, `Accepted`, `Delivered` 순서를 검증한다. runner는 client assertion과
  Tracking server evidence를 함께 확인한다.
- Java와 Kotlin의 `./run_sample.sh`가 각각 `deliverydispatch-reassignment=completed`,
  `deliverydispatch-server-evidence=completed`, `deliverydispatch=completed`, 전체 self-check marker를
  출력하고 종료 코드 0으로 통과했다.
- 공통 runner는 Spring lifecycle의 25초 drain 기한을 기다리며 서버를 동시에 종료한다. 종료 중
  SIGABRT, SIGSEGV 또는 기한 초과 강제 종료가 발생하면 성공으로 처리하지 않는다.

### G5-SAMPLE-003 Java/Kotlin TicTacToe 종료 흐름

- 상태: 완료
- 공통 spec은 최종 game state를 확인한 뒤 client가 `LeaveGameReq`를 보내고, room Spot의
  `onLeaveActor`와 Entry Spot의 actor destroy를 server evidence로 확인하도록 요구한다.
- 두 언어의 공유 계약, handler와 client를 `LeaveGameReq`로 정렬했다. Java와 Kotlin의
  `Shared`, `Server`, `Client` 컴파일은 모두 통과했다.
- 두 runner에 `player-x`와 `player-o` 각각의 actor destroy 완료 marker 확인을 추가했다.
- native actor의 leave는 core의 Spot 이탈 뒤 framework가 같은 현재 노드의 Entry Spot 재가입을
  시작하고, Entry admission과 joined callback이 끝난 뒤 완료되도록 runtime을 수정했다. routed actor도
  같은 현재 노드의 Entry Spot으로 이동한다.
- native와 routed actor의 Entry 복귀 순서를 `SpotActorTransferContractTest`에 고정했고 전체 테스트가
  통과했다. Java `./run_sample.sh`는 `PASS TicTacToe.Java`, Kotlin runner는
  `PASS TicTacToe.Kotlin`을 출력하고 종료 코드 0으로 통과했다.
- Kotlin의 과거 고정 `delay(500)`은 제거했으며 두 runner 모두 지연 우회 없이 server destroy
  evidence를 기다린다.

### G5-SAMPLE-005 회귀 계약 테스트

- 상태: 통과
- `SampleReleaseGateContractTest.commonSampleMessageNamesRemainVisibleInJavaAndKotlinSources`를 추가했다.
- Java/Kotlin TicTacToe의 `LeaveGameReq`, Java DeliveryDispatch의 수정된 공통 메시지 이름,
  Kotlin DeliveryDispatch의 `AssignDeliveryMsg`를 고정하고 과거 이름 재도입을 거부한다.
- `./gradlew --no-daemon --no-parallel --max-workers=2 :zlink-framework-testkit:contractTest --tests '*SampleReleaseGateContractTest*'`
  실행이 통과했다.

### G5-SAMPLE-004 Java SupportChat 상담원 conversation 계약

- 상태: 완료
- Java의 단일 `ConversationStore`와 Entry Spot 우회 핸들러를 제거했다. Java와 Kotlin 모두
  conversation마다 `ConversationSpot`과 `Conversation` aggregate가 참여자, 메시지 순서, typing,
  idle과 close 상태 전이를 소유한다. application 계층의 allocator와 배정 서비스는 framework 세부
  구현을 port 뒤에 두고, notification publisher가 domain event를 공개 notify 계약으로 바꾼다.
- 두 언어의 공유 계약은 `EnsureAgentConversationReq`/`EnsureAgentConversationRes`를 포함한 공통
  메시지 이름과 필드를 사용한다. actor 참조는 공개 `ActorRefSnapshot`을 사용하며 conversation 범위
  request의 `ConversationId`는 payload가 아니라 stream metadata로 전달한다. 사용되지 않던 과거
  `*SupportReq`, `ActorRefWire`, HTTP assertion DTO와 저장소는 제거했다.
- client self-check는 상담원 한 명이 고객 두 명의 conversation을 동시에 처리하는 흐름, 메시지 순서,
  typing, 고객과 상담원 재접속, 명시적 close, idle 뒤 close, 닫힌 conversation의 오류와 typing 무시,
  상담원 부재 시 `WaitingForAgent`를 검증한다.
- Java와 Kotlin의 `./run_sample.sh`가 각각 `supportchat-closed-typing-ignore=verified`,
  `supportchat-server-evidence=completed`, `supportchat=completed`를 출력하고 종료 코드 0으로 통과했다.
  runner는 server 로그에서 `WaitingForAgent`, `Active`, `WaitingForClose`, `Closed` 전이를 확인한다.
- `SampleReleaseGateContractTest`는 두 언어의 공통 메시지 이름, `ActorRefSnapshot`, metadata 기반
  conversation request payload를 고정하고 과거 DTO 재도입을 거부한다.

## 완료 조건

- [x] 12개 언어/sample 행의 여섯 영역이 모두 `PASS`다.
- [x] 각 sample 개별 runner가 실제 client self-check와 server evidence를 확인하고 종료 코드 0을 반환한다.
- [x] `ZLINK_SAMPLE_LANGUAGES=java ./samples/run_samples.sh`와
  `ZLINK_SAMPLE_LANGUAGES=kotlin ./samples/run_samples.sh`가 모두 통과한다.
- [x] 공통 spec의 메시지 이름을 자동 대조하는 contract test가 추가되어 이후 과거 이름으로 되돌아가지 않는다.

2026-07-14에는 각 sample runner가 언어와 sample별 Docker Redis를 직접 준비하도록 바꾼 뒤 두
통합 명령을 다시 실행했다. Java 6종과 Kotlin 6종 모두 실제 client self-check와 server evidence를
확인했으며, 두 실행은 각각 `All Java/Kotlin samples passed`를 출력하고 종료 코드 0으로 끝났다.
