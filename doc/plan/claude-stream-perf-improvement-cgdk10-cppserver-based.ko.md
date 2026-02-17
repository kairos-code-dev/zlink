# CGDK10/CppServer 아키텍처 분석 기반 zlink STREAM 소켓 성능 개선 계획

- 작성일: 2026-02-17
- 참조 분석 문서:
  - [CGDK10 아키텍처 분석](../study/cgdk10-architecture-analysis.md)
  - [CppServer 아키텍처 분석](../study/cppserver-architecture-analysis.md)
- 참조 기존 계획:
  - [ASIO 성능 개선 포인트](./asio-performance-improvement-points.ko.md)
  - [STREAM 5-Stack Parity 계획](./stream-4stack-parity-and-zlink-improvement.ko.md)
  - [Codex STREAM 성능 계획](./codex-stream-socket-performance-plan.ko.md)

---

## 1. 목적

CGDK10과 CppServer의 고성능 기법을 zlink STREAM 엔진의 현재 구현과 대조하여,
적용 가능한 개선 항목을 식별하고 구현 계획을 수립한다.

**비교 방법론**: 각 프레임워크의 핵심 기법을 추출하고, zlink 현재 코드에서
해당 기법의 적용 여부를 확인한 뒤, 미적용 항목에 대해 구현 방안을 설계한다.

---

## 2. 현재 상태 요약

### 2.0 TCP STREAM 엔진 경로 — 핵심 아키텍처 참고

zlink에서 STREAM 소켓은 **transport에 따라 서로 다른 엔진**을 사용한다:

| Transport | Listener 엔진 | Connecter | 소스 |
|-----------|--------------|-----------|------|
| **TCP** | `asio_raw_engine_t` (← `asio_engine_t`) | **비활성** (close/terminate) | `asio_tcp_listener.cpp:351`, `asio_tcp_connecter.cpp:395` |
| IPC | `asio_stream_engine_t` | **비활성** | `asio_ipc_listener.cpp:288`, `asio_ipc_connecter.cpp:309` |
| TLS | `asio_stream_engine_t` | **비활성** | `asio_tls_listener.cpp:447`, `asio_tls_connecter.cpp:502` |
| WS | `asio_stream_engine_t` | **비활성** | `asio_ws_listener.cpp:518`, `asio_ws_connecter.cpp:535` |

> **STREAM 소켓은 현재 모든 transport에서 connect(outbound)가 비활성이며, listen(inbound) 전용이다.**

```cpp
// asio_tcp_listener.cpp:351-352 (TCP: asio_raw_engine 선택)
if (options.type == ZLINK_STREAM)
    engine = new (std::nothrow) asio_raw_engine_t (fd_, options, endpoint_pair);
```

TCP STREAM은 `asio_raw_engine_t`를 통해 `stream_fast_encoder_t` / `stream_fast_decoder_t`를
사용한다 (`asio_raw_engine.cpp:53-99`). `asio_stream_engine_t`는 IPC/TLS/WS 전용이다.

> **이 문서의 모든 개선 항목에는 적용 대상 엔진을 명시한다.**
> TCP 벤치마크 성능 목표라면 `asio_raw_engine_t`/`asio_engine_t` 경로가 핵심이고,
> `asio_stream_engine_t` 대상 항목(3.2/3.3/3.6/3.7)은 IPC/TLS/WS 성능에만 영향을 준다.

### 2.1 이미 적용된 기법

| 기법 | 출처 | zlink 적용 현황 | 소스 |
|------|------|----------------|------|
| TCP_NODELAY 기본 활성화 | CppServer | O — `tune_tcp_socket()`에서 항상 `nodelay=1` 설정 | `tcp.cpp:30-41` |
| TCP_NODELAY 호출 경로 | — | O — listener/connecter 모두 호출 | `asio_tcp_listener.cpp:389`, `asio_tcp_connecter.cpp:417` |
| Double-Buffer Send | CppServer | O — main/flush swap (**IPC/TLS/WS 전용**) | `asio_stream_engine.cpp:490-496` |
| Partial Write Offset | CppServer | O — `_send_buffer_flush_offset` (**IPC/TLS/WS 전용**) | `asio_stream_engine.cpp:534` |
| Gather Write (writev) | CGDK10 Scatter-Gather | O — 임계값 이상 메시지 (TCP/전체) | `asio_engine.cpp:578-637` |
| Zero-Copy Decoder | CGDK10 shared_buffer | O — `shared_message_memory_allocator` (TCP 경로) | `stream_fast_decoder.cpp:61-70` |
| Lock-Free Inter-Thread Queue | CGDK10 lockfree_stack | O — `ypipe_t` (CAS 기반) | `core/ypipe.hpp` |
| Per-Thread IO Context | CppServer IO-Per-Thread | O — io_thread별 독립 io_context | `core/io_thread.hpp` |
| Length-Prefix Protocol | CGDK10/CppServer | O — 4바이트 빅엔디안 길이 접두사 | `stream_fast_encoder.cpp:21` |
| 적응형 수신 버퍼 성장 | CppServer 2x 성장 | O — 2배 확장 (**IPC/TLS/WS 전용**) | `asio_stream_engine.cpp:306-311` |
| Fast-Path 단일 프레임 송신 | zlink 자체 | O — routing_id 직접 첨부 | `stream.cpp:89-119` |
| Handler Memory Pool (기본) | CppServer HandlerStorage | △ — 256바이트 인라인 할당자 **파일 존재하나 비동기 콜백에 미적용** | `handler_allocator.hpp` |

### 2.2 미적용 또는 부분 적용 기법

| 기법 | 출처 | zlink 현황 | 영향 영역 |
|------|------|-----------|----------|
| TCP_NODELAY 공개 옵션 | CppServer | **부분** — 기본 활성화되지만 `ZLINK_TCP_NODELAY`가 공식 API(`zlink.h`)에 미정의. STREAM뿐 아니라 전 소켓 타입에서 공식 상수 없음 | 시나리오 인자 무시 |
| 수신 경로 Zero-Copy (IPC/TLS/WS) | CGDK10 buffer_view | **미적용** — `push_one_frame`이 매번 memcpy | throughput (대형 메시지) |
| recv 버퍼 memmove 회피 (IPC/TLS/WS) | CGDK10 포인터 전진 | **미적용** — 매 배치 memmove | CPU 효율 |
| Handler Pool 활성화 | CppServer 1KB Pool | **미적용** — 256바이트 할당자 파일 존재하나 비동기 콜백에서 사용하지 않음 | 고빈도 I/O 시 malloc 병목 |
| Single-Frame Recv 소켓 옵션 | zlink 자체 | **환경변수 뒤에 숨김** — 소켓 옵션으로 제어 불가 | recv 경로 오버헤드 |
| Adaptive Send Batch 크기 (IPC/TLS/WS) | CppServer TrySend | **고정 크기까지 채움** | latency (저부하 시) |
| 미사용 멤버 제거 (IPC/TLS/WS) | CGDK10 캐시 최적화 | `_tx_msg` 등 4개 멤버 미사용 | 캐시 라인 효율 |

---

## 3. 개선 항목 상세

### 3.1 STREAM 소켓 TCP_NODELAY 공개 옵션 노출 및 제어

#### 3.1.1 현재 상태

TCP_NODELAY는 **이미 기본 활성화**되어 있다:

```cpp
// tcp.cpp:30-38 — tune_tcp_socket()
int nodelay = 1;
const int rc = setsockopt (s_, IPPROTO_TCP, TCP_NODELAY,
              reinterpret_cast<char *> (&nodelay), sizeof (int));
```

이 함수는 TCP listener(`asio_tcp_listener.cpp:389`)와
connecter(`asio_tcp_connecter.cpp:417`) 양쪽에서 호출된다.

#### 3.1.2 문제

STREAM 소켓에서 `ZLINK_TCP_NODELAY` 소켓 옵션이 **공개되어 있지 않다**.
벤치마크 시나리오(`run_stream_compare.sh:122,382`)가 `--tcp-nodelay 1`을 전달하지만,
zlink STREAM 시나리오 코드에서 경고만 출력하고 적용하지 않는다:

```cpp
// test_scenario_stream_zlink.cpp:71-74 (현재)
// STREAM socket doesn't expose TCP_NODELAY in this API.
if (opt.tcp_nodelay != 0)
    fprintf(stderr, "zlink stream: tcp_nodelay requested but no public socket option is exposed; using runtime default\n");
```

반면 다른 시나리오(cppserver, asio, cgdk10)는 모두 이 인자를 정상 적용한다.
또한 PAIR/ROUTER/PUBSUB 벤치마크에서는 `ZLINK_TCP_NODELAY`(=26)를
`zlink_setsockopt`으로 설정하지만, 이 상수가 공식 헤더(`core/include/zlink.h`)에
정의되어 있지 않아 벤치마크 코드에서 로컬로 `#define`하고 있다.

#### 3.1.3 근거

CppServer는 세션 연결 시 TCP_NODELAY를 기본 설정한다:

> `tcp_session.cpp:65-66` — `_socket.set_option(asio::ip::tcp::no_delay(true))`

CppServer 분석 문서 4.4절:
```
TCP_NODELAY (Nagle OFF):
  send(10B) → 즉시 전송!
  → Echo 서버, 게임, 실시간 시스템에 필수
```

다른 스택과의 공정한 벤치마크 비교를 위해서도, 시나리오 인자가 실제로 동작해야 한다.

#### 3.1.4 구현 방안

**A. 공식 API 상수 정의**:
- `core/include/zlink.h` — `ZLINK_TCP_NODELAY` 상수를 공식 정의 (현재 벤치마크에서 26으로 사용 중)

**B. options_t 연동**:
- `core/src/core/options.hpp` — `tcp_nodelay` 필드 추가 (기본값: 1)
- `core/src/core/options.cpp` — `setsockopt`/`getsockopt` 핸들러에서 STREAM 타입 허용

**C. 시나리오 코드 수정**:
- `test_scenario_stream_zlink.cpp` — 경고 대신 실제 `zlink_setsockopt(sock, ZLINK_TCP_NODELAY, ...)` 호출

#### 3.1.5 난이도/효과

- 난이도: **낮음** (옵션 배관 작업)
- 효과: 벤치마크 시나리오 공정성 확보, 사용자 코드에서 Nagle 제어 가능

---

### 3.2 수신 경로 Zero-Copy — push_one_frame memcpy 제거

> **적용 대상**: `asio_stream_engine_t` (IPC/TLS/WS 전용)
> TCP 경로는 `stream_fast_decoder` + `shared_message_memory_allocator`로 이미 zero-copy 수신 경로를 사용한다.

#### 3.2.1 문제

`asio_stream_engine_t::push_one_frame()` (`asio_stream_engine.cpp:405-431`)이
매 프레임마다 `msg.init_size(size_) + memcpy(msg.data(), data_, size_)`를 수행한다.

```cpp
// asio_stream_engine.cpp:411-415 (현재)
msg_t msg;
int rc = msg.init_size (size_);       // ← 매번 malloc (큰 메시지면 LMSG)
errno_assert (rc == 0);
if (size_ > 0)
    memcpy (msg.data (), data_, size_);  // ← 매번 복사
```

반면, TCP 경로가 사용하는 `stream_fast_decoder`는 이미
`shared_message_memory_allocator` 기반의 zero-copy 경로를 구현해두었다
(`stream_fast_decoder.cpp:61-70`).

#### 3.2.2 근거

CGDK10 `_buffer_view.h:607-622`:
```cpp
// 버퍼 내 메모리를 직접 참조 반환 (완전 Zero-Copy)
T& _extract_general() {
    T* p = reinterpret_cast<T*>(this->data_);
    this->data_ += sizeof(T);
    this->size_ -= sizeof(T);
    return *p;  // 복사 없음
}
```

CGDK10 분석 문서 3.2절:
```
CGDK10 Zero-Copy:
  Buffer → reinterpret_cast로 직접 Object로 읽기
  (포인터 캐스팅, 0회 복사)
```

CppServer `tcp_session.cpp:455`:
```cpp
// 수신 데이터를 직접 전달 (중간 복사 없음)
onReceived(_receive_buffer.data(), size);
```

#### 3.2.3 구현 방안

수신 버퍼를 참조 카운트 메모리 블록으로 관리하여, 파싱된 프레임이
수신 버퍼 내 메모리를 직접 참조하는 `msg_t`를 생성한다.

기존 `msg_t::init()` 오버로드 활용:
```cpp
// msg.hpp — 이미 존재하는 zero-copy 초기화
int init (void *data_, size_t size_, msg_free_fn *ffn_, void *hint_,
          content_t *content_ = NULL);
```

변경 대상:
- `core/src/engine/asio/asio_stream_engine.hpp` — recv 버퍼를 refcounted 블록으로 교체
- `core/src/engine/asio/asio_stream_engine.cpp` — `push_one_frame()` 리팩토링

**핵심 설계**:
1. recv 버퍼 할당 시 `atomic_counter_t` 참조 카운트 헤더를 앞에 붙인 블록 할당
2. `push_one_frame()`에서 `msg.init(data_ptr, size, dec_ref_fn, block_ptr)` 호출
3. 각 msg가 블록의 참조 카운트를 증가, 소비 시 감소
4. 모든 msg 소비 후 블록 해제

`shared_message_memory_allocator`(`decoder_allocators.hpp:53-103`)의
`call_dec_ref` / `inc_ref` / `advance_content` 패턴을 재사용 가능.

**주의사항**: 참조 카운트 버퍼의 생명주기 관리가 복잡하다. 수신 버퍼가 아직
참조 중인 상태에서 새 async_read를 시작하면 backpressure / 버퍼 재사용 경합이
발생할 수 있으므로 `shared_message_memory_allocator`의 allocate/release 패턴을
그대로 따라야 한다.

#### 3.2.4 난이도/효과

- 난이도: **상** (참조 카운트 생명주기 관리 + backpressure/버퍼 재사용 경합 + 기존 memmove 구조와의 충돌 해소 필요)
- 효과: IPC/TLS/WS 경로에서 대형 메시지(64KB) 수신 시 memcpy 제거, throughput 개선

---

### 3.3 recv 버퍼 memmove 회피 — 오프셋 기반 관리

> **적용 대상**: `asio_stream_engine_t` (IPC/TLS/WS 전용)
> TCP 경로의 `asio_engine_t`는 별도의 수신 버퍼 관리 구조를 사용한다.

#### 3.3.1 문제

`process_input_buffer()` (`asio_stream_engine.cpp:390-396`)에서 매 배치 처리 후
잔여 데이터를 버퍼 앞으로 이동한다:

```cpp
// asio_stream_engine.cpp:390-396 (현재)
if (offset > 0) {
    if (offset < _recv_size) {
        memmove (&_recv_buffer[0], &_recv_buffer[offset], _recv_size - offset);
        _recv_size -= offset;
    } else {
        _recv_size = 0;
    }
}
```

대량 수신 시 `memmove` 비용이 누적된다.

#### 3.3.2 근거

CGDK10 `net.io.packetable.stream.h:116-117`:
```cpp
// 포인터 전진만으로 메시지 경계 이동 (Zero-Copy: 같은 버퍼 내에서 이동)
((buffer_view&)message).add_data(message_size);
remained_size -= message_size;
```

CGDK10 분석 문서 4.4절: 패킷 파서가 포인터 전진만 수행하고, 복사/이동 없음.

#### 3.3.3 구현 방안

`_recv_offset` 멤버를 추가하여 소비된 위치를 추적.
memmove는 오프셋이 버퍼 크기의 절반을 넘을 때만 수행.

변경 대상:
- `core/src/engine/asio/asio_stream_engine.hpp` — `size_t _recv_offset` 추가
- `core/src/engine/asio/asio_stream_engine.cpp` — `process_input_buffer`, `start_async_read` 수정

**핵심 변경**:
```
// 변경 전:
//   매 배치 후 memmove

// 변경 후:
//   _recv_offset 전진으로 소비 위치 추적
//   async_read_some는 _recv_buffer[_recv_size] 부터 기록
//   _recv_offset > buffer.size()/2 일 때만 memmove로 압축
//   → memmove 빈도 대폭 감소
```

#### 3.3.4 난이도/효과

- 난이도: **낮~중** (오프셋 변수 추가 자체는 단순하나, backpressure/async_read 상호작용 검증 필요)
- 효과: 고빈도 소형 메시지 처리 시 CPU 효율 개선

---

### 3.4 ASIO Handler Memory Pool 확대 적용

#### 3.4.1 현재 상태

zlink에는 이미 `handler_allocator` (`handler_allocator.hpp`)가 존재한다:

```cpp
// handler_allocator.hpp — 기존 256바이트 인라인 할당자
class handler_allocator {
    bool _in_use;
    typename std::aligned_storage<256>::type _storage;
public:
    void *allocate(std::size_t size) {
        if (!_in_use && size <= sizeof(_storage)) { _in_use = true; return &_storage; }
        return ::operator new(size);
    }
    void deallocate(void *pointer) {
        if (pointer == &_storage) _in_use = false;
        else ::operator delete(pointer);
    }
};

template <typename Handler>
inline custom_alloc_handler<Handler>
make_custom_alloc_handler(handler_allocator &alloc, Handler h);
```

#### 3.4.2 문제

이 할당자는 파일로 존재하지만 **실제 비동기 콜백에서 전혀 사용되지 않는다**.
`asio_stream_engine_t`(`asio_stream_engine.cpp:315,503`)과
`asio_engine_t`(`asio_engine.cpp:469,570`)의 async 핸들러가 모두
plain 람다를 직접 전달하며, `make_custom_alloc_handler`를 호출하지 않는다.

또한 256바이트는 CppServer의 1KB에 비해 작아, 복잡한 핸들러(캡처 변수가 많은 람다)에서
폴백 힙 할당이 발생할 수 있다.

#### 3.4.3 근거

CppServer `memory.h:26-54` — `HandlerStorage`:
```cpp
class HandlerStorage {
    bool _in_use;
    std::byte _storage[1024];  // 1KB 고정 스택 메모리
    ...
};
```

CppServer 분석 문서 5절:
```
Hot path에서 힙 할당 완전 제거
→ malloc/free 0회, 캐시 친화적
```

CppServer는 세션당 2개 (`_receive_storage`, `_send_storage`)를 보유하고,
서버당 1개 (`_acceptor_storage`)를 보유한다 (`tcp_session.h:256,264`, `tcp_server.h:232`).

CGDK10도 `Npoolable`로 실행 객체를 풀링한다 (`net.io.receivable.stream.rio.h:98-104`).

#### 3.4.4 구현 방안

기존 `handler_allocator.hpp`를 **재사용 및 확장**한다:

변경 대상:
- `core/src/engine/asio/handler_allocator.hpp` — 스토리지 크기를 256 → 1024바이트로 확대 검토
- `core/src/engine/asio/asio_stream_engine.cpp` — 비동기 콜백에 `make_custom_alloc_handler` 일관 적용
- `core/src/engine/asio/asio_engine.cpp` — TCP 경로의 비동기 콜백에도 동일 적용 검토

**핵심**: 신규 파일을 만들지 않고, 기존 `handler_allocator`를 활용한다.

#### 3.4.5 난이도/효과

- 난이도: **중~상** (할당자 코드 자체는 존재하나, 전 엔진의 비동기 콜백에 래핑 적용 + 사이즈 확대 검증 필요)
- 효과: 고동시성에서 malloc/free 오버헤드 감소, 캐시 친화적

---

### 3.5 Single-Frame Recv 소켓 옵션 노출

#### 3.5.1 문제

`stream_single_frame_recv_enabled()` (`stream.cpp:17-21`)가 환경변수
`ZLINK_STREAM_SINGLE_FRAME_RECV`에 의존한다:

```cpp
bool stream_single_frame_recv_enabled () {
    const char *env = getenv ("ZLINK_STREAM_SINGLE_FRAME_RECV");
    return env && *env && *env != '0';
}
```

이 모드는 멀티파트(routing_id 프레임 + payload 프레임)를 단일 프레임으로 통합하여
recv 경로의 메시지 생성/복사를 절반으로 줄인다.

#### 3.5.2 구현 방안

**기본값을 뒤집지 않는다.** 기존 사용자 코드와 테스트가 멀티파트(routing_id + payload)
형식을 전제하므로, 기본값 변경은 호환성 파괴 위험이 높다.

대신, `ZLINK_STREAM_SINGLE_FRAME_RECV` **소켓 옵션**으로 노출하여
사용자가 소켓 단위로 opt-in할 수 있도록 한다:

```cpp
// 사용자 코드
int enable = 1;
zlink_setsockopt(sock, ZLINK_STREAM_SINGLE_FRAME_RECV, &enable, sizeof(enable));
```

변경 대상:
- `core/include/zlink.h` — `ZLINK_STREAM_SINGLE_FRAME_RECV` 소켓 옵션 상수 추가
- `core/src/core/options.hpp` — `stream_single_frame_recv` 필드 추가 (기본값: 0)
- `core/src/sockets/stream.cpp` — 환경변수 대신 `options.stream_single_frame_recv` 참조

환경변수는 하위 호환을 위해 폴백으로 유지하되, 소켓 옵션이 우선한다.

#### 3.5.3 난이도/효과

- 난이도: **낮음** (옵션 배관 작업)
- 효과: recv 경로 msg 생성 1회 감소 (opt-in), routing ID 처리 단순화

---

### 3.6 Adaptive Send Batch 크기

> **적용 대상**: `asio_stream_engine_t` (IPC/TLS/WS 전용)

#### 3.6.1 문제

`fill_send_main_buffer()` (`asio_stream_engine.cpp:442`)가 항상
`_send_buffer_limit`(최소 512KB)까지 채운 뒤 전송을 개시한다:

```cpp
while (_send_buffer_main.size () < _send_buffer_limit) {
    if (_session->pull_msg (&msg) == -1) {
        if (errno == EAGAIN) { _output_stopped = true; break; }
        ...
    }
    // 버퍼에 메시지 추가
}
```

저부하 시나리오에서 첫 메시지가 도착해도 나머지 511KB를 기다리지는 않지만
(`EAGAIN`으로 빠져나옴), 고부하에서는 512KB 단위로만 전송이 발생하여
중간 크기 배치의 즉시 전송 기회를 놓칠 수 있다.

#### 3.6.2 근거

CppServer `TrySend()` (`tcp_session.cpp:487-563`):
- flush 버퍼가 비어있으면 main을 즉시 swap하여 전송
- 가용 데이터가 있으면 크기에 관계없이 즉시 전송 개시
- 축적은 flush 중에 main 버퍼에서 자연스럽게 발생

#### 3.6.3 구현 방안

현재 구조에서 `fill_send_main_buffer`는 `EAGAIN`까지 pull하므로,
실제로는 가용한 만큼만 채우는 구조가 이미 동작한다.
다만 `_send_buffer_limit` 자체가 비정상적으로 크면 `vector::resize`가
불필요하게 큰 버퍼를 유지할 수 있다.

**방안**: `_send_buffer_limit`의 최소값을 512KB에서 64KB로 낮추고,
실제 throughput에 따라 동적으로 조정하는 로직은 추후 검토.

변경 대상:
- `core/src/engine/asio/asio_stream_engine.cpp` — 생성자에서 `_send_buffer_limit` 최소값 조정

#### 3.6.4 난이도/효과

- 난이도: **낮음** (상수 변경)
- 효과: 저부하/소형메시지 시나리오에서 전송 대기 시간 감소 가능. 단, 고부하에서 배치 효과 감소 가능성 있으므로 벤치마크 검증 필요.

---

### 3.7 미사용 멤버 변수 제거

> **적용 대상**: `asio_stream_engine_t` (IPC/TLS/WS 전용)

#### 3.7.1 문제

`asio_stream_engine.hpp:105-108`에 미사용 멤버가 4개 존재:

```cpp
msg_t _tx_msg;                  // 105 — 미사용
bool _tx_msg_valid;             // 106 — 미사용
unsigned char _tx_header[4];    // 107 — 미사용
size_t _tx_total_size;          // 108 — 미사용
```

이 멤버들은 hot 구조체의 캐시 라인을 낭비한다.

#### 3.7.2 근거

CGDK10의 `buffer_base`가 `sizeof == 16` (64비트)으로 캐시 라인에 최적화된 것처럼,
hot path 구조체는 최소한의 멤버만 유지해야 한다 (CGDK10 분석 문서 3.1절).

#### 3.7.3 구현 방안

변경 대상:
- `core/src/engine/asio/asio_stream_engine.hpp` — 미사용 4개 멤버 삭제

#### 3.7.4 난이도/효과

- 난이도: **매우 낮음**
- 효과: 구조체 크기 ~80바이트 감소, 캐시 적중률 미세 개선

---

## 4. 구현 우선순위

### Tier 1: 즉시 적용 (낮은 난이도, 확실한 효과)

| # | 항목 | 대상 엔진 | 난이도 | 기대 효과 |
|---|------|----------|--------|----------|
| 3.1 | TCP_NODELAY 소켓 옵션 노출 | 전체 (TCP) | 낮음 | 벤치마크 공정성, 사용자 제어 |
| 3.3 | recv memmove 회피 | IPC/TLS/WS | 낮~중 | CPU 효율 개선 |
| 3.5 | Single-Frame Recv 소켓 옵션 | 전체 | 낮음 | recv 경로 단순화 (opt-in) |
| 3.7 | 미사용 멤버 제거 | IPC/TLS/WS | 매우 낮음 | 캐시 효율 |

### Tier 2: 구조적 개선 (중간 난이도, 높은 효과)

| # | 항목 | 대상 엔진 | 난이도 | 기대 효과 |
|---|------|----------|--------|----------|
| 3.4 | Handler Memory Pool 활성화 | 전체 | 중~상 | malloc/free 감소 |
| 3.2 | push_one_frame zero-copy | IPC/TLS/WS | 상 | 대형 메시지 memcpy 제거 |

### Tier 3: 튜닝 (검증 후 적용)

| # | 항목 | 대상 엔진 | 난이도 | 기대 효과 |
|---|------|----------|--------|----------|
| 3.6 | Adaptive Send Batch | IPC/TLS/WS | 낮음 | latency 개선 (벤치마크 검증 필요) |

---

## 5. 기법 대조표 (CGDK10 / CppServer / zlink)

```
┌──────────────────────┬──────────────┬──────────────┬──────────────────────────┐
│                      │ CGDK10       │ CppServer    │ zlink (현재 + 계획)      │
├──────────────────────┼──────────────┼──────────────┼──────────────────────────┤
│ I/O 모델             │ IOCP/RIO/    │ Asio 기반    │ Asio 기반                │
│                      │ epoll 직접   │ epoll/IOCP   │ epoll/IOCP               │
│                      │              │              │ ✓ 동일                   │
├──────────────────────┼──────────────┼──────────────┼──────────────────────────┤
│ 스레드 모델          │ 스레드풀     │ Per-Thread   │ Per-Thread io_context    │
│                      │              │ io_service   │ ✓ 동일                   │
├──────────────────────┼──────────────┼──────────────┼──────────────────────────┤
│ 송신 버퍼링          │ Scatter-     │ Double-      │ Double-Buffer Swap       │
│                      │ Gather       │ Buffer Swap  │ + Gather Write           │
│                      │              │              │ ✓ 동일 + 확장            │
├──────────────────────┼──────────────┼──────────────┼──────────────────────────┤
│ 수신 Zero-Copy       │ reinterpret  │ 직접 전달    │ TCP: ✓ shared_memory_    │
│                      │ _cast 참조   │ (raw buffer) │   allocator 사용         │
│                      │              │              │ IPC/TLS/WS: ✗ memcpy    │
│                      │              │              │ → 3.2에서 개선 계획      │
├──────────────────────┼──────────────┼──────────────┼──────────────────────────┤
│ 핸들러 메모리        │ Npoolable    │ 1KB Stack    │ ✗ 256B 할당자 코드 존재  │
│                      │ 오브젝트 풀  │ Pool         │   but 미사용 → 3.4 활성화│
├──────────────────────┼──────────────┼──────────────┼──────────────────────────┤
│ 직렬화               │ Zero-Copy    │ 없음 (raw)   │ 4B length prefix         │
│                      │ (reinterpret)│              │ ✓ 경량                   │
├──────────────────────┼──────────────┼──────────────┼──────────────────────────┤
│ 동기화               │ Lock-Free    │ Minimal Lock │ ypipe (Lock-Free)        │
│                      │ CAS Stack    │ + Strand     │ + 단일 I/O 스레드        │
│                      │              │              │ ✓ 동일                   │
├──────────────────────┼──────────────┼──────────────┼──────────────────────────┤
│ TCP_NODELAY          │ 기본 전제    │ 기본 활성화  │ ✓ 기본 활성화            │
│                      │              │              │ △ 소켓 옵션 미노출       │
│                      │              │              │ → 3.1에서 옵션 노출      │
├──────────────────────┼──────────────┼──────────────┼──────────────────────────┤
│ recv 버퍼 관리       │ 포인터 전진  │ 2x 성장      │ 2x 성장 + memmove        │
│                      │ (이동 없음)  │              │ → 3.3에서 개선 계획      │
├──────────────────────┼──────────────┼──────────────┼──────────────────────────┤
│ Bounds Check         │ Release에서  │ N/A          │ 경량 (영향 미미)         │
│                      │ 완전 제거    │              │                          │
├──────────────────────┼──────────────┼──────────────┼──────────────────────────┤
│ 프로토콜 지원        │ TCP          │ TCP/UDP/     │ TCP + STREAM/ZMP/RAW     │
│                      │              │ HTTP/WS/SSL  │                          │
└──────────────────────┴──────────────┴──────────────┴──────────────────────────┘
```

---

## 6. 검증 방법

각 개선 항목 적용 후:

```bash
# 1. 단위/기능 테스트
cd core/build && ctest --output-on-failure

# 2. STREAM 전용 기능 테스트
./core/tests/test_stream_socket
./core/tests/test_stream_fastpath

# 3. 시나리오 벤치마크 (smoke)
cd core/tests/scenario/stream
./run_stream_compare.sh --stack zlink --ccu 100 --duration 2

# 4. 시나리오 벤치마크 (full, 5-stack 비교)
./run_stream_compare.sh --ccu 10000 --duration 5 --repeats 3
```

Tier 1 항목은 개별 적용 + 테스트, Tier 2 항목은 Tier 1 완료 후 순차 적용하여
각 단계의 효과를 독립적으로 측정한다.

---

## 7. 관련 코드 경로

| 컴포넌트 | 파일 | 핵심 위치 |
|---------|------|----------|
| TCP 소켓 튜닝 | `core/src/transports/tcp/tcp.cpp` | `tune_tcp_socket()` L30-41 |
| TCP Listener | `core/src/transports/tcp/asio_tcp_listener.cpp` | L351 (엔진 선택), L389 (tune) |
| TCP Connecter | `core/src/transports/tcp/asio_tcp_connecter.cpp` | L395 (STREAM 비활성), L417 (tune) |
| ASIO Raw 엔진 (TCP) | `core/src/engine/asio/asio_raw_engine.cpp` | `plug_internal()` L53-99 |
| ASIO Base 엔진 | `core/src/engine/asio/asio_engine.cpp` | Gather write L578-637 |
| ASIO Stream 엔진 (IPC/TLS/WS) | `core/src/engine/asio/asio_stream_engine.cpp` | 전체 |
| ASIO Stream 엔진 헤더 | `core/src/engine/asio/asio_stream_engine.hpp` | L96-108 |
| Handler 할당자 | `core/src/engine/asio/handler_allocator.hpp` | 전체 (256B 인라인) |
| STREAM 소켓 | `core/src/sockets/stream.cpp` | `xsend()`, `xrecv()` |
| Fast Encoder | `core/src/protocol/stream_fast_encoder.cpp` | 전체 |
| Fast Decoder | `core/src/protocol/stream_fast_decoder.cpp` | `payload_size_ready()` |
| Decoder Allocator | `core/src/protocol/decoder_allocators.hpp` | `shared_message_memory_allocator` |
| 소켓 옵션 | `core/src/core/options.hpp` | tcp_nodelay 관련 |
| 공개 API | `core/include/zlink.h` | `ZLINK_TCP_NODELAY` (미정의 상태) |
| 벤치마크 스크립트 | `core/tests/scenario/stream/run_stream_compare.sh` | 전체 |
| zlink 시나리오 | `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp` | L71-74 |

---

## 8. 참고 문서

### 8.1 내부 문서

- [CGDK10 아키텍처 분석](../study/cgdk10-architecture-analysis.md) — Zero-Copy, Lock-Free, Scatter-Gather
- [CppServer 아키텍처 분석](../study/cppserver-architecture-analysis.md) — Double-Buffer, Handler Pool, IO-Per-Thread
- [ASIO 성능 개선 포인트](./asio-performance-improvement-points.ko.md) — 기존 적용 이력
- [STREAM 5-Stack Parity 계획](./stream-4stack-parity-and-zlink-improvement.ko.md) — 기준선 및 이력
- [Codex STREAM 성능 계획](./codex-stream-socket-performance-plan.ko.md) — TCP 엔진 경로 분석
- [고성능 STREAM 소켓 스펙](./high-performance-stream-socket-specification.ko.md)
- [STREAM CS Fastpath 설계안](./stream-cs-fastpath-cppserver-based.ko.md)

### 8.2 외부 참고

- [Boost.Asio 공식 문서](https://www.boost.org/doc/libs/release/doc/html/boost_asio.html)
- [CppServer GitHub](https://github.com/chronoxor/CppServer)
- [CGDK10 GitHub](https://github.com/CGLabs/CGDK10.Cpp)
