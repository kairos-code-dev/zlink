# Phase 3 구현 결과 (Strand 직렬화)

## 구현된 수정사항

### Phase 3: Strand 직렬화 구현

**구현 내용:**

1. **`asio_engine.hpp` - Strand 멤버 추가 (line 223-224)**
   ```cpp
   std::unique_ptr<boost::asio::strand<boost::asio::io_context::executor_type>> _strand;
   ```

2. **`asio_engine.cpp` - `plug()` 메서드에서 Strand 초기화 (line 209-211)**
   ```cpp
   _strand.reset(new boost::asio::strand<boost::asio::io_context::executor_type>(
       _io_context->get_executor()));
   ```

3. **`asio_engine.cpp` - `start_async_read()` 핸들러 래핑 (line 376-380)**
   ```cpp
   _transport->async_read_some(
       _read_buffer_ptr, read_size,
       boost::asio::bind_executor(*_strand, [this](ec, bytes) {
           on_read_complete(ec, bytes);
       }));
   ```

4. **`asio_engine.cpp` - `start_async_write()` 핸들러 래핑 (line 432-436)**
   ```cpp
   _transport->async_write_some(
       _write_buffer.data(), _write_buffer.size(),
       boost::asio::bind_executor(*_strand, [this](ec, bytes) {
           on_write_complete(ec, bytes);
       }));
   ```

5. **`asio_engine.cpp` - `restart_input()` 직렬화 (line 901-913)**
   ```cpp
   bool zmq::asio_engine_t::restart_input()
   {
       boost::asio::dispatch(*_strand, [this]() { restart_input_internal(); });
       return true;  // Always return true
   }
   ```

6. **`asio_engine.cpp` - `restart_input_internal()` 분리 (line 915-1100)**
   - 기존 `restart_input()` 로직을 `restart_input_internal()`로 분리
   - 재귀 호출 시 `restart_input_internal()` 직접 호출 (line 1089)

## 테스트 결과

### 2K 메시지 10회 반복 테스트

| Run | 결과 | Throughput (M/s) |
|-----|------|------------------|
| 1 | **FAIL** | - |
| 2 | SUCCESS | 2.72 |
| 3 | SUCCESS | 2.77 |
| 4 | **FAIL** | - |
| 5 | **FAIL** | - |
| 6 | **FAIL** | - |
| 7 | SUCCESS | 2.12 |
| 8 | SUCCESS | 2.72 |
| 9 | SUCCESS | 2.34 |
| 10 | SUCCESS | 2.90 |

**성공률: 60% (10회 중 6회)**

### 10K 메시지 5회 반복 테스트

| Run | 결과 |
|-----|------|
| 1 | **FAIL** |
| 2 | **FAIL** |
| 3 | **FAIL** |
| 4 | **FAIL** |
| 5 | **FAIL** |

**성공률: 0% (5회 중 0회)**

## 결과 비교

| Phase | 2K 성공률 | 10K 성공률 | 변화 |
|-------|----------|-----------|------|
| Phase 2a (Double-check 전) | 70% | 0% | Baseline |
| Phase 2b (flush 후 check) | 70% | 미테스트 | 변화 없음 |
| **Phase 3 (Strand)** | **60%** | **0%** | **악화** ⚠️ |

## 분석

### 예상과 다른 결과
Codex와 Gemini 모두 Strand 직렬화로 100% 성공률을 예상했으나, 실제로는:
- 2K 메시지: 70% → 60% (**-10% 악화**)
- 10K 메시지: 0% → 0% (변화 없음)

### 가능한 원인

#### Hypothesis A: dispatch의 비동기 특성
```cpp
bool zmq::asio_engine_t::restart_input()
{
    boost::asio::dispatch(*_strand, [this]() { restart_input_internal(); });
    return true;  // ← 즉시 반환
}
```

**문제점:**
- `dispatch`는 현재 스레드가 strand 안에 있으면 즉시 실행하지만, **아니면 post처럼 비동기 실행**
- `restart_input_internal()`의 실제 반환값이 손실됨
- Session이 `restart_input()`을 호출하는 스레드가 어디인지 불명확

#### Hypothesis B: Timer 핸들러 미래핑
Gemini 제안에서 언급된 부분:
- `add_timer()`의 `async_wait()` 핸들러도 `bind_executor` 필요 (line 1362-1364)
- 현재 미구현 상태

하지만 timer는 handshake/heartbeat용이므로 IPC deadlock과 직접 연관 낮음.

#### Hypothesis C: Strand 오버헤드
- Strand 직렬화로 인한 **추가 지연**이 IPC 초고속 환경에서 오히려 타이밍 문제 악화
- Event loop tick이 늦어져서 pending buffer가 더 많이 쌓임

#### Hypothesis D: restart_input 호출 경로 문제
- `restart_input()`이 session에서 호출되는데, session의 실행 컨텍스트가 이미 다른 strand에 있을 가능성
- 이 경우 `dispatch`가 post로 동작하여 **실행 순서 보장 실패**

## 미구현 사항

### 1. Timer 핸들러 래핑
`add_timer()` (line 1362-1364):
```cpp
_timer->async_wait([this, id_](const boost::system::error_code &ec) {
    on_timer(id_, ec);
});
```

**해야 할 수정:**
```cpp
_timer->async_wait(boost::asio::bind_executor(*_strand,
    [this, id_](const boost::system::error_code &ec) {
        on_timer(id_, ec);
    }));
```

### 2. Transport handshake 핸들러 래핑
`start_transport_handshake()` (line 238-242):
```cpp
_transport->async_handshake(
    handshake_type,
    [this](const boost::system::error_code &ec, std::size_t) {
        on_transport_handshake(ec);
    });
```

**해야 할 수정:**
```cpp
_transport->async_handshake(
    handshake_type,
    boost::asio::bind_executor(*_strand,
        [this](const boost::system::error_code &ec, std::size_t) {
            on_transport_handshake(ec);
        }));
```

## 질문사항

### Codex에게:
1. `dispatch` vs `post` 차이가 문제인가?
   - `dispatch`는 현재 스레드가 strand 안에 있으면 즉시 실행, 아니면 post
   - `restart_input()`을 호출하는 session의 실행 컨텍스트는?
2. `restart_input_internal()`의 반환값 손실이 문제인가?
3. Timer 핸들러 미래핑이 60% 실패의 원인일 수 있는가?
4. Strand 오버헤드가 IPC 초고속 환경에서 오히려 해가 되는가?

### Gemini에게:
1. Phase 3 구현이 제안한 내용과 일치하는가?
2. 왜 성능이 악화되었는가? (70% → 60%)
3. Timer 핸들러와 Transport handshake 핸들러도 래핑해야 하는가?
4. `dispatch`가 아닌 다른 방식(예: `post`)을 써야 하는가?
5. 아니면 Strand 접근 자체가 잘못되었는가?

## 제안하는 다음 단계

### Option 1: 미구현 사항 보완 후 재테스트
- Timer 핸들러 래핑
- Transport handshake 핸들러 래핑
- 2K/10K 재테스트

### Option 2: dispatch → post 변경
```cpp
boost::asio::post(*_strand, [this]() { restart_input_internal(); });
```
- 항상 비동기 실행하여 일관성 확보

### Option 3: Phase 2a로 롤백 + Speculative Read 검토
- Strand가 해결책이 아닐 가능성
- Option B (Speculative Read) 재검토 필요

### Option 4: 더 상세한 디버그
- `ENGINE_DBG` 활성화 (`-DZMQ_ASIO_DEBUG=1`)
- Strand 실행 경로 추적
- Race condition 정확한 시점 파악

## 요청사항

현재 Phase 3 Strand 직렬화 결과를 분석하고 다음 중 하나를 권장해주세요:

**Option 1**: Timer/Handshake 핸들러 추가 래핑 후 재시도
**Option 2**: `dispatch` → `post` 변경
**Option 3**: Strand 접근 포기, Speculative Read로 전환
**Option 4**: 다른 접근 제안

분석 결과를 각각:
- `docs/team/20260116_ipc-deadlock-debug/codex_phase3_analysis.md`
- `docs/team/20260116_ipc-deadlock-debug/gemini_phase3_analysis.md`

에 작성해주세요.
