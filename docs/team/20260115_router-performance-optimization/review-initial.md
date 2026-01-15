# 계획 초기 검토 (Claude)

## 검증 완료 사항

### 1. 경로 접근성 ✓
- **libzmq-ref 경로**: `/mnt/d/project/ulalax/libzmq-ref` 접근 가능 확인됨
- **zlink 경로**: `/home/ulalax/project/ulalax/zlink` (현재 프로젝트)

### 2. 프로파일링 도구 ✓
- **perf**: `/usr/bin/perf` 설치 확인됨
- **flamegraph**: 필요시 설치 가능 (perf script 기반)
- **valgrind**: 확인 필요 (일반적으로 설치됨)

### 3. 벤치마크 환경 ✓
- 현재 `benchwithzmq/` 디렉토리에 벤치마크 스크립트 존재
- 10x 반복 테스트 이미 실행 완료
- baseline 결과: `docs/performance/baseline/benchmark_baseline_10x.txt`

## 계획 강화 사항

### 1. 단계 0 보완 필요
**추가 항목:**
- libzmq-ref 빌드 필요 (현재 소스만 존재)
- libzmq-ref 벤치마크 도구 확인 또는 benchwithzmq 스크립트 수정
- 동일한 벤치마크 프레임워크 사용 (통일성 확보)

**추천:**
```bash
# libzmq-ref 빌드
cd /mnt/d/project/ulalax/libzmq-ref
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 벤치마크 도구 확인
ls build/bin/ | grep bench
```

### 2. 단계 1 구체화
**ROUTER 구현 파일 위치:**

**zlink:**
- `src/router.cpp` - ROUTER 소켓 구현
- `src/router.hpp` - ROUTER 헤더
- `src/fq.cpp` / `src/fq.hpp` - Fair queue (입력)
- `src/lb.cpp` / `src/lb.hpp` - Load balancer (출력)
- `src/pipe.cpp` / `src/pipe.hpp` - Pipe 구현
- `src/mailbox.cpp` / `src/mailbox.hpp` - ASIO 기반 mailbox

**libzmq-ref:**
- `src/router.cpp` - ROUTER 소켓 구현 (표준)
- `src/fq.cpp` / `src/lb.cpp` - Queue 구현
- `src/pipe.cpp` - Pipe 구현
- `src/mailbox.cpp` - 표준 mailbox (epoll/select 기반)

**비교 시작점:**
1. `diff -u /mnt/d/project/ulalax/libzmq-ref/src/router.cpp src/router.cpp`
2. `diff -u /mnt/d/project/ulalax/libzmq-ref/src/mailbox.cpp src/mailbox.cpp`
3. `diff -u /mnt/d/project/ulalax/libzmq-ref/src/pipe.cpp src/pipe.cpp`

### 3. 단계 2 프로파일링 구체화
**측정 시나리오:**
```bash
# ROUTER 패턴, 64B 메시지 프로파일링
perf record -g -F 99 ./benchwithzmq/build/bench_router_dealer tcp 64 1000000
perf report
perf script | ~/FlameGraph/stackcollapse-perf.pl | ~/FlameGraph/flamegraph.pl > router_64b.svg
```

**비교 지표:**
- CPU cycles per message
- Instructions per message
- Cache miss rate
- Branch misprediction rate
- Lock contention (futex calls)

### 4. 누락된 고려사항

#### A. 벤치마크 결과 재현성
- CPU frequency scaling 고정 필요
- Background 프로세스 최소화
- NUMA node 고정 (taskset)
- 10회 반복의 표준편차 확인

#### B. ASIO 특성 분석
- ASIO proactor pattern overhead
- async_read/async_write 호출 오버헤드
- io_context.run() 이벤트 루프 비용
- strand를 통한 동기화 비용

#### C. 메시지 경로 추적
- ROUTER send 경로: `router::xsend() -> lb::send() -> pipe::write()`
- ROUTER recv 경로: `router::xrecv() -> fq::recv() -> pipe::read()`
- Identity frame 처리 오버헤드
- Fair-queuing 알고리즘 비용

#### D. 최적화 후 회귀 테스트
- 전체 테스트 스위트 61/61 통과 유지
- 다른 패턴(PAIR, PUBSUB, DEALER)에 영향 없음 확인
- 큰 메시지 성능 유지 확인

### 5. 산출물 구조 제안
```
docs/team/20260115_router-performance-optimization/
├── plan.md                    # 현재 계획 (Codex)
├── review-initial.md          # 초기 검토 (Claude)
├── review-gemini.md           # Gemini 리뷰 (예정)
├── review-codex.md            # Codex 검증 (예정)
├── analysis/
│   ├── router-diff.md        # ROUTER 구현 차이 분석
│   ├── profiling-zlink.txt   # zlink 프로파일링 결과
│   ├── profiling-libzmq.txt  # libzmq-ref 프로파일링 결과
│   └── hotspots.md           # Hot spot 비교 분석
├── optimization/
│   ├── phase1-router.md      # ROUTER 최적화 결과
│   ├── phase2-batching.md    # 메시지 배칭 결과
│   └── phase3-ipc.md         # IPC 최적화 결과
└── benchmark/
    ├── baseline.txt          # 현재 baseline (이미 존재)
    ├── after-phase1.txt      # Phase 1 후 결과
    ├── after-phase2.txt      # Phase 2 후 결과
    └── after-phase3.txt      # Phase 3 후 결과
```

## 다음 단계 권장사항

1. **libzmq-ref 빌드 및 벤치마크 확인** (단계 0)
2. **ROUTER 구현 diff 분석** (단계 1 시작)
3. **perf 프로파일링 실행** (단계 2 준비)
4. **Codex 계획 검증 요청** (Phase 1 완료)

## 리스크 재평가

### 중요도: High
- libzmq-ref가 Windows 기반으로 빌드되었을 경우 Linux 벤치마크와 직접 비교 어려움
  - **대응**: libzmq-ref도 WSL/Linux에서 재빌드하여 동일 환경 확보

### 중요도: Medium
- ASIO 구조적 차이로 인한 최적화 한계
  - **대응**: 최적화 가능 범위와 구조적 한계를 명확히 구분하여 문서화

### 중요도: Low
- 프로파일링 결과 해석의 복잡성
  - **대응**: flamegraph 활용 및 주요 hot path만 집중 분석

## 결론

Codex의 계획은 **전체적으로 잘 구성**되어 있으며, 위의 보완 사항들을 추가하면 실행 가능한 계획이 됩니다.

**권장 조치:**
1. libzmq-ref 빌드 확인 및 벤치마크 도구 준비
2. 단계 0 (환경 준비) 먼저 완료
3. ROUTER diff 분석으로 단계 1 시작
4. 각 단계별로 Codex/Gemini 리뷰 진행

**계획 승인 여부**: ✅ 승인 (보완사항 반영 후)
