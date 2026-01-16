# IPC 데드락 해결 방안

## 문제 요약

Codex 분석에 따르면, 현재 IPC 데드락은 다음 원인으로 발생:
1. **Read 재무장 누락**: `restart_input()`에서 `!_read_pending` 체크 후 `start_async_read()` 호출 사이에 race window 존재
2. **IPC의 초고속 특성**: 데이터 도착이 async_read 재무장보다 빨라서 데드락 발생
3. **Pending buffer 처리 경계 문제**: `_input_stopped=false`인데 `_pending_buffers`가 남아있을 수 있음

## 해결 방안 선택지

### Option 1: Speculative Read (libzmq 방식) - 권장 ✅

**개념**: backpressure 해제 후 동기 read를 먼저 시도하고, EAGAIN이면 async read로 전환

**구현**:
```cpp
bool zmq::asio_engine_t::restart_input ()
{
    // ... pending buffers 처리 ...

    _input_stopped = false;
    _session->flush ();

    ENGINE_DBG ("restart_input: completed, attempting speculative read");

    //  Speculative read: 즉시 한 번 더 읽기 시도 (libzmq 패턴)
    //  IPC처럼 빠른 전송에서 read 재무장 누락을 방지
    boost::system::error_code ec;
    size_t bytes = _socket->read_some(
        boost::asio::buffer(_read_buffer_ptr, _insize),
        ec);

    if (!ec && bytes > 0) {
        //  데이터를 즉시 읽었음 - 처리
        ENGINE_DBG ("speculative read: got %zu bytes", bytes);
        process_input_data(bytes);

        //  계속 읽을 데이터가 있을 수 있으니 async read 시작
        start_async_read ();
    } else if (ec == boost::asio::error::would_block) {
        //  EAGAIN - 정상, async read 시작
        ENGINE_DBG ("speculative read: EAGAIN, starting async");
        start_async_read ();
    } else {
        //  에러 발생
        error (connection_error);
        return false;
    }

    return true;
}
```

**장점**:
- libzmq와 동일한 패턴 (검증된 설계)
- IPC 초고속 환경에서 안정적
- Read 재무장 누락을 구조적으로 방지

**단점**:
- 동기 read 호출이 추가됨 (하지만 IPC는 거의 즉시 반환)
- 코드 복잡도 약간 증가

**예상 효과**: 데드락 완전 해결, 40% 실패율 → 0%

---

### Option 2: Unconditional start_async_read() - 간단한 대안

**개념**: `restart_input()` 끝에서 `_read_pending` 체크 없이 무조건 `start_async_read()` 호출

**구현**:
```cpp
bool zmq::asio_engine_t::restart_input ()
{
    // ... pending buffers 처리 ...

    _input_stopped = false;
    _session->flush ();

    ENGINE_DBG ("restart_input: completed, forcing async read restart");

    //  항상 async read 재시작 (idempotent하게 설계)
    //  start_async_read()는 이미 _read_pending 체크를 하므로 안전
    start_async_read ();

    return true;
}
```

**장점**:
- 구현 매우 간단
- `start_async_read()`는 이미 `_read_pending` 체크를 하므로 중복 호출 방지됨

**단점**:
- Race condition이 완전히 해결되지 않을 수 있음
- `_read_pending`이 false가 되는 타이밍과 재무장 사이에 여전히 window 존재

**예상 효과**: 40% 실패율 → 10-20% 수준으로 개선 (완전 해결 아님)

---

### Option 3: ASIO Strand 직렬화 - 근본적 해결

**개념**: `on_read_complete()`와 `restart_input()` 모두를 동일 strand에서 실행하여 완전 직렬화

**구현**:
```cpp
// src/asio/asio_engine.hpp
class asio_engine_t {
    // ...
    boost::asio::io_context::strand _strand;
};

// src/asio/asio_engine.cpp
zmq::asio_engine_t::asio_engine_t (/* ... */)
    : // ...
      _strand (*io_context),
      // ...
{
}

void zmq::asio_engine_t::start_async_read ()
{
    // ...
    _socket->async_read_some(
        boost::asio::buffer(_read_buffer_ptr, _insize),
        boost::asio::bind_executor(
            _strand,
            [this](const boost::system::error_code &ec, std::size_t bytes) {
                on_read_complete(ec, bytes);
            }));
}

// restart_input()도 strand에서 실행되도록 보장
bool zmq::asio_engine_t::restart_input ()
{
    //  이 함수가 strand 외부에서 호출될 수 있으므로 post
    if (!_strand.running_in_this_thread()) {
        boost::asio::post(_strand, [this]() {
            restart_input_impl();
        });
        return true;
    }

    return restart_input_impl();
}

bool zmq::asio_engine_t::restart_input_impl ()
{
    //  실제 구현 (기존 코드)
    // ...
}
```

**장점**:
- Race condition 완전 해결
- ASIO 권장 패턴
- Thread-safety 보장

**단점**:
- 구현 복잡도 높음
- 기존 코드 많이 수정 필요
- Session/pipe 계층도 strand 고려 필요

**예상 효과**: 데드락 완전 해결, 모든 race condition 제거

---

## 권장 구현 순서

### Phase 1: Speculative Read (Option 1) - 즉시 적용 ✅

**이유**:
- libzmq 검증된 패턴
- 구현 간단 (1시간 이내)
- 즉시 효과 확인 가능

**단계**:
1. `restart_input()` 끝에 speculative read 추가
2. `process_input_data()` 헬퍼 함수 추가 (기존 `on_read_complete()` 로직 재사용)
3. 테스트: `BENCH_MSG_COUNT=10000 ./build/bin/comp_zlink_pair zlink ipc 64`
4. 성공하면 모든 패턴 테스트

**성공 기준**:
- 10K 메시지: 10회 연속 성공
- 200K 메시지: 5회 연속 성공

### Phase 2: Strand 직렬화 (Option 3) - 조건부

**조건**: Phase 1이 충분하지 않거나, 다른 race condition 발견 시

**이유**:
- 근본적 해결
- 향후 확장성 보장

**단계**:
1. `_strand` 멤버 추가
2. `async_read_some()` 핸들러에 `bind_executor` 적용
3. `restart_input()` strand 체크 및 post
4. 전체 테스트 스위트 실행

---

## 검증 방법

### 1. 기본 테스트
```bash
# 2K 메시지 (40% 실패율 예상)
for i in {1..10}; do
    BENCH_MSG_COUNT=2000 timeout 10 ./build/bin/comp_zlink_pair zlink ipc 64 || echo "FAIL $i";
done

# 10K 메시지
for i in {1..5}; do
    BENCH_MSG_COUNT=10000 timeout 30 ./build/bin/comp_zlink_pair zlink ipc 64 || echo "FAIL $i";
done
```

### 2. 전체 패턴 테스트
```bash
for pattern in pair pubsub dealer_dealer router_router; do
    echo "=== Testing $pattern ==="
    BENCH_MSG_COUNT=10000 ./build/bin/comp_zlink_${pattern} zlink ipc 64
done
```

### 3. 성능 회귀 검증
```bash
# TCP도 여전히 정상 작동하는지 확인
./build/bin/comp_zlink_pair zlink tcp 64
./build/bin/comp_zlink_router_router zlink tcp 64
```

---

## 다음 단계

**즉시 실행**: Phase 1 (Speculative Read) 구현
**예상 시간**: 1-2시간
**예상 효과**: IPC 데드락 완전 해결

구현 후 결과를 `docs/team/20260116_ipc-deadlock-debug/test_results.md`에 기록.
