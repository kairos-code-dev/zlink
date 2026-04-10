# zlink.h Current-State Has-More Surface Cleanup Plan

> 상태: 현재 `zlink.h` 공개 상태 기준 상세 계획.
> 범위: 이미 제거된 과거 pub/sub 이름은 제외하고, 지금도 `zlink.h`에 남아
> 있는 공개 surface만 기준으로 정리한다.
> 수정 범위: `core/`만 포함한다. bindings/외부 래퍼는 이번 범위에서 제외한다.

## 1. 문서 기준

이 문서는 과거 계획 문서를 다시 서술하지 않는다.

- 기준은 오직 현재 공개 헤더 [`zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h)다.
- 이미 `zlink.h`에서 제거된 구 pub/sub 이름은 현재 제거 대상 목록에 넣지 않는다.
- 이 문서에서 말하는 "제거 대상"은 지금도 `zlink.h`에 실제로 남아 있는
  공개 항목만 의미한다.

즉, 이 문서는 `spot`/`xpub` 구 이름 migration 문서가 아니라,
현재 canonical public C API를 더 단순하게 만들기 위한
`zlink.h` 현행 상태 정리 문서다.

## 2. 현재 `zlink.h`에서 설명 가능한 canonical surface

현재 공개 헤더에서 canonical public C API로 설명할 수 있는 축은 아래와 같다.

### 2.1 Canonical Transport

- `zlink_send`
- `zlink_send_rid`
- `zlink_recv`
- `zlink_recv_handler`

메시지 shape:

- 송신: `zlink_msg_t *parts + part_count`
- 수신: `source_rid + parts array`

### 2.2 Canonical Pub/Sub

- `zlink_publish`
- `zlink_subscribe`
- `zlink_unsubscribe`
- `zlink_subscribe_recv`
- `zlink_subscribe_handler`
- `zlink_subscription_event_recv`
- `zlink_subscription_event_handler`

메시지 shape:

- publish: `topic + parts array`
- subscribe recv/callback: `source_rid + topic + parts array`
- subscription event recv/callback: `source_rid + subscribed + topic`

## 3. 현재 `zlink.h`에 실제 남아 있는 legacy/compat 공개 항목

현재 공개 헤더 기준으로 정리 대상이라고 말할 수 있는 항목은 아래뿐이다.

### 3.1 multipart 상태 조회 옵션

- `ZLINK_SOCKOPT_RCVMORE`
- `ZLINK_RCVMORE`

의미:

- receive path가 현재 메시지 뒤에 더 많은 frame이 남아 있는지 조회하는
  legacy socket option이다.
- canonical `zlink_recv`/`zlink_subscribe_recv` 설명에는 필요하지 않다.

### 3.2 multipart flag/property and helpers

- `ZLINK_MORE`
- `zlink_msg_more`
- `zlink_msg_get`
- `zlink_msg_set`

의미:

- multipart continuation을 나타내는 공개 flag/property 축이다.
- `zlink_msg_more()`는 공개 `has more` 모델 자체를 노출한다.
- `zlink_msg_get()` / `zlink_msg_set()`은 현재 `ZLINK_MORE` 같은 공개
  property 축을 함께 실어 나른다.
- 다만 `zlink_msg_get()` / `zlink_msg_set()`은 `ZLINK_SHARED` 같은
  비 has-more 성격의 property 접근에도 쓰이고 있으므로, 최종 제거 전에는
  `core/` 내부 사용을 더 좁은 API로 치환하는 선행 정리가 필요하다.
- canonical public surface는 이미 `parts array`로 shape를 설명하므로,
  공개 설명 관점에서는 중복 축이다.

### 3.3 구 callback 이름 alias typedef

- `zlink_spot_handler_fn`
- `zlink_xpub_handler_fn`

의미:

- 각각 `zlink_subscribe_handler_fn`,
  `zlink_subscription_event_handler_fn`의 공개 alias typedef다.
- 함수 선언은 이미 canonical 이름으로 정리됐는데 typedef alias가 남아 있으면
  공개 헤더 차원에서는 구 naming이 완전히 정리되지 않은 상태가 된다.
- canonical public surface 설명만 남기려면 이 alias typedef도 최종 제거 대상이다.

## 4. 현재 `zlink.h` 기준으로 제거 대상이 아닌 항목

다음은 현재 문서에서 제거 대상으로 보지 않는다.

- 이미 `zlink.h`에서 사라진 구 pub/sub public 이름
- `zlink.h`에 선언되지 않은 compat 함수
- `zlink.h`에 공개 상수로 존재하지 않는 `ZLINK_SNDMORE`

즉 현재 `zlink.h` 기준으로 남은 정리 대상은 다음 두 축이다.

- `RCVMORE`/`MORE` + `zlink_msg_more/get/set` 공개 has-more surface
- `spot`/`xpub` 구 callback 이름 alias typedef

주의:

- 구현 내부나 테스트 유틸에는 `ZLINK_SNDMORE` 사용이 남아 있을 수 있다.
- 그러나 이 문서는 사용자 요청대로 현재 공개 `zlink.h` 상태만 기준으로 쓴다.
- 따라서 `ZLINK_SNDMORE`는 `현재 zlink.h 공개 제거 대상`이 아니라
  `내부 리팩터링 관련 참고 항목`으로만 다룬다.
- bindings/외부 래퍼 영향은 이번 문서 범위에 넣지 않는다.
- 이번 문서는 `core/` 내부 정리와 `core/include/zlink.h` 공개 축소만 다루고,
  bindings 후속 정리는 별도 작업으로 남긴다.

## 5. 왜 지금도 정리가 필요한가

현재 canonical recv 계열은 multipart 전체를 한 번에 다룬다.

- `zlink_recv`
- `zlink_subscribe_recv`
- `zlink_subscription_event_recv`

그런데 공개 헤더에 `RCVMORE`/`MORE`와 `zlink_msg_more/get/set` 축이 남아
있으면 사용자는 다시 `frame-by-frame recv + has more 조회` 모델을 같이
배워야 한다.

또한 공개 함수 이름은 이미 canonical pub/sub 이름으로 정리됐는데,
typedef alias에만 `spot`/`xpub` 구 naming이 남아 있으면
공개 surface 설명이 다시 혼합된다.

이 상태는 POSD 관점에서 좋지 않다.

- 하나의 public contract를 두 방식으로 설명해야 한다.
- "어떤 API가 표준 경로인가"를 설명할 때 예외 문장이 길어진다.
- public header가 현재 권장 surface와 과거 운용 surface를 동시에 실어 나른다.

## 6. 제거를 막는 실제 blocker

공개 헤더 기준 제거 대상은 작지만, 내부 의존은 아직 남아 있다.

### 6.1 internal recv forwarding이 `RCVMORE`에 의존

현재 다음 구현은 frame 하나를 받고 `getsockopt(ZLINK_RCVMORE)`로
multipart 경계를 판단한다.

- [spot_data_plane.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_data_plane.cpp)
- [proxy.cpp](/home/hep7/project/kairos/zlink/core/src/sockets/proxy.cpp)

의미:

- `zlink.h`에서 `ZLINK_RCVMORE`만 지워도 내부 설명 모델은 그대로 남는다.
- 내부 forwarding부터 `multipart batch` helper 기준으로 바꿔야
  공개 제거가 자연스러워진다.

### 6.2 send path는 여전히 frame-by-frame 조립 모델을 쓴다

현재 일부 내부 구현은 multipart 경계를 직접 조립하는 구조를 유지한다.

- [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp)
- [gateway.cpp](/home/hep7/project/kairos/zlink/core/src/services/gateway/gateway.cpp)
- [spot_pub.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_pub.cpp)

의미:

- 비록 `ZLINK_SNDMORE`가 지금 `zlink.h` 공개 상수는 아니더라도,
  내부가 여전히 frame-stream 모델을 쓴다.
- recv 축 정리와 send 축 정리를 함께 봐야 public surface 설명이 단순해진다.

### 6.3 core tests가 공개 has-more surface를 직접 참조

`core/` 내부에는 여전히 `RCVMORE`, `zlink_msg_more`, `zlink_msg_get`
호출에 기대는 테스트와 구현이 남아 있다.

대표 영향 범위:

- `core/tests/**`
- `core/src/**`

의미:

- header에서 정의를 제거하면 호출부 정리도 같은 단계에서 같이 해야 한다.

### 6.4 internal implementation이 alias typedef 이름을 사용 중

현재 내부 구현은 아래 구 typedef 이름을 직접 사용한다.

- [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp)
- [socket_base.hpp](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
- [socket_base.cpp](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)
- [xpub.cpp](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)

의미:

- 공개 헤더에서 alias typedef를 제거하려면 내부 코드도
  canonical typedef 이름으로 함께 치환해야 한다.
- 이 작업은 의미 보존 rename에 가깝고, `RCVMORE` 제거보다 범위가 작다.

## 7. 영향 범위

### 7.1 Public Header / Docs

- [zlink.h](/home/hep7/project/kairos/zlink/core/include/zlink.h)
- [socket.md](/home/hep7/project/kairos/zlink/doc/spec/core/socket/README.md)
- [socket.ko.md](/home/hep7/project/kairos/zlink/doc/spec/core/socket/README.ko.md)
- [README.md](/home/hep7/project/kairos/zlink/doc/spec/core/README.md)
- [README.ko.md](/home/hep7/project/kairos/zlink/doc/spec/core/README.ko.md)

### 7.2 Internal Core

- [socket_base.cpp](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)
- [proxy.cpp](/home/hep7/project/kairos/zlink/core/src/sockets/proxy.cpp)
- [spot_data_plane.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_data_plane.cpp)
- [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp)
- [gateway.cpp](/home/hep7/project/kairos/zlink/core/src/services/gateway/gateway.cpp)
- [spot_pub.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_pub.cpp)
- [socket_base.hpp](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
- [xpub.cpp](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)

### 7.3 Core Tests

- `core/tests/**`

이번 계획은 `core/`만 수정 범위로 본다.

## 8. 실행 단계

### Phase 1. 공개 문서 기준 정리

목표:

- 현재 권장 public C API가 canonical recv/send/publish surface임을
  문서에서 먼저 명확히 한다.
- `RCVMORE`/`MORE`, `zlink_msg_more/get/set`, 구 callback alias typedef는
  현재 헤더 잔여 surface로만 기술한다.

작업:

- API 문서에서 canonical 예제를 우선 배치
- `RCVMORE` 기반 사용 예시 제거 또는 legacy note로 격하
- `zlink_msg_more/get/set` 기반 사용 예시 제거 또는 legacy note로 격하
- 구 `spot`/`xpub` callback typedef 명칭을 canonical typedef로 정리
- 과거 pub/sub 이름 migration 내용과 현재 문서를 분리 유지

완료 기준:

- 문서가 현재 `zlink.h` 상태와 정확히 일치한다.
- 이미 제거된 이름이 현재 제거 대상처럼 적혀 있지 않다.

### Phase 2. 공개 alias typedef 제거

목표:

- 공개 헤더에서 구 callback 이름 alias typedef를 제거한다.

작업:

- `zlink.h`에서 `zlink_spot_handler_fn` 삭제
- `zlink.h`에서 `zlink_xpub_handler_fn` 삭제
- 내부 구현과 테스트에서
  `zlink_subscribe_handler_fn`,
  `zlink_subscription_event_handler_fn`으로 치환

완료 기준:

- 공개 헤더에 `spot`/`xpub` 구 callback typedef가 남아 있지 않다.
- 내부 코드가 canonical typedef 이름만 사용한다.

### Phase 3. internal recv multipart core 정리

목표:

- internal forwarding이 `RCVMORE` 없이 동작하게 만든다.

작업:

- `proxy.cpp`에 `recv one multipart` helper 추가
- `spot_data_plane.cpp` fanout/mesh forwarding을 frame loop에서
  multipart vector helper 기반으로 변경
- reusable helper를 `multipart batch` 개념으로 정리

완료 기준:

- internal implementation에서 `getsockopt(ZLINK_RCVMORE)` 사용이 제거된다.
- public recv contract와 internal forwarding contract가 같은 모델로 설명된다.

### Phase 4. 공개 `RCVMORE`/`MORE` 제거

목표:

- `zlink.h`에서 현재 남아 있는 has-more legacy 공개 항목을 제거한다.

작업:

- `ZLINK_SOCKOPT_RCVMORE` 제거
- `ZLINK_RCVMORE` 제거
- `ZLINK_MORE` 제거
- `zlink_msg_more` 제거
- `zlink_msg_get` 제거
- `zlink_msg_set` 제거
- `ZLINK_SHARED` 사용처는 별도 좁은 API 또는 internal 경로로 선치환
- 관련 `core` tests/docs 수정

완료 기준:

- `zlink.h`에 현재 canonical recv/send/pubsub 설명과 무관한
  has-more 공개 항목이 남지 않는다.

## 9. 세부 체크리스트

### 9.1 공개 header 체크리스트

- [ ] `zlink.h`에서 `zlink_spot_handler_fn` 제거
- [ ] `zlink.h`에서 `zlink_xpub_handler_fn` 제거
- [ ] `zlink.h`에서 `ZLINK_SOCKOPT_RCVMORE` 제거
- [ ] `zlink.h`에서 `ZLINK_RCVMORE` 제거
- [ ] `zlink.h`에서 `ZLINK_MORE` 제거
- [ ] `zlink.h`에서 `zlink_msg_more` 제거
- [ ] `zlink.h`에서 `zlink_msg_get` 제거
- [ ] `zlink.h`에서 `zlink_msg_set` 제거

### 9.2 구현 체크리스트

- [ ] 내부 구현에서 `zlink_spot_handler_fn` 사용 제거
- [ ] 내부 구현에서 `zlink_xpub_handler_fn` 사용 제거
- [ ] `proxy.cpp`에서 `getsockopt(ZLINK_RCVMORE)` 제거
- [ ] `spot_data_plane.cpp`에서 `getsockopt(ZLINK_RCVMORE)` 제거
- [ ] `socket_base.cpp`의 공개 option 노출 정리
- [ ] `core/src`에서 `zlink_msg_more` 의존 제거
- [ ] `core/src`에서 `zlink_msg_get(..., ZLINK_MORE)` 의존 제거
- [ ] `core/src`에서 `zlink_msg_get/set(..., ZLINK_SHARED)` 사용처를
  더 좁은 API로 치환
- [ ] recv/send internal helper를 canonical multipart batch 모델로 정리

### 9.3 호출부 체크리스트

- [ ] core tests에서 `RCVMORE` 의존 제거
- [ ] core tests에서 `zlink_msg_more/get/set` 의존 제거 또는 대체
- [ ] 문서/예제에서 구 callback alias typedef 이름 제거
- [ ] 문서/예제에서 `has more` 기반 설명 제거

## 10. 검증 계획

### 10.1 Build

- `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
- `cmake --build core/build -j$(nproc)`

### 10.2 Focused Tests

- `test_socket_with_handler`
- `test_getsockopt_memset`
- `test_disconnect_inproc`
- `test_msg_flags`
- `test_monitor_service_contract`
- `test_spot_service_introspection`

### 10.3 추가 회귀 점검 영역

- proxy 관련 integration tests
- spot data plane forwarding tests

## 11. 완료 정의

다음 조건을 모두 만족하면 이 문서 범위의 작업은 완료다.

- 공개 [`zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h)에
  `RCVMORE`/`MORE` 공개 정의가 없다.
- 공개 [`zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h)에
  `zlink_msg_more/get/set` 공개 선언이 없다.
- 공개 [`zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h)에
  `zlink_spot_handler_fn`, `zlink_xpub_handler_fn` alias typedef가 없다.
- canonical transport/pubsub surface만으로 recv/send shape를 설명할 수 있다.
- internal forwarding이 더 이상 `getsockopt(ZLINK_RCVMORE)`에 의존하지 않는다.
- `core` tests/docs가 현재 공개 헤더 상태와 일치한다.

## 12. 현재 판단 요약

- 이미 제거된 구 pub/sub 함수 이름은 이 문서의 현재 제거 대상이 아니다.
- 현재 `zlink.h` 기준 실질 제거 대상은
  `RCVMORE`/`MORE`/`zlink_msg_more/get/set` 축과 구 callback alias typedef다.
- 이 중 alias typedef 제거는 먼저 진행 가능하고,
  has-more surface 제거는 내부 multipart forwarding 정리가 먼저다.

즉, 지금 기준 작업의 본질은
`pub/sub 함수 이름 정리`가 아니라
`현재 zlink.h 에 남아 있는 has-more public surface + 구 callback alias 정리`다.
