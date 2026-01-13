# Review Synthesis and Plan Updates

## 검토 요약

Gemini와 Codex 두 AI의 검토 결과를 종합하여 계획을 수정했습니다.

### Gemini 검토 핵심 사항 (gemini_review_v2.md)

**조건부 승인** - Phase 순서 조정 및 WebSocket 리스크 검토 반영 필요

1. **CRITICAL: Phase 의존성 오류 발견**
   - 문제: Phase 1 (Speculative Write)이 Phase 3 (Transport 인터페이스)에 의존
   - Phase 1은 `_transport->write_some()` 호출이 필요하나, 인터페이스는 Phase 3에서 추가 예정
   - **결론: 구현 불가능한 순서**

2. **수정된 Phase 순서 제안**
   - Phase 1: Transport Interface 확장 (동기 `write_some` 추가) 및 TCP/TLS/WS/WSS 구현
   - Phase 2: Speculative Write 구현 (Engine 레벨)
   - Phase 3: Encoder 버퍼 직접 사용 (Zero-Copy 및 수명 관리)

3. **추가 검증 요구사항**
   - WebSocket 프레임 처리 시 부분 쓰기 처리 확인
   - would_block 강제 유발 테스트 추가
   - 모든 Transport (TCP, TLS, WS, WSS)에서 동기 write_some 동작 확인

### Codex 검토 핵심 사항 (codex_validation_v2.md)

**구현 가능하나, 출력 경로 리팩터링이 전제 조건**

1. **코드 레벨 구현 가능성**: 가능하나 `process_output()`와 `_outpos/_outsize` 상태 처리 리팩터링 필요

2. **상태 전이 충돌 위험** (Phase 2)
   - `restart_output()` 재진입 시 동기/비동기 write 중복 실행 가능
   - `_write_pending` 단일 플래그로 인한 상태 꼬임 위험
   - **대응**: 단일 write-in-flight 규칙 명문화 및 테스트

3. **Encoder 버퍼 수명 문제** (Phase 3)
   - 동기 경로는 안전, async 경로는 복사 또는 메시지 보존 필요
   - encoder 포인터는 다음 encode 호출 또는 메시지 close 시 무효화

4. **Transport 인터페이스 확장**
   - TCP/SSL: straightforward
   - WS/WSS: frame 기반 특성 때문에 의미 재정의 필요
   - Beast의 `write_some()` 동작을 "한 프레임 전송 시도"로 재정의

5. **벤치마크 기준 보강 필요**
   - Phase별 성공 기준 추가 (이전 Phase 대비 개선)
   - Phase 1: 짧은 메시지 p99 latency 개선 폭
   - Phase 2: CPU/throughput 개선 기준
   - Phase 3: Transport별 기능 검증

## 계획 반영 사항

### 1. Phase 순서 수정 ✅

**변경 전 (오류)**:
1. Speculative Write 도입
2. Encoder 버퍼 직접 사용
3. Transport 인터페이스 확장

**변경 후 (수정)**:
1. **Transport 인터페이스 확장** (Speculative Write의 전제 조건)
2. **Speculative Write 도입** (Engine 레벨)
3. **Encoder 버퍼 직접 사용** (Zero-Copy)

### 2. Phase별 성능 기준 추가 ✅

각 Phase에 구체적인 성공 기준을 추가했습니다:

**Phase 1 (Transport 인터페이스)**:
- TCP/TLS transport에서 동기 write_some 성공률 측정
- WebSocket transport에서 frame 전송 정확도 검증

**Phase 2 (Speculative Write)**:
- 짧은 메시지(< 1KB) p99 latency가 baseline 대비 **30% 이상 개선**
- would_block 강제 유발 테스트에서 async fallback 정상 동작
- 데이터 무결성 검증

**Phase 3 (Zero-Copy)**:
- CPU 사용률 baseline 대비 **10% 이상 감소**
- throughput baseline 대비 **15% 이상 향상**
- 모든 transport에서 zero-copy 경로 동작 확인

### 3. 상태 전이 규칙 명시 ✅

Phase 2에 상태 전이 규칙을 명시적으로 추가:
- speculative_write 진입 시 `_write_pending` 검사 (true면 skip)
- would_block 발생 시 즉시 `_write_pending = true` 설정
- 단일 write-in-flight 보장 (동기/비동기 중복 방지)
- `_output_stopped`는 버퍼 상태와 일치 유지

### 4. WebSocket 프레임 처리 검증 추가 ✅

Phase 1에 WebSocket 특수 처리 요구사항 추가:
- frame 단위 전송으로 의미 재정의
- would_block 동작 검증 필수
- 부분 쓰기(partial write) 처리 검증

### 5. Edge Case 테스트 계획 추가 ✅

새로운 "Edge Case 테스트" 섹션 추가:
1. would_block 강제 유발 테스트
2. 버퍼 사이즈 축소 테스트
3. WebSocket 프레임 경계 테스트

### 6. 단위 테스트 요구사항 추가 ✅

Phase별 단위 테스트 항목:
- 상태 전이 충돌 테스트
- encoder 버퍼 수명 테스트
- Transport별 `write_some()` 동작 검증

### 7. 리스크 섹션 상세화 ✅

리스크를 Phase별로 재구성:
- **상태 전이 충돌** (Phase 2 핵심 리스크)
- **버퍼 수명 문제** (Phase 3 핵심 리스크)
- **WebSocket 프레임 처리** (Phase 1 검증 필수)
- **부분 전송 처리** (공통)

## 결론

두 AI 검토자 모두 **전략 자체는 매우 타당**하다고 평가했으며, 다음 사항을 조건으로 승인:

1. ✅ Phase 순서를 Transport → Speculative Write → Zero-Copy로 수정
2. ✅ Phase별 성공 기준 추가
3. ✅ 상태 전이 규칙 명문화
4. ✅ WebSocket/WSS 특수 처리 명시
5. ✅ Edge case 테스트 계획 추가

모든 피드백이 `plan.md`에 반영되었으며, 이제 구현 준비가 완료되었습니다.
