# ZMQ -> ZLINK 네이밍 전환 계획 (호환성 제거)

목표: 기존 `zmq`/`ZMQ`/`ZeroMQ` 관련 명칭을 전부 `zlink`/`ZLINK`로 변경하고
**호환성은 제공하지 않음**.

---

## 0) 원칙

- **호환성 없음**: `zmq_*`, `ZMQ_*`, `zmq.h`, `libzmq.so` 등은 모두 제거 대상
- **전면 치환**: 코드/스크립트/문서 어디든 `zmq` 관련 문자열은 `zlink`로 통일
- **단계별 안정화**: 대량 rename 후 빌드/테스트로 단계별 확인

---

## 1) 변경 범위

### 1.1 Public API (C)
- 함수: `zmq_*` -> `zlink_*`
- 타입: `zmq_*_t` -> `zlink_*_t`
- 매크로/옵션: `ZMQ_*` -> `ZLINK_*`
- 에러 코드/상수/소켓 옵션 전부 `ZLINK_`로 치환

### 1.2 C++ 바인딩
- 네임스페이스: `zmq::` -> `zlink::`
- 래퍼 타입/함수명 동일 치환

### 1.3 헤더/파일명
- `include/zmq.h` -> `include/zlink.h`
- `include/zmq_utils.h` -> `include/zlink_utils.h`
- `include/zmq_threadsafe.h` -> `include/zlink_threadsafe.h`
- 관련 소스 파일명/참조 경로 전부 갱신

### 1.4 라이브러리/빌드/패키징
- 라이브러리명: `libzmq` -> `libzlink`
- SONAME/출력 파일명/버전 파일 갱신
- pkg-config: `libzmq.pc` -> `libzlink.pc` (패키지명도 `libzlink`)
- CMake 패키지: `ZeroMQConfig.cmake` -> `ZlinkConfig.cmake`
- CMake 옵션: `ZMQ_*` -> `ZLINK_*`
- 배포/패키징 파일명 및 spec/nuget/obs 정리

### 1.5 테스트/벤치/문서/CI
- 테스트/벤치 코드에서 `zmq` 제거
- 스크립트/CI 워크플로우 이름과 경로 갱신
- 문서/README/예제 코드 전부 `zlink` 기준으로 갱신

---

## 2) 작업 단계

### Phase 1: Public API/헤더/소스 정리
1. 헤더 파일명 변경 및 include 경로 갱신
2. C API 심볼/타입/매크로 전면 rename
3. C++ 래퍼 네임스페이스/타입명 rename
4. export 리스트, version script, .pc 파일 갱신

검증:
- `cmake --build build` 컴파일 통과

---

### Phase 2: 빌드 시스템/패키징 전환
1. CMake 타겟/패키지명/설치 경로 수정
2. `pkg-config` 및 CMake Config 파일명 변경
3. 플랫폼별 빌드 스크립트/패키징(spec/nuget/obs) 갱신

검증:
- 설치 후 `pkg-config --libs libzlink` 정상 동작
- CMake `find_package(Zlink)` 동작

---

### Phase 3: 테스트/벤치/문서/CI 정리
1. 테스트/벤치 코드 전면 rename
2. 벤치 baseline 디렉토리/스크립트 경로 수정
3. 문서/예제/README 업데이트
4. CI 워크플로우 변경 반영

검증:
- `ctest --output-on-failure` 통과
- 벤치 스크립트 최소 1회 실행 확인

---

## 3) 리스크 및 대응

- **대량 rename 누락**
  - `rg -n \"\\bzmq\\b|\\bZMQ_\\b|ZeroMQ|libzmq\"`로 전수 탐색
- **외부 호환성 완전 파괴**
  - 호환성 미제공 방침 명확히 문서화
- **패키징/CI 경로 깨짐**
  - 빌드 스크립트와 워크플로우에서 경로 점검

---

## 4) 완료 기준 체크리스트

- [ ] `include/zlink.h` 사용, `include/zmq.h` 제거
- [ ] `libzlink.so` 생성, `libzmq.so` 없음
- [ ] 코드/문서에 `zmq`/`ZMQ_`/`ZeroMQ` 문자열 없음
- [ ] 테스트/벤치/CI 동작 확인

---

## 5) 권장 검증 명령

- 탐색: `rg -n \"\\bzmq\\b|\\bZMQ_\\b|ZeroMQ|libzmq\"`
- 빌드: `cmake -B build -DZLINK_BUILD_TESTS=ON`
- 컴파일: `cmake --build build`
- 테스트: `ctest --output-on-failure`
