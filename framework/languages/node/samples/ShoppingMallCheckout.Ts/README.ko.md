# ShoppingMallCheckout TypeScript Sample

ShoppingMallCheckout 샘플은 checkout 흐름에서 결제 승인, 재고 예약, 주문 확정을 순서대로 처리하는 과정을 보여준다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 checkout 단계가 순서대로 진행되는지 검증한다.
- `Server`는 commerce API와 workflow 책임을 샘플 안에서 분리해 보여준다.
- `Shared`는 checkout 상태와 패킷 이름을 정의한다.

## Success Condition

클라이언트가 `PASS ShoppingMallCheckout.Ts`를 출력하면 checkout 상태 전이가 검증된 것이다.

## 회귀 테스트

`framework/languages/node/test/contract/sample-regression.test.js`가 샘플 파일, runner 연결, public API 경계를 확인한다.
