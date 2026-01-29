# ZMQ -> ZLINK 네이밍 전환 계획 (호환성 제거)

> 목표: 기존 `zmq` 명칭을 전부 `zlink`로 치환하고 **호환성은 제공하지 않음**.
> 범위: Public API/ABI, 빌드/패키징, 테스트/벤치/문서, 내부 네임스페이스/파일명.

---

## 0) 원칙

- **호환성 없음**: `zmq_*`, `ZMQ_*`, `zmq.h`, `libzmq.so`는 제거 대상
- **전면 치환**: 코드/스크립트/문서 어디든 `zmq` 문자열이 있으면 `zlink`로 교체
- **단계별 컴파일 안정화**: 큰 변경이므로 단계별 빌드/테스트로 안정화

---

## 1) 변경 범위 (정리)

### 1.1 Public API (C)
- 함수: `zmq_*` -> `zlink_*`
- 타입: `zmq_*_t` -> `zlink_*_t`
- 매크로: `ZMQ_*` -> `ZLINK_*`
- 에러/상수/옵션: 전부 `ZLINK_`로 치환

### 1.2 헤더/심볼
- `include/zmq.h` -> `include/zlink.h`
- `include/zmq_utils.h` 등 존재 시 `zlink_*`로 교체
- exported symbol 파일(있다면)도 변경

### 1.3 라이브러리/패키징
- `libzmq.so` -> `libzlink.so`
- `pkg-config` 파일: `libzmq.pc` -> `zlink.pc`
- CMake targets: `libzmq`, `libzmq-static` -> `libzlink`, `libzlink-static`
- 설치 경로/SONAME/버전 파일 전부 업데이트

### 1.4 내부 코드
- 네임스페이스 `zmq::` -> `zlink::`
- 파일명/디렉토리명 중 `zmq` 포함 시 변경
  - 예: `src/api/zmq.cpp` -> `src/api/zlink.cpp` 등
- 내부 상수/매크로 `ZMQ_` 접두어 제거

### 1.5 테스트/벤치/문서/스크립트
- `tests/`, `unittests/`, `bench*`, `build-scripts/` 전부 치환
- 예제/문서: `zmq` 관련 문구 전면 수정
- 환경 변수: `ZMQ_*` -> `ZLINK_*`
- CI/GitHub Actions 워크플로우(`.github/workflows/*.yml`) 내
  - `libzmq`, `zmq.h`, `ZMQ_*`, `pkg-config libzmq` 참조 전부 변경

---

## 2) 상세 작업 단계

### Phase 1: Public API/헤더 전환
1. `include/zlink.h` 생성 및 **기존 `zmq.h` 제거**
2. 헤더 안의 모든 `ZMQ_*` 상수 -> `ZLINK_*`
3. C API 함수/타입 전부 rename
4. `src/api/` 구현 함수명 전면 변경
5. 빌드 깨짐 확인 후 1차 컴파일 통과

**검증**
- `cmake --build build` (compile OK)

---

### Phase 2: 라이브러리/패키징 전환
1. CMake 타겟명 변경 (`libzlink`, `libzlink-static`)
2. SONAME/출력 파일명 변경
3. `pkg-config` 파일/설치 규칙 변경
4. `find_package` 관련 파일명 변경

**검증**
- `ldd`/`nm`로 `libzlink.so` 심볼 확인
- 설치 후 `pkg-config --libs zlink` 확인

---

### Phase 3: 내부 네임스페이스/파일명 정리
1. `namespace zmq` -> `namespace zlink` (전체 코드)
2. 관련 using, 클래스명, include 경로 수정
3. `src/api/zmq*.cpp` 등 파일명 변경
4. `benchwithzmq` 등 디렉토리 재명명

**검증**
- 전체 빌드 및 링크 성공

---

### Phase 4: 테스트/벤치/문서/스크립트 치환
1. 테스트 코드 전부 `ZLINK_*`로 변경
2. 벤치 스크립트, run scripts 업데이트
3. 문서(README/plan/rfc) 내 `zmq` 언급 치환
4. GitHub Actions 워크플로우 갱신
5. 외부 참조(벤치 baseline) 경로 정리

**검증**
- `ctest --output-on-failure` 통과
- 벤치 스크립트 실행 확인

---

## 3) 리스크 및 대응

- **대량 rename로 인한 누락**
  - `rg -n "\bzmq\b|\bZMQ_"`로 전체 탐색, CI 검사 추가
- **ABI/링커 문제**
  - 기존 `libzmq` 심볼 의존 라이브러리는 모두 깨짐
- **외부 문서/스크립트 영향**
  - 배포 전 전수 점검 필요

---

## 4) 변경 체크리스트 (완료 기준)

- [ ] `include/zlink.h` 존재, `include/zmq.h` 제거
- [ ] `libzlink.so` 생성, `libzmq.so` 없음
- [ ] 모든 `zmq_*`, `ZMQ_*` 제거
- [ ] 테스트/벤치 전부 `zlink` 명칭 사용
- [ ] 문서 업데이트 완료

---

## 5) 실행 순서 제안

1) Phase 1 (Public API) -> 2) Phase 2 (라이브러리) -> 3) Phase 3 (내부) -> 4) Phase 4 (테스트/문서)

---

## 6) 작업 도구 제안

- 탐색: `rg -n "\bzmq\b|\bZMQ_"`
- 일괄 rename: `rg` + `perl -pi -e` 또는 `apply_patch`
- 빌드 검증: `cmake --build build -j 8`
- 테스트: `ctest --output-on-failure`
