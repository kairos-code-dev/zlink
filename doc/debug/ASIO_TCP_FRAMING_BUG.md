# ASIO TCP Message Framing Bug

## 문제 요약

ASIO TCP 엔진에서 고처리량(200k+ 메시지) 테스트 시 메시지 프레이밍 오류 발생

### 증상

- **정상 동작**: 100k 메시지까지는 정상
- **오류 발생**: 200k 메시지에서 hang 또는 framing error
- **오류 메시지**: `Recv error at 11227: rc=0 errno=Resource temporarily unavailable`
- **또 다른 오류**: `Recv error at 21505: rc=124 errno=Resource temporarily unavailable`

### 핵심 문제

- 4바이트 메시지를 보냈는데 수신 측에서 `rc=7`, `rc=124`, `rc=0` 등 잘못된 크기 수신
- 이는 메시지 경계(framing)가 깨졌음을 의미
- libzmq epoll 버전에서는 동일 테스트 통과

---

## 환경

- **OS**: Linux (WSL2)
- **빌드**: C++20, Boost.Asio 1.87
- **ZMQ 소켓**: `ZMQ_PAIR`
- **HWM 설정**: 0 (unlimited)

---

## 테스트 방법

### 1. 테스트 코드 컴파일

```bash
# 테스트 코드 위치
/tmp/debug_test7.cpp

# ASIO 버전 빌드
cmake -B build-bench-asio \
  -DWITH_BOOST_ASIO=ON \
  -DBUILD_BENCHMARKS=ON \
  -DZMQ_CXX_STANDARD=20

cmake --build build-bench-asio --target libzmq -j$(nproc)

# 테스트 컴파일
g++ -O2 -o /tmp/test_asio /tmp/debug_test7.cpp \
  -I./include \
  -Lbuild-bench-asio/lib \
  -lzmq -lpthread \
  -Wl,-rpath,build-bench-asio/lib
```

### 2. 테스트 실행

```bash
# ASIO 버전 테스트 (30초 타임아웃)
timeout 30 /tmp/test_asio 2>&1

# 기대 결과
# Final: sent=200000 recv=200000 valid=200000

# 실제 결과 (버그)
# Recv error at 8002: rc=7 errno=Resource temporarily unavailable
# Final: sent=200000 recv=8002 valid=8002
```

### 3. 비교 테스트 (epoll 버전)

```bash
# epoll 버전 빌드
cmake -B build-bench \
  -DWITH_BOOST_ASIO=OFF \
  -DBUILD_BENCHMARKS=ON

cmake --build build-bench --target libzmq -j$(nproc)

# 테스트 컴파일
g++ -O2 -o /tmp/test_epoll /tmp/debug_test7.cpp \
  -I./include \
  -Lbuild-bench/lib \
  -lzmq -lpthread \
  -Wl,-rpath,build-bench/lib

# epoll 버전 테스트
timeout 30 /tmp/test_epoll 2>&1
# 정상: Final: sent=200000 recv=200000 valid=200000
```

---

## 테스트 코드 (debug_test7.cpp)

```cpp
#include <zmq.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <atomic>
#include <cstring>

int main() {
    void *ctx = zmq_ctx_new();
    void *s_bind = zmq_socket(ctx, ZMQ_PAIR);
    void *s_conn = zmq_socket(ctx, ZMQ_PAIR);

    int hwm = 0;
    zmq_setsockopt(s_bind, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(s_conn, ZMQ_RCVHWM, &hwm, sizeof(hwm));

    zmq_bind(s_bind, "tcp://127.0.0.1:5577");
    zmq_connect(s_conn, "tcp://127.0.0.1:5577");

    int msg_count = 200000;
    std::atomic<int> sent_count{0};
    std::atomic<int> recv_count{0};
    std::atomic<int> valid_recv{0};
    std::atomic<bool> done{false};

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cerr << "Starting test" << std::endl;

    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            int32_t recv_val = -1;
            int rc = zmq_recv(s_bind, &recv_val, sizeof(recv_val), 0);
            if (rc != sizeof(recv_val)) {
                std::cerr << "Recv error at " << i << ": rc=" << rc
                          << " errno=" << zmq_strerror(errno) << std::endl;
                break;
            }
            recv_count++;
            if (recv_val >= 0 && recv_val < msg_count) {
                valid_recv++;
            }
        }
    });

    std::thread monitor([&]() {
        while (!done) {
            std::cerr << "sent=" << sent_count
                      << " recv=" << recv_count
                      << " valid=" << valid_recv << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });

    for (int i = 0; i < msg_count; ++i) {
        int32_t val = i;
        int rc = zmq_send(s_conn, &val, sizeof(val), 0);
        if (rc != sizeof(val)) {
            std::cerr << "Send error at " << i << ": " << zmq_strerror(errno) << std::endl;
            break;
        }
        sent_count++;
    }
    std::cerr << "Sender done: " << sent_count << std::endl;

    receiver.join();
    done = true;
    monitor.join();

    std::cerr << "Final: sent=" << sent_count
              << " recv=" << recv_count
              << " valid=" << valid_recv << std::endl;

    zmq_close(s_bind);
    zmq_close(s_conn);
    zmq_ctx_term(ctx);
    return 0;
}
```

---

## 분석된 원인 (추정)

### 1. Buffer Pointer 동기화 문제

`asio_engine.cpp`의 데이터 흐름:

1. `start_async_read()`: `_decoder->get_buffer(&_read_buffer_ptr, &read_size)` 호출
2. async_read 완료 시 `_read_buffer_ptr`에 데이터 저장
3. `on_read_complete()`: `_inpos = _read_buffer_ptr`, `_insize = bytes_transferred`
4. `_decoder->resize_buffer(bytes_transferred)` 호출
5. `process_input()`: `_decoder->decode(_inpos, ...)` 호출

**문제점**:
- `resize_buffer()` 호출 후 decoder 내부 상태가 변경될 수 있음
- `_inpos`가 더 이상 유효한 위치를 가리키지 않을 수 있음
- Backpressure 상황(EAGAIN)에서 부분적으로 처리된 데이터 추적 오류

### 2. restart_input()의 버퍼 처리 문제

**수정 전 (버그)**:
```cpp
while (_insize > 0) {
    unsigned char *data;
    size_t size;
    _decoder->get_buffer (&data, &size);  // 새 버퍼 요청 - 문제!
    rc = _decoder->decode (data, _insize, processed);
    ...
}
```

**수정 후 (Codex 수정)**:
```cpp
while (_insize > 0) {
    rc = _decoder->decode (_inpos, _insize, processed);  // 기존 버퍼 사용
    _inpos += processed;
    _insize -= processed;
    ...
}
```

### 3. 남아있는 문제

Codex 수정에도 불구하고 문제가 계속되는 이유:

- `process_input()`의 decode 루프에서도 유사한 문제가 있을 수 있음
- decoder가 `rc == 0`(더 많은 데이터 필요) 반환 시 잔여 데이터 처리 로직 불완전
- `_read_buffer_ptr`와 `_inpos` 간의 동기화 문제

---

## 수정 시도 내역

### 시도 1: on_read_complete()에 _insize 체크 추가

```cpp
// on_read_complete() 마지막 부분
if (!_input_stopped && _insize == 0)
    start_async_read ();
```

**결과**: 부분적 개선, 문제 지속

### 시도 2: restart_input() 버퍼 처리 수정 (Codex)

`get_buffer()` 대신 `_inpos` 직접 사용

**결과**: 문제 지속 (`rc=7` 오류 발생)

---

## 디버깅 방법

### 1. 디버그 로깅 활성화

```cpp
// src/asio/asio_engine.cpp 라인 27
#define ASIO_ENGINE_DEBUG 1
```

### 2. 빌드 후 테스트

```bash
cmake --build build-bench-asio --target libzmq -j$(nproc)
g++ -O2 -o /tmp/test_asio /tmp/debug_test7.cpp \
  -I./include -Lbuild-bench-asio/lib -lzmq -lpthread \
  -Wl,-rpath,build-bench-asio/lib

timeout 30 /tmp/test_asio 2>&1 | head -100
```

### 3. 로그 분석 포인트

- `start_async_read`: 버퍼 포인터와 크기
- `on_read_complete`: 읽은 바이트 수
- `process_input`: decode 루프 진행 상황
- `restart_input`: EAGAIN 후 재개 시 버퍼 상태

---

## 비교: stream_engine_base vs asio_engine

### stream_engine_base (정상 동작)

```cpp
void stream_engine_base_t::in_event()
{
    // 1. decoder에서 버퍼 얻기
    _decoder->get_buffer(&data, &size);

    // 2. 동기 read
    nbytes = tcp_read(_s, data, size);

    // 3. resize_buffer로 decoder에 알림
    _decoder->resize_buffer(nbytes);

    // 4. decode - decoder가 내부적으로 버퍼 위치 관리
    while (true) {
        rc = _decoder->decode(NULL, 0, processed);  // 주목: NULL 전달
        if (rc == 0 || rc == -1) break;
        ...
    }
}
```

### asio_engine (문제)

```cpp
void asio_engine_t::on_read_complete(...)
{
    // 1. 비동기 read 완료 - 데이터는 이미 버퍼에 있음
    _inpos = _read_buffer_ptr;  // 외부에서 버퍼 위치 추적
    _insize = bytes_transferred;

    // 2. resize_buffer
    _decoder->resize_buffer(bytes_transferred);

    // 3. process_input() 호출
}

bool asio_engine_t::process_input()
{
    // decode 시 외부 추적 변수 사용
    rc = _decoder->decode(decode_buf, decode_size, processed);
    _inpos += processed;
    _insize -= processed;
}
```

**핵심 차이점**:
- stream_engine_base: decoder가 내부적으로 버퍼 관리
- asio_engine: 외부에서 `_inpos`/`_insize`로 버퍼 위치 추적 (동기화 문제 발생 가능)

---

## 다음 조사 방향

1. **decoder 인터페이스 분석**: `decode(NULL, 0, ...)` 호출 시 동작 확인
2. **버퍼 관리 단순화**: `_inpos`/`_insize` 제거하고 decoder 내부 상태 활용
3. **backpressure 시나리오 재현**: EAGAIN 발생 시점과 버퍼 상태 추적
4. **메모리 덤프**: 오류 발생 시점의 버퍼 내용 확인

---

## 관련 파일

- `src/asio/asio_engine.cpp`: ASIO TCP 엔진 구현
- `src/asio/asio_engine.hpp`: 헤더 파일
- `src/stream_engine_base.cpp`: 참조용 epoll 기반 구현
- `src/decoder.hpp`, `src/v2_decoder.cpp`: ZMTP decoder

---

*Updated: 2026-01-12*
