# C++ Bindings Testing

## 기본 빌드

```bash
./bindings/cpp/build.sh ON ON
```

루트 CMake 직접 호출 시 필요한 옵션:

- `-DZLINK_BUILD_CPP_BINDINGS=ON`
- `-DZLINK_CPP_BUILD_TESTS=ON`
- `-DZLINK_CPP_BUILD_SAMPLES=ON`

## 검증 순서

```bash
ctest --test-dir core/build --output-on-failure -L contract
ctest --test-dir core/build --output-on-failure -L sample-smoke -j1
```

## 검증 자산

- `tests/contract/`: 바인딩 전용 계약 검증
- `samples/`: 사용자-facing 사용 예제 + `sample-smoke` 실행 엔트리

## 타깃 규칙

- contract test 이름: `test_cpp_contract_<topic>`
- sample target 이름: `sample_cpp_<pattern>_<mode>`
- CTest 라벨:
  - `contract`
  - `sample-smoke`

## 공통 정책

- 모든 sample/test 실행 파일은 `-UNDEBUG`로 빌드되어 `assert` 검증이 유지된다.
- CTest 타임아웃은 테스트별 `120s`다.

## API 레퍼런스 생성

```bash
cd bindings/cpp
doxygen Doxyfile
```

생성 결과:

- `bindings/cpp/doxygen/html/index.html`
- `cmake --build core/build --target zlink_cpp_api_docs`
