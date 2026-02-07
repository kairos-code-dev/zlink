# C++ Binding Tests

## 빌드 옵션
- 루트 CMake에서:
  - `-DZLINK_BUILD_CPP_BINDINGS=ON`
  - `-DZLINK_CPP_BUILD_TESTS=ON`
  - `-DZLINK_CPP_BUILD_EXAMPLES=ON` (예제 빌드)

## 예시
```bash
./bindings/cpp/build.sh ON
ctest --test-dir bindings/cpp/build --output-on-failure -R test_cpp_
```

## 테스트 목록
- `test_cpp_basic`: context/socket/message/poller 기본 동작
- `test_cpp_spot`: spot publish/recv 스모크

## 예제 목록
- `cpp_pair_basic`
- `cpp_dealer_router`
- `cpp_spot_basic`
- `cpp_monitor_basic`
