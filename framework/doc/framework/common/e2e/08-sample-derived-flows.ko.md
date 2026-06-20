<!-- framework-adapter-nav:start -->
[이전: Dispatch 오류와 관측성](07-dispatch-error-observability.ko.md) | [E2E 목차](README.ko.md) | [다음: 복구와 lifecycle](09-resilience-lifecycle.ko.md)
<!-- framework-adapter-nav:end -->

# 샘플 기반 업무 흐름 E2E

이 문서는 공통 샘플에서 가져온 실제 업무형 흐름을 검증 전용 scenario로 재구성한다.
샘플은 읽기 쉬운 정상 흐름을 유지하고, 이 문서의 E2E는 scale-out, 실패, 재연결,
중복 요청 같은 변형을 더 적극적으로 만든다.

## FLOW-001 Bingo gateway scale-out

우선순위: `P0`

출처: [Bingo 샘플](../sample/bingo/README.ko.md)

구성:

- Session server 2개
- API server 2개
- Play server 2개
- Registry 1개
- stream client 3개

절차:

1. player 1은 Session server A에 연결하고, player 2와 observer는 Session server B에
   연결한다.
2. player 1과 player 2가 matching을 요청한다.
3. API request가 두 API server로 분산되는지 확인한다.
4. Play server가 room owner를 선택하고 두 actor를 같은 room에 join시킨다.
5. observer는 event 구독만 수행한다.

검증:

- 두 player는 같은 room id를 받는다.
- actor bound push가 각 client에 도착한다.
- observer는 room event를 받지만 player actor destroy 대상이 아니다.
- API와 Play server evidence에서 scale-out 분산이 보인다.

## FLOW-002 Bingo dispatch error variant

우선순위: `P1`

이 시나리오는 Bingo 샘플에 넣지 않고 E2E 전용으로 둔다.

절차:

1. 정상 Bingo 서버 구성을 띄운다.
2. 테스트 client가 API channel로 미등록 request를 보낸다.
3. 테스트 client가 Play Spot route로 미등록 request를 보낸다.
4. 이후 정상 matching 흐름을 실행한다.

검증:

- 미등록 request는 error reply로 끝난다.
- API와 Play 로그에 각각 `dispatch-error`가 남는다.
- 오류 이후 정상 Bingo 흐름이 성공한다.

## FLOW-003 TicTacToe manual endpoint scale-out

우선순위: `P0`

출처: [TicTacToe 샘플](../sample/tictactoe/README.ko.md)

절차:

1. API server 2개와 Play server 2개를 수동 endpoint로 연결한다.
2. room route store는 테스트별 prefix를 사용한다.
3. 여러 game을 만들고 owner Play server를 evidence로 확인한다.
4. host client는 owner Play stream endpoint로 연결하고, guest와 observer client는
   owner가 아닌 Play stream endpoint로 연결한다.

검증:

- room route store가 올바른 Play owner를 가리킨다.
- guest join은 remote Play owner로 relay된다.
- observer는 game state push를 받는다.
- API request는 두 Play endpoint에 분산된다.

## FLOW-004 SupportChat waiting and assignment

우선순위: `P1`

출처: [SupportChat 샘플](../sample/supportchat/README.ko.md)

절차:

1. customer가 conversation을 연다.
2. agent가 아직 없으면 상태는 `WaitingForAgent`다.
3. agent가 available 상태로 들어온다.
4. assignment event와 chat message를 주고받는다.

검증:

- agent 부재는 오류 response가 아니다.
- agent join 후 customer와 agent 모두 assignment push를 받는다.
- closed conversation에 message를 보내면 error response가 나온다.

## FLOW-005 DeliveryDispatch success and reassignment

우선순위: `P0`

출처: [DeliveryDispatch 샘플](../sample/deliverydispatch/README.ko.md)

절차:

1. 배송 요청 A는 courier A가 수락한다.
2. 배송 요청 B는 courier A가 timeout되고 courier B로 재배정된다.
3. Tracking server가 status event를 publish한다.
4. Session server가 customer stream으로 status push를 보낸다.

검증:

- 성공 배송은 `Assigned -> Accepted -> PickedUp -> Delivered` 순서다.
- 재배정 배송은 `Assigned -> Reassigned -> Accepted -> PickedUp -> Delivered` 순서다.
- customer client는 두 배송의 최종 push를 받는다.
- DispatchCenter evidence에는 timeout과 reassignment가 남는다.

## FLOW-006 ShoppingMall idempotent workflow

우선순위: `P1`

출처: [ShoppingMall 샘플](../sample/event/shoppingmall.ko.md)

절차:

1. 같은 order id로 주문 시작 요청을 두 번 보낸다.
2. workflow Spot은 중복 start를 idempotent하게 처리한다.
3. projection을 조회한다.
4. projection store를 지운 뒤 rebuild request를 보낸다.

검증:

- 중복 요청으로 재고가 두 번 차감되지 않는다.
- projection rebuild 후 order state가 복구된다.
- workflow owner scale-out에서 같은 order id는 같은 owner로 간다.

## FLOW-007 GameQuest event sourcing and reconnect

우선순위: `P1`

출처: [GameQuest 샘플](../sample/event/gamequest.ko.md)

절차:

1. Game API A와 B가 gameplay event를 publish한다.
2. QuestMission A와 B가 fanout event를 받아 player quest Spot을 갱신한다.
3. client가 quest progress stream을 구독한다.
4. client reconnect 뒤 snapshot과 이후 event를 확인한다.

검증:

- 같은 gameplay event id는 중복 처리되지 않는다.
- quest owner routing이 player id 기준으로 안정적이다.
- reconnect 뒤 client는 최신 progress를 다시 확인할 수 있다.

## FLOW-008 cross-sample shared capabilities

우선순위: `P2`

목적:

샘플별 도메인은 다르지만 같은 framework 기능이 같은 의미로 쓰이는지 확인한다.

검증 축:

- Bingo와 TicTacToe의 stream bound push 의미
- SupportChat과 DeliveryDispatch의 customer session push 의미
- ShoppingMall과 GameQuest의 event-sourced Spot idempotency 의미
- DeliveryDispatch와 channel messaging E2E의 timeout handling 의미
