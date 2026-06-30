# ShoppingMall C++ Sample

ShoppingMall 샘플은 주문 생성과 order workflow 상태 전이를 C++ 샘플 구조로 보여준다.
클라이언트는 HTTP API를 호출하고, `CommerceApi`는 route mesh를 통해 주문 workflow 소유
노드로 업무 명령을 보낸다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 주문 생성, 중복 요청, 실패 경로, projection rebuild를 검증한다.
- `Registry`는 discovery registry를 실행한다.
- `CommerceApi`는 HTTP 요청을 받고 idempotency 키로 주문 id를 정한다.
- `OrderWorkflow`는 `shoppingmall.order.workflow.route` route channel에서 주문 상태 전이를
  처리한다.
- `Shared`는 주문 상태 계약을 정의한다.

## Success Condition

클라이언트가 `shoppingmall=completed`를 출력하면 주문 workflow가 검증된 것이다.

## 회귀 테스트

`test_cpp_framework_sample_parity`와 CMake sample target이 샘플 구조와 빌드를 확인한다.
