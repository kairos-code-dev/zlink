<!-- framework-adapter-nav:start -->
[이전: Discovery와 scale-out](03-discovery-scaleout.ko.md) | [E2E 목차](README.ko.md) | [다음: Spot, actor, session](05-spot-actor-session.ko.md)
<!-- framework-adapter-nav:end -->

# Publish, Subscribe, Stream E2E

publish-subscribe와 stream은 request-response보다 실패가 조용히 묻히기 쉽다. 이 문서는
fanout 전달, subscriber filter, stream session lifecycle, reconnect, inbound observer를
검증한다.

## PUB-001 fanout basic delivery

우선순위: `P0`

구성:

- publisher 1개
- subscriber 3개
- topic 1개

절차:

1. subscriber가 topic handler를 등록한다.
2. publisher가 sequence가 있는 event를 publish한다.
3. 각 subscriber evidence를 조회한다.

검증:

- 모든 subscriber가 같은 sequence를 받는다.
- payload field가 손상되지 않는다.
- publish 호출은 response를 기다리지 않는다.

## PUB-002 topic filter

우선순위: `P0`

구성:

- topic `orders.created`
- topic `orders.cancelled`
- subscriber A는 created만 구독
- subscriber B는 cancelled만 구독
- subscriber C는 둘 다 구독

검증:

- A는 created만 받는다.
- B는 cancelled만 받는다.
- C는 둘 다 받는다.
- 등록하지 않은 topic은 dispatch error 없이 무시되거나 정책대로 관측된다.

## PUB-003 late subscriber

우선순위: `P1`

절차:

1. publisher가 event 1-5를 publish한다.
2. subscriber를 뒤늦게 시작한다.
3. publisher가 event 6-10을 publish한다.

검증:

- 기본 fanout은 과거 event를 replay하지 않는다.
- late subscriber는 연결 이후 event만 받는다.
- replay 기능을 지원하는 언어 또는 확장이 있으면 별도 시나리오로 분리한다.

## PUB-004 subscriber slow handler

우선순위: `P1`

절차:

1. subscriber A는 빠르게 처리한다.
2. subscriber B는 일부 event에서 느리게 처리한다.
3. publisher가 연속 publish한다.

검증:

- B의 느린 handler가 A의 수신을 막지 않는다.
- B 내부 queue overflow 정책이 있으면 observer 또는 로그로 확인된다.
- event 순서 보장 범위가 문서와 맞다.

## STR-001 stream session auth와 packet dispatch

우선순위: `P0`

절차:

1. client connector가 stream endpoint에 연결한다.
2. `AuthenticateReq`를 보낸다.
3. server session handler가 actor id 또는 user id를 반환한다.
4. client가 인증 이후 packet을 보낸다.

검증:

- 인증 전 금지 packet은 error 또는 reject로 끝난다.
- 인증 후 허용 packet은 handler로 dispatch된다.
- session id와 client id가 evidence에 남는다.

## STR-002 stream reconnect

우선순위: `P0`

절차:

1. client가 연결하고 인증한다.
2. server 또는 network를 끊는다.
3. client가 reconnect한다.
4. 같은 user id로 다시 인증한다.

검증:

- old session은 close lifecycle을 탄다.
- new session은 독립 session id를 가진다.
- bound actor 정책이 있으면 재연결 뒤 actor handle이 복구된다.

## STR-003 inbound observer

우선순위: `P1`

절차:

1. stream connector inbound observer를 등록한다.
2. server가 notify packet을 보낸다.
3. client callback과 observer가 모두 packet 정보를 받는다.

검증:

- observer는 payload 처리 성공 여부와 관계없이 metadata를 남긴다.
- observer 실패는 원래 packet dispatch를 깨뜨리지 않는다.
- observer queue overflow가 있으면 `ObserverDropped` 같은 오류로 보고된다.

## STR-004 stream backpressure

우선순위: `P1`

절차:

1. server가 빠르게 notify를 보낸다.
2. client handler를 일부러 느리게 만든다.
3. queue limit 또는 timeout 정책을 관측한다.

검증:

- memory가 무한히 증가하지 않는다.
- drop, disconnect, backpressure 중 선택된 정책이 문서와 맞다.
- 오류는 client-visible event 또는 log marker로 남는다.

## STR-005 stream request와 channel request 혼합

우선순위: `P1`

구성:

- stream session
- API channel
- Play channel

절차:

1. stream packet handler가 channel request를 호출한다.
2. channel response를 받아 stream client에 reply한다.
3. 동시에 server push를 보낸다.

검증:

- stream request correlation과 channel request correlation이 섞이지 않는다.
- channel timeout은 stream response error로 변환된다.
- push는 request reply와 별개로 도착한다.
