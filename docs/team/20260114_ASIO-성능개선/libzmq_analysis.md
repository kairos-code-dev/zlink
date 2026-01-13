# libzmq 원본 send 경로 분석 및 ASIO 구현 재설계

## 1. libzmq 원본 send 경로 상세 분석

### 1.1 전체 흐름 요약

```
zmq_send() 호출
    │
    ▼
socket_base_t::send()
    │ - process_commands() 호출 (비동기 명령 처리)
    │ - xsend() 호출
    ▼
dealer_t::xsend() (socket type별 구현)
    │ - lb_t::sendpipe() 호출
    ▼
lb_t::sendpipe()
    │ - pipe_t::write() - lock-free ypipe에 메시지 쓰기
    │ - pipe_t::flush() - 완료시 flush
    ▼
pipe_t::flush()
    │ - ypipe_t::flush() - CAS로 원자적 포인터 업데이트
    │ - send_activate_read() - reader thread가 sleeping이면 깨움
    ▼
session_base_t::read_activated()
    │ - engine->restart_output() 호출
    ▼
stream_engine_base_t::restart_output()
    │ - set_pollout() - poller에 POLLOUT 등록
    │ - out_event() - **Speculative Write** 즉시 시도!
    ▼
stream_engine_base_t::out_event()
    │ - encoder->encode() - 메시지 인코딩
    │ - write() → tcp_write() → send() - **커널 직통 송신**
    ▼
커널 send() syscall
```

### 1.2 핵심 설계 원칙

#### 1.2.1 Speculative Write (투기적 쓰기)

libzmq의 가장 중요한 최적화는 `restart_output()`에서 즉시 `out_event()`를 호출하는 것이다.

```cpp
// stream_engine_base.cpp:377-391
void zmq::stream_engine_base_t::restart_output ()
{
    if (unlikely (_io_error))
        return;

    if (likely (_output_stopped)) {
        set_pollout ();
        _output_stopped = false;
    }

    //  Speculative write: The assumption is that at the moment new message
    //  was sent by the user the socket is probably available for writing.
    //  Thus we try to write the data to socket avoiding polling for POLLOUT.
    //  Consequently, the latency should be better in request/reply scenarios.
    out_event ();  // <-- 핵심: 즉시 쓰기 시도!
}
```

이 설계의 의미:
- 메시지가 도착하면 POLLOUT 이벤트를 **기다리지 않고** 즉시 쓰기 시도
- 대부분의 경우 소켓 버퍼에 여유가 있으므로 **바로 커널로 전송**
- POLLOUT 루프를 돌 때까지 기다리는 지연이 없음

#### 1.2.2 out_event()의 단순한 흐름

```cpp
// stream_engine_base.cpp:308-375
void zmq::stream_engine_base_t::out_event ()
{
    zmq_assert (!_io_error);

    //  If write buffer is empty, try to read new data from the encoder.
    if (!_outsize) {
        if (unlikely (_encoder == NULL)) {
            zmq_assert (_handshaking);
            return;
        }

        _outpos = NULL;
        _outsize = _encoder->encode (&_outpos, 0);

        // out_batch_size만큼 메시지를 모으는 루프
        while (_outsize < static_cast<size_t> (_options.out_batch_size)) {
            if ((this->*_next_msg) (&_tx_msg) == -1) {
                if (errno == ECONNRESET)
                    return;
                else
                    break;  // <-- 더 이상 메시지 없으면 즉시 탈출!
            }
            _encoder->load_msg (&_tx_msg);
            unsigned char *bufptr = _outpos + _outsize;
            const size_t n =
              _encoder->encode (&bufptr, _options.out_batch_size - _outsize);
            zmq_assert (n > 0);
            if (_outpos == NULL)
                _outpos = bufptr;
            _outsize += n;
        }

        if (_outsize == 0) {
            _output_stopped = true;
            reset_pollout ();
            return;
        }
    }

    //  직접 write() 호출 - 추가 복사 없이!
    const int nbytes = write (_outpos, _outsize);

    if (nbytes == -1) {
        reset_pollout ();
        return;
    }

    _outpos += nbytes;
    _outsize -= nbytes;
}
```

**핵심 포인트:**
1. `_outpos`는 encoder 내부 버퍼를 **직접 가리킴** (복사 없음)
2. `write()`는 `_outpos`에서 커널로 **직접 전송**
3. `out_batch_size` 루프는 **더 이상 메시지 없으면 즉시 탈출**
4. **별도의 _write_buffer로 복사하지 않음**

#### 1.2.3 encoder의 zero-copy 설계

```cpp
// encoder.hpp:47-100
size_t encode (unsigned char **data_, size_t size_) ZMQ_FINAL
{
    unsigned char *buffer = !*data_ ? _buf : *data_;
    const size_t buffersize = !*data_ ? _buf_size : size_;

    // ...

    //  If there are no data in the buffer yet and we are able to
    //  fill whole buffer in a single go, let's use zero-copy.
    if (!pos && !*data_ && _to_write >= buffersize) {
        *data_ = _write_pos;  // <-- encoder 내부 버퍼 포인터 직접 반환!
        pos = _to_write;
        _write_pos = NULL;
        _to_write = 0;
        return pos;
    }

    // 작은 메시지는 encoder 내부 버퍼에 복사
    const size_t to_copy = std::min (_to_write, buffersize - pos);
    memcpy (buffer + pos, _write_pos, to_copy);
    // ...
}
```

**핵심:**
- 큰 메시지: encoder가 메시지 데이터 포인터를 **직접 반환** (zero-copy)
- 작은 메시지: encoder 내부 버퍼(_buf)에 복사 후 포인터 반환
- 어느 경우든 **engine은 추가 복사 없이 encoder 포인터를 직접 사용**

#### 1.2.4 tcp_write()의 단순함

```cpp
// tcp.cpp:186-241
int zmq::tcp_write (fd_t s_, const void *data_, size_t size_)
{
#ifdef ZMQ_HAVE_WINDOWS
    const int nbytes = send (s_, (char *) data_, static_cast<int> (size_), 0);
    // 에러 처리...
#else
    ssize_t nbytes = send (s_, static_cast<const char *> (data_), size_, 0);
    // 에러 처리...
#endif
    return static_cast<int> (nbytes);
}
```

**핵심:**
- 단순히 시스템 `send()` 호출
- 비동기 콜백, completion handler 없음
- 논블로킹 소켓이므로 즉시 반환

### 1.3 pipe와 ypipe: Lock-Free 메시지 전달

#### ypipe의 원자적 flush

```cpp
// ypipe.hpp:76-98
bool flush ()
{
    if (_w == _f)
        return true;

    //  CAS로 원자적 업데이트 시도
    if (_c.cas (_w, _f) != _w) {
        //  Reader가 sleeping 상태 - 깨워야 함
        _c.set (_f);
        _w = _f;
        return false;  // caller가 reader를 깨워야 함
    }

    _w = _f;
    return true;  // reader가 깨어있음, 알림 불필요
}
```

**핵심:**
- Writer thread(소켓)와 Reader thread(I/O thread)가 **lock 없이** 통신
- CAS 연산으로 reader의 상태 확인 및 업데이트를 **원자적**으로 수행
- Reader가 sleeping이면 `send_activate_read()`로 깨움

### 1.4 libzmq send 경로의 시간 복잡도

최적 경로 (Speculative Write 성공 시):
1. `pipe_t::write()` - O(1) lock-free 큐 삽입
2. `ypipe_t::flush()` - O(1) CAS 연산
3. `stream_engine_base_t::restart_output()` - O(1)
4. `stream_engine_base_t::out_event()` - O(1) encode + write
5. `tcp_write()` - O(1) syscall

**총 지연: 매우 짧음 - pipe 쓰기부터 커널 send까지 직통**

---

## 2. ASIO 구현 vs 원본 libzmq 비교

### 2.1 현재 ASIO 구현의 문제점

#### 2.1.1 restart_output()에서 Speculative Write 부재

```cpp
// asio_engine.cpp:649-660 (현재 구현)
void zmq::asio_engine_t::restart_output ()
{
    if (unlikely (_io_error))
        return;

    if (likely (_output_stopped)) {
        _output_stopped = false;
    }

    //  Start async write
    start_async_write ();  // <-- 비동기 쓰기 시작만 함
}
```

**문제:**
- `start_async_write()`는 **비동기**로 동작
- libzmq처럼 즉시 `out_event()` 호출하여 **동기적 쓰기 시도**를 하지 않음
- 메시지가 ASIO io_context 루프를 거쳐야만 송신됨

#### 2.1.2 process_output()에서 불필요한 복사

```cpp
// asio_engine.cpp:628-647 (현재 구현)
void zmq::asio_engine_t::process_output ()
{
    // ... encoder 루프 ...

    //  Copy data to write buffer (reuse capacity to avoid reallocations)
    const size_t out_batch_size =
      static_cast<size_t> (_options.out_batch_size);
    const size_t target =
      _outsize > out_batch_size ? _outsize : out_batch_size;
    if (_write_buffer.capacity () < target)
        _write_buffer.reserve (target);
    _write_buffer.resize (_outsize);
    memcpy (_write_buffer.data (), _outpos, _outsize);  // <-- 항상 복사!

    // ...
}
```

**문제:**
- encoder 버퍼에서 `_write_buffer`로 **항상 memcpy**
- libzmq는 encoder 포인터를 **직접 사용**하여 복사 없이 송신
- 작은 메시지에서도 복사 오버헤드 발생

#### 2.1.3 async_write_some의 completion handler 오버헤드

```cpp
// asio_engine.cpp:383-390 (현재 구현)
if (_transport) {
    _transport->async_write_some (
      _write_buffer.data (), _write_buffer.size (),
      [this] (const boost::system::error_code &ec, std::size_t bytes) {
          on_write_complete (ec, bytes);
      });
}
```

**문제:**
- 매 쓰기마다 **lambda 생성** 및 **io_context 스케줄링**
- completion handler가 호출될 때까지 **대기**
- libzmq는 `send()` 직접 호출로 즉시 반환

### 2.2 구조적 차이점 요약

| 측면 | libzmq 원본 | ASIO 구현 (현재) |
|------|-------------|------------------|
| 쓰기 시점 | restart_output()에서 **즉시** out_event() 호출 | async 요청 후 io_context 루프 대기 |
| 버퍼 관리 | encoder 포인터 **직접 사용** | _write_buffer로 **복사 후** 전송 |
| syscall | `send()` **동기** 호출 (논블로킹) | `async_write_some` **비동기** |
| 완료 처리 | 즉시 반환, 다음 out_event에서 잔여 처리 | completion handler 콜백 |
| 이벤트 루프 | POLLOUT 이벤트 또는 speculative write | io_context::run() |

### 2.3 왜 ASIO가 느린가

1. **지연 발생 지점 1: io_context 스케줄링**
   - libzmq: pipe flush → restart_output → **즉시** out_event → send
   - ASIO: pipe flush → restart_output → async_write_some → **io_context 대기** → handler → send

2. **지연 발생 지점 2: 불필요한 복사**
   - libzmq: encoder 버퍼 → **직접** send
   - ASIO: encoder 버퍼 → **memcpy** → _write_buffer → async_write → send

3. **지연 발생 지점 3: Batching 루프**
   - 둘 다 `out_batch_size` 루프 사용
   - 그러나 libzmq는 메시지 없으면 **즉시 탈출** 후 바로 send
   - ASIO는 루프 후 **복사** 후 **비동기 요청**

---

## 3. ASIO 기반에서 libzmq 구조 재현 방안

### 3.1 핵심 원칙: Synchronous-First 설계

ASIO를 사용하면서도 libzmq의 단순함을 유지하려면:

**"먼저 동기적으로 시도하고, 실패하면 비동기로 대기"**

### 3.2 제안 1: Speculative Synchronous Write

```cpp
void zmq::asio_engine_t::restart_output ()
{
    if (unlikely (_io_error))
        return;

    if (likely (_output_stopped)) {
        _output_stopped = false;
    }

    // libzmq 방식: 즉시 동기적 쓰기 시도
    speculative_write ();
}

void zmq::asio_engine_t::speculative_write ()
{
    // 버퍼 준비 (복사 최소화)
    if (_outsize == 0) {
        prepare_output_direct ();  // encoder 포인터 직접 사용
        if (_outsize == 0) {
            _output_stopped = true;
            return;
        }
    }

    // 동기적 쓰기 시도 (논블로킹)
    boost::system::error_code ec;
    size_t bytes_written = _transport->write_some (_outpos, _outsize, ec);

    if (ec == boost::asio::error::would_block) {
        // 소켓 버퍼 가득 참 - 비동기로 전환
        start_async_write_for_remainder ();
        return;
    }

    if (ec) {
        error (connection_error);
        return;
    }

    _outpos += bytes_written;
    _outsize -= bytes_written;

    // 더 보낼 데이터가 있으면 다시 시도
    if (_outsize > 0) {
        speculative_write ();  // 재귀적 시도
    }
}
```

### 3.3 제안 2: Direct Encoder Buffer 사용

```cpp
void zmq::asio_engine_t::prepare_output_direct ()
{
    if (unlikely (_encoder == NULL))
        return;

    _outpos = NULL;
    _outsize = _encoder->encode (&_outpos, 0);

    // out_batch_size 루프 - 메시지 없으면 즉시 탈출
    while (_outsize < static_cast<size_t> (_options.out_batch_size)) {
        if ((this->*_next_msg) (&_tx_msg) == -1)
            break;  // 더 이상 메시지 없음 - 즉시 탈출!

        _encoder->load_msg (&_tx_msg);
        unsigned char *bufptr = _outpos + _outsize;
        const size_t n = _encoder->encode (
            &bufptr, _options.out_batch_size - _outsize);
        zmq_assert (n > 0);
        if (_outpos == NULL)
            _outpos = bufptr;
        _outsize += n;
    }

    // 핵심: _write_buffer로 복사하지 않음!
    // _outpos는 encoder 내부 버퍼를 직접 가리킴
}
```

### 3.4 제안 3: Hybrid Async-Sync 패턴

비동기 쓰기가 필요한 경우(소켓 버퍼 가득 참)만 async 사용:

```cpp
void zmq::asio_engine_t::start_async_write_for_remainder ()
{
    if (_write_pending || _outsize == 0)
        return;

    _write_pending = true;

    // 비동기 쓰기 - encoder 버퍼 직접 사용
    // 주의: async 완료 전에 encoder 버퍼가 변경되지 않도록 보장 필요
    _transport->async_write_some (
        _outpos, _outsize,
        [this] (const boost::system::error_code &ec, std::size_t bytes) {
            on_async_write_complete (ec, bytes);
        });
}

void zmq::asio_engine_t::on_async_write_complete (
    const boost::system::error_code &ec, std::size_t bytes)
{
    _write_pending = false;

    if (_terminating || !_plugged)
        return;

    if (ec) {
        if (ec != boost::asio::error::operation_aborted)
            error (connection_error);
        return;
    }

    _outpos += bytes;
    _outsize -= bytes;

    // 더 보낼 데이터 있으면 다시 speculative write 시도
    if (_outsize > 0 || !_output_stopped) {
        speculative_write ();
    }
}
```

### 3.5 제안 4: 버퍼 수명 관리

ASIO async 연산 중 버퍼 수명 보장 방법:

#### 방법 A: Encoder 버퍼 직접 사용 + 쓰기 완료까지 메시지 유지

```cpp
// encoder에서 메시지 데이터를 가리키고 있을 때,
// async_write 완료 전까지 _tx_msg를 close하지 않음
void zmq::asio_engine_t::prepare_output_direct ()
{
    // ...
    // _tx_msg.close()를 async_write 완료 후로 지연
}
```

#### 방법 B: 복사가 필요한 경우만 버퍼 사용

```cpp
void zmq::asio_engine_t::ensure_write_buffer ()
{
    // async 쓰기가 필요하고 encoder 버퍼가 변경될 수 있는 경우에만 복사
    if (_write_pending_async && _outsize > 0) {
        if (_write_buffer.size () < _outsize)
            _write_buffer.resize (_outsize);
        memcpy (_write_buffer.data (), _outpos, _outsize);
        _outpos = _write_buffer.data ();
    }
}
```

### 3.6 Transport 인터페이스 확장

`i_asio_transport`에 동기 쓰기 메서드 추가:

```cpp
class i_asio_transport {
public:
    // 기존 비동기 메서드
    virtual void async_write_some (
        const void *data, size_t size, write_handler handler) = 0;

    // 새로운 동기 메서드 (논블로킹)
    virtual size_t write_some (
        const void *data, size_t size, boost::system::error_code &ec) = 0;
};
```

TCP transport 구현:

```cpp
size_t tcp_transport_t::write_some (
    const void *data, size_t size, boost::system::error_code &ec)
{
    return _socket->write_some (
        boost::asio::buffer (data, size), ec);
}
```

---

## 4. 수정된 구현 전략

### 4.1 기존 계획의 한계

기존 `plan.md`의 접근:
- `out_batch_size` 기반 분기만으로는 **구조적 문제** 해결 불가
- "즉시성 경로"를 추가해도 **async 콜백 대기**는 남음
- 소켓 옵션 추가는 **복잡성 증가**만 초래

### 4.2 새로운 전략: libzmq 구조 재현

#### Phase 1: Speculative Write 도입

1. `restart_output()`에서 `speculative_write()` 호출
2. 동기적 `write_some()` 시도
3. `would_block` 시에만 async로 전환

**예상 효과:** 대부분의 메시지가 **즉시 송신**, latency 대폭 감소

#### Phase 2: Encoder 버퍼 직접 사용

1. `_write_buffer`로의 복사 제거
2. `_outpos`가 encoder 버퍼를 직접 가리킴
3. 버퍼 수명 관리 주의

**예상 효과:** memcpy 오버헤드 제거, throughput 향상

#### Phase 3: Transport 인터페이스 확장

1. `write_some()` 동기 메서드 추가
2. TCP, TLS, WebSocket transport에 구현
3. 기존 async 메서드와 공존

**예상 효과:** 모든 transport에서 speculative write 가능

### 4.3 구현 우선순위

| 순서 | 항목 | 복잡도 | 예상 효과 |
|-----|------|--------|----------|
| 1 | Speculative Write | 중 | **높음** - 주요 latency 원인 해결 |
| 2 | Direct Encoder Buffer | 중 | 중 - 복사 오버헤드 제거 |
| 3 | Transport 확장 | 저 | 높음 - Phase 1 활성화 |
| 4 | 버퍼 수명 관리 | 고 | 필수 - 안정성 보장 |

### 4.4 벤치마크 기준

각 단계마다 측정:
```bash
taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20
```

측정 항목:
- p50/p99 latency
- throughput (msg/s)
- CPU 사용률

목표:
- **p99 latency**: libzmq 대비 +-10% 이내
- **throughput**: libzmq 대비 +-10% 이내

---

## 5. 결론

### 5.1 핵심 발견

libzmq의 성능 비결은 **Speculative Write**와 **Zero-Copy** 설계:
1. 메시지가 도착하면 **즉시** 커널 send() 시도
2. encoder 버퍼를 **직접** 사용하여 복사 최소화
3. 필요할 때만 poller 이벤트 대기

### 5.2 ASIO 구현 개선 방향

ASIO의 비동기 모델을 유지하면서도:
1. **동기적 시도를 먼저** - speculative_write()
2. **실패 시에만 비동기** - would_block 처리
3. **버퍼 복사 최소화** - encoder 포인터 직접 사용

### 5.3 기대 효과

이 전략 적용 시:
- 짧은 메시지 latency: **libzmq 수준으로 회복**
- throughput: 복사 제거로 **향상**
- 코드 복잡도: 기존 대비 **낮음** (옵션 추가 대신 구조 개선)

---

## 참고

- libzmq 원본 소스: `/home/ulalax/project/ulalax/libzmq-ref`
- 현재 ASIO 구현: `/home/ulalax/project/ulalax/zlink/src/asio/asio_engine.cpp`
- 기존 계획: `/home/ulalax/project/ulalax/zlink/docs/team/20260114_ASIO-성능개선/plan.md`
