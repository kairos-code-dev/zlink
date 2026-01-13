# Claude 리뷰: ASIO 성능 개선 계획

## 전반적 평가
계획 문서는 기존 ASIO_PERF_REDO_PLAN.md를 상당히 개선했으며, 구현 단계와 측정 방법이 구체화되었습니다. 다만 몇 가지 추가 고려사항과 보완이 필요합니다.

## 1. 성능 측정 방법 검토

### 잘된 점
- p50/p99 latency와 throughput 측정 명시
- CPU 고정 (`taskset -c 0`) 사용
- 워밍업 고려
- A/B 테스트 방법론 제시

### 개선 필요 사항
1. **메모리 사용량 측정 누락**
   - `_write_buffer` 복사 제거로 인한 메모리 사용량 변화 측정 필요
   - 제안: `valgrind --tool=massif` 또는 `/proc/[pid]/status`로 RSS 측정

2. **CPU 사용률 측정 누락**
   - 복사 제거 및 경로 단순화로 인한 CPU 사용률 변화 측정
   - 제안: `perf stat` 또는 `top`으로 CPU % 측정

3. **다양한 메시지 크기 시나리오**
   - 현재 "< 1024B"만 언급
   - 제안: 32B, 256B, 512B, 1KB, 4KB, 16KB 등 다양한 크기로 테스트
   - 각 크기별로 최적의 threshold 값 찾기

4. **동시성 테스트 시나리오 부족**
   - 단일 스레드만 아닌 멀티 스레드/멀티 소켓 환경에서의 성능
   - 제안: 1, 2, 4, 8 소켓 동시 사용 시나리오 추가

5. **TCP vs IPC 비교**
   - 계획에는 "IPC/TCP 각각"이라고만 언급
   - 제안: 각 프로토콜별 baseline 대비 개선율을 별도 표로 정리

## 2. 구현 단계의 실현 가능성과 위험 요소

### 잘된 점
- 코드 위치를 명확히 지정 (src/asio/asio_engine.cpp:process_output)
- 동시성 안전성 고려 (`_write_pending`, `_write_in_progress`)
- 기존 패턴 따르기 (옵션 ID 추가 → options 저장 → socket_base 적용)

### 위험 요소 및 대응 방안

1. **`_encoder->message_ready()` 메서드 존재 여부 불확실**
   - 위험: 해당 메서드가 없을 수 있음
   - 대응: 실제 코드 확인 필요, 대안으로 `_encoder` 내부 상태를 다른 방식으로 확인

2. **encoder 결과 직접 전달 시 lifetime 문제**
   - 위험: `_outpos`가 encoder 내부 버퍼를 가리키는 경우, 비동기 write 중 invalidate될 수 있음
   - 대응: encoder 버퍼 lifetime 보장 메커니즘 확인 필요, 불가능하면 stack buffer 복사 방식 사용

3. **타이머 0ms 설정 시 busy-wait 위험**
   - 위험: 타이머를 0으로 설정하면 즉시 flush되지만, 다음 메시지 대기 시 CPU 100% 사용 가능
   - 대응: 타이머 0은 "타이머 비활성화 + 즉시 전송"을 의미하도록 명확히 구현

4. **ABI 호환성**
   - 위험: 새 소켓 옵션 추가 시 기존 바이너리와의 호환성 문제
   - 대응: `include/zmq.h`에 옵션 ID를 끝에 추가하고, 버전 관리 명확히

5. **기본값 변경의 영향**
   - 위험: `_write_batch_max_messages`/`_write_batch_max_bytes` 기본값을 1로 변경하면 기존 사용자에게 성능 저하 가능
   - 대응: **기본값은 유지**하고, 명시적 옵션 설정 시에만 변경되도록 권장

## 3. 실패 사례 분석 충분성

### 잘된 점
- 과거 실패 원인 3가지를 명확히 정리
  1. 배치 타이머가 latency 지배
  2. `_write_buffer` 복사 비용
  3. per-socket 제어 불가

- 개선 방향과 원인의 1:1 대응 명확

### 추가 필요 사항

1. **과거 실패 문서 직접 링크**
   - 현재: "doc/plan/FAILED_PERFORMANCE_WORK/" 경로만 언급
   - 제안: 각 실패 사례 문서를 읽고 구체적인 실패 지점과 본 계획의 차이점을 표로 정리

2. **성능 저하가 발생한 커밋/브랜치 정보**
   - 언제부터 성능이 나빠졌는지 git history에서 확인
   - 해당 커밋의 변경사항과 본 계획의 개선점 비교

3. **libzmq와의 성능 gap 정량화**
   - "libzmq 수준으로 회복"이라고만 언급
   - 제안: 현재 zlink와 libzmq의 latency/throughput 차이를 숫자로 명시
   - 목표: "p99 latency를 X% 이내로, throughput을 Y% 이상으로"

## 4. 추가 고려 사항

### 4.1 테스트 자동화
- 현재: 수동으로 `benchwithzmq/run_benchmarks.sh` 실행
- 제안: CI/CD에 성능 regression 테스트 추가
  - GitHub Actions에 benchmark job 추가
  - 성능이 일정 threshold 이하로 떨어지면 실패

### 4.2 문서화
- 새 옵션(`ZMQ_ASIO_BATCH_TIMEOUT`, `ZMQ_ASIO_WRITE_BATCHING`)의 사용법을 README 또는 별도 문서에 추가
- 언제 어떤 옵션을 사용해야 하는지 decision tree 제공

### 4.3 점진적 배포 전략
1. Phase 1: 즉시성 경로 추가 (기본값 유지)
2. Phase 2: 소켓 옵션 추가 및 테스트
3. Phase 3: 타이머 옵션화
4. Phase 4: 기본값 변경 (선택적)

각 Phase마다 벤치마크 실행 및 기록

### 4.4 롤백 계획
- 성능 개선이 목표치에 미달하거나 버그 발생 시 롤백 방법
- feature flag 또는 compile-time option으로 새 경로 비활성화 가능하도록

### 4.5 코드 리뷰 체크리스트
- [ ] 메모리 leak 없음 (valgrind 확인)
- [ ] Thread safety 검증 (TSan 실행)
- [ ] 기존 테스트 모두 통과
- [ ] 새 옵션에 대한 단위 테스트 추가
- [ ] 성능 개선 수치 확인 (최소 20% 이상)

## 5. 권장 수정 사항

### 계획 문서에 추가할 섹션

#### 5.1 "현재 성능 baseline" 섹션
```markdown
## 현재 성능 Baseline (2026-01-14)
- libzmq (v4.3.5):
  - TCP 256B: p50=XXus, p99=XXus, throughput=XXXmsg/s
  - IPC 256B: p50=XXus, p99=XXus, throughput=XXXmsg/s
- zlink (current):
  - TCP 256B: p50=XXus, p99=XXus, throughput=XXXmsg/s
  - IPC 256B: p50=XXus, p99=XXus, throughput=XXXmsg/s
- 성능 gap: TCP p99 latency X배, IPC throughput Y% 저하
```

#### 5.2 "성능 목표" 섹션
```markdown
## 성능 목표
1. Short-term (Phase 1-2 완료 시)
   - p99 latency를 libzmq 대비 150% 이내로 (현재 300%에서)
   - throughput을 libzmq 대비 80% 이상으로 (현재 50%에서)

2. Long-term (Phase 3-4 완료 시)
   - p99 latency를 libzmq 대비 110% 이내로
   - throughput을 libzmq 대비 95% 이상으로
```

#### 5.3 "위험 관리" 섹션
각 구현 단계별 위험 요소와 대응 방안을 표로 정리

## 결론

계획은 전반적으로 잘 구성되어 있으나, 다음 사항을 보완하면 더욱 견고해질 것입니다:

1. **측정 지표 확대**: 메모리, CPU, 다양한 메시지 크기, 동시성 시나리오
2. **정량적 목표 설정**: 현재 baseline과 목표 수치 명시
3. **위험 관리 강화**: 각 단계별 위험 요소와 대응 방안 상세화
4. **점진적 배포**: Phase별 배포 및 검증 전략
5. **문서화 계획**: 새 옵션 사용법 가이드

이러한 보완 후 구현에 착수하는 것을 권장합니다.
