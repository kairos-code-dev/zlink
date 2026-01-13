# Gemini Review: ASIO 성능 개선 계획 및 분석

## 1. 총평
제출된 **libzmq 구조 재현 전략**과 **분석 보고서**는 매우 타당하며, ASIO 구현이 libzmq 대비 성능(특히 Latency)이 떨어지는 근본 원인을 정확하게 짚고 있습니다. "Speculative Write(동기 우선)"와 "Zero-Copy(Encoder 버퍼 직접 사용)"는 성능 회복을 위한 필수적인 방향입니다.

다만, 실행 계획(Phase)의 순서와 일부 리스크 관리에서 보완이 필요합니다.

## 2. 세부 검토 의견

### 2.1 libzmq 구조 재현 전략의 타당성 (유효함)
- **분석의 정확성:** `restart_output` → `out_event` → `write`로 이어지는 libzmq의 "즉시 전송(Speculative Write)" 경로와 ASIO의 "비동기 스케줄링 대기" 경로의 차이를 정확히 분석했습니다.
- **해결책의 적절성:** ASIO에서도 `would_block` 전까지는 동기 `write_some`을 수행하고, 불가피한 경우에만 비동기로 전환하는 Hybrid 방식은 표준적인 ASIO 고성능 패턴이며 이 프로젝트에 적합합니다.

### 2.2 3단계 Phase 접근의 실행 가능성 (수정 필요)
**현재 계획의 순서(1 → 2 → 3)는 기술적 의존성을 고려할 때 실행이 불가능하거나 비효율적입니다.**

- **문제점:** Phase 1(Speculative Write 도입)을 구현하려면 `_transport->write_some()`(동기 메서드) 호출이 필수적입니다. 그러나 현재 계획상 Transport 인터페이스 확장은 Phase 3에 있습니다. 인터페이스가 없는 상태에서 Phase 1을 구현할 수 없습니다.
- **제안:** **Phase 3(Transport 확장)를 Phase 1(Speculative Write)보다 먼저 수행하거나 동시에 진행해야 합니다.**
  - **수정된 순서:**
    1. **Phase 1:** Transport 인터페이스 확장 (`write_some` 추가) 및 TCP/TLS 구현
    2. **Phase 2:** Speculative Write 로직 적용 (Engine 레벨)
    3. **Phase 3:** Encoder 버퍼 직접 사용 (Zero-Copy)

### 2.3 리스크 대응의 충분성 (보완 필요)
- **WebSocket 프레임 처리 (중요):**
  - TCP와 달리 WebSocket은 메시지 프레임(Frame) 단위로 전송됩니다.
  - `speculative_write`가 데이터를 부분적으로만 썼을 때(Partial Write), WebSocket Transport 계층에서 이를 적절히 프레임으로 캡슐화하거나 버퍼링할 수 있는지 검증이 필요합니다. 단순 `write_some` 매핑만으로는 WS 프로토콜이 깨질 위험이 있습니다.
- **Async 전환 시 데이터 복사 검증:**
  - Phase 3에서 "Async 전환 시에만 복사"한다고 했는데, 이 시점의 경계 조건(Edge case) 테스트가 필수적입니다.
  - 벤치마크 환경에서는 소켓 버퍼가 가득 차는 `would_block` 상황이 잘 발생하지 않을 수 있으므로, 강제로 `would_block`을 유발하거나 버퍼 사이즈를 줄인 상태에서의 단위 테스트가 추가되어야 합니다.

### 2.4 빠진 내용 및 개선점
- **구체적인 테스트 계획:** 단순히 "기존 벤치마크 수행" 외에, **"Async Fallback 경로"**가 정상 동작하는지 확인하는 테스트가 필요합니다. (예: 수신 측을 일시 중단시켜 송신 버퍼를 가득 채운 뒤 데이터 무결성 검증)
- **TLS 고려:** TLS Transport(`ssl_transport`)도 `write_some`을 지원해야 합니다. `boost::asio::ssl::stream`은 `write_some`을 지원하므로 래핑만 하면 되지만, Phase 1 단계에서 누락되지 않도록 명시해야 합니다.
- **WSS Transport:** WebSocket TLS(`wss_transport`)도 동일하게 고려해야 합니다.

## 3. 결론 및 제안

전략 자체는 매우 훌륭합니다. 다만 실행 순서를 현실적으로 조정하고, WebSocket/TLS 등 특수 Transport에 대한 고려를 추가하면 완벽할 것입니다.

**승인 여부:** **조건부 승인** (Phase 순서 조정 및 WebSocket 리스크 검토 반영 후 진행)

### 수정 제안된 Phase 순서
1. **Phase 1:** Transport Interface 확장 (동기 `write_some` 추가) 및 TCP/TLS/WS/WSS 구현
2. **Phase 2:** Speculative Write 구현 (Engine 레벨)
3. **Phase 3:** Encoder 버퍼 직접 사용 (Zero-Copy 및 수명 관리)

### 추가 검증 요구사항
- WebSocket 프레임 처리 시 부분 쓰기 처리 확인
- would_block 강제 유발 테스트 추가
- 모든 Transport (TCP, TLS, WS, WSS)에서 동기 write_some 동작 확인
