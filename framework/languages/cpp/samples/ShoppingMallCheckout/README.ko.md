# ShoppingMallCheckout C++ Sample

ShoppingMallCheckout 샘플은 checkout 단계가 결제 승인, 재고 예약, 주문 확정으로 진행되는 흐름을 C++ 샘플 구조로 보여준다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 checkout 상태가 순서대로 전이되는지 검증한다.
- `Server`는 commerce API와 workflow 책임을 분리한다.
- `Shared`는 checkout 상태 계약을 정의한다.

## Success Condition

클라이언트가 `shoppingmallcheckout=completed`를 출력하면 checkout workflow가 검증된 것이다.

## 회귀 테스트

`test_cpp_framework_sample_parity`와 CMake sample target이 샘플 구조와 빌드를 확인한다.
