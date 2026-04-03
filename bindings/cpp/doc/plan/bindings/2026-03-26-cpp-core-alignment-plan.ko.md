# C++ Bindings `core` 최신 정렬 작업 계획

작성일: 2026-03-26

## 1. 목적

`core` 라이브러리의 최신 공개 C API와 현재 `bindings/cpp`의 C++ 헤더 전면이
크게 어긋난 상태를 정리하고, 최신 `core`를 기준으로 C++ 바인딩을 다시
정렬한다.

이번 작업의 목표는 단순히 "컴파일되게 만들기"가 아니다. 다음을 동시에 달성해야
한다.

- 최신 `core/include/zlink.h` 기준으로 C++ 바인딩이 실제로 유효한 래퍼가 될 것
- `doc/guide`가 설명하는 최신 사용 모델과 C++ surface가 같은 개념을 말할 것
- 구형/삭제된 C API를 억지로 감추는 얕은 호환 래퍼를 늘리지 않을 것
- RAII, type-safe option, minimal wrapper라는 현재 바인딩의 장점은 유지할 것

## 2. 현재 상태 요약

### 2.1 소스 오브 트루스

이번 정렬 작업의 우선순위는 아래 순서로 둔다.

1. `../../../../core/include/zlink.h`
2. `../../../../doc/guide/*.md`, `../../../../doc/api/*.md`
3. 현재 `bindings/cpp/include`
4. 현재 `bindings/cpp/tests`

즉, 문서와 기존 바인딩이 충돌하면 기본적으로 최신 `core/include/zlink.h`를
정답으로 보고, 문서는 동작 의미를 보완하는 자료로 사용한다.

### 2.2 실제 확인된 주요 불일치

현재 헤더를 읽어 확인한 결과, C++ 바인딩은 일부 최신화 흔적이 있지만 전체적으로는
구형 public surface를 전제로 작성되어 있다.

#### 기반 계층 불일치

- [`include/zlink/socket.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/socket.hpp)
  는 구형 `zlink_send`, `zlink_recv`, `zlink_setsockopt`, `zlink_getsockopt`,
  `zlink_msg_send`, `zlink_msg_recv`, STREAM 전용 attach API를 사용한다.
- 최신 `core`의 송수신 surface는 멀티파트 배열 중심의
  `zlink_send`, `zlink_send_rid`, `zlink_recv`, `zlink_publish`,
  `zlink_subscribe` 구조다.
- 최신 `core`는 `zlink_set_option`/`zlink_get_option`과
  router/dealer/pub/sub/stream 전용 option domain API를 사용한다.

#### 서비스 계층 불일치

- [`include/zlink/services/discovery.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/services/discovery.hpp)
  는 `zlink_discovery_new_typed`, `zlink_discovery_receiver_count`,
  `zlink_discovery_get_receivers` 등 현재 `core`에 없는 함수를 전제로 한다.
- [`include/zlink/services/registry.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/services/registry.hpp)
  는 `zlink_registry_set_endpoints`, `zlink_registry_start`,
  `zlink_registry_setsockopt`를 사용하지만, 최신 `core`는
  `zlink_registry_bind`와 snapshot/query 계열 API로 재편되었다.
- [`include/zlink/services/spot.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/services/spot.hpp)
  는 `spot_pub`/`spot_sub` 분리 모델, `connect_peer_pub`,
  `set_discovery(service)` 등 구형 surface에 강하게 묶여 있다.
- 최신 가이드
  [`doc/guide/07-1-discovery.ko.md`](/home/hep7/project/kairos/zlink/doc/guide/07-1-discovery.ko.md),
  [`doc/guide/07-3-spot.ko.md`](/home/hep7/project/kairos/zlink/doc/guide/07-3-spot.ko.md)
  는 `zlink_discovery_new(ctx, service_type, service_name)`,
  `zlink_socket_attach_discovery`, `zlink_spot_new`, `zlink_spot_node_attach_discovery`
  기반의 최신 모델을 설명한다.

#### 모니터/폴러/보조 surface 불일치

- [`include/zlink/monitor.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/monitor.hpp)
  는 `zlink_monitor_recv`를 전제로 하지만 최신 `core`는
  `zlink_socket_monitor_recv`, `zlink_monitor_snapshot`, `zlink_monitor_close`
  구조다.
- [`include/zlink/poller.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/poller.hpp)
  는 `zlink_poller_add_spot_sub`, `zlink_poller_add_receiver` 같은
  삭제된 API를 호출한다.
- `compat.hpp`에는 현재 `core`에 존재하지 않는 구형 symbol alias가 많이 남아 있다.

#### 테스트 계층 불일치

- [`tests/test_cpp_core_service_discovery.cpp`](/home/hep7/project/kairos/zlink/bindings/cpp/tests/test_cpp_core_service_discovery.cpp)
  는 `zlink::service::receiver_t`를 사용하지만, 현재 C++ 바인딩 include에는
  해당 타입이 없다.
- 여러 테스트가 구형 send/recv convenience와 구형 서비스 모델을 전제로 한다.
- 따라서 테스트는 단순 보정이 아니라 "최신 C API를 감싼 C++ 계약" 기준으로
  다시 정리해야 한다.

### 2.3 문서 간 충돌

- `guide` 문서는 대체로 최신 서비스 모델과 맞는다.
- 반면
  [`doc/api/polling.ko.md`](/home/hep7/project/kairos/zlink/doc/api/polling.ko.md)
  는 polling 제거를 설명하지만, 최신 `core/include/zlink.h`에는 poller API가
  존재한다.

결론:

- 동작 계약은 `core/include/zlink.h`를 기준으로 다시 잡아야 한다.
- 문서는 최신 guide를 우선 참고하되, `api` 문서 중 낡은 부분은 함께 정리
  대상에 포함해야 한다.

## 3. 설계 원칙

이번 정렬 작업은 POSD 원칙에 맞춰 진행한다.

- 얕은 호환층보다 깊은 모듈을 우선한다.
- 삭제된 `core` 개념을 C++에서만 살려두는 래퍼는 만들지 않는다.
- 현재 `core`가 멀티파트/서비스 모니터/스냅샷 중심으로 바뀌었다면 C++도 같은
  개념을 직접 드러낸다.
- 예전 편의 API가 유지 가치가 있더라도, 최신 계약 위에서 자연스럽게 구현되는
  것만 남긴다.
- 최신 `core`에 없는 API를 유지하기 위한 전용 변환 계층은 최소화하거나 제거한다.

## 3.1 이번 작업에서 고정하는 결정

아래 항목은 구현 전에 더 논의하지 않고 이번 작업에서 바로 적용한다.

### 유지하는 C++ convenience

- `socket_t::send(message_t &, send_flag)`
- `socket_t::send(std::vector<message_t> &, send_flag)`
- `socket_t::send(const zlink_routing_id_t &, message_t &, send_flag)`
- `socket_t::send(const zlink_routing_id_t &, std::vector<message_t> &, send_flag)`
- `socket_t::recv(message_t &, recv_flag)`
- `socket_t::recv(std::vector<message_t> &, recv_flag)`
- `socket_t::recv(zlink_routing_id_t &, message_t &, recv_flag)`
- `socket_t::recv(zlink_routing_id_t &, std::vector<message_t> &, recv_flag)`

정책:

- `socket_t`는 `message_t`/multipart 기반 `send`/`recv` overload만 제공한다.
- bytes/string 변환은 `message_t`가 담당한다.
- 같은 행위의 변형은 이름을 늘리지 않고 시그니처 overload로만 구분한다.

### 제거하는 convenience

- `socket_t::recv(void *, size_t, recv_flag)`
- `socket_t::send(const void *, size_t, send_flag)`
- `socket_t::send(const std::string &, send_flag)`
- STREAM 전용 `stream_attach*`, `stream_send*` 계열
- socket peer inspect 계열
- 구형 `setsockopt/getsockopt` 스타일 C++ wrapper

제거 이유:

- 최신 `core`의 multipart/handler 모델과 자연스럽게 대응하지 않는다.
- 억지 유지 시 wrapper semantics가 C API보다 더 복잡해진다.

### 서비스 surface 결정

- `receiver_t`는 도입하지 않는다.
- standalone `spot_pub_t`, `spot_sub_t`는 도입하지 않는다.
- 서비스 계층은 `registry_t`, `registry_query_client_t`, `discovery_t`,
  `spot_node_t`, `spot_t`로 고정한다.

### discovery attach 진입점 결정

- raw socket discovery attach의 주 진입점은 `socket_t::attach_discovery(...)`로
  둔다.
- `discovery_t`에는 대응되는 lifecycle helper를 추가하지 않는다.

이유:

- attach의 대상은 discovery가 아니라 socket이므로 소유권과 행위가 더 명확하다.

### `compat.hpp` 정책

- `compat.hpp` 파일은 남긴다.
- 단, umbrella header [`include/zlink.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink.hpp)
  에서는 포함하지 않는다.
- `compat.hpp`에는 최신 `core`에 존재하는 API에 대한 얇은 deprecated shim만
  남긴다.
- 삭제된 symbol을 부활시키는 shim은 금지한다.

### 샘플/테스트 정책

- `core/tests` 포팅형 테스트는 삭제를 기본값으로 한다.
- 샘플은 패턴별 `recv`/`callback` 2종을 반드시 제공한다.
- 자동 검증은 contract test와 sample smoke만 맡는다.
- `bindings/cpp/perf/**`는 별도 작업 범위로 두고 이번 완료 판정에는 포함하지 않는다.

### `message_t` 변환 정책

- `message_t`는 bytes/string 변환 helper를 제공한다.
- 변환 API 이름은 `from_*`, `to_*` 계열의 명시적 함수로 통일한다.
- implicit conversion, 자동 serialize/deserialize, 템플릿 기반 범용 직렬화는
  도입하지 않는다.

### 콜백 API 정책

- 초기 구현에서는 `std::function` 기반 callback wrapper를 도입하지 않는다.
- callback 등록 API는 네이티브 C callback typedef와 `void *userdata`를 그대로
  받는 thin wrapper로 고정한다.
- send/recv data path는 `message_t` 기반으로 정리하되, callback registration은
  제어 surface로 취급한다.

고정 시그니처:

- `int socket_t::recv_handler(zlink_socket_msg_handler_fn, void *userdata = NULL)`
- `int socket_t::subscribe_handler(zlink_subscribe_handler_fn, void *userdata = NULL)`
- `int socket_t::send_ready_handler(zlink_send_ready_handler_fn, void *userdata = NULL)`
- `int monitor_handle_t::handler(zlink_socket_monitor_handler_fn, void *userdata = NULL)`
- `int service_monitor_handle_t::handler(zlink_service_monitor_handler_fn, void *userdata = NULL)`

소유권 규칙:

- callback payload ownership은 native C API 계약을 그대로 따른다.
- C++ wrapper는 callback payload를 `message_t`나 `std::vector<message_t>`로
  재포장하지 않는다.
- callback 샘플은 native payload close 규칙을 명시적으로 보여준다.

## 4. 목표 산출물

최종 산출물은 다음과 같다.

- 최신 `core`와 정합한 header-only C++ 바인딩
- 패턴별 `recv`/`callback` 샘플과 실행 동선
- 바인딩 전용 contract/smoke 테스트 최소 세트
- 최신 API 기준으로 갱신된 바인딩 문서
- 제거/변경된 C++ surface를 명확히 설명하는 migration note

## 5. 작업 범위

### 포함

- `bindings/cpp/include/**`
- `bindings/cpp/samples/**`
- `bindings/cpp/tests/**`
- `bindings/cpp/CMakeLists.txt`
- `bindings/cpp/build.sh`
- `bindings/cpp/API_DRAFT.md`
- `doc/bindings/cpp*.md`
- `bindings/cpp/TESTING.md`
- `bindings/cpp/README.doxygen.md`
- `bindings/cpp/Doxyfile`

### 제외

- `core/` 구현 변경
- `core/perf/`, `core/bench/` 변경
- `bindings/cpp/perf/**` 변경
- Node/Java/.NET 바인딩 변경

## 5.1 구현 완료 후 기대 public surface

umbrella header [`include/zlink.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink.hpp)
는 최종적으로 아래 계층만 노출한다.

### 공통

- `common.hpp`
- `types.hpp`
- `error.hpp`
- `message.hpp`
- `context.hpp`
- `socket.hpp`
- `monitor.hpp`
- `service_monitor.hpp`
- `poller.hpp`
- `runtime.hpp`
- `atomic_counter.hpp`
- `timers.hpp`
- `stopwatch.hpp`
- `thread.hpp`

### 서비스

- `services/registry.hpp`
- `services/query.hpp`
- `services/discovery.hpp`
- `services/spot.hpp`

### umbrella에서 제외하는 것

- `compat.hpp`
- 삭제된 개념을 담았던 임시/이관용 헤더
- standalone `spot_pub` / `spot_sub` / `receiver` 성격의 헤더

## 6. 실행 전략

이번 작업은 "전면 재작성"처럼 보이지만, 실제로는 아래 세 층으로 분할해야 한다.

1. 바닥 계약 정렬
2. 서비스/모니터/snapshot surface 정렬
3. 테스트와 문서의 의미 재정렬

핵심은 기반 계층을 먼저 고친 뒤 그 위에 서비스 surface를 얹는 것이다.
지금 상태에서 서비스 wrapper부터 손대면 change amplification이 커진다.

## 6.1 테스트 전략

이번 작업에서는 `core/tests` 포팅 성격의 C++ 테스트를 기본 전략으로 삼지
않는다. 그 방식은 바인딩이 아니라 `core` 자체를 중복 검증하게 만들고,
`core` 변경이 있을 때 C++ 바인딩 유지비만 과도하게 키운다.

대신 검증 자산을 아래 3개 층으로 재편한다.

### 1. Samples

목적:

- 사용자-facing 사용법 제공
- 패턴별 `recv`/`callback` surface를 실제 코드로 고정

원칙:

- 각 패턴마다 `recv` 버전과 `callback` 버전을 분리한다.
- 예제는 읽기 쉬운 최소 형태로 유지한다.
- 샘플은 correctness의 자동 판정 수단이 아니라 사용 예제이자 실행 smoke
  entry로 취급한다.

샘플 구조:

- `samples/pair/pair_recv.cpp`
- `samples/pair/pair_callback.cpp`
- `samples/pubsub/pubsub_recv.cpp`
- `samples/pubsub/pubsub_callback.cpp`
- `samples/dealer_router/dealer_router_recv.cpp`
- `samples/dealer_router/dealer_router_callback.cpp`
- `samples/stream/stream_recv.cpp`
- `samples/stream/stream_callback.cpp`
- `samples/spot/spot_recv.cpp`
- `samples/spot/spot_callback.cpp`

### 2. Contract Tests

목적:

- C API는 맞더라도 C++ wrapper에서만 틀릴 수 있는 계약을 검증

원칙:

- 테스트 수는 작게 유지한다.
- `core`의 transport/protocol matrix를 다시 구현하지 않는다.
- wrapper lifetime, ownership, typed option, mode transition 같은
  바인딩 전용 위험만 본다.

남길 테스트 범위:

- `message_t` move/copy/close/refcnt/bytes-string 변환 contract
- `socket_t`의 `send`/`recv` overload와 multipart API 매핑
- `recv`와 callback 모드의 배타성
- typed option/domain option 매핑
- monitor/service monitor wrapper
- discovery attach, unified `spot`, registry query 같은 바인딩 조립 계약

### 3. Perf 별도 작업

- 기존 `bindings/cpp/perf/**`는 이미 별도 자산으로 존재한다.
- 이번 정렬 작업은 perf 복구나 확장을 완료 조건으로 삼지 않는다.
- perf는 팀장님이 별도 트랙으로 진행한다는 전제로 문서 범위에서 제외한다.

## 6.3 디렉토리 구조

검증 자산은 아래처럼 나눈다.

```text
bindings/cpp/
  include/
  samples/
    pair/
      pair_recv.cpp
      pair_callback.cpp
    pubsub/
      pubsub_recv.cpp
      pubsub_callback.cpp
    dealer_router/
      dealer_router_recv.cpp
      dealer_router_callback.cpp
    stream/
      stream_recv.cpp
      stream_callback.cpp
    spot/
      spot_recv.cpp
      spot_callback.cpp
    common/
      sample_common.hpp
  tests/
    contract/
      test_cpp_contract_message.cpp
      test_cpp_contract_socket.cpp
      test_cpp_contract_callback_mode.cpp
      test_cpp_contract_options.cpp
      test_cpp_contract_monitor.cpp
      test_cpp_contract_service.cpp
    common/
      test_helpers.hpp
```

구조 원칙:

- 샘플은 패턴별로 폴더를 나눈다.
- 각 패턴 폴더에서 `recv`/`callback` 파일명을 대칭으로 유지한다.
- 샘플 공통 코드는 `samples/common/`으로 모은다.
- contract test도 `tests/contract/`로 의미를 명확히 한다.
- 기존 `perf/` 디렉토리는 유지하되 이번 작업 구조 정의에는 포함하지 않는다.

## 6.4 CMake 타깃 설계

`CMakeLists.txt`는 샘플과 contract test를 명확히 분리해야 한다.

고정 옵션:

- `ZLINK_CPP_BUILD_SAMPLES`
- `ZLINK_CPP_BUILD_TESTS`

`build.sh` 인터페이스도 같이 바꾼다.

```bash
./bindings/cpp/build.sh [RUN_TESTS] [RUN_SAMPLES]
```

기본값:

- `RUN_TESTS=ON`
- `RUN_SAMPLES=ON`

`build.sh`가 넘겨야 하는 옵션:

- `-DZLINK_CPP_BUILD_TESTS=${RUN_TESTS}`
- `-DZLINK_CPP_BUILD_SAMPLES=${RUN_SAMPLES}`

타깃 성격:

- `zlink-cpp`
  - header-only 인터페이스 라이브러리 유지
- `sample_cpp_pair_recv`, `sample_cpp_pair_callback` 등
  - 각 샘플 executable
- `test_cpp_contract_message`, `test_cpp_contract_socket` 등
  - 최소 contract test executable

CMake helper 함수:

- `add_cpp_sample(target source)`
- `add_cpp_contract_test(target source)`

함수 규칙:

- sample target 이름은 `sample_cpp_<pattern>_<mode>`
- contract test 이름은 `test_cpp_contract_<topic>`
- 샘플과 테스트 모두 `libzlink`와 `zlink-cpp`를 링크
- Windows PATH 설정과 공통 compile option 처리는 helper 함수 안에 캡슐화

CTest 등록 방식:

- 샘플은 기본적으로 빌드 대상에 포함한다.
- 샘플은 전부 `sample_smoke_*` 이름으로 CTest에 등록한다.
- contract test는 전부 CTest에 등록한다.

`sample-smoke` 등록 대상:

- `sample_cpp_pair_recv`
- `sample_cpp_pair_callback`
- `sample_cpp_pubsub_recv`
- `sample_cpp_pubsub_callback`
- `sample_cpp_dealer_router_recv`
- `sample_cpp_dealer_router_callback`
- `sample_cpp_stream_recv`
- `sample_cpp_stream_callback`
- `sample_cpp_spot_recv`
- `sample_cpp_spot_callback`

라벨:

- `contract`
- `sample-smoke`

검증 동선:

1. configure/build
2. `ctest --test-dir core/build -L contract`
3. `ctest --test-dir core/build -L sample-smoke -j1`

핵심 원칙:

- 샘플은 사용자 문서와 실행 예제 역할
- contract test는 자동 실패 판정 역할

## 6.5 삭제 기준

삭제 대상:

- `core/tests`를 거의 그대로 옮긴 transport/reconnect/HWM/protocol 세부 검증
- `core` 자체가 맞는지만 확인하는 테스트
- 최신 `core`에 더 이상 없는 wrapper/type을 전제로 한 테스트

유지 대상:

- C++ wrapper에서만 깨질 수 있는 수명/소유권/타입 매핑/API 조립 테스트

## 6.6 old-to-new 대응표

아래 표는 구현 시 바로 참고하는 치환 기준이다.

| 현재/구형 C++ 개념 | 처리 | 최신 기준 |
|---|---|---|
| `zlink_setsockopt`/`zlink_getsockopt` 기반 wrapper | 제거 | `zlink_set_option` / `zlink_get_option` + domain option API |
| `socket_t::recv(void *, size_t)` | 제거 | `recv(message_t&)`, `recv(std::vector<message_t>&)` |
| `socket_t::send(const void *, size_t)` | 제거 | `message_t::from_bytes(...)` + `send(message_t&)` |
| `socket_t::send(const std::string &)` | 제거 | `message_t::from_string(...)` + `send(message_t&)` |
| STREAM 전용 `stream_attach*`/`stream_send*` | 제거 | generic `recv_handler`, `send_ready_handler`, `send_rid` 계열 |
| `zlink_discovery_new_typed` | 제거 | `zlink_discovery_new(ctx, service_type, service_name)` |
| `receiver_t` | 제거 | raw socket + `attach_discovery`, 또는 서비스 wrapper 직접 사용 |
| `zlink_registry_set_endpoints` + `start` | 제거 | `zlink_registry_bind` |
| `spot_pub` / `spot_sub` 분리 wrapper | 제거 | unified `spot_t` |
| `connect_peer_pub` / `disconnect_peer_pub` | 제거 | `connect_peer` / `disconnect_peer` |
| `zlink_monitor_recv` | 제거 | `zlink_socket_monitor_recv`, `zlink_service_monitor_recv` |
| `zlink_poller_add_spot_sub` 류 전용 poller helper | 제거 | generic poller add/modify/remove |
| `compat.hpp`의 삭제된 symbol shim | 제거 | 최신 API에 대한 deprecated thin shim만 유지 |

## 7. 상세 단계

### Phase 0. API 인벤토리와 정렬 기준 고정

목표:

- 최신 `core` 기준의 C++ 대상 surface를 먼저 확정한다.

작업:

- `core/include/zlink.h`의 공개 symbol을 그룹화한다.
  - context/message/socket
  - option domain
  - monitor/service monitor
  - registry/discovery/spot
  - topology/query/snapshot
  - poller/proxy/utilities
- 현재 C++ header가 참조하는 symbol을 전수 추출한다.
- "현행 유지", "최신 API로 치환", "완전 제거" 세 가지로 분류한다.
- 문서 충돌 항목을 따로 적재한다.

산출물:

- symbol mapping checklist
- 삭제 대상 목록
- 신규 wrapper 대상 목록

완료 기준:

- 최신 `core`에서 없는 함수명을 더 이상 모른 채 작업하지 않는 상태

### Phase 1. 기반 타입과 공통 헤더 재정렬

대상 파일:

- [`include/zlink/common.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/common.hpp)
- [`include/zlink/types.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/types.hpp)
- [`include/zlink/error.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/error.hpp)
- [`include/zlink/context.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/context.hpp)
- [`include/zlink/message.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/message.hpp)

핵심 작업:

- 최신 enum/mask/type을 `types.hpp`에 다시 반영한다.
- 누락된 service/monitor/topology 관련 enum과 struct 접근 헬퍼를 추가한다.
- `context_t`를 최신 context option surface에 맞춘다.
  - `ctx_get`, `ctx_set`, `shutdown`, `term`
  - `ZLINK_CTX_OPT_BLOCKY`를 `context_option::blocky`로 반영
- `message_t`를 최신 message API 기준으로 정리한다.
  - `init/init_size/init_data/move/copy/data/size/gets/refcnt/close`
  - 더 이상 없는 `msg_more`, `msg_set`, `msg_get`는 제거
- `message_t`에 payload 변환 helper를 추가한다.
  - `from_bytes(...)`
  - `from_string(...)`
  - `from_external(...)`
  - `to_bytes()`
  - `to_string()`
- raw struct를 안전하게 다루는 helper를 추가한다.
  - routing id string/binary 변환
  - fixed-size char array를 `std::string`으로 바꾸는 읽기 헬퍼

완료 기준:

- 기반 헤더만 포함해도 최신 `core/include/zlink.h`와 심볼 충돌이 없어야 한다.

### Phase 2. `socket_t`를 최신 멀티파트/옵션 모델로 재구성

대상 파일:

- [`include/zlink/socket.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/socket.hpp)

고정 방향:

- 저수준 정식 API는 `send`/`recv` overload 중심으로 재구성한다.
- `socket_t`는 `message_t`와 `std::vector<message_t>`만 다룬다.
- single-part, multipart, routing id 유무는 모두 `send`/`recv` 시그니처로 구분한다.
- `recv(void *, size_t)`는 제거한다.
- `send(const void *, size_t)`와 `send(const std::string &)`도 제거한다.
- bytes/string payload 편의성은 `message_t`에서만 제공한다.

구체 작업:

- 생성자 시그니처를 최신 `zlink_socket(ctx, type)`에 맞춘다.
- `send` 계열을 재설계한다.
  - `send(message_t&)`
  - `send(std::vector<message_t>&)`
  - `send(const zlink_routing_id_t&, message_t&)`
  - `send(const zlink_routing_id_t&, std::vector<message_t>&)`
- `recv` 계열을 재설계한다.
  - `recv(message_t&)`
  - `recv(std::vector<message_t>&)`
  - `recv(zlink_routing_id_t&, message_t&)`
  - `recv(zlink_routing_id_t&, std::vector<message_t>&)`
- 구독/발행 surface를 소켓 레벨에서 반영한다.
  - `set_subscription`, `unset_subscription`, `subscription_at`
  - `publish`, `subscribe`, `subscription_event`
- callback surface를 최신 계약으로 맞춘다.
  - `recv_handler`
  - `subscribe_handler`
  - `send_ready_handler`
- option API를 최신 domain 구조에 맞춘다.
  - common `set_option/get_option`
  - `set_router_option/get_router_option`
  - `set_dealer_option`
  - `set_pub_option/get_pub_option`
  - `set_sub_option/get_sub_option`
  - `set_stream_option/get_stream_option`
  - `set_routing_id/get_routing_id`
  - `set_tls_server/set_tls_client`
- raw socket discovery attach 지원을 추가한다.
  - `socket_t::attach_discovery(discovery_t&)`

삭제/축소 후보:

- STREAM 전용 `stream_attach*`, `stream_send*` 계열
- socket peer inspect 계열이 최신 `core`에 없으므로 제거
- `zlink_setsockopt`/`zlink_getsockopt` 기반 old typed option path 제거

완료 기준:

- `socket_t`가 최신 raw socket public contract를 대표하는 중심 래퍼가 된다.

### Phase 3. monitor / service monitor / snapshot surface 추가

대상 파일:

- [`include/zlink/monitor.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/monitor.hpp)
- 신규 [`include/zlink/service_monitor.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/service_monitor.hpp)

핵심 작업:

- socket monitor open/recv/close RAII 래퍼 추가
- `zlink_monitor_snapshot()` wrapper 추가
- monitor event mask와 snapshot state/detail mask typed enum은 `types.hpp`에 정리
- service monitor open/recv/close wrapper 추가
- `zlink_service_event_t`를 C++에서 읽기 쉬운 helper 제공

고정 모델:

- `monitor_handle_t`
- `service_monitor_handle_t`
- event struct는 C struct를 그대로 노출하되 string extractor helper 제공

완료 기준:

- raw socket monitor와 discovery/spot/service monitor가 C++에서 일관된 방식으로
  열리고, 읽히고, 닫힌다.

### Phase 4. poller/runtime/utilities 정리

대상 파일:

- [`include/zlink/poller.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/poller.hpp)
- [`include/zlink/runtime.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/runtime.hpp)
- [`include/zlink/timers.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/timers.hpp)
- [`include/zlink/thread.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/thread.hpp)
- [`include/zlink/stopwatch.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/stopwatch.hpp)
- [`include/zlink/atomic_counter.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/atomic_counter.hpp)

핵심 작업:

- `poller_t`를 최신 generic poller API에 맞춰 단순화한다.
- `spot`/`receiver` 전용 poller helper처럼 `core`에 없는 얕은 wrapper는 제거한다.
- `runtime.hpp`는 version/proxy/has만 남기되 최신 function signature에 맞춘다.
- ancillary wrapper는 최신 `core`에 존재하는 API만 유지한다.

주의:

- `doc/api/polling.ko.md`와 실제 header가 충돌하므로, 구현은 header를 따른다.
- 이후 문서 단계에서 poller 지원 범위를 명확히 재서술한다.

완료 기준:

- poller/utilities 계층에 더 이상 삭제된 `core` symbol reference가 없어야 한다.

### Phase 5. 서비스 wrapper 전면 재구성

대상 파일:

- [`include/zlink/services/registry.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/services/registry.hpp)
- [`include/zlink/services/discovery.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/services/discovery.hpp)
- [`include/zlink/services/spot.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/services/spot.hpp)
- 신규 [`include/zlink/services/query.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/services/query.hpp)

#### 5.1 registry_t

재설계 방향:

- `set_endpoints + start` 모델을 버리고 `bind(pub, router)` 중심으로 바꾼다.
- snapshot/query를 first-class API로 올린다.

필수 기능:

- `bind`
- `set_id`
- `add_peer`
- `set_heartbeat`
- `set_broadcast_interval`
- `destroy`
- `status_snapshot`
- `service_summary_snapshot`
- `member_peers`
- `member_peer_metadata`
- `topology_snapshot`
- `topology_query`

#### 5.2 registry_query_client_t 신규 추가

필수 기능:

- `new`
- `connect`
- `snapshot`
- `destroy`

이 타입은 현재 `registry_t`와 역할이 다르므로 별도 깊은 모듈로 두는 것이 낫다.

#### 5.3 discovery_t

재설계 방향:

- `service_type + service_name`이 생성 시 고정되는 최신 모델로 전환한다.
- 예전 `receiver_count(service)` 식 동적 조회 모델은 제거한다.

필수 기능:

- `discovery_t(context_t&, service_type, const std::string &service_name)`
- `connect_registry`
- `set_value/get_value`
- `set_metadata/get_metadata`
- `member_peers`
- `member_peer_metadata`
- `destroy`

고정 결정:

- raw socket attach helper는 `socket_t` 쪽에만 둔다.

#### 5.4 spot_node_t

재설계 방향:

- 수동 mesh와 discovery attach를 둘 다 지원하되, 최신 `core`가 노출하는 최소
  surface만 감싼다.

필수 기능:

- `bind`
- `connect_peer`
- `disconnect_peer`
- `attach_discovery`
- `status_snapshot`
- `peers_snapshot`
- `peers_query`
- `subjects_snapshot`
- `destroy`

#### 5.5 unified spot_t

재설계 방향:

- 구형 `spot_pub`/`spot_sub` 래퍼를 버리고 unified `spot_t` 중심으로 정리한다.

필수 기능:

- `publish`
- `set_subscription`
- `unset_subscription`
- `subscription_at`
- `subscribe`
- `subscription_event`
- `subscribe_handler`
- `send_ready_handler`
- `destroy`

삭제 대상:

- standalone `spot_pub_t`, `spot_sub_t` 성격의 old wrapper

완료 기준:

- 현재 guide가 설명하는 discovery/spot 모델을 C++에서 직접 표현할 수 있어야 한다.

### Phase 6. `compat.hpp` 정리와 호환 정책 확정

대상 파일:

- [`include/zlink/compat.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/compat.hpp)

원칙:

- 최신 `core`에 없는 API를 `compat.hpp`에서 되살리지 않는다.
- `compat.hpp`는 "이름 정리용 얇은 alias"만 허용한다.

실행안:

- 삭제된 symbol alias를 전수 제거한다.
- 최신 API 위에 자연스럽게 올릴 수 있는 이름 정리용 deprecated shim만 남긴다.
- `compat.hpp` 파일은 유지하되 umbrella include에서는 계속 제외한다.

완료 기준:

- `compat.hpp`에는 삭제된 symbol reference가 없어야 한다.
- `compat.hpp`에는 최신 API 위의 deprecated thin shim만 남아야 한다.

### Phase 7. 샘플/contract test 재편

대상 파일:

- `bindings/cpp/samples/**`
- `bindings/cpp/tests/*.cpp`
- `bindings/cpp/tests/test_helpers.hpp`
- `bindings/cpp/CMakeLists.txt`

원칙:

- `core` 포팅형 대량 테스트는 제거한다.
- 샘플은 사용자-facing 패턴 검증과 실행 진입점 역할을 맡긴다.
- `tests`는 바인딩 전용 contract/smoke만 남긴다.

작업:

- 기존 `core` 포팅형 테스트를 전수 분류한다.
  - 삭제: `core` 동작 재검증 성격의 테스트
  - 유지/축소: 바인딩 계약 확인 테스트
- `samples/` 디렉토리를 신설한다.
- 패턴별 `recv`/`callback` 샘플을 작성한다.
- 샘플 실행 타깃을 CMake에 추가한다.
- contract test를 최소 세트로 재작성한다.
- raw socket monitor, service monitor, typed option, unified `spot`,
  discovery attach, registry query 등만 남긴다.
- header-only compile smoke test를 별도 유지한다.

샘플 묶음:

- `pair_recv`
- `pair_callback`
- `pubsub_recv`
- `pubsub_callback`
- `dealer_router_recv`
- `dealer_router_callback`
- `stream_recv`
- `stream_callback`
- `spot_recv`
- `spot_callback`

contract test 묶음:

- `test_cpp_contract_message.cpp`
- `test_cpp_contract_socket.cpp`
- `test_cpp_contract_callback_mode.cpp`
- `test_cpp_contract_options.cpp`
- `test_cpp_contract_monitor.cpp`
- `test_cpp_contract_service.cpp`

완료 기준:

- 대량의 `core` 포팅 테스트가 제거된다.
- 각 주요 패턴에 대해 `recv`/`callback` 샘플이 존재한다.
- 바인딩 전용 contract test만 남는다.

### Phase 8. 문서/예제 정리

대상 파일:

- [`API_DRAFT.md`](/home/hep7/project/kairos/zlink/bindings/cpp/API_DRAFT.md)
- [`doc/bindings/cpp.ko.md`](/home/hep7/project/kairos/zlink/doc/bindings/cpp.ko.md)
- [`doc/bindings/cpp.md`](/home/hep7/project/kairos/zlink/doc/bindings/cpp.md)
- [`README.doxygen.md`](/home/hep7/project/kairos/zlink/bindings/cpp/README.doxygen.md)
- [`Doxyfile`](/home/hep7/project/kairos/zlink/bindings/cpp/Doxyfile)

작업:

- 예제 코드를 최신 `core` 모델로 다시 작성한다.
- 구형 `spot_pub/sub`, `receiver_t`, STREAM 전용 attach API 설명을 제거한다.
- unified `spot`, service monitor, snapshot/query client 사용 예제를 추가한다.
- 바뀐 C++ surface를 "breaking changes"로 명시한다.

완료 기준:

- 문서 예제가 현재 코드와 같이 빌드/이해 가능해야 한다.

## 8. 우선순위

실행 우선순위는 아래와 같다.

1. 삭제된 symbol reference 제거
2. 기반 header 정렬
3. `socket_t` 재구성
4. monitor/service monitor
5. registry/discovery/spot
6. 샘플/contract test 정리
7. 문서

이 순서를 지키는 이유는 `socket_t`와 공통 타입이 먼저 안정화되어야 서비스
wrapper의 형태를 자연스럽게 만들 수 있기 때문이다.

## 9. 파일별 실행 체크리스트

구현 시작 시 아래 순서대로 진행하면 된다.

### Slice 1. 기반 공통 계층

대상:

- [`include/zlink/common.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/common.hpp)
- [`include/zlink/types.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/types.hpp)
- [`include/zlink/error.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/error.hpp)
- [`include/zlink/context.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/context.hpp)
- [`include/zlink/message.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/message.hpp)

해야 할 일:

- enum/type 정의를 최신 `core`에 맞춘다.
- 삭제된 message helper를 제거한다.
- `message_t`의 copy/move/refcnt 계약을 고정한다.
- `message_t`의 bytes/string 변환 API를 추가한다.
- 공통 string/routing-id helper를 추가한다.

끝났다고 볼 조건:

- 공통 헤더만 포함하는 compile smoke가 통과한다.

### Slice 2. raw socket / monitor / poller

대상:

- [`include/zlink/socket.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/socket.hpp)
- [`include/zlink/monitor.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/monitor.hpp)
- [`include/zlink/poller.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/poller.hpp)
- [`include/zlink/runtime.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/runtime.hpp)

해야 할 일:

- `send`/`recv` overload 중심 송수신으로 재작성한다.
- callback/send-ready/monitor API를 최신 계약으로 맞춘다.
- stream 전용 old helper를 제거한다.
- poller를 generic API만 남기도록 단순화한다.

끝났다고 볼 조건:

- raw socket 샘플 8종이 전부 빌드된다.
- monitor/option contract test가 통과한다.

### Slice 3. 서비스 계층

대상:

- [`include/zlink/services/registry.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/services/registry.hpp)
- [`include/zlink/services/discovery.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/services/discovery.hpp)
- [`include/zlink/services/spot.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/services/spot.hpp)
- 신규 `include/zlink/service_monitor.hpp`
- 신규 `include/zlink/services/query.hpp`

해야 할 일:

- `registry_t`를 bind/snapshot/query 모델로 전환한다.
- `discovery_t`를 fixed service-view 모델로 전환한다.
- `spot_node_t`, `spot_t`를 unified 최신 모델로 재작성한다.
- [`include/zlink/service_monitor.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/service_monitor.hpp)
  와 [`include/zlink/services/query.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/services/query.hpp)
  를 추가한다.

끝났다고 볼 조건:

- `spot_recv`, `spot_callback` 샘플이 모두 빌드되고 `sample-smoke`에 포함된다.
- service contract test가 통과한다.

### Slice 4. compat / umbrella / 문서 표면

대상:

- [`include/zlink/compat.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/compat.hpp)
- [`include/zlink.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink.hpp)
- [`API_DRAFT.md`](/home/hep7/project/kairos/zlink/bindings/cpp/API_DRAFT.md)
- [`doc/bindings/cpp.ko.md`](/home/hep7/project/kairos/zlink/doc/bindings/cpp.ko.md)
- [`doc/bindings/cpp.md`](/home/hep7/project/kairos/zlink/doc/bindings/cpp.md)

해야 할 일:

- `compat.hpp`를 축소한다.
- umbrella header에 새 헤더를 연결한다.
- 문서와 예제를 새 surface 기준으로 바꾼다.

끝났다고 볼 조건:

- umbrella header include가 단독 compile smoke를 통과한다.
- 문서 예제와 샘플 이름이 일치한다.

### Slice 5. 샘플 / contract test

대상:

- `bindings/cpp/samples/**`
- `bindings/cpp/tests/**`
- [`CMakeLists.txt`](/home/hep7/project/kairos/zlink/bindings/cpp/CMakeLists.txt)

해야 할 일:

- 샘플 디렉토리를 신설한다.
- contract test를 최소 세트로 재작성한다.
- sample-smoke/contract 라벨을 정리한다.

끝났다고 볼 조건:

- `contract` 라벨 테스트가 모두 통과한다.
- 전체 sample smoke가 통과한다.

현재 진행 메모:

- `samples/` 10종, `tests/contract/` 최소 세트, `sample-smoke`/`contract`
  라벨 구조는 반영됐다.
- unified `spot_t` self-delivery는 sub monitor의 `spot_filter_applied`와
  pub monitor snapshot의 `ZLINK_MONITOR_STATE_READY`를 함께 확인한 뒤
  `recv`/`callback` 경로를 진행하도록 정리했다.
- 대응 확인용으로 `core/tests/e2e/spot/test_spot_service_introspection.cpp`에
  `test_spot_unified_spot_callback_self_delivery` 회귀를 추가했고, 이 C API
  회귀는 반복 실행을 통과한다.
- `test_cpp_contract_service`에 unified `spot_t` self-delivery recv 계약을
  추가했고, `test_cpp_contract_callback_mode`,
  `sample_cpp_spot_recv`, `sample_cpp_spot_callback`는 각 20회 반복 실행을
  통과했다.
- 최종 검증으로 `./bindings/cpp/build.sh ON ON`,
  `ctest --test-dir core/build -L contract --output-on-failure`,
  `ctest --test-dir core/build -L sample-smoke --output-on-failure -j1`,
  전체 샘플 10종 수동 실행이 모두 통과했다.

## 10. 리스크

- `doc/api` 일부 문서가 낡아 있어 문서만 보고 구현하면 다시 어긋날 수 있다.
- 서비스 계층은 개념 변경 폭이 커서 단순 rename으로 끝나지 않는다.
- 편의 API를 많이 유지하려고 하면 오히려 최신 `core`의 multipart/monitor/service
  모델을 흐리게 만든다.
- 기존 테스트를 얼마나 과감히 삭제할지 기준이 흔들리면 구조가 다시 비대해질 수 있다.
- 샘플만으로 correctness를 보장하려 하면 wrapper 계약 회귀를 놓칠 수 있다.
- `bindings/cpp/build.sh` 인터페이스를 문서와 같이 갱신하지 않으면 기본 검증 경로가
  샘플/contract 분리를 반영하지 못한다.

## 11. 완료 기준

다음이 모두 만족되면 작업 완료로 본다.

- `bindings/cpp/include`가 최신 `core/include/zlink.h` 기준으로 컴파일된다.
- C++ 헤더에서 최신 `core`에 없는 symbol reference가 제거된다.
- raw socket, monitor, service monitor, registry/discovery/spot의 최신 핵심
  기능을 C++에서 사용할 수 있다.
- 샘플이 주요 패턴별 `recv`/`callback` 사용법을 제공한다.
- contract test가 바인딩 전용 계약만 검증한다.
- 바인딩 문서와 예제가 최신 surface를 설명한다.

## 12. 구현 완료 시 검증 절차

구현이 끝나면 최소한 아래 순서로 검증한다.

1. `./bindings/cpp/build.sh ON ON`
2. `ctest --test-dir core/build -L contract --output-on-failure`
3. `ctest --test-dir core/build -L sample-smoke --output-on-failure -j1`
4. 전체 샘플 수동 실행

수동 실행 대상:

- `sample_cpp_pair_recv`
- `sample_cpp_pair_callback`
- `sample_cpp_pubsub_recv`
- `sample_cpp_pubsub_callback`
- `sample_cpp_dealer_router_recv`
- `sample_cpp_dealer_router_callback`
- `sample_cpp_stream_recv`
- `sample_cpp_stream_callback`
- `sample_cpp_spot_recv`
- `sample_cpp_spot_callback`

검증 결과 기록 형식:

- 빌드 성공 여부
- contract test pass/fail
- sample smoke pass/fail
- 전체 샘플 수동 실행 pass/fail

## 13. 실행 순서 요약

실제 착수 순서는 아래가 가장 안전하다.

1. symbol mapping 문서화
2. `types.hpp`, `message.hpp`, `context.hpp`
3. `socket.hpp`
4. `monitor.hpp`, `poller.hpp`, `runtime.hpp`
5. `services/registry.hpp`, `services/discovery.hpp`, `services/spot.hpp`
6. `compat.hpp`
7. 샘플/contract test 재편
8. 문서 정리

## 14. 첫 구현 배치 제안

작업을 한 번에 크게 묶지 말고, 아래 3개 배치로 나누는 것이 좋다.

### Batch 1

- 기반 타입
- `socket_t`
- monitor/poller/runtime

목표:

- 최신 `core` 기준으로 C++ 공통 계층을 먼저 살린다.

### Batch 2

- registry/discovery/spot
- service monitor
- snapshot/query

목표:

- 최신 서비스 계층 모델을 C++에서 자연스럽게 노출한다.

### Batch 3

- 샘플 작성
- contract test 최소화
- 문서/예제 갱신
- `compat.hpp` 최종 축소

목표:

- 남은 구형 개념을 정리하고, 사용자-facing 샘플과 최소 contract 검증 체계를 닫는다.

## 15. 결론

이 작업은 "몇 개 함수명 치환" 수준이 아니라, 현재 `bindings/cpp`가 아직 들고 있는
구형 서비스/송수신/option 모델을 최신 `core` public surface 중심으로 다시
재조립하는 작업이다.

따라서 가장 중요한 원칙은 두 가지다.

- 최신 `core/include/zlink.h`에 없는 개념은 C++에서도 과감히 정리할 것
- convenience는 유지하더라도 최신 multipart/service contract 위에만 올릴 것

이 원칙을 지키면 change amplification을 줄이면서도, 이후 다른 바인딩과의 정렬이
더 쉬운 C++ surface를 만들 수 있다.
