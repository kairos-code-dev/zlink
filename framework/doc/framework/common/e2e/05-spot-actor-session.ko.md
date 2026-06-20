<!-- framework-adapter-nav:start -->
[이전: Publish, subscribe, stream](04-pubsub-stream.ko.md) | [E2E 목차](README.ko.md) | [다음: Handler 등록과 codec](06-handler-registration-codec.ko.md)
<!-- framework-adapter-nav:end -->

# Spot, Actor, Session E2E

Spot과 actor는 channel보다 상태와 lifecycle이 복잡하다. 이 문서는 Entry Spot, user Spot,
actor join, bound session push, timer, route resolver를 실제 multi-process 구조로 검증한다.

## SPOT-001 Entry Spot 생성과 request

우선순위: `P0`

구성:

- Spot node 1개
- Entry Spot 1개
- client 또는 channel handler 1개

절차:

1. client가 Entry Spot에 `CreateRoomReq`를 보낸다.
2. Entry Spot은 room id와 target Spot rid를 반환한다.
3. server evidence에 Spot 생성 기록을 남긴다.

검증:

- Spot rid는 요청 payload에서 결정한 domain id와 매핑된다.
- 같은 room id로 다시 요청하면 idempotent 결과 또는 명확한 duplicate error가 나온다.
- 생성된 Spot이 이후 request를 받을 수 있다.

## SPOT-002 user Spot request와 state mutation

우선순위: `P0`

절차:

1. user Spot을 만든다.
2. `UpdateStateReq`를 여러 번 보낸다.
3. `GetStateReq`로 최종 상태를 조회한다.

검증:

- state update 순서가 보존된다.
- concurrent request 정책이 직렬 처리인지 병렬 처리인지 문서와 맞다.
- 실패한 update는 state를 부분 변경하지 않는다.

## SPOT-003 actor join

우선순위: `P0`

절차:

1. actor factory로 actor를 만든다.
2. Entry Spot에서 user Spot으로 actor를 join시킨다.
3. actor context로 Spot request를 보낸다.

검증:

- actor id와 actor type이 evidence에 남는다.
- actor는 join된 Spot에서만 해당 handler를 실행한다.
- 중복 join은 idempotent 또는 명확한 error로 끝난다.

## SPOT-004 bound session push

우선순위: `P0`

구성:

- stream session
- actor
- user Spot

절차:

1. stream client가 인증한다.
2. session은 actor를 만들거나 찾고 bind한다.
3. user Spot이 actor에게 notify를 보낸다.
4. actor push가 stream client로 도착한다.

검증:

- client는 notify payload를 받는다.
- actor push는 현재 bound session으로만 간다.
- session reconnect 뒤에는 새 session으로 push가 간다.

## SPOT-005 Spot timer

우선순위: `P1`

절차:

1. Spot에서 timer를 등록한다.
2. timer tick마다 state를 갱신하고 event를 publish한다.
3. 일정 횟수 뒤 timer를 중지한다.

검증:

- tick 횟수와 state가 일치한다.
- timer 중지 뒤 추가 tick이 없다.
- Spot close 시 timer가 정리된다.

## SPOT-006 route resolver

우선순위: `P0`

구성:

- Spot node A, B
- room id에 따라 owner node를 고르는 resolver

절차:

1. room id `room-a`는 node A로, `room-b`는 node B로 보내도록 resolver를 구성한다.
2. client가 두 room에 request를 보낸다.
3. node별 evidence를 확인한다.

검증:

- 각 request는 resolver가 선택한 node로 간다.
- owner 정보가 없는 room은 `SpotRouteNotFound` 또는 대응 오류로 끝난다.
- resolver cache가 있다면 topology 변경 뒤 갱신된다.

## SPOT-007 remote actor request

우선순위: `P1`

절차:

1. actor는 node A에 있다.
2. request는 node B에 들어온다.
3. framework가 actor owner 또는 Spot route를 찾아 node A로 전달한다.

검증:

- actor handler는 node A에서 실행된다.
- node B는 relay evidence만 남기고 handler를 실행하지 않는다.
- actor가 없으면 명확한 actor not found error가 나온다.

## SPOT-008 Spot publish와 channel attach

우선순위: `P1`

절차:

1. Spot이 domain event를 publish한다.
2. attached channel subscriber가 event를 받는다.
3. Spot handler가 attached channel client로 request를 보낸다.

검증:

- publish topic과 channel request가 서로 다른 경로로 처리된다.
- Spot lifecycle 중 attach가 없으면 startup validation 또는 runtime error가 명확하다.
- event payload와 request payload codec이 모두 올바르게 적용된다.
