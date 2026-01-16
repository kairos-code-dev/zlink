# TCP Performance Gap Analysis

## 발견: TCP 성능이 libzmq-ref의 절반 수준

### 성능 비교 (10K messages, 64B, TCP)

| Pattern | libzmq-ref (M/s) | zlink (M/s) | zlink 달성률 |
|---------|------------------|-------------|-------------|
| PAIR | **5.0 ~ 5.4** | 2.7 ~ 2.9 | **53-58%** ❌ |
| DEALER_DEALER | **5.0 ~ 5.6** | 2.8 ~ 2.9 | **50-58%** ❌ |
| ROUTER_ROUTER_POLL | **4.0 ~ 4.5** | 2.2 | **49-55%** ❌ |

### IPC vs TCP 비교

| Metric | IPC | TCP |
|--------|-----|-----|
| **zlink 성능** | 4.78 M/s | 2.9 M/s |
| **libzmq-ref 성능** | 4.5 ~ 5.9 M/s | 5.0 ~ 5.4 M/s |
| **zlink 달성률** | **81-106%** ✅ | **53-58%** ❌ |

## 핵심 발견

### IPC는 성공했지만 TCP는 실패

**IPC Phase 5 최적화 효과:**
- Before: 0-70% success (deadlock)
- After: 100% success + libzmq-ref 수준 성능 ✅

**TCP 현황:**
- 안정성: 100% (deadlock 없음) ✅
- 성능: **libzmq-ref의 절반** ❌

## 왜 이런 차이가 발생했는가?

### 1. Phase 5 최적화가 IPC에만 효과적?

**Phase 5 변경사항:**
1. Speculative Read (모든 transport)
2. IPC Speculative Write 비활성화 (`supports_speculative_write() = false`)

**문제점:**
- Speculative Read는 **모든 transport**에 적용됨
- 그런데 왜 IPC만 libzmq 수준으로 올라갔고 TCP는 절반인가?

### 2. 가능한 원인

#### Hypothesis A: ASIO 오버헤드

**libzmq-ref:**
- epoll 직접 사용 (Linux)
- 최소한의 추상화 레이어

**zlink:**
- ASIO 사용 (boost::asio)
- 추상화 레이어 오버헤드

**증거:**
- IPC도 ASIO를 사용하는데 왜 libzmq 수준인가?
- → IPC는 로컬 통신이라 ASIO 오버헤드가 상대적으로 작음
- → TCP는 네트워크 스택 + ASIO 오버헤드 = 2배 차이?

#### Hypothesis B: Speculative Write 미작동

**현재 구현:**
```cpp
// TCP는 supports_speculative_write() = true (기본값)
virtual bool supports_speculative_write() const { return true; }
```

**의문점:**
- TCP Speculative Write가 실제로 작동하는가?
- 얼마나 자주 sync write에 성공하는가?
- EAGAIN이 너무 자주 발생해서 async로 fallback되는가?

#### Hypothesis C: TCP 버퍼링 이슈

**TCP 특성:**
- Nagle's algorithm
- TCP_NODELAY 설정 확인 필요
- Send/Receive buffer size

**libzmq-ref vs zlink:**
- Socket options가 다를 수 있음
- 버퍼 크기, nodelay 등

#### Hypothesis D: ASIO의 async_write 비효율

**libzmq-ref:**
- `send()` syscall 직접 호출
- Non-blocking socket + epoll

**zlink:**
- `async_write_some()` → ASIO 큐잉
- 핸들러 호출 오버헤드
- Completion queue 오버헤드

## 검증 필요 사항

### 1. TCP Speculative Write 통계 수집

```cpp
// tcp_transport.cpp에 카운터 추가
static std::atomic<uint64_t> tcp_write_some_calls (0);
static std::atomic<uint64_t> tcp_write_some_bytes (0);
static std::atomic<uint64_t> tcp_write_some_eagain (0);
static std::atomic<uint64_t> tcp_async_write_calls (0);
```

**측정 목표:**
- Speculative write 성공률
- EAGAIN 비율
- Async write fallback 비율

### 2. TCP Socket Options 비교

**확인 필요:**
- TCP_NODELAY 설정
- SO_SNDBUF / SO_RCVBUF 크기
- TCP_QUICKACK (Linux)

### 3. ASIO vs epoll 프로파일링

**도구:**
- `perf` - CPU 프로파일링
- `strace` - Syscall 추적
- `tcpdump` - 네트워크 패킷 분석

**비교 대상:**
- libzmq-ref syscall 패턴
- zlink syscall 패턴

### 4. Async Write 최적화 가능성

**아이디어:**
- Batch writing (여러 메시지 한 번에)
- Corked socket (TCP_CORK)
- Writev 사용 (scatter-gather I/O)

## 최적화 방향

### Option A: TCP Speculative Write 튜닝

**현재:**
```cpp
std::size_t bytes = _socket->write_some(buffer, ec);
if (ec == boost::asio::error::would_block) {
    start_async_write(); // fallback
}
```

**개선 아이디어:**
- Retry logic (1-2회 재시도)
- Adaptive strategy (성공률 기반 전환)

### Option B: ASIO Bypass (Advanced)

**극단적 접근:**
- TCP만 ASIO 대신 epoll 직접 사용
- IPC/WS/WSS는 ASIO 유지

**리스크:**
- 아키텍처 복잡도 증가
- 유지보수 부담

### Option C: ASIO 튜닝

**Settings:**
- `io_context::concurrency_hint`
- Strand 사용 재검토
- Executor 설정

### Option D: libzmq-ref TCP 구현 분석

**학습:**
- libzmq-ref가 어떻게 5 M/s를 달성하는가?
- 어떤 최적화 기법을 사용하는가?
- zlink에 적용 가능한가?

## 기대 효과

**If TCP를 libzmq-ref 수준으로 개선:**

| Pattern | Current | Target | Improvement |
|---------|---------|--------|-------------|
| PAIR/tcp/64B | 2.9 M/s | **5.4 M/s** | **+86%** |
| DEALER_DEALER/tcp/64B | 2.9 M/s | **5.6 M/s** | **+93%** |
| ROUTER_ROUTER_POLL/tcp/64B | 2.2 M/s | **4.5 M/s** | **+105%** |

**Impact:**
- TCP 성능 2배 향상
- libzmq-ref와 완전 동등
- Production readiness 강화

## 우선순위

### P0 (Immediate)
1. TCP Speculative Write 통계 수집
2. Socket options 비교 및 튜닝

### P1 (High)
3. ASIO vs epoll 프로파일링
4. libzmq-ref TCP 구현 분석

### P2 (Medium)
5. ASIO 튜닝 실험
6. Batch writing 프로토타입

## 결론

**사용자의 관찰이 정확합니다:**

> "ipc가 libzmq 수준으로 올라온건데? tcp도 비슷하게 처리하면 되지 않을까?"

**현황:**
- ✅ IPC: Phase 5로 libzmq-ref 수준 달성 (81-106%)
- ❌ TCP: 여전히 절반 수준 (53-58%)

**다음 단계:**
- TCP 성능 격차 원인 규명
- 동일한 최적화 접근 적용
- 목표: TCP도 libzmq-ref 수준으로 향상

**기회:**
- IPC 성공 경험을 TCP에 적용
- 2배 성능 향상 가능성
- zlink를 진정한 libzmq-ref 대체제로 만들기

---

**Action Items:**
1. TCP 통계 수집 코드 추가
2. libzmq-ref TCP 소스 분석
3. Speculative Write 튜닝
4. 성능 재측정

**Expected Timeline:** 1-2 weeks for investigation + optimization
