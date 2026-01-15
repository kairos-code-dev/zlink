# CPU 사용률 측정 계획

**날짜**: 2026-01-15
**요청자**: User
**목적**: ASIO vs epoll 이벤트 루프의 CPU 효율성 비교

## 배경

Stage 1 분석 결과:
- **ROUTER 구현은 100% 동일** (zlink == libzmq-ref)
- **성능 차이 원인**: ASIO 이벤트 루프 오버헤드
- **추정 오버헤드**:
  - ASIO async callback 모델: ~40%
  - ROUTER multipart 증폭: ~30%
  - 이벤트 루프 스케줄링: ~30%

**가설**: ASIO의 추가 오버헤드가 CPU 사용률에도 영향을 미칠 것

## 측정 도구

### 1. `/usr/bin/time -v` (✅ 사용 가능)

**측정 항목:**
```bash
/usr/bin/time -v ./bench_router_dealer tcp 64 1000000 2>&1 | grep -E 'User time|System time|CPU|Maximum resident|Voluntary context|Involuntary context'
```

**출력 예시:**
```
User time (seconds): 2.45
System time (seconds): 0.89
Percent of CPU this job got: 98%
Maximum resident set size (kbytes): 4352
Voluntary context switches: 125
Involuntary context switches: 34
```

**분석 포인트:**
- User time: 사용자 공간 CPU 시간 (이벤트 루프 로직)
- System time: 커널 공간 CPU 시간 (시스템콜 오버헤드)
- CPU percentage: 전체 CPU 활용률
- Context switches: 스케줄링 오버헤드

### 2. `strace -c` (✅ 설치됨)

**측정 항목:**
```bash
strace -c -f -o strace_output.txt ./bench_router_dealer tcp 64 1000000
```

**출력 예시:**
```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 45.23    0.125678          12     10234           epoll_wait
 23.45    0.065123           6     10234           write
 18.32    0.050890           5     10234           read
  8.12    0.022567          11      2045           futex
  4.88    0.013542          13      1023           mmap
```

**분석 포인트:**
- epoll_wait 호출 빈도 및 시간
- read/write 시스템콜 패턴
- futex (동기화 오버헤드)
- 총 시스템콜 시간 비율

### 3. `perf` (❌ WSL2 제약으로 사용 불가)

**대안**:
- `perf` 대신 `/usr/bin/time`과 `strace`로 충분한 분석 가능
- 필요시 Windows Performance Recorder 사용 (WSL 외부)

## 측정 시나리오

### 시나리오 1: ROUTER/DEALER 패턴 (핵심)

**zlink:**
```bash
cd /home/ulalax/project/ulalax/zlink/build/linux-x64/tests

# CPU 사용률 측정
/usr/bin/time -v ./bench_router_dealer tcp 64 1000000 2>&1 | tee ../../cpu_zlink_router_64B.txt

# 시스템콜 분석
strace -c -f -o ../../strace_zlink_router_64B.txt ./bench_router_dealer tcp 64 1000000
```

**libzmq-ref:**
```bash
cd /home/ulalax/project/ulalax/libzmq-ref/perf/router_bench/build

# CPU 사용률 측정
/usr/bin/time -v ./router_throughput tcp://127.0.0.1:5555 64 1000000 2>&1 | tee router_cpu_ref_64B.txt

# 시스템콜 분석
strace -c -f -o router_strace_ref_64B.txt ./router_throughput tcp://127.0.0.1:5555 64 1000000
```

### 시나리오 2: 메시지 크기 변화

**측정 대상:**
- 64B (작은 메시지 - 최악의 경우)
- 256B (중간 크기)
- 1KB (큰 메시지)
- 8KB (매우 큰 메시지)

**목적**: 메시지 크기에 따른 CPU 오버헤드 변화 분석

### 시나리오 3: Transport 비교

**측정 대상:**
- TCP (기본)
- inproc (공유 메모리)
- IPC (Unix domain socket)

**목적**: Transport별 CPU 오버헤드 차이 분석

## 비교 메트릭

| 메트릭 | 설명 | 목표 |
|--------|------|------|
| **User CPU time** | 사용자 공간 CPU 시간 | zlink <= libzmq-ref * 1.1 |
| **System CPU time** | 시스템콜 CPU 시간 | zlink <= libzmq-ref * 1.2 |
| **Total CPU %** | 전체 CPU 활용률 | 95% 이상 (CPU bound 확인) |
| **Context switches** | 스케줄링 횟수 | zlink <= libzmq-ref * 1.3 |
| **epoll_wait calls** | 이벤트 대기 호출 | 비슷하거나 적어야 함 |
| **Syscall overhead** | 시스템콜 비율 | System time / Total time |

## 예상 결과

### ASIO (zlink) 예상:

```
User time: 2.8s (↑ 높음 - 콜백 오버헤드)
System time: 0.9s (비슷)
CPU: 97% (높은 활용률)
Context switches: 150 (↑ 약간 높음)
epoll_wait calls: 10,234 (비슷)
```

### epoll (libzmq-ref) 예상:

```
User time: 2.1s (↓ 낮음 - 직접 호출)
System time: 0.9s (비슷)
CPU: 98% (높은 활용률)
Context switches: 120
epoll_wait calls: 10,234 (비슷)
```

**예상 차이:**
- User time: ASIO가 30-40% 더 높음 (콜백 오버헤드)
- System time: 비슷 (시스템콜은 동일)
- Context switches: ASIO가 약간 높음

## 측정 스크립트

### `measure_cpu.sh`

```bash
#!/bin/bash
# CPU 사용률 측정 스크립트

IMPL=$1       # zlink 또는 libzmq-ref
PATTERN=$2    # router, pair, pubsub
MSGSIZE=$3    # 64, 256, 1024, 8192
COUNT=$4      # 1000000
OUTPUT_DIR=$5

if [ "$IMPL" = "zlink" ]; then
    BIN="/home/ulalax/project/ulalax/zlink/build/linux-x64/tests/bench_${PATTERN}"
    ARGS="tcp $MSGSIZE $COUNT"
elif [ "$IMPL" = "libzmq-ref" ]; then
    BIN="/home/ulalax/project/ulalax/libzmq-ref/perf/router_bench/build/router_throughput"
    ARGS="tcp://127.0.0.1:5555 $MSGSIZE $COUNT"
fi

PREFIX="${OUTPUT_DIR}/${IMPL}_${PATTERN}_${MSGSIZE}B"

echo "=== Measuring CPU usage for $IMPL $PATTERN $MSGSIZE bytes ==="

# CPU 사용률 측정
echo "Running /usr/bin/time..."
/usr/bin/time -v $BIN $ARGS 2>&1 | tee "${PREFIX}_cpu.txt"

# 시스템콜 분석
echo "Running strace..."
strace -c -f -o "${PREFIX}_strace.txt" $BIN $ARGS

echo "Results saved to ${PREFIX}_cpu.txt and ${PREFIX}_strace.txt"
```

### 실행 예시:

```bash
cd /home/ulalax/project/ulalax/zlink/docs/team/20260115_router-performance-optimization/output/bench

# zlink 측정
./measure_cpu.sh zlink router 64 1000000 .

# libzmq-ref 측정
./measure_cpu.sh libzmq-ref router 64 1000000 .

# 결과 비교
grep "User time" *_cpu.txt
grep "System time" *_cpu.txt
grep "Percent of CPU" *_cpu.txt
```

## 분석 절차

### 1. CPU 시간 분석

```bash
# User time 비교 (이벤트 루프 오버헤드)
echo "=== User Time Comparison ==="
grep "User time" zlink_router_64B_cpu.txt
grep "User time" libzmq-ref_router_64B_cpu.txt

# System time 비교 (시스템콜 오버헤드)
echo "=== System Time Comparison ==="
grep "System time" zlink_router_64B_cpu.txt
grep "System time" libzmq-ref_router_64B_cpu.txt

# CPU 비율
echo "=== CPU Percentage ==="
grep "Percent of CPU" zlink_router_64B_cpu.txt
grep "Percent of CPU" libzmq-ref_router_64B_cpu.txt
```

### 2. 시스템콜 분석

```bash
# 총 시스템콜 시간
echo "=== Total Syscall Time ==="
head -5 zlink_router_64B_strace.txt
head -5 libzmq-ref_router_64B_strace.txt

# epoll_wait 호출 횟수
echo "=== epoll_wait Calls ==="
grep "epoll_wait" zlink_router_64B_strace.txt
grep "epoll_wait" libzmq-ref_router_64B_strace.txt

# futex (동기화)
echo "=== Futex Calls ==="
grep "futex" zlink_router_64B_strace.txt
grep "futex" libzmq-ref_router_64B_strace.txt
```

### 3. 메모리 사용량 분석

```bash
# 최대 메모리 사용량
echo "=== Memory Usage ==="
grep "Maximum resident" zlink_router_64B_cpu.txt
grep "Maximum resident" libzmq-ref_router_64B_cpu.txt

# 페이지 폴트
grep "page faults" zlink_router_64B_cpu.txt
grep "page faults" libzmq-ref_router_64B_cpu.txt
```

## 산출물

### 파일 구조:

```
docs/team/20260115_router-performance-optimization/output/bench/
├── measure_cpu.sh                    # 측정 스크립트
├── zlink_router_64B_cpu.txt          # zlink CPU 측정
├── zlink_router_64B_strace.txt       # zlink 시스템콜 분석
├── libzmq-ref_router_64B_cpu.txt     # libzmq-ref CPU 측정
├── libzmq-ref_router_64B_strace.txt  # libzmq-ref 시스템콜 분석
├── cpu_comparison_summary.md         # CPU 비교 요약
└── syscall_comparison_summary.md     # 시스템콜 비교 요약
```

### 보고서 템플릿:

**`cpu_comparison_summary.md`**
```markdown
# CPU Usage Comparison: zlink vs libzmq-ref

## Test Configuration
- Pattern: ROUTER/DEALER
- Message size: 64 bytes
- Message count: 1,000,000
- Transport: TCP

## CPU Time Comparison

| Metric | zlink (ASIO) | libzmq-ref (epoll) | Difference |
|--------|-------------|-------------------|------------|
| User time | X.XX s | X.XX s | +XX% |
| System time | X.XX s | X.XX s | +XX% |
| Total time | X.XX s | X.XX s | +XX% |
| CPU % | XX% | XX% | +XX% |

## System Call Analysis

| Syscall | zlink calls | libzmq-ref calls | Difference |
|---------|------------|------------------|------------|
| epoll_wait | XXX | XXX | +XX% |
| read | XXX | XXX | +XX% |
| write | XXX | XXX | +XX% |
| futex | XXX | XXX | +XX% |

## Context Switches

| Type | zlink | libzmq-ref | Difference |
|------|-------|-----------|------------|
| Voluntary | XXX | XXX | +XX% |
| Involuntary | XXX | XXX | +XX% |

## Analysis

### CPU Overhead Source
- User time overhead: XX% (콜백 dispatch)
- System time overhead: XX% (시스템콜)
- Total overhead: XX%

### Optimization Targets
1. [...]
2. [...]
3. [...]
```

## 실행 타임라인

1. **측정 스크립트 생성** (5분)
   - `measure_cpu.sh` 작성
   - 실행 권한 부여

2. **ROUTER 패턴 측정** (10분)
   - zlink 측정 (64B, 256B, 1KB, 8KB)
   - libzmq-ref 측정 (64B, 256B, 1KB, 8KB)

3. **결과 분석** (15분)
   - CPU 시간 비교
   - 시스템콜 패턴 분석
   - 보고서 작성

4. **최적화 전략 업데이트** (10분)
   - CPU 오버헤드 기반 우선순위 조정
   - 목표치 재설정

## 성공 기준

**측정 완료:**
- ✅ zlink 4가지 메시지 크기 측정
- ✅ libzmq-ref 4가지 메시지 크기 측정
- ✅ CPU 시간 비교 완료
- ✅ 시스템콜 분석 완료

**분석 완료:**
- ✅ User time 오버헤드 정량화
- ✅ System time 비교
- ✅ 시스템콜 패턴 차이 규명
- ✅ 최적화 우선순위 업데이트

## 다음 단계

CPU 측정 결과를 바탕으로:

1. **Stage 2 최적화 우선순위 확정**
   - CPU 오버헤드가 높은 부분 집중
   - 목표 개선율 재설정

2. **최적화 구현 시작**
   - ASIO 이벤트 루프 배칭
   - 콜백 오버헤드 감소
   - 메모리 할당 최적화

3. **개선 검증**
   - 최적화 전/후 CPU 사용률 비교
   - 성능 개선율 측정
   - 회귀 테스트
