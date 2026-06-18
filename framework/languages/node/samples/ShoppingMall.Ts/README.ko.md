# ShoppingMall TypeScript Sample

ShoppingMall 샘플은 commerce API가 주문을 시작하고 order workflow 역할이 주문 상태를 이어서 처리하는 흐름을 보여준다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 주문 생성부터 배송 상태까지의 workflow를 검증한다.
- `Server`는 commerce API와 order workflow 책임을 분리해 보여준다.
- `Shared`는 주문 상태 계약과 패킷 이름을 정의한다.

## Success Condition

클라이언트가 `PASS ShoppingMall.Ts`를 출력하면 주문 workflow 상태 전이가 검증된 것이다.

## 회귀 테스트

`framework/languages/node/test/contract/sample-regression.test.js`가 샘플 파일, runner 연결, public API 경계를 확인한다.
