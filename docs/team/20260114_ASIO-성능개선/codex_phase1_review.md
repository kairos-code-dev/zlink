# Phase 1 구현 코드 리뷰 - Transport 인터페이스 확장

**리뷰어:** Codex (Code Analysis Agent)
**리뷰 날짜:** 2026-01-14
**리뷰 대상:** Phase 1: Transport 인터페이스 확장 (동기 write_some 지원)

---

## 1. 총평

### 구현 품질: **우수 (A-)**

Phase 1 구현은 계획된 요구사항을 **정확히 충족**하며, 코드 품질과 일관성 면에서 높은 수준을 보여줍니다.

**강점:**
- 모든 4개 transport(TCP/TLS/WS/WSS)에서 `write_some()` 동기 메서드를 성공적으로 구현
- 에러 처리가 일관되고 정확하게 errno 매핑
- WebSocket의 frame-based 전송 특성을 정확히 이해하고 구현
- would_block 동작이 모든 transport에서 통일됨
- 주석이 상세하고 설계 의도가 명확히 문서화됨

**개선 필요 사항:**
- WebSocket/WSS의 동기 `write()` 호출이 blocking 가능성 (경미한 리스크)
- SSL 에러 카테고리 처리에서 일부 불일치
- 일부 edge case에 대한 명시적 테스트 필요

### 계획 부합도: **100%**

`docs/team/20260114_ASIO-성능개선/plan.md`의 Phase 1 요구사항 완전 충족:

- ✅ `i_asio_transport` 인터페이스에 `write_some(const uint8_t* data, size_t len)` 추가
- ✅ TCP/TLS transport 구현 (stream_descriptor/ssl_stream의 write_some 래핑)
- ✅ WebSocket/WSS transport 구현 (frame 단위 전송)
- ✅ would_block 시 EAGAIN/EWOULDBLOCK 반환
- ✅ 기존 async API와 공존

---

## 2. 세부 검토

### 2.1 인터페이스 설계 (`i_asio_transport.hpp`)

#### ✅ 시그니처 적절성

```cpp
virtual std::size_t write_some (const std::uint8_t *data, std::size_t len) = 0;
```

**평가:** 우수
- 반환 타입: `std::size_t` - 부분 전송을 표현 가능
- 에러 전달: `errno` 사용 - POSIX 스타일 일관성 유지
- 파라미터: const 포인터 + 길이 - 표준 C++ 관례 준수

#### ✅ 주석 문서화

Lines 71-92의 주석이 매우 상세함:
- 반환값 의미 명확히 설명
- would_block vs 실제 에러 구분 방법 제시
- Transport별 동작 차이 명시 (TCP/TLS: 부분 쓰기, WebSocket: frame 전체)
- Handshake 완료 후 호출 제약 명시

**권장사항:** 주석에 사용 예시 추가 고려
```cpp
//  Example:
//    size_t bytes = transport->write_some(data, len);
//    if (bytes == 0) {
//      if (errno == EAGAIN) { /* would_block - retry later */ }
//      else { /* actual error */ }
//    }
```

---

### 2.2 TCP Transport (`tcp_transport.cpp`)

#### ✅ 구현 정확성 (Lines 138-192)

**Windows 경로:**
```cpp
bytes_written = _socket->write_some (boost::asio::buffer (data, len), ec);
```

**POSIX 경로:**
```cpp
bytes_written = _stream_descriptor->write_some (boost::asio::buffer (data, len), ec);
```

**평가:** 우수
- Platform별 분기 정확
- ASIO의 동기 write_some을 직접 사용 (올바른 래핑)
- 부분 쓰기 정상 처리 (bytes_written < len 허용)

#### ✅ 에러 처리 일관성 (Lines 168-191)

| Boost 에러 | errno 매핑 | 평가 |
|-----------|-----------|------|
| `would_block`, `try_again` | `EAGAIN` | ✅ 정확 |
| `broken_pipe`, `connection_reset` | `EPIPE` | ✅ 정확 |
| `not_connected` | `ENOTCONN` | ✅ 정확 |
| `bad_descriptor` | `EBADF` | ✅ 정확 |
| 기타 | `EIO` | ✅ 안전한 fallback |

**권장사항:** 없음. 에러 매핑이 완벽함.

#### ✅ 상태 검증 (Lines 148-152, 158-161)

```cpp
if (!_socket || !_socket->is_open ()) {
    errno = EBADF;
    return 0;
}
```

**평가:** 우수
- Transport 상태를 먼저 확인
- nullptr 체크 후 is_open() 호출 (crash 방지)
- 적절한 에러 코드 설정

---

### 2.3 SSL Transport (`ssl_transport.cpp`)

#### ✅ SSL 계층 처리 (Lines 120-179)

```cpp
bytes_written = _ssl_stream->write_some (boost::asio::buffer (data, len), ec);
```

**평가:** 우수
- SSL stream의 동기 write_some 직접 호출
- SSL 레코드 크기 제약 자동 처리됨
- Handshake 완료 검증 (`_handshake_complete` 체크)

#### ⚠️ SSL 에러 처리 개선 필요 (Lines 154-159)

```cpp
if (ec.category () == boost::asio::error::get_ssl_category ()) {
    errno = EIO;
    return 0;
}
```

**문제점:** SSL 에러를 모두 `EIO`로 통합
- SSL renegotiation 필요 시: `SSL_ERROR_WANT_READ/WRITE` → 현재는 `EIO`로 처리
- 실제로는 EAGAIN이 더 적절할 수 있음 (일시적 상태)

**권장사항:** SSL 특정 에러 세분화
```cpp
if (ec.category () == boost::asio::error::get_ssl_category ()) {
    // SSL_ERROR_WANT_READ/WRITE는 would_block과 유사
    if (ec.value () == SSL_ERROR_WANT_READ
        || ec.value () == SSL_ERROR_WANT_WRITE) {
        errno = EAGAIN;
        return 0;
    }
    // 실제 SSL 에러는 EIO
    errno = EIO;
    return 0;
}
```

**우선순위:** Minor - 현행 구현도 동작하나, 성능 최적화 시 개선 권장

#### ✅ 기타 에러 처리

EOF 처리 추가 (Line 169-171):
```cpp
else if (ec == boost::asio::error::eof) {
    errno = ECONNRESET;
}
```

**평가:** 우수 - TCP와 달리 EOF를 명시적 처리 (SSL 특성 반영)

---

### 2.4 WebSocket Transport (`ws_transport.cpp`)

#### ✅ Frame-Based 전송 설계 (Lines 220-290)

**핵심 설계 결정:**
```cpp
// Line 242-250 주석
// WebSocket is frame-based protocol.
// We must write the complete frame atomically to maintain protocol integrity.
// Beast's write() sends a complete frame synchronously.
```

**평가:** 탁월
- WebSocket 프로토콜의 **frame 경계 무결성** 보장
- `write()` 사용 (frame 전체 전송) vs `write_some()` (부분 전송 불가)
- 주석으로 설계 의도 명확히 문서화

#### ⚠️ Blocking 가능성 (Lines 257-258)

```cpp
bytes_written = _ws_stream->write (boost::asio::buffer (data, len), ec);
```

**잠재적 문제:**
- Beast의 `websocket::stream::write()`는 **전체 frame을 전송할 때까지 blocking**
- 소켓 버퍼가 가득 찬 경우: would_block 대신 **실제로 대기할 수 있음**
- 큰 메시지 전송 시 speculative write가 오래 걸릴 수 있음

**리스크 평가:**
- **Low-Medium**: 주석(Lines 247-250)에서 이미 인식함
  - "Small messages (typical ZMQ use case) fit in socket buffer"
  - "If buffer is full, we get would_block immediately"
  - "Large messages should use async path anyway"

**검증 필요 사항:**
1. Beast의 `write()`가 논블로킹 소켓에서 **즉시** would_block을 반환하는지 확인
2. 아니면 실제로 일부 데이터를 전송할 때까지 **blocking**하는지 확인

**권장 대응:**
- Phase 2 테스트에서 "소켓 버퍼 full 상황"을 강제로 만들어 검증
- Blocking이 관찰되면:
  - Option 1: 작은 메시지만 write_some 사용, 큰 메시지는 async 강제
  - Option 2: 논블로킹 모드 명시적 설정 확인

#### ✅ 에러 매핑 (Lines 260-286)

| Boost/Beast 에러 | errno 매핑 | 평가 |
|------------------|-----------|------|
| `would_block`, `try_again` | `EAGAIN` | ✅ 정확 |
| `websocket::error::closed` | `ECONNRESET` | ✅ 적절 |
| `eof` | `ECONNRESET` | ✅ 일관성 |
| 기타 transport 에러 | TCP와 동일 | ✅ 일관성 유지 |

**평가:** 우수 - WebSocket 특정 에러도 올바르게 처리

---

### 2.5 WSS Transport (`wss_transport.cpp`)

#### ✅ 이중 계층 처리 (Lines 227-304)

```cpp
bytes_written = _wss_stream->write (boost::asio::buffer (data, len), ec);
```

**평가:** 우수
- SSL + WebSocket 복합 계층을 단일 write로 처리
- Handshake 완료 검증: SSL과 WebSocket 모두 체크 (Line 235)
- `next_layer().next_layer()` 체인으로 TCP 소켓 접근 (Line 240)

#### ✅ 에러 처리 통합 (Lines 267-298)

**SSL 에러 우선 처리:**
```cpp
if (ec.category () == boost::asio::error::get_ssl_category ()) {
    errno = EIO;
    ASIO_DBG_WSS ("write_some SSL error: %s", ec.message ().c_str ());
    return 0;
}
```

**평가:** 우수
- SSL 에러를 먼저 검사 (Line 276-279)
- WebSocket 에러 포함 (Lines 290-291)
- WS transport과 동일한 매핑 유지

#### ⚠️ 동일 Blocking 리스크

WS transport와 동일한 frame blocking 이슈 존재. SSL 계층 추가로 blocking 가능성 약간 증가 가능.

**권장사항:** WS transport과 동일한 테스트 및 검증 필요

---

## 3. 발견된 이슈

### 3.1 Critical Issues

**없음** - 치명적 버그나 설계 결함 없음

---

### 3.2 Major Issues

**없음** - Phase 2 구현을 막을 주요 문제 없음

---

### 3.3 Minor Issues

#### Issue #1: WebSocket/WSS의 동기 write() Blocking 가능성

**파일:** `ws_transport.cpp:257`, `wss_transport.cpp:264`

**설명:**
- Beast의 `websocket::stream::write()`가 논블로킹 소켓에서도 부분적으로 blocking할 수 있음
- Frame 전체를 전송할 때까지 대기할 가능성

**영향:**
- Speculative write 최적화 효과 감소 (큰 메시지에서)
- 짧은 메시지에서는 영향 미미

**우선순위:** P2 (Phase 2 테스트 단계에서 검증)

**권장 조치:**
1. Phase 2 edge case 테스트 시 강제 검증
2. Blocking 관찰 시 크기 기반 fallback 로직 추가

---

#### Issue #2: SSL 에러 세분화 부족

**파일:** `ssl_transport.cpp:155`, `wss_transport.cpp:276`

**설명:**
- 모든 SSL 에러를 `EIO`로 통합
- `SSL_ERROR_WANT_READ/WRITE`는 일시적 상태이므로 `EAGAIN`이 더 적절

**영향:**
- 성능 최적화 기회 누락 (재시도 가능한 상황을 에러로 처리)
- 현행 구현도 동작은 함 (async fallback으로 복구 가능)

**우선순위:** P3 (성능 최적화 단계에서 개선)

**권장 조치:**
```cpp
if (ec.category () == boost::asio::error::get_ssl_category ()) {
    int ssl_error = SSL_get_error(_ssl_stream->native_handle(),
                                   ec.value());
    if (ssl_error == SSL_ERROR_WANT_READ ||
        ssl_error == SSL_ERROR_WANT_WRITE) {
        errno = EAGAIN;
    } else {
        errno = EIO;
    }
    return 0;
}
```

---

#### Issue #3: 에러 발생 시 bytes_written 값 미사용

**파일:** 모든 transport

**설명:**
- 에러 발생 시 `bytes_written` 값을 무시하고 항상 0 반환
- ASIO는 에러 시에도 부분 전송 바이트를 반환할 수 있음

**영향:**
- 극히 드문 경우 부분 전송 데이터 손실 가능
- 대부분의 경우 문제 없음 (에러 시 연결 종료되므로)

**우선순위:** P4 (낮음 - 표준 관행)

**현행 유지 권장:** 대부분의 네트워크 라이브러리가 동일한 방식 사용

---

## 4. 개선 제안

### 4.1 코드 품질

#### 제안 #1: 공통 에러 매핑 함수 추출

**근거:** 4개 transport에서 동일한 에러 매핑 코드 중복

**제안:**
```cpp
// asio/error_mapping.hpp
inline int map_boost_error_to_errno(const boost::system::error_code& ec) {
    if (ec == boost::asio::error::would_block
        || ec == boost::asio::error::try_again) {
        return EAGAIN;
    }
    if (ec == boost::asio::error::broken_pipe
        || ec == boost::asio::error::connection_reset) {
        return EPIPE;
    }
    // ... (나머지 매핑)
    return EIO;  // default
}
```

**효과:**
- 중복 제거로 유지보수성 향상
- 에러 매핑 일관성 자동 보장

**우선순위:** P3 (선택적 리팩터링)

---

#### 제안 #2: write_some 반환값 디버그 로깅 추가

**근거:** Phase 2 테스트 시 동작 추적 필요

**제안:**
```cpp
// tcp_transport.cpp
ASIO_DBG_TCP("write_some: requested=%zu, written=%zu, errno=%d",
             len, bytes_written, errno);
```

**효과:**
- Speculative write 성공률 측정 가능
- would_block 발생 빈도 추적

**우선순위:** P2 (Phase 2 시작 전 추가 권장)

---

### 4.2 테스트

#### 제안 #3: Transport별 write_some 단위 테스트 추가

**필요한 테스트:**

1. **정상 동작 테스트**
   - 작은 메시지 전송 → 전체 전송 확인
   - 큰 메시지 전송 → 부분 전송 확인 (TCP/TLS)

2. **would_block 테스트**
   - 소켓 버퍼 full 강제 → errno == EAGAIN 확인

3. **WebSocket frame 무결성 테스트**
   - Frame 경계 유지 확인
   - 수신 측에서 정상 파싱 검증

4. **에러 처리 테스트**
   - 연결 종료 중 write → EPIPE 확인
   - Handshake 전 write → ENOTCONN 확인

**구현 위치:** `tests/test_asio_transport_write_some.cpp` (신규)

**우선순위:** P1 (Phase 2 시작 전 필수)

---

### 4.3 문서화

#### 제안 #4: WebSocket blocking 동작 명시적 문서화

**근거:** write() blocking 가능성이 설계 의도인지 명확히 해야 함

**권장 추가 주석 (ws_transport.cpp:242 이후):**
```cpp
//  IMPORTANT: Beast's websocket::write() on a non-blocking socket:
//  - Returns immediately with would_block if send buffer is full
//  - Otherwise, may block briefly while framing (typically < 1ms)
//  - For speculative write, this is acceptable for small messages
//  - Verified non-blocking behavior with test_ws_write_buffer_full
```

**우선순위:** P2 (테스트 완료 후 추가)

---

## 5. Phase 2 준비 상태 평가

### 5.1 완료된 전제 조건

✅ **모든 transport에서 write_some() 호출 가능**
- TCP, TLS, WS, WSS 모두 구현 완료

✅ **would_block 발생 시 올바른 에러 코드 반환**
- 모든 transport에서 EAGAIN/EWOULDBLOCK 반환 확인

✅ **기존 async API와 공존**
- async_write_some()과 write_some() 독립적으로 동작

---

### 5.2 검증 필요 사항

⚠️ **WebSocket frame 기반 전송의 부분 쓰기 처리**
- 이론적 검증: 완료 (frame 무결성 유지 설계)
- 실증적 검증: **필요** (소켓 버퍼 full 시나리오 테스트)

⚠️ **TCP/TLS transport에서 동기 write_some 성공률**
- 벤치마크 필요: Phase 2 도입 후 측정
- 목표: 작은 메시지의 80%+ 즉시 전송 성공

---

### 5.3 Phase 2 구현 권장사항

#### 1. Speculative Write 도입 순서

**Step 1:** TCP transport만 우선 적용
- 가장 단순하고 검증 용이
- Blocking 리스크 없음

**Step 2:** TLS transport 적용
- SSL 에러 처리 개선 (Issue #2 해결)
- 성능 영향 측정

**Step 3:** WebSocket/WSS 적용
- Blocking 동작 검증 후 진행
- 필요시 크기 제한 적용 (예: 8KB 이하만 동기)

#### 2. 상태 전이 규칙 구현

```cpp
// asio_engine.cpp - speculative_write() 추가
void zmq::asio_engine_t::speculative_write ()
{
    // Phase 1에서 구현된 write_some() 활용
    if (_write_pending)  // 중복 방지
        return;

    // 버퍼 준비 (Phase 3에서 zero-copy로 개선)
    prepare_output_buffer();
    if (_outsize == 0) {
        _output_stopped = true;
        return;
    }

    // 동기 쓰기 시도
    size_t bytes = _transport->write_some(_outpos, _outsize);

    if (bytes == 0) {
        if (errno == EAGAIN) {
            // would_block - async로 전환
            start_async_write();
            return;
        }
        // 실제 에러
        error(connection_error);
        return;
    }

    // 부분/전체 전송 성공
    _outpos += bytes;
    _outsize -= bytes;

    if (_outsize > 0) {
        // 잔여 데이터 - async로 전환
        start_async_write();
    }
}
```

#### 3. 측정 지표

Phase 2 완료 후 반드시 측정:
- **Speculative write 성공률** (즉시 전송 / 총 시도)
- **p99 latency 개선도** (baseline 대비 %)
- **would_block 발생 빈도** (async fallback 비율)

---

## 6. 최종 승인 여부

### 승인 상태: **조건부 승인 (Approved with Conditions)**

#### 승인 근거:
1. ✅ Phase 1 요구사항 100% 충족
2. ✅ 코드 품질 우수 (A- 등급)
3. ✅ Phase 2 구현 가능 (blocking 요소 없음)
4. ✅ 에러 처리 일관성 확보
5. ✅ 문서화 충실

#### 조건 (Phase 2 시작 전 이행):

1. **필수 (Mandatory):**
   - [ ] WebSocket/WSS의 write() blocking 동작 검증 테스트 작성 및 실행
   - [ ] Transport별 write_some 단위 테스트 추가 (제안 #3)
   - [ ] 디버그 로깅 추가 (제안 #2)

2. **권장 (Recommended):**
   - [ ] SSL 에러 세분화 개선 (Issue #2) - 성능 최적화 단계에서
   - [ ] 공통 에러 매핑 함수 추출 (제안 #1) - 리팩터링 시

3. **선택 (Optional):**
   - [ ] WebSocket blocking 동작 문서화 (제안 #4) - 테스트 완료 후

---

## 7. 요약

### 주요 성과
- ✅ 4개 transport에 동기 write 인터페이스 성공적 추가
- ✅ WebSocket의 frame 기반 전송 특성 정확히 반영
- ✅ 에러 처리 일관성 확보
- ✅ Phase 2 구현 준비 완료

### 개선 필요
- ⚠️ WebSocket write() blocking 동작 실증 검증 필요
- ⚠️ SSL 에러 세분화로 최적화 기회 활용 권장
- ⚠️ 단위 테스트 추가로 안정성 보장 필요

### 다음 단계
1. 필수 조건 이행 (단위 테스트 추가)
2. Phase 2 시작: TCP transport 우선 speculative write 도입
3. 벤치마크로 성능 개선 검증
4. 점진적으로 다른 transport 확대

---

**최종 평가:** Phase 1 구현은 **높은 품질**로 완료되었으며, Phase 2로 진행 가능합니다. 단, 필수 조건(단위 테스트)을 먼저 이행하여 안정성을 확보하는 것을 강력히 권장합니다.

---

## 부록: 체크리스트

### Phase 1 완료 기준 달성도

| 기준 | 상태 | 비고 |
|-----|------|------|
| 모든 transport에서 write_some() 호출 가능 | ✅ 완료 | TCP/TLS/WS/WSS 모두 구현 |
| would_block 시 올바른 에러 코드 반환 | ✅ 완료 | EAGAIN 반환 확인 |
| WebSocket frame 기반 전송 검증 | ⚠️ 이론 완료 | 실증 테스트 필요 |

### Phase 2 준비 상태

| 항목 | 상태 | 필요 조치 |
|-----|------|----------|
| Transport 인터페이스 준비 | ✅ 완료 | 없음 |
| 에러 처리 일관성 | ✅ 완료 | 없음 |
| 단위 테스트 | ❌ 미완료 | 테스트 추가 필요 |
| 디버그 로깅 | ⚠️ 부분 완료 | write_some 로깅 추가 권장 |

---

**리뷰 완료일:** 2026-01-14
**다음 리뷰:** Phase 2 구현 완료 후
