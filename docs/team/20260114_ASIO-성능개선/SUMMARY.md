# ASIO 성능 개선 계획 - 요약 보고서

## 진행 프로세스

Multi-AI Team Collaboration 워크플로우를 사용하여 성능 개선 계획을 수립했습니다.

### Phase 1: 계획 수립 및 검증 (완료)

1. **Codex**: 초기 계획 문서 작성 (docs/team/20260114_ASIO-성능개선/plan.md)
2. **Claude**: 계획 검토 및 개선점 도출 (docs/team/20260114_ASIO-성능개선/claude_review.md)
3. **Codex**: 코드 레벨 검증 및 실행 가능성 평가 (docs/team/20260114_ASIO-성능개선/codex_validation.md)
4. **Codex**: 검증 결과 기반 계획 수정 (docs/team/20260114_ASIO-성능개선/plan.md - 최종)

## 주요 발견 사항

### 원본 계획의 문제점

Codex 검증을 통해 다음 문제점들이 발견되었습니다:

1. **존재하지 않는 코드 요소 참조**
   - `_write_batching_enabled`, 배치 타이머, `_write_batch_max_messages/_bytes`: 현재 코드에 없음
   - `_encoder->message_ready()`: i_encoder 인터페이스에서 접근 불가

2. **구현 불가능한 접근 방식**
   - stack/local buffer 사용: async write에서 수명 문제 발생
   - encoder 결과 직접 전달: lifetime 보장 불가

3. **누락된 중요 항목**
   - WebSocket 엔진 (`src/asio/asio_ws_engine.cpp`) 동기화
   - 테스트 계획
   - 실제 병목 검증 단계

## 수정된 최종 계획

### 핵심 변경사항

1. **즉시성 전용 쓰기 경로**
   - ❌ 타이머 기반 → ✅ `out_batch_size` 기반 분기
   - ❌ stack buffer → ✅ 멤버 버퍼 (`_write_buffer` 또는 `_immediate_write_buffer`)
   - ❌ `_encoder->message_ready()` → ✅ `_outsize` 등 실제 접근 가능한 상태

2. **out_batch_size 튜닝 및 옵션화**
   - 현재 기본값 8192 유지
   - latency-critical 환경에서 1 또는 작은 값으로 조정 가능하도록 문서화
   - trade-off 명시 (throughput 감소 가능성)

3. **소켓 옵션 추가**
   - `ZMQ_ASIO_WRITE_BATCHING`: per-socket latency 최적화 경로 선택
   - telemetry 카운터: 배치 평균 메시지 수, 배치 미사용 쓰기 횟수

4. **WebSocket 엔진 포함**
   - `src/asio/asio_ws_engine.cpp`에도 동일한 최적화 적용
   - WS 경로의 병목 유지 방지

5. **테스트 및 검증**
   - 즉시성 경로 수명 문제 검증 테스트
   - WS 엔진 동작 확인 테스트
   - 실제 병목 프로파일링 단계 추가
   - CPU/메모리 사용률 측정

## 구현 대상 코드 위치

### 주요 파일

1. **src/asio/asio_engine.cpp:588** (`process_output`)
   - `out_batch_size` 루프 분기 추가
   - 짧은 메시지 즉시 전송 경로 구현
   - 멤버 버퍼 사용으로 수명 보장

2. **src/asio/asio_ws_engine.cpp** (`process_output`)
   - TCP/IPC와 동일한 최적화 적용

3. **include/zmq.h, src/options.hpp, src/options.cpp, src/socket_base.cpp**
   - 새 소켓 옵션 정의 및 적용
   - 기존 패턴 따름: 옵션 ID 추가 → options 저장 → socket_base 적용

## 성능 측정 계획

### 측정 항목

- **Latency**: p50, p99
- **Throughput**: msg/s
- **메모리**: RSS (valgrind/massif)
- **CPU**: 사용률 (perf stat)

### 테스트 시나리오

- 메시지 크기: 32B, 256B, 512B, 1KB, 4KB, 16KB
- 프로토콜: TCP, IPC, WS
- `out_batch_size`: 8192 (기본), 1024, 1
- 동시성: 1, 2, 4, 8 소켓

### 실행 방법

```bash
taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20
```

### 결과 기록 위치

- `benchwithzmq/benchmark_result.txt`
- `benchwithzmq/zlink_BENCHMARK_RESULTS.md`
- 날짜, 커밋 해시, 옵션 값, 환경 정보 포함

## 다음 단계

### 사용자 승인 필요 항목

1. ✅ 최종 계획 승인
2. ⏳ 옵션 명명 확정 (`ZMQ_ASIO_WRITE_BATCHING` 등)
3. ⏳ 프로파일링 도구 선택 확정

### 승인 후 진행 순서

**Phase 2: 구현 및 코드 리뷰**
1. Claude가 계획에 따라 코드 구현
2. Codex가 코드 리뷰 수행
3. 리뷰 피드백 반영 및 재리뷰 (통과 시까지)

**Phase 3: 문서화 및 성능 테스트**
1. Gemini가 변경사항 문서화
2. 성능 테스트 실행 및 결과 기록
3. 최종 결과 정리 (`doc/plan/ASIO_PERF_REDO_RESULTS.md`)

## 산출물 위치

```
docs/team/20260114_ASIO-성능개선/
├── plan.md                    # 최종 계획 (Codex 수정)
├── claude_review.md           # Claude 리뷰
├── codex_validation.md        # Codex 검증
└── SUMMARY.md                 # 본 문서
```

## 원본 계획 문서 업데이트

원본 계획 문서 (`doc/plan/ASIO_PERF_REDO_PLAN.md`)도 수정사항을 반영하여 업데이트했습니다.

---

**계획 승인 후 바로 구현에 착수할 수 있습니다.**
