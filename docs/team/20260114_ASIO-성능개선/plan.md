# ASIO 성능 개선 계획 (libzmq 구조 재현)

## 목표
- libzmq의 send 경로를 재현해 짧은 메시지 latency를 회복한다.
- Speculative Write와 encoder zero-copy를 중심으로 구조를 단순화한다.
- 불필요한 소켓 옵션 추가를 중단하고, 구조적 개선으로 해결한다.

## 핵심 변경사항
1. 기존 `out_batch_size` 분기 접근 대신 **libzmq 구조 재현** 전략 적용
2. **Speculative Write(투기적 쓰기)**를 핵심 전송 경로로 도입
3. **encoder 버퍼 직접 사용**으로 복사 제거
4. **transport 인터페이스 확장**으로 동기 write 지원
5. **불필요한 소켓 옵션 추가 제거** (옵션/telemetry로 우회하지 않음)

## 구현 전략: 3단계 Phase

### Phase 1: Transport 인터페이스 확장
**목표:** 모든 transport에서 동기 `write_some()` 지원 (Speculative Write의 전제 조건)

- 구현 내용
  - `i_asio_transport` 인터페이스에 동기 `write_some(const uint8_t* data, size_t len)` 메서드를 추가한다.
  - TCP/TLS transport에 구현 (`stream_descriptor::write_some`, `ssl_stream::write_some` 래핑)
  - WebSocket/WSS transport에 구현 (frame 단위 전송으로 의미 재정의, would_block 동작 검증 필수)
  - 기존 async API와 공존하도록 구현한다.
- 대상 파일
  - `src/asio/i_asio_transport.hpp` (인터페이스 추가)
  - `src/asio/tcp_transport.cpp`, `src/asio/ssl_transport.cpp` (동기 write 구현)
  - `src/asio/ws_transport.cpp`, `src/asio/wss_transport.cpp` (frame 기반 동기 write 구현)
- 완료 기준
  - 모든 transport에서 `write_some()` 호출 가능
  - would_block 발생 시 올바른 에러 코드 반환 (EAGAIN/EWOULDBLOCK)
  - **WebSocket frame 기반 전송에서 부분 쓰기(partial write) 처리 검증**
- Phase별 성능 기준
  - TCP/TLS transport에서 동기 write_some 성공률 측정
  - WebSocket transport에서 frame 전송 정확도 검증 (프레임 경계 유지)

### Phase 2: Speculative Write 도입
**목표:** 메시지 도착 즉시 동기 write를 시도하고, 실패 시에만 async로 전환

- 구현 내용
  - `restart_output()` 또는 동일한 출력 재개 지점에서 `speculative_write()`를 호출하도록 변경한다.
  - `speculative_write()`는 동기 `_transport->write_some()`을 시도하고, `would_block` 시에만 기존 async 경로로 전환한다.
  - `process_output()`의 흐름은 "동기 우선, 실패 시 async"로 재구성한다.
  - **상태 전이 규칙 명시**:
    - speculative_write 진입 시 `_write_pending` 검사 (true면 skip)
    - would_block 발생 시 즉시 `_write_pending = true` 설정 후 async 전환
    - 단일 write-in-flight 보장 (동기/비동기 중복 방지)
    - `_output_stopped`는 버퍼 상태와 일치 유지
- 대상 파일
  - `src/asio/asio_engine.cpp` (process_output, restart_output 리팩터링)
  - `src/asio/asio_ws_engine.cpp` (동일한 출력 재개 지점 적용)
- 완료 기준
  - 짧은 메시지에서 POLLOUT 대기 없이 즉시 전송됨
  - would_block 시 기존 async 경로로 정상 전환
  - **상태 전이 충돌 없이 동작** (단위 테스트로 검증)
- Phase별 성능 기준
  - **짧은 메시지(< 1KB) p99 latency가 baseline(현행 ASIO) 대비 30% 이상 개선**
  - would_block 강제 유발 테스트에서 async fallback 정상 동작
  - 수신 측 일시 중단 → 송신 버퍼 full → 데이터 무결성 검증

### Phase 3: Encoder 버퍼 직접 사용 (Zero-Copy)
**목표:** `_write_buffer` 복사를 제거하고 encoder 버퍼 포인터를 직접 사용

- 구현 내용
  - `_outpos`가 encoder 내부 버퍼를 직접 가리키도록 경로를 정리한다.
  - 동기 write 경로에서는 encoder 버퍼를 그대로 `write_some()`에 전달한다.
  - **async 경로로 전환되는 경우에만 버퍼 수명 보장을 위해 복사를 수행한다.**
  - encoder zero-copy 조건을 훼손하지 않도록 `encode()` 호출 흐름을 유지한다.
  - encoder 버퍼 lifetime 정책 문서화 (동기 경로 안전성, async 전환 시 복사 필요성)
- 대상 파일
  - `src/asio/asio_engine.cpp` (process_output의 버퍼 처리 로직)
  - `src/asio/asio_ws_engine.cpp`
  - `src/encoder.hpp` (수명/포인터 정책 주석 보강)
- 완료 기준
  - 동기 write 경로에서 memcpy가 제거됨 (프로파일링으로 확인)
  - async 경로로 전환될 때만 복사가 발생함
  - **encoder 버퍼 무효화 전 async write 완료 보장** (수명 안전성 검증)
- Phase별 성능 기준
  - **CPU 사용률 baseline 대비 10% 이상 감소** (memcpy 제거 효과)
  - **throughput baseline 대비 15% 이상 향상**
  - 모든 transport(TCP/TLS/WS/WSS)에서 zero-copy 경로 동작 확인

## 소켓 옵션 정책
- 새로운 소켓 옵션(`ZMQ_ASIO_WRITE_BATCHING` 등) 추가는 하지 않는다.
- 기존 옵션/telemetry로 우회하지 않고 구조를 단순화한다.
- 이미 추가된 옵션이 있다면 제거하거나 비활성화한다.

## 벤치마크 및 검증

### 성능 벤치마크
- 각 Phase 완료 후 아래 벤치를 동일 조건으로 수행한다.
  ```bash
  taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20
  ```
- 기록 항목
  - p50/p99 latency
  - throughput (msg/s)
  - CPU 사용률
- 최종 목표 기준 (Phase 3 완료 후)
  - p99 latency: libzmq 대비 +-10% 이내
  - throughput: libzmq 대비 +-10% 이내

### Edge Case 테스트 (Phase 2/3에서 필수)
1. **would_block 강제 유발 테스트**
   - 수신 측을 일시 중단시켜 송신 버퍼를 가득 채움
   - speculative write → async fallback 경로 정상 동작 확인
   - 데이터 무결성 검증 (손실/중복/순서 보장)

2. **버퍼 사이즈 축소 테스트**
   - 소켓 송신 버퍼를 의도적으로 작게 설정 (SO_SNDBUF)
   - would_block 발생 빈도 증가 시나리오에서 안정성 검증

3. **WebSocket 프레임 경계 테스트**
   - 부분 쓰기 발생 시 frame 프로토콜 무결성 확인
   - 수신 측에서 정상 frame 파싱 검증

### 단위 테스트 추가 요구사항
- 상태 전이 충돌 테스트 (`_write_pending` 중복 진입 방지)
- encoder 버퍼 수명 테스트 (async 전환 시 복사 검증)
- Transport별 `write_some()` 동작 검증 (TCP/TLS/WS/WSS)

## 리스크 및 대응

### 상태 전이 충돌 (Phase 2 핵심 리스크)
- **리스크**: `restart_output()` 재진입 시 동기/비동기 write 중복 실행
- **대응**:
  - speculative_write 진입 시 `_write_pending` 검사 후 true면 skip
  - would_block 발생 시 즉시 `_write_pending = true` 설정
  - 단일 write-in-flight 규칙 명문화 및 단위 테스트 추가
  - `_output_stopped`는 실제 버퍼 상태와 일치 유지

### 버퍼 수명 문제 (Phase 3 핵심 리스크)
- **리스크**: encoder 버퍼 포인터가 async 완료 전 무효화될 수 있음
- **대응**:
  - 동기 경로는 즉시 write로 안전 보장
  - async 전환 시에만 `_write_buffer`로 복사하여 수명 보호
  - encoder `encode()` 호출 규칙 유지 (다음 encode 전까지 유효)
  - async write 완료 전 `process_output()` 재진입 방지

### WebSocket 프레임 처리 (Phase 1 검증 필수)
- **리스크**: 부분 쓰기 시 WebSocket frame 경계가 깨질 수 있음
- **대응**:
  - Beast의 `write_some()` 동작을 "한 프레임 전송 시도"로 재정의
  - would_block 발생 시 프레임 캡슐화 상태 유지 검증
  - WS/WSS transport 단위 테스트에서 부분 전송 시나리오 검증

### 부분 전송 처리
- **대응**: `_outpos/_outsize` 갱신은 기존 흐름과 동일하게 유지
  - 동기 write 후 전송된 바이트만큼 `_outpos` 이동
  - 나머지는 async 경로에서 처리

## 참고
- 분석 문서: `docs/team/20260114_ASIO-성능개선/libzmq_analysis.md`
- libzmq 원본 소스: `/home/ulalax/project/ulalax/libzmq-ref`
- 현행 ASIO 구현: `src/asio/asio_engine.cpp`
