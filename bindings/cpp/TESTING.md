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

## 테스트 목록
- `test_cpp_basic`: context/socket/message/poller 기본 동작
- `test_cpp_spot`: spot publish/recv 스모크
