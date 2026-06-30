# ShoppingMall TypeScript Sample

ShoppingMall 샘플은 commerce API가 주문을 접수하고 order workflow 역할이 주문 상태를 이어서
처리하는 흐름을 보여준다. 클라이언트는 ZLink HTTP client로 두 API 인스턴스를 호출하고,
서버는 registry, commerce API, order workflow 역할을 분리해서 실행한다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 주문 생성, 멱등성, projection rebuild, scale-out 조회를 검증한다.
- `Server/CommerceApi`는 HTTP 요청 검증, 멱등성 예약, workflow routing을 담당한다.
- `Server/OrderWorkflow`는 예약된 주문 command를 처리하고 주문 상태를 갱신한다.
- `Server/Registry`는 샘플 실행 중 사용할 discovery registry를 띄운다.
- `Shared`는 주문 상태 계약과 패킷 이름을 정의한다.

## Success Condition

클라이언트가 `PASS ShoppingMall.Ts`를 출력하면 주문 workflow 상태 전이가 검증된 것이다.

## 회귀 테스트

`framework/languages/node/test/contract/sample-regression.test.js`가 샘플 파일, runner 연결, public API 경계를 확인한다.
