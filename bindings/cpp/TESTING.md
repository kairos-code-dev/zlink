# C++ Binding Tests

## 빌드 옵션
- 루트 CMake에서:
  - `-DZLINK_BUILD_CPP_BINDINGS=ON`
  - `-DZLINK_CPP_BUILD_TESTS=ON`

## 예시
```bash
./bindings/cpp/build.sh ON
ctest --test-dir bindings/cpp/build --output-on-failure -R test_cpp_
```

## 테스트 실행 정책
- `test_cpp_core_*` 타깃은 `-UNDEBUG`로 빌드되어 `assert` 검증이 항상 활성화됨.
- CTest 타임아웃은 테스트별 `120s`로 설정됨.

## Core 포팅 상태 (`core/tests` 기준)
- 총 `79`개 중 `64`개 포팅됨 (`test_cpp_core_*`).
- 미포팅(포팅 불가/환경 의존) `15`개:
  - `asio_connect`, `asio_poller`, `asio_ssl`, `asio_tcp`, `asio_ws`
  - `bind_fuzzer`, `bind_null_fuzzer`, `bind_ws_fuzzer`
  - `connect_fuzzer`, `connect_null_fuzzer`, `socket_options_fuzzer`, `z85_decode_fuzzer`
  - `pair_tcp_cap_net_admin`
  - `pgm_epgm`, `pgm_smoke`

## 포팅 이슈 명시
- `test_cpp_core_spot_pubsub_scenario`:
  - 현재는 스모크 수준으로 유지.
  - full 시나리오(`zlink_spot_pub_publish_bytes`/`zlink_spot_sub_recv` polling 기반) 직접 포팅 시
    바인딩 테스트 프로세스에서 hang가 재현됨.
  - 동일 계열 C 코어 테스트는 통과하므로 바인딩 스택/호출 경로 이슈 후보로 추적 필요.

## API 레퍼런스 생성
```bash
cd bindings/cpp
doxygen Doxyfile
```

생성 결과:
- `bindings/cpp/doxygen/html/index.html`
- (CMake 타깃) `cmake --build bindings/cpp/build --target zlink_cpp_api_docs`
