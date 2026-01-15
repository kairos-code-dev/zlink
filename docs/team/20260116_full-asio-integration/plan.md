# 완전한 ASIO 네이티브 아키텍처 전환 계획

## 배경 요약
- 현재 ZMQ는 signaler(eventfd/socketpair)를 통해 스레드 간 통신을 수행함.
- asio_poller는 Windows(WSAPoll)와 Unix(stream_descriptor)로 분리되어 있음.
- tcp_transport는 Windows(tcp::socket)와 Unix(stream_descriptor)로 분리되어 있음.

## 목표
1) Mailbox/Signaler ASIO 전환
- signaler(eventfd/socketpair) 제거
- io_context::post() 기반의 스레드 간 통신으로 전환
- add_fd() 기반 등록 제거

2) Stream_descriptor 완전 제거
- TCP 전용 tcp::socket만 사용
- poll_entry_t 단순화

3) Platform #ifdef 완전 제거
- Windows WSAPoll 제거
- Unix stream_descriptor 제거
- 모든 플랫폼 동일 코드 경로

4) Poller 간소화
- add_fd(fd_t) 인터페이스 변경 또는 제거
- TCP 소켓만 처리하는 단순 구조

## 핵심 설계 방향
- 모든 스레드 간 통신은 asio::io_context의 작업 큐로 통합한다.
- 모든 네트워크 이벤트는 tcp::socket 기반 async I/O로 처리한다.
- polling API(WSAPoll/epoll/stream_descriptor)는 완전히 제거한다.
- 플랫폼 분기 코드는 제거하고, 동일한 코드 경로를 유지한다.

## 상세 구현 계획

### 1) mailbox.hpp/cpp를 io_context 기반으로 재작성

#### 목표 동작
- 기존 signaler 기반 "깨우기" 메커니즘 대신 io_context::post()를 사용한다.
- mailbox는 "큐에 명령을 넣고, io_context 스레드에 처리 작업을 스케줄"하는 역할만 유지한다.

#### 설계 접근
- mailbox는 내부적으로 lock-free 또는 mutex 기반 큐를 유지한다.
- `send(command)` 수행 시:
  - 큐에 command를 push
  - `io_context::post()`로 `process_mailbox()`를 등록
- `process_mailbox()`는 큐를 drain하여 `process_command()`를 호출한다.

#### 중복 스케줄 방지
- post 호출 남발을 방지하기 위해 guard 플래그를 둔다.
- 예시:
  - `atomic<bool> scheduled`
  - send()에서 `if (!scheduled.exchange(true)) post(process_mailbox)`
  - process_mailbox() 끝에서 `scheduled.store(false)` 후, 큐가 남아있으면 다시 post

#### 예상 문제점
- 큐 처리 시점이 정확히 thread wakeup과 동일하지 않음.
- 과도한 post 호출로 스케줄 폭주 가능성.
- io_context가 stop 상태이면 post된 작업이 실행되지 않을 수 있음.

#### 보완
- stop/shutdown 시점에 mailbox를 명시적으로 drain 처리.
- post 대신 dispatch를 사용할지 검토(동일 스레드일 경우 즉시 실행).

---

### 2) io_thread.cpp, reaper.cpp의 mailbox 등록 방식 변경

#### 기존 구조
- io_thread/reaper는 signaler의 fd를 poller에 등록하고 wakeup을 감지함.

#### 변경 방향
- mailbox는 io_context 기반이므로 poller 등록이 사라진다.
- io_thread/reaper는 "mailbox 이벤트"가 아니라 "mailbox post"에 의해 직접 수행된다.

#### io_thread 변경 요약
- `add_fd(mailbox.get_fd())` 관련 코드 삭제.
- io_thread의 run loop는 io_context.run() 중심으로 단순화.
- mailbox 생성 시 io_context 포인터를 전달하여 post 사용.

#### reaper 변경 요약
- reaper 역시 동일한 io_context 기반 mailbox를 사용한다.
- 기존 폴링 루프 내 mailbox 감시 로직 제거.

#### 예상 문제점
- shutdown 시점에 reaper의 drain 순서가 변경됨.
- 기존 "폴링 기반"에서 "post 기반"으로 전환될 때 타이밍 차이가 생김.

---

### 3) asio_poller에서 stream_descriptor 제거

#### 목표
- 모든 소켓 감시는 `tcp::socket` 기반으로 통합.
- poll_entry_t는 TCP 전용으로 단순화.

#### 변경 방향
- stream_descriptor 관련 멤버 삭제.
- poll_entry_t는 `tcp::socket`과 관련된 상태만 유지.
- read/write 이벤트는 async_read/async_write 콜백으로 구현.

#### 예상 문제점
- 기존 fd 기반 모니터링 삭제 시, 일부 내부 컴포넌트가 fd를 요구할 수 있음.
- tcp_transport 통합 단계와 동시 진행 필요.

---

### 4) asio_poller에서 WSAPoll 제거

#### 목표
- Windows 전용 WSAPoll 경로 제거.
- asio_poller는 io_context 기반 async I/O만 사용.

#### 변경 방향
- #ifdef ZMQ_HAVE_WINDOWS 내 WSAPoll 코드 제거.
- 공통 async 기반 코드만 유지.

#### 예상 문제점
- 기존 WSAPoll 기반 timeout 처리 흐름이 사라짐.
- 타임아웃 또는 idle 처리 로직을 다른 위치로 이관 필요.

---

### 5) tcp_transport Unix 경로를 tcp::socket로 통합

#### 목표
- Unix에서 stream_descriptor 사용 제거.
- tcp_transport는 모든 플랫폼에서 tcp::socket만 사용.

#### 변경 방향
- unix 경로의 stream_descriptor 기반 코드 제거.
- tcp::socket 기반 read/write 경로를 공통화.
- 소켓 옵션 설정, connect/accept flow를 단일 경로로 정리.

#### 예상 문제점
- 기존 Unix 경로의 low-level fd 최적화가 제거되며, 성능 영향 가능.
- 윈도우/유닉스 동작 차이(에러 코드, 소켓 옵션) 정리 필요.

---

### 6) 모든 #ifdef ZMQ_HAVE_WINDOWS 제거

#### 목표
- 공통 코드 경로로 통합.
- asio_poller, tcp_transport, mailbox 모두 동일 코드.

#### 변경 방향
- Windows 전용 분기 제거.
- 필요한 경우 platform abstraction은 별도 helper 함수로 분리하되, 동일 인터페이스 제공.

#### 예상 문제점
- Windows 전용 옵션이나 오류 처리 로직이 사라질 수 있음.
- async I/O 동작 시 OS별 차이(예: cancel semantics) 정리 필요.

---

## Poller 인터페이스 변경 방안

### 현재 문제점
- add_fd(fd_t) 기반 API는 stream_descriptor 기반 설계를 전제로 함.
- eventfd/socketpair를 등록하기 위한 구조가 존재.

### 변경 방안
1) add_fd(fd_t) 제거
- poller는 "socket object"만 관리하도록 단순화.
- 등록은 tcp_transport가 socket을 생성/accept할 때 수행.

2) poll_entry_t 단순화
- fd, events, revents 제거.
- socket 포인터 및 상태만 유지.

3) mailbox 관련 API 삭제
- mailbox는 poller에 등록되지 않음.

### 예상 문제점
- 기존 poller API에 의존하는 코드 리팩토링 필요.
- timer 기반 이벤트 처리 로직 정리 필요.

---

## 단계별 구현 방법과 예상 문제점

### 단계 1: mailbox io_context 전환
- mailbox 구조체에 io_context 포인터 추가.
- send()에서 queue push 후 io_context::post() 실행.
- scheduled 플래그로 중복 post 방지.
- 테스트: mailbox 기반 메시지 처리 흐름 확인.
- 리스크: post 폭주, stop 상태 처리.

### 단계 2: io_thread/reaper 변경
- poller 등록 삭제.
- io_context.run() 기반으로 이벤트 루프 단순화.
- 기존 poller loop 제거 및 shutdown 흐름 재구성.
- 리스크: thread 종료 순서, drain 시점 변경.

### 단계 3: asio_poller stream_descriptor 제거
- fd/stream_descriptor 관련 코드 제거.
- poll_entry_t를 socket 전용으로 변경.
- 리스크: 내부 fd 의존 코드 정리 필요.

### 단계 4: asio_poller WSAPoll 제거
- Windows 전용 WSAPoll 코드 삭제.
- 타임아웃 처리 로직 재설계.
- 리스크: 기존 timeout/idle 로직 영향.

### 단계 5: tcp_transport 통합
- unix 경로 stream_descriptor 제거.
- tcp::socket만 사용하는 공통 코드로 정리.
- 리스크: OS별 socket option 정리 필요.

### 단계 6: #ifdef ZMQ_HAVE_WINDOWS 제거
- 플랫폼 분기 삭제 후 공통 경로만 유지.
- 리스크: Windows 전용 edge case(취소, 에러 코드) 처리 누락 가능.

---

## 리스크 분석 및 대응
- mailbox는 ZMQ 코어이므로 변경 폭이 크고 위험.
  - 대응: 단계별 refactor + 기존 테스트 전체 통과 필수.
- process_command() 호출 경로가 변경될 수 있음.
  - 대응: 명령 처리 순서 보존 여부 확인.
- 기존 테스트와 성능 회귀 가능성.
  - 대응: 테스트 전체 실행 + 벤치마크 비교.

---

## 검증/테스트 계획
- 기본: `./build.sh` 또는 `./build-scripts/linux/build.sh x64 ON`
- CMake: `cmake -B build -DZMQ_BUILD_TESTS=ON && cmake --build build && ctest --output-on-failure`
- 필요 시 bench: `-DBUILD_BENCHMARKS=ON`

---

## 마이그레이션 순서 제안
1) mailbox io_context 전환 (가장 위험도 높음)
2) io_thread/reaper 변경
3) asio_poller stream_descriptor 제거
4) asio_poller WSAPoll 제거
5) tcp_transport 통합
6) #ifdef ZMQ_HAVE_WINDOWS 제거 및 클린업

