[English](io-thread.md) | 한국어

# I/O Thread 내부 구조

이 문서는 zlink context 내부에서 I/O 스레드가 어떤 일을 하는지,
어떻게 생성되고, 작업이 어떻게 분배되는지 설명한다.

고수준 스레딩 모델(application thread, reaper thread, 스레드 간 통신)은
[Threading Model](threading-model.ko.md)을 참고.

## 1. 개요

각 I/O 스레드는 전용 **비동기 이벤트 루프**를 실행하며 다음을 수행한다.

1. 등록된 소켓의 읽기/쓰기 준비 상태를 폴링
2. mailbox(스레드 간 명령 전달 채널)를 통해 수신된 명령 처리
3. 타이머 실행

I/O 스레드는 zlink 네트워킹의 핵심이다. 실제 네트워크 송수신, 프로토콜
인코딩/디코딩, 연결 관리가 모두 I/O 스레드에서 일어난다.

## 2. 생성과 수명

I/O 스레드는 지연(lazy) 생성된다 — `zlink_ctx_new()`는 context를 할당하지만
첫 번째 소켓이 생성될 때까지 스레드를 실행하지 않는다.

```c
void *ctx = zlink_ctx_new();
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);  /* must be set before first socket */

void *socket = zlink_socket(ctx, ZLINK_SOCKET_DEALER);  /* triggers thread launch */
```

내부적으로 `ctx_runtime_resources.cpp:start_io_threads_locked()`가
`io_thread_count`개의 `io_thread_t` 인스턴스를 생성한다. 각 스레드는
고유 slot ID를 받고, mailbox가 context의 slot registry에 등록되어
명령 라우팅에 사용된다.

스레드 이름은 `IO/0`, `IO/1`, ... `IO/N-1` 패턴을 따른다.

## 3. 이벤트 루프

각 I/O 스레드는 Boost ASIO 기반 poller(`asio_poller.cpp`)를 소유한다.
`poller_t::loop()`의 메인 루프는 다음 사이클을 반복한다:

```
┌─────────────────────────────────────────────┐
│                Event Loop                   │
│                                             │
│  1. 만료된 타이머 실행                      │
│  2. io_context.poll()  — non-blocking       │
│     준비된 I/O 이벤트 일괄 처리             │
│  3. 준비된 이벤트가 없으면:                 │
│     io_context.run_for(100ms) — blocking    │
│  4. 폐기된 poll entry 정리                  │
│                                             │
│  ← 반복 ───────────────────────────────────→│
└─────────────────────────────────────────────┘
```

- **2단계**: non-blocking `poll()`로 준비된 이벤트를 한 번에 배치 처리하여
  throughput을 높인다.
- **3단계**: 대기 중인 이벤트가 없으면 최대 100ms 블로킹하여 busy-wait
  CPU 소비를 방지한다.

## 4. 소켓 I/O 처리

소켓(TCP, IPC)은 `start_wait_read()` / `start_wait_write()`를 통해
poller에 등록되며, 내부적으로 Boost ASIO의 `async_wait`를 호출한다.
소켓이 읽기/쓰기 가능해지면:

- **Read ready** → engine의 `in_event()` 콜백이 호출되어 네트워크에서
  데이터를 읽고, 프로토콜 프레임을 디코딩하고, receive pipe로 메시지를 전달한다.
- **Write ready** → engine의 `out_event()` 콜백이 호출되어 send pipe에서
  메시지를 꺼내고, 인코딩하여 네트워크에 전송한다.

콜백은 자동으로 재등록되므로, 소켓이 폐기될 때까지 모니터링이 계속된다.

## 5. 명령(Command) 처리

각 I/O 스레드는 **mailbox**를 가진다 — 락-프리 큐
(`ypipe_t<command_t>`) 와 깨우기 신호용 signaler 의 조합이다.

```cpp
// io_thread.cpp — process_mailbox()
command_t cmd;
while (_mailbox.recv(&cmd, 0) == 0)
    cmd.destination->process_command(cmd);
```

명령은 application 스레드에서 `ctx_t::send_command()` 를 통해 도착하며,
다음과 같은 종류가 있다:

| Command | 용도 |
|---------|------|
| `plug` | 새 session/engine을 이 I/O 스레드에 부착 |
| `attach` | pipe를 session에 연결 |
| `bind` | endpoint에서 listen 시작 |
| `activate_read` | pipe 읽기 재개 |
| `activate_write` | pipe 쓰기 재개 |
| `stop` | I/O 스레드 종료 |

mailbox handle 자체도 poller에 등록되어 있어, 명령이 도착하면 블로킹
대기 중인 이벤트 루프를 깨운다.

## 6. 스레드 할당

소켓이 새 연결을 생성할 때 다음 기준으로 I/O 스레드를 선택한다:

1. **어피니티 마스크** — 설정된 경우 후보 집합을 제한
2. **최소 부하 선택** — 후보 중 등록된 핸들 수가 가장 적은 스레드 선택

이를 통해 네트워크 연결이 I/O 스레드에 분산된다. 할당 단위는
소켓이 아닌 **연결(connection)** 이다 — 하나의 소켓이 여러 연결을 가지면
여러 I/O 스레드에 걸칠 수 있다.

## 7. 튜닝 가이드라인

| 시나리오 | 권장 `ZLINK_IO_THREADS` |
|----------|------------------------|
| 소켓 1개, 연결 소수 | 1 (기본값) |
| 소켓 다수 또는 연결 다수 | 2–4 |
| 고성능 서버 (100+ 연결) | 가용 CPU 코어 수에 맞춤 |

I/O 스레드를 CPU 코어 수 이상으로 설정해도 이점이 없고 context-switch
오버헤드만 증가한다. 4 이상으로 올리기 전에
[perf 벤치마크](../../core/perf/)로 프로파일링하라.

## 주요 소스 파일

| 파일 | 역할 |
|------|------|
| `core/src/core/io_thread.hpp/.cpp` | I/O thread 클래스, mailbox 처리 |
| `core/src/core/ctx_runtime_resources.cpp` | `start_io_threads_locked()`에서 스레드 생성 |
| `core/src/engine/asio/asio_poller.hpp/.cpp` | Boost ASIO 이벤트 루프, 소켓 모니터링 |
| `core/src/core/poller_base.hpp` | Worker thread 기반 클래스 |
| `core/src/core/mailbox.hpp` | Lock-free command queue + signaler |

---
[← Threading Model](threading-model.ko.md)
