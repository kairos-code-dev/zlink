# Phase 1: syscall 정밀 계측 결과

**날짜:** 2026-01-16
**패턴:** ROUTER_ROUTER
**Transport:** TCP
**Message Size:** 64B

## 실행 환경

```bash
# 벤치마크 빌드 및 실행
benchwithzmq/run_benchmarks.sh --pattern ROUTER_ROUTER --runs 1 --zlink-only

# syscall 프로파일링
strace -f -c -e trace=read,write,sendto,recvfrom,epoll_wait \
  taskset -c 1 ./build/bench/bin/comp_zlink_router_router zlink tcp 64

strace -f -c -e trace=read,write,sendto,recvfrom,epoll_wait \
  taskset -c 1 ./build/bench/bin/comp_std_zmq_router_router libzmq tcp 64
```

## Syscall 통계 비교

### 전체 syscall 횟수

| Library | sendto | recvfrom | recvfrom (EAGAIN) | epoll_wait | read | write | throughput |
|---------|--------|----------|-------------------|------------|------|-------|------------|
| **zlink** | 3,626 | 5,631 | **2,010 (35.7%)** | 3,644 | 10 | 3 | 442K msg/s |
| **libzmq** | 3,625 | 3,633 | **9 (0.25%)** | 3,644 | 2,836 | 2,829 | 421K msg/s |
| **차이** | +0% | **+55%** | **+22,233%** | +0% | -99% | -100% | +5% |

### 핵심 발견

1. **recvfrom 호출 횟수**
   - zlink: 5,631 calls
   - libzmq: 3,633 calls
   - **zlink가 55% 더 많이 호출**

2. **EAGAIN 에러 빈도**
   - zlink: 2,010회 (recvfrom의 35.7%)
   - libzmq: 9회 (recvfrom의 0.25%)
   - **zlink가 223배 더 많은 EAGAIN 발생**

3. **epoll_wait 횟수**
   - 양쪽 모두 3,644회로 동일
   - epoll 호출 자체는 문제 없음

4. **read/write syscall**
   - libzmq는 read/write를 2,800+ 회 사용
   - zlink는 거의 사용하지 않음 (초기화 단계만)
   - 이는 libzmq의 다른 컴포넌트(mailbox 등)에서 사용하는 것으로 추정

## 상세 패턴 분석

### zlink의 recvfrom 패턴

```
recvfrom(8, ..., 8192, 0, NULL, NULL) = 66       # 성공
recvfrom(8, ..., 8192, 0, NULL, NULL) = -1 EAGAIN  # 즉시 실패
recvfrom(7, ..., 8192, 0, NULL, NULL) = 66       # 성공
recvfrom(7, ..., 8192, 0, NULL, NULL) = -1 EAGAIN  # 즉시 실패
epoll_wait(...)                                   # 다시 대기
```

**문제점:**
- 매 수신 성공 직후 **즉시 다시 recvfrom**을 호출
- 소켓에 더 이상 데이터가 없으면 **즉시 EAGAIN** 발생
- 그 후 epoll_wait로 돌아가서 다시 readable 이벤트를 기다림
- **불필요한 syscall 낭비**

### libzmq의 recvfrom 패턴

```
recvfrom(11, ..., 8192, 0, NULL, NULL) = 66  # 성공
recvfrom(12, ..., 8192, 0, NULL, NULL) = 66  # 성공
recvfrom(11, ..., 8192, 0, NULL, NULL) = 66  # 성공
recvfrom(12, ..., 8192, 0, NULL, NULL) = 66  # 성공
...
```

**특징:**
- **연속적으로 성공**
- EAGAIN이 거의 발생하지 않음 (초기화 단계에만 9회)
- epoll이 알려준 만큼만 read하거나, 버퍼를 비울 때까지 loop

## recvfrom 크기 분포

### zlink

- 초기화: 10, 11, 54, 103, 50 바이트 (핸드셰이크)
- 핸드셰이크: 6 바이트 (PING/PONG)
- **벤치마크 페이로드: 66 바이트 일정** (2바이트 헤더 + 64바이트 데이터)
- 버퍼 크기: 8192 바이트 (충분함)

### libzmq

- 초기화: 10, 1, 53 바이트 (작은 조각으로 읽기)
- 핸드셰이크: 50, 6 바이트
- **벤치마크 페이로드: 66 바이트 일정**
- 버퍼 크기: 8192 바이트

**차이점:**
- libzmq는 초기화 단계에서 작은 단위(1, 10, 53 바이트)로 여러 번 읽음
- zlink는 한 번에 더 큰 단위로 읽지만, 읽은 후 즉시 다시 시도하여 EAGAIN

## 근본 원인 분석

### zlink ASIO 엔진의 문제

`src/asio/asio_engine.cpp`의 `on_read_complete` 핸들러가 다음과 같이 동작:

1. `async_read_some` 완료 → `on_read_complete` 호출
2. 수신한 데이터를 처리
3. **즉시 다음 `async_read_some` 호출**
4. 소켓에 더 이상 데이터가 없으면 → `recvfrom` → **EAGAIN**
5. ASIO가 epoll에 다시 등록하여 대기

### 왜 이것이 문제인가?

1. **불필요한 syscall 오버헤드**
   - 매 메시지마다 추가로 1회의 실패한 recvfrom 호출
   - 2,010회의 EAGAIN은 순수한 낭비

2. **Proactor 패턴의 오용**
   - Proactor 패턴은 "준비되면 알려줌"이 장점
   - 하지만 zlink는 "준비 안 되었는데 시도"를 반복

3. **libzmq 대비 비효율**
   - libzmq는 epoll이 알려준 후에만 read
   - 또는 read loop로 EAGAIN까지 읽어서 버퍼를 비움

## 버퍼 크기 비교

| 항목 | zlink | libzmq | 비고 |
|------|-------|--------|------|
| recvfrom 버퍼 | 8192 | 8192 | 동일 |
| 실제 수신 크기 | 66 | 66 | 동일 (64B 메시지) |
| EAGAIN 빈도 | 35.7% | 0.25% | zlink가 223배 높음 |

버퍼 크기 자체는 문제가 아닙니다. **언제 read를 호출하는지**가 문제입니다.

## 예상되는 성능 영향

### 현재 오버헤드 계산

- 불필요한 recvfrom 호출: 2,010회
- 각 실패한 syscall 비용: ~0.5 μs (추정)
- 총 낭비 시간: 2,010 × 0.5 = **~1,005 μs (1 ms)**

### 벤치마크 컨텍스트

- 총 메시지 수: ~3,600 (throughput test)
- 총 실행 시간: ~8.1 ms (3,600 / 442K)
- EAGAIN 오버헤드: **1 ms / 8.1 ms = 12.3%**

**참고:** 이 계산은 근사치입니다. 실제 오버헤드는 CPU cache, context switch 등 다른 요인에 의해 달라질 수 있습니다.

## 해결 방향

### 전략 B: Proactor 핸들러 배칭

**목표:** EAGAIN 횟수를 libzmq 수준(~9회)으로 감소

**방법:**

1. **수신 버퍼 내 다건 메시지 처리**
   - `on_read_complete`에서 수신한 바이트 수만큼 메시지를 파싱
   - 모든 메시지를 처리한 후에 다음 `async_read_some` 호출

2. **즉시 재시도 방지**
   - 데이터를 읽은 직후 즉시 다시 읽지 말 것
   - epoll이 readable 이벤트를 알려줄 때까지 대기

3. **Read Loop (선택적)**
   - 또는 EAGAIN이 나올 때까지 loop로 계속 read
   - 하지만 이는 Proactor 패턴과 맞지 않을 수 있음

### 성공 기준

- recvfrom EAGAIN 횟수: 2,010 → **< 50** (95% 감소)
- 총 recvfrom 호출 횟수: 5,631 → **~3,650** (libzmq 수준)
- 성능 향상: 예상 5-10%

## 다음 단계

1. `src/asio/asio_engine.cpp`의 `on_read_complete` 핸들러 수정
2. 수신 버퍼 내 메시지 파싱 로직 개선
3. 마이크로 벤치마크로 검증
4. 전체 벤치마크 재측정

## 참고 자료

- strace 로그: `/tmp/zlink_recvfrom_detail.log`, `/tmp/libzmq_recvfrom_detail.log`
- 계획서: `docs/team/20260116_syscall-optimization/plan.md`
- 이전 실험: `docs/team/20260116_epoll-wait-optimization/experiment_results.md`
