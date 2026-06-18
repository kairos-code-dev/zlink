# ShoppingMall C++ Sample

ShoppingMall 샘플은 주문 생성과 order workflow 상태 전이를 C++ 샘플 구조로 보여준다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 주문 생성, 결제, 포장, 배송 상태를 검증한다.
- `Server`는 commerce API와 order workflow 책임을 분리한다.
- `Shared`는 주문 상태 계약을 정의한다.

## Success Condition

클라이언트가 `shoppingmall=completed`를 출력하면 주문 workflow가 검증된 것이다.

## 회귀 테스트

`test_cpp_framework_sample_parity`와 CMake sample target이 샘플 구조와 빌드를 확인한다.
