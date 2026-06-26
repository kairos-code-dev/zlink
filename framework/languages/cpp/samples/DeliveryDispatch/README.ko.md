# DeliveryDispatch C++ Sample

DeliveryDispatch 샘플은 배달 생성, courier 배정, 픽업, 완료까지의 상태 전이를 C++ 샘플 구조로 보여준다.
현재 C++ 구현은 client self-check와 상태 전이를 검증하는 compact 샘플이며, 공통 시나리오의
`DeliveryTrackingSpot`/customer actor join 구조까지 구현한 full 샘플은 아니다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 배달 dispatch 흐름을 시나리오처럼 검증한다.
- `Server`는 dispatch API, dispatch center, courier, tracking, session 책임을 샘플 역할로 나눈다.
- `Shared`는 배달 상태 계약을 정의한다.

## Success Condition

클라이언트가 `deliverydispatch=completed`를 출력하면 배달 상태 전이가 검증된 것이다.

## 회귀 테스트

`test_cpp_framework_sample_parity`와 CMake sample target이 샘플 구조와 빌드를 확인한다.
