# C++ Wrapper Plan (cppzmq-style)

## 목표
- 코어 C API를 얇게 감싸는 **header-only C++ 래퍼** 제공
- 예외 없이 **에러 코드 기반** 동작(필요 시 opt-in 예외)
- C++11 최소 요구(코어 스타일과 충돌 최소화)
- ABI 영향 최소화(템플릿/inline 중심)

---

## 설계 원칙
- **Thin wrapper**: 함수 호출과 타입 안전성만 제공
- **RAII**: context/socket/message의 생명주기 자동 관리
- **No hidden threads**: 추가 스레드/큐 도입 금지
- **Explicit ownership**: msg 이동/복사 규칙 명확화
- **Zero-cost when possible**: 인라인과 move로 오버헤드 최소화

---

## 디렉토리 구조
- `bindings/cpp/include/zlink.hpp`  // 메인 헤더
- `bindings/cpp/include/zlink_addon.hpp`  // 선택적 헬퍼
- `bindings/cpp/tests/`  // 바인딩 테스트
- `bindings/cpp/examples/`  // 샘플
- `bindings/cpp/CMakeLists.txt`

---

## API 범위 (전체 구현)
### 핵심 타입
- `zlink::context_t`
- `zlink::socket_t`
- `zlink::message_t`
- `zlink::poller_t` (가능하면 C API poller 그대로 래핑)

### 함수군
- context: create/terminate/setsockopt/getsockopt
- socket: bind/connect/send/recv/close
- message: init/init_size/init_data/copy/move
- monitor/events: 최소 래핑

상세 API 초안은 `bindings/cpp/API_DRAFT.md` 참조.

### 제외
- experimental APIs

---

## API 디자인 상세
### 1) context_t
- 생성: `context_t(int io_threads = ZLINK_IO_THREADS_DFLT)`
- 소멸 시 `zlink_ctx_term` 호출
- 복사 금지, move 허용

### 2) socket_t
- 생성: `socket_t(context_t&, int type)`
- 소멸 시 `zlink_close`
- move만 허용
- `bind`, `connect`는 `std::string`/`const char*` 모두 제공
- `send`/`recv` 오버로드:
  - raw buffer
  - `message_t`
  - `std::string` (copy)

### 3) message_t
- C msg와 1:1 매핑
- move-only 또는 move+copy 지원(복사 시 deep-copy)
- `data()`, `size()`, `more()` 제공

### 4) error handling
- 기본은 **에러 코드 반환**
- `zlink_error_t` 스타일 헬퍼 제공
- opt-in 예외는 `ZLINK_CPP_EXCEPTIONS` 플래그로 토글

---

## CMake/배포 전략
- header-only → 설치는 `include/`만
- `bindings/cpp` 단독 빌드 가능하도록 구성
- 코어 빌드 옵션과 분리되되, CI에서 헤더 유효성 검사

---

## 테스트 계획
- Core C API 테스트와 동일한 패턴 재사용
- 최소 테스트:
  - context/socket 생성/종료
  - send/recv 기본 시나리오 (PAIR/DEALER/ROUTER/STREAM)
  - message move/copy
  - poller 기본 동작

---

## 구현 계획
- 기본 RAII 클래스/헤더 구조
- context/socket/message 타입 정의
- send/recv 오버로드
- setsockopt/getsockopt 래핑
- poller
- monitor 이벤트
- TLS 옵션 헬퍼
- examples 추가
- 바인딩 전용 README

---

## 리스크/주의
- API 이름 충돌 방지(네임스페이스 `zlink` 고정)
- 성능 회귀 방지(불필요한 복사 금지)
- ABI 안정성: header-only이지만 템플릿 남발 금지
