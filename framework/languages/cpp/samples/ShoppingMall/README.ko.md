# ShoppingMall C++ 샘플

`ShoppingMall`은 주문 생성, 멱등성, 실패 보상, projection rebuild, scale-out owner
선택을 C++ framework 표면으로 보여주는 샘플이다. 서버는 구 registry 프로세스를 쓰지
않고 Redis location store에 현재 프로세스 위치를 등록한다.

## 실행

```bash
ZLINK_CPP_BUILD_DIR=build-redis-vcpkg ./framework/languages/cpp/samples/ShoppingMall/run_sample.sh
```

## 구성

- `Server/CommerceApi/`는 HTTP 주문 요청을 받고 workflow 역할로 주문 처리를 위임한다.
- `Server/OrderWorkflow/`는 주문 상태 전이, 실패 처리, projection rebuild를 담당한다.
- `Client/`는 성공 주문, 멱등성, pending 복구, 재고 실패, 결제 실패, projection rebuild,
  지연 읽기 일관성, scale-out 증거를 검증한다.
- `run_sample.sh`는 실행마다 전용 Redis 컨테이너와 실행별 상태 파일을 만들고, 모든 key와
  log를 실행별 디렉터리 아래로 격리한다.

성공하면 client log에 `shoppingmall=completed`가 남고 runner가
`PASS ShoppingMall.Cpp`를 출력한다.
