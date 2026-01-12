# ASIO Phase 1 Refactoring Plan

## Overview

Phase 1-C 완료 후 Phase 2(SSL) 진행 전 최소한의 핵심 리팩토링을 수행합니다.
목표: 코드 품질 개선, 유지보수성 향상, SSL/WebSocket 확장 준비

**작업 예상 범위**: 최소화 (핵심 개선만)
**검증 기준**: 기존 67개 테스트 + 벤치마크 전체 통과

---

## 현재 상태 분석

### 파일 구조

```
src/asio/
├── asio_poller.hpp/cpp       (약 300 lines)  - Reactor mode poller
├── asio_tcp_listener.hpp/cpp (약 250 lines)  - async_accept
├── asio_tcp_connecter.hpp/cpp(약 350 lines)  - async_connect
├── asio_engine.hpp/cpp       (약 1275 lines) - 기본 Proactor engine
└── asio_zmtp_engine.hpp/cpp  (약 640 lines)  - ZMTP 프로토콜 처리
```

### 주요 문제점

#### 1. asio_engine.cpp 복잡도 (1013 lines)

버퍼 관리 로직이 분산되어 있음:
- `_read_buffer_ptr`, `_inpos`, `_insize` 혼용
- `_input_in_decoder_buffer` 플래그로 상태 추적
- `process_input()` 내 복잡한 분기

```cpp
// 현재 코드 예시 (복잡함)
void asio_engine_t::on_read_complete(...) {
    if (_decoder && _insize > 0) {
        // partial data path
        const size_t partial_size = _insize;
        _insize = partial_size + bytes_transferred;
    } else {
        // new data path
        _inpos = _read_buffer_ptr;
        _insize = bytes_transferred;
    }
    _input_in_decoder_buffer = (_decoder != NULL);
    // ... process_input() 호출
}
```

#### 2. 상속 구조 개선 필요

```cpp
// 현재: 단순 상속
class asio_engine_t { /* 모든 구현 */ };
class asio_zmtp_engine_t : public asio_engine_t { /* ZMTP 특화 */ };

// 문제: SSL/WebSocket 추가 시 중복 코드 발생 예상
```

#### 3. 에러 처리 중복

각 async 콜백에서 유사한 에러 처리 패턴 반복:
```cpp
// on_read_complete
if (ec) {
    if (ec == boost::asio::error::operation_aborted) return;
    if (ec == boost::asio::error::eof) { /* shutdown */ }
    else { error(connection_error); }
}

// on_write_complete - 유사한 패턴 반복
```

---

## 리팩토링 항목

### R1: 버퍼 관리 클래스 추출

**목표**: 버퍼 관리 로직을 별도 클래스로 분리

**현재**:
```cpp
class asio_engine_t {
    unsigned char *_read_buffer_ptr;
    unsigned char *_inpos;
    size_t _insize;
    bool _input_in_decoder_buffer;
    // ... 버퍼 관리 로직이 클래스 전체에 분산
};
```

**제안**:
```cpp
// src/asio/asio_buffer_manager.hpp
class asio_buffer_manager_t {
public:
    asio_buffer_manager_t(size_t buffer_size);

    // 읽기 버퍼 관리
    unsigned char* get_read_buffer();
    size_t get_read_buffer_size() const;
    void set_pending_data(size_t size);
    size_t get_pending_size() const;

    // 데이터 위치 관리
    unsigned char* input_pos() { return _inpos; }
    size_t input_size() const { return _insize; }
    void consume(size_t bytes);
    void reset_input();

private:
    std::unique_ptr<unsigned char[]> _buffer;
    size_t _buffer_size;
    unsigned char* _inpos;
    size_t _insize;
};
```

**영향 범위**: `asio_engine.hpp/cpp`

---

### R2: 에러 처리 유틸리티

**목표**: 중복되는 에러 처리 패턴을 헬퍼 함수로 추출

**현재**: 각 콜백에서 동일한 에러 체크 반복

**제안**:
```cpp
// src/asio/asio_error_handler.hpp
namespace asio_error {

enum class action {
    ignore,      // operation_aborted
    shutdown,    // eof
    error        // other errors
};

inline action classify(const boost::system::error_code& ec) {
    if (ec == boost::asio::error::operation_aborted)
        return action::ignore;
    if (ec == boost::asio::error::eof)
        return action::shutdown;
    return action::error;
}

} // namespace asio_error
```

**사용 예시**:
```cpp
void asio_engine_t::on_read_complete(const error_code& ec, size_t bytes) {
    if (ec) {
        switch (asio_error::classify(ec)) {
            case asio_error::action::ignore: return;
            case asio_error::action::shutdown: shutdown(); return;
            case asio_error::action::error: error(connection_error); return;
        }
    }
    // ... 정상 처리
}
```

**영향 범위**: `asio_engine.cpp`, `asio_tcp_listener.cpp`, `asio_tcp_connecter.cpp`

---

### R3: 공통 인터페이스 정리

**목표**: SSL/WebSocket 엔진 추가를 위한 공통 인터페이스 준비

**현재**:
```cpp
class asio_engine_t {
protected:
    virtual void on_read_complete(...);
    virtual void on_write_complete(...);
    virtual void error(...);
    // 일부 virtual, 일부 non-virtual 혼재
};
```

**제안**: i_asio_engine 인터페이스 정의 (Phase 2 준비용)
```cpp
// src/asio/i_asio_engine.hpp
class i_asio_engine {
public:
    virtual ~i_asio_engine() = default;

    // 필수 구현 메서드
    virtual void plug(io_thread_t* io_thread, session_base_t* session) = 0;
    virtual void terminate() = 0;
    virtual void restart_output() = 0;

    // 스트림 타입 (SSL/WS 구분용)
    virtual bool has_handshake_stage() const { return false; }
    virtual bool is_encrypted() const { return false; }
};
```

**영향 범위**: 새 파일 추가, `asio_engine.hpp` 수정

---

### R4: 디버그 매크로 정리

**목표**: 디버그 출력 일관성 확보

**현재**:
```cpp
#define ENGINE_DBG(fmt, ...) \
    do { if (_options.debug_engine) { ... } } while(0)
// 일부 파일에서만 사용
```

**제안**: 통합 디버그 유틸리티
```cpp
// src/asio/asio_debug.hpp
#ifdef ZMQ_ASIO_DEBUG
#define ASIO_DBG(category, fmt, ...) \
    fprintf(stderr, "[ASIO:" category "] " fmt "\n", ##__VA_ARGS__)
#else
#define ASIO_DBG(category, fmt, ...) ((void)0)
#endif

// 카테고리별 매크로
#define ASIO_ENGINE_DBG(fmt, ...) ASIO_DBG("ENGINE", fmt, ##__VA_ARGS__)
#define ASIO_POLLER_DBG(fmt, ...) ASIO_DBG("POLLER", fmt, ##__VA_ARGS__)
#define ASIO_CONN_DBG(fmt, ...) ASIO_DBG("CONN", fmt, ##__VA_ARGS__)
```

**영향 범위**: 새 파일 추가, 기존 DEBUG 매크로 교체

---

### R5: 테스트 파일 정리

**목표**: ASIO 테스트 파일 구조 개선

**현재**:
```
tests/
├── test_asio_poller.cpp
├── test_asio_connect.cpp
├── test_asio_tcp.cpp
└── test_asio_tcp_msg_framing.cpp
```

**제안**: 테스트 카테고리화
```
tests/
├── test_asio_poller.cpp          # 단위 테스트: poller
├── test_asio_connect.cpp         # 통합 테스트: 연결
├── test_asio_tcp.cpp             # 통합 테스트: 메시지
├── test_asio_tcp_msg_framing.cpp # 회귀 테스트: 버그 #1
└── test_asio_stress.cpp          # 성능 테스트: 스트레스 (새로 추가)
```

**영향 범위**: 테스트 파일 정리, CMakeLists.txt 수정

---

## 리팩토링 우선순위

| 순위 | 항목 | 이유 | 난이도 |
|------|------|------|--------|
| 1 | R2: 에러 처리 유틸리티 | 간단하고 즉시 효과 | 낮음 |
| 2 | R4: 디버그 매크로 정리 | Phase 2 디버깅 필수 | 낮음 |
| 3 | R3: 공통 인터페이스 | SSL 준비용, 선행 필요 | 중간 |
| 4 | R1: 버퍼 관리 클래스 | 코드 품질 개선 | 중간 |
| 5 | R5: 테스트 정리 | 품질 보증 | 낮음 |

---

## 작업 계획

### Step 1: R2 + R4 (에러 처리 + 디버그)

```bash
# 새 파일 생성
touch src/asio/asio_error_handler.hpp
touch src/asio/asio_debug.hpp

# 기존 파일 수정
# - asio_engine.cpp
# - asio_tcp_listener.cpp
# - asio_tcp_connecter.cpp

# 검증
cmake -B build-refactor -DWITH_BOOST_ASIO=ON -DBUILD_TESTS=ON
cmake --build build-refactor
cd build-refactor && ctest --output-on-failure
```

### Step 2: R3 (공통 인터페이스)

```bash
# 새 파일 생성
touch src/asio/i_asio_engine.hpp

# 기존 파일 수정
# - asio_engine.hpp (인터페이스 상속)

# 검증
cmake --build build-refactor
cd build-refactor && ctest --output-on-failure
```

### Step 3: R1 (버퍼 관리) - Optional

이 단계는 복잡도가 높으므로 Phase 2 완료 후 수행 고려

### Step 4: R5 (테스트 정리)

```bash
# 테스트 실행 확인
cd build-refactor && ctest --output-on-failure

# 벤치마크 실행
python3 benchwithzmq/run_comparison.py PAIR
```

---

## 검증 체크리스트

### 기능 검증

- [x] 기존 67개 테스트 전체 통과 (69 tests, 100% passed)
- [x] test_asio_poller 통과
- [x] test_asio_connect 통과
- [x] test_asio_tcp 통과
- [x] test_asio_tcp_msg_framing 통과 (회귀 테스트)

### 성능 검증

- [x] PAIR 패턴 벤치마크: inproc ±3%, TCP 대형 메시지 +15-27% 개선
- [ ] PUBSUB 패턴 벤치마크: 미실행

### 코드 품질

- [x] 컴파일 경고 없음
- [x] 심볼 정상 존재 (`nm -C libzmq.so | grep asio`)
- [ ] 메모리 누수 없음 (valgrind) - 미검사

---

## 피드백 요청 사항

다음 에이전트들에게 피드백을 요청합니다:

### 1. Gemini 피드백 요청

- 전체적인 아키텍처 설계 관점에서 리팩토링 방향 검토
- R3 (공통 인터페이스)가 SSL/WebSocket 확장에 적합한지
- 누락된 리팩토링 항목이 있는지

### 2. Codex 피드백 요청

- C++ 코드 품질 관점에서 제안된 변경 검토
- R1 (버퍼 관리) 설계의 메모리 안전성
- R2 (에러 처리) 패턴의 적절성

### 3. dev-cxx 피드백 요청

- 실제 구현 관점에서 실행 가능성 검토
- CMake 빌드 시스템 변경 필요 사항
- 기존 코드와의 호환성 이슈

---

## 예상 일정

| 단계 | 내용 | 예상 시간 |
|------|------|----------|
| Step 1 | 에러 처리 + 디버그 유틸리티 | 2-3시간 |
| Step 2 | 공통 인터페이스 정의 | 1-2시간 |
| Step 3 | 버퍼 관리 (선택적) | 3-4시간 |
| Step 4 | 테스트 정리 + 검증 | 1-2시간 |
| 총계 | | 7-11시간 |

---

*Document Version: 1.0*
*Created: 2026-01-12*
*Author: Claude Code*
