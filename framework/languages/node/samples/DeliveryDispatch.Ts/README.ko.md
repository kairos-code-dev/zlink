# DeliveryDispatch TypeScript Sample

DeliveryDispatch 샘플은 배달 요청이 접수되고, dispatch center가 courier에게 작업을 배정하며,
상태 변경이 고객 세션으로 전달되는 흐름을 보여준다. 현재 TypeScript 구현은 client self-check와
상태 전이를 검증하는 compact 샘플이며, 공통 시나리오의 `DeliveryTrackingSpot`/customer actor
join 구조까지 구현한 full 샘플은 아니다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 배달 생성, 배정, 픽업, 완료까지의 흐름을 시나리오처럼 검증한다.
- `Server`는 dispatch API, dispatch center, courier, tracking, session 역할을 TypeScript 샘플 구조 안에서 분리해 보여준다.
- `Shared`는 요청, 응답, 상태 알림 이름을 한곳에 둔다.

## Success Condition

클라이언트가 `PASS DeliveryDispatch.Ts`를 출력하면 배달 생성부터 완료까지의 상태 전이가 검증된 것이다.

## 회귀 테스트

`framework/languages/node/test/contract/sample-regression.test.js`가 샘플 파일, runner 연결, public API 경계를 확인한다.
