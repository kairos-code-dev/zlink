# ASIO tcp::socket 통합 리팩토링 계획

## 목표
- 하이브리드 접근으로 Windows/Unix 코드 통합
- TCP는 `boost::asio::ip::tcp::socket` 사용, 비-TCP FD는 `stream_descriptor` 유지
- `tcp::socket::async_wait` 기반 reactor 패턴 유지
- read/write syscall 경유 제거로 성능 개선

## 단계별 구현 계획

### 1) 현행 구조 파악 및 의존성 정리
- **대상 파일**
  - `src/asio/asio_poller.cpp`
  - `src/asio/asio_poller.hpp`
  - `src/asio/tcp_transport.cpp`
  - `src/asio/tcp_transport.hpp`
- **작업 내용**
  - WSAPoll 경로와 Unix `stream_descriptor` 사용 경로를 분리한 `#ifdef` 구간
    정리 대상 목록화.
  - `poll_entry_t`가 보유한 descriptor 타입/라이프사이클 파악.
  - `tcp_transport`가 `stream_descriptor`를 구성하는 위치와 연결 시점 정리.
- **예상 문제**
  - 플랫폼별 다른 핸들/소켓 타입 추상화가 누락되어 있을 수 있음.
- **해결 방법**
  - `poll_entry_t`에 소켓/FD 참조를 포함시켜 플랫폼별 타입 분기 제거.
  - `boost::asio::ip::tcp::socket::native_handle_type` 사용 지점을
    명확히 표준화.

### 2) poll_entry_t 구조 통합
- **대상 파일**
  - `src/asio/asio_poller.hpp`
- **작업 내용**
  - `poll_entry_t`에 `variant<tcp::socket*, stream_descriptor>` 형태로
    엔트리 타입 통합.
  - `async_wait`에 필요한 이벤트/상태 값을 플랫폼 공통으로 유지.
  - 기존 `WSAPoll` 관련 상태(Windows 전용 플래그/데이터) 제거.
- **예상 문제**
  - 소켓/FD 소유권 및 수명 관리가 불명확해질 위험.
- **해결 방법**
  - `tcp_transport`에서 소켓 객체를 소유하고,
    `poll_entry_t`는 비소유 포인터만 유지.
  - FD용 `stream_descriptor`는 poller 내부에서 소유/관리.
  - 소켓 해제 시 `poll_entry_t` 정리/무효화 경로 명시.

### 3) asio_poller 통합 리팩토링
- **대상 파일**
  - `src/asio/asio_poller.cpp`
  - `src/asio/asio_poller.hpp`
- **작업 내용**
  - `add_fd(fd_t)` 인터페이스 유지 (`stream_descriptor` 경유).
  - TCP 전용 등록용 `add_socket(tcp::socket*)` 또는 `add_tcp_socket()` 추가.
  - `poll_entry_t`의 타입에 따라 `async_wait` 호출 경로 분기.
  - Windows `WSAPoll()` 호출 경로 제거.
  - 플랫폼 조건부 분기 제거 및 공통 처리 흐름 정리.
- **예상 문제**
  - Windows에서 `async_wait`의 이벤트 전달 타이밍이
    기존 `WSAPoll`과 미묘하게 다를 수 있음.
- **해결 방법**
  - `async_wait` 완료 핸들러에서 기존 poller 상태 전이 로직을
    그대로 재사용하도록 래핑.
  - 기존 Windows 경로의 이벤트 마스킹/필터 규칙을 공통 규칙으로 이식.

### 4) tcp_transport 통합 리팩토링
- **대상 파일**
  - `src/asio/tcp_transport.cpp`
  - `src/asio/tcp_transport.hpp`
- **작업 내용**
  - `stream_descriptor` 제거 대신, TCP 외 FD용으로 유지.
  - `tcp::socket`을 직접 소유하고, read/write 경로가
    소켓 API를 사용하도록 변경.
  - `asio_poller` 등록 시 `add_socket()` 경유로 통일.
- **예상 문제**
  - 기존 FD 기반 라이프사이클과 TCP 소켓 경로가
    혼재되어 종료 순서/에러 처리 차이 발생 가능.
- **해결 방법**
  - 소켓 종료/에러 처리 경로를 명시적으로 테스트하고
    종료 순서(transport -> poller) 정리.
  - `native_handle()` 사용 구간 최소화.

### 5) 성능 경로 정리 및 계측
- **대상 파일**
  - `src/asio/tcp_transport.cpp`
  - `perf/` 또는 기존 성능 측정 스크립트
- **작업 내용**
  - read/write 경로가 `tcp::socket` 경유로 수행되는지 점검.
  - 필요한 경우 간단한 측정 로그(빌드 플래그 기반) 추가.
- **예상 문제**
  - 기존 측정 기준과 비교가 어려울 수 있음.
- **해결 방법**
  - 동일 테스트 케이스/환경에서 전후 비교값을 기록.
  - 결과를 `docs/team/20260116_asio-tcp-socket-integration/plan.md`
    하단에 업데이트.

### 6) 테스트 및 검증
- **대상**
  - `tests/test_*.cpp`, `unittests/unittest_*.cpp`
  - 기존 플랫폼별 테스트 시나리오
- **작업 내용**
  - Windows/Unix 공통 경로로 기본 통신/에러 테스트 수행.
  - 접속 해제, 타임아웃, 대량 메시지 등 시나리오 확인.
- **예상 문제**
  - 플랫폼별 이벤트 처리 차이로 타이밍 의존 테스트 실패 가능.
- **해결 방법**
  - 타이밍 의존 로직 완화 또는 재시도/대기 조건 명확화.

## 변경 포인트 요약 (파일별)
- `src/asio/asio_poller.hpp`
  - `poll_entry_t`를 `variant<tcp::socket*, stream_descriptor>` 구조로 통합.
  - Windows 전용 WSAPoll 관련 필드 제거.
  - `add_socket()` 또는 `add_tcp_socket()` 추가.
- `src/asio/asio_poller.cpp`
  - `add_fd()`는 `stream_descriptor` 경유로 유지.
  - `add_socket()` 추가 및 타입 분기 기반 `async_wait` 처리.
  - `WSAPoll()` 경로 제거.
- `src/asio/tcp_transport.hpp/.cpp`
  - `stream_descriptor`는 TCP 외 FD용으로 유지.
  - `tcp::socket` 소유/전달 및 소켓 API 기반 read/write로 전환.

## 리스크 및 대응
- **이벤트 처리 차이**: Windows/Unix 이벤트 시점 차이로 상태 전이 문제 발생 가능
  - 대응: 공통 이벤트 마스킹 규칙 정의, 회귀 테스트 추가
- **소켓 수명 관리**: poller와 transport의 소켓 접근 순서 충돌 가능
  - 대응: 소유권 명확화 및 소켓 종료 경로 정리
- **성능 회귀**: `async_wait` 경로 설정이 비효율적으로 유지될 가능성
  - 대응: perf 측정 후 핫패스 최소화 및 불필요한 wait 제거
- **signaler 호환성**: eventfd/pipe/socketpair FD 등록 실패 위험
  - 대응: `add_fd()`와 `stream_descriptor` 유지로 호환성 확보
