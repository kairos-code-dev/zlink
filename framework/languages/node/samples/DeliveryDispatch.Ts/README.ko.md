# DeliveryDispatch TypeScript Sample

DeliveryDispatch 샘플은 배달 요청이 접수되고, dispatch center가 courier에게 작업을 배정하며,
상태 변경이 고객 세션으로 전달되는 흐름을 보여준다. TypeScript 구현은 dispatch channel,
courier gateway, courier stream bind, courier actor-node route mesh,
tracking fanout, customer stream push를 별도 role로 실행해 client self-check로 검증한다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 배달 생성, 배정, 픽업, 완료까지의 흐름을 시나리오처럼 검증한다.
- `Server/DispatchApi`는 HTTP 요청을 dispatch channel request로 바꾼다.
- `Server/DispatchCenter`는 dispatch queue, timeout, 재배정, tracking publish를 맡는다.
- `Server/Courier`는 CourierGateway와 courier actor-node route handler를 둔다.
- `Server/CourierSession`은 배송원 stream bind를 받고 CourierGateway에 courier id와 session route를 등록한다.
- `Server/Tracking`은 status evidence, DeliveryTrackingSpot, status fanout publish를 맡는다.
- `Server/Session`은 고객 stream subscription과 status notify push를 맡는다.
- `Shared`는 요청, 응답, 상태 알림 이름을 한곳에 둔다.

## Success Condition

클라이언트가 `PASS DeliveryDispatch.Ts`를 출력하면 배송원 A/B stream bind, 고객 subscription,
성공 배차, timeout 재배차, server evidence 검증이 모두 통과한 것이다.

## 회귀 테스트

`framework/languages/node/test/contract/sample-regression.test.js`가 샘플 파일, runner 연결, public API 경계를 확인한다.
