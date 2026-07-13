# 핵심 개념

ZLink Framework 는 transport 를 직접 다루기보다, 애플리케이션의 의도에 맞는 실행
모델을 고르게 한다.

## 1. channel

channel 은 서비스 간 메시지 경로다. request/reply, one-way send, publish 를 제공한다.
상태를 오래 들고 있지 않은 서비스 호출에 적합하다.

## 2. Spot

Spot 은 상태를 가진 실행 단위다. 같은 Spot 안의 packet, timer, outbound continuation 은
같은 실행 문맥에서 직렬로 처리되어 상태를 보호한다.

## 3. actor

actor 는 사용자, 플레이어, 세션 같은 논리 객체다. actor별 mailbox 가 순서를 보장하고,
join 이후에는 현재 Spot 위치를 다시 확인해 dispatch 한다.

## 4. stream

stream 은 외부 client 와 장기 연결을 유지한다. session 은 `onConnected`,
`onDispatch`, `onDisconnected`, `onError` 로 lifecycle 을 받는다.

## 5. location store와 monitoring

location store는 topology 조회와 자동 연결의 기준이다. monitoring은 socket, location runtime,
Spot 상태 변화를 framework typed event로 전달한다.

## 회귀 테스트

각 개념의 동작은 `test/contract` 의 channel, spot, actor, stream, location,
monitoring 테스트에서 확인한다.
