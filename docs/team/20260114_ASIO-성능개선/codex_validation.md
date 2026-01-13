# Codex Validation: ASIO 성능 개선 계획

## 1) Claude 리뷰 지적사항 타당성

### 타당한 지적
- **`_encoder->message_ready()` 존재 여부**: 현재 `_encoder`는 `i_encoder` 인터페이스 포인터이며 `message_ready()`가 노출돼 있지 않습니다. 계획의 분기 조건은 코드 레벨에서 바로 사용 불가하므로 타당한 지적입니다. (`src/i_encoder.hpp`, `src/encoder.hpp`)
- **encoder 결과 직접 전달 lifetime 문제**: `async_write_some`은 버퍼가 완료 콜백까지 살아 있어야 합니다. 계획의 “stack/local buffer로 1회 복사”는 async 쓰기에 부적절합니다. 타당합니다.
- **ABI 호환성**: 새 옵션 ID 추가 시 `include/zmq.h`의 옵션 enum 확장 순서가 중요합니다. 타당합니다.
- **기본값 변경 영향**: `out_batch_size` 기본값(현재 8192)을 1로 낮추면 전체 경로의 특성이 달라지고 기존 사용자에 영향 가능성이 큽니다. 타당합니다.
- **측정 범위 확대(메모리/CPU, 다양한 메시지 크기, 동시성)**: 성능 개선과 트레이드오프를 검증하기 위한 요구로 합리적입니다.

### 현재 코드 기준으로는 부정확하거나 과잉인 지적
- **타이머 0ms/배치 타이머 관련 위험**: 현재 `src/asio/asio_engine.cpp`에는 배치 타이머나 `_write_batching_enabled`가 존재하지 않습니다. 계획 문서에 “타이머”가 언급되지만 코드와 불일치합니다. 이 지적은 **계획이 코드와 맞지 않다는 관점에서는 유효**하나, 실제 위험(0ms busy-wait)은 현재 코드 기준으로는 적용되지 않습니다.

## 2) 계획의 실행 가능성(코드 레벨)

### 실행 가능
- `process_output()`의 `out_batch_size` 루프/복사 경로는 실제 존재합니다. 작은 메시지에 대한 즉시성 분기를 추가하는 구조 변경 자체는 가능합니다. (`src/asio/asio_engine.cpp`)
- 새 소켓 옵션 추가는 기존 패턴(옵션 ID → `options_t` 저장 → `socket_base` 적용)으로 확장 가능합니다. (`include/zmq.h`, `src/options.*`, `src/socket_base.cpp`)

### 계획 문서의 구체 항목 중 **코드 불일치/불가능 요소**
- **`_write_batching_enabled`, 배치 타이머, `_write_batch_max_messages/_bytes`**: 현재 코드에 해당 멤버/로직이 없습니다. 계획이 다른 브랜치를 기준으로 작성된 것으로 보이며, 그대로는 구현 불가합니다.
- **`_encoder->message_ready()` 기반 분기**: `_encoder` 타입에서 사용할 수 없습니다.
- **stack/local buffer 1회 복사 경로**: 비동기 write에서 스택 버퍼는 수명 문제가 있어 불가능합니다. 최소한 멤버 버퍼(예: `_write_buffer`) 또는 고정 persistent 버퍼로 복사해야 합니다.
- **`out_batch_size` 루프 “건너뛰기”**: 가능하지만, 현재 encoder가 `encode(&outpos, 0)` 이후 `_outpos` 버퍼를 반환하는 방식과 맞물립니다. outpos lifetime을 보장하는 설계 없이 “직접 전달”은 안전하지 않습니다.

## 3) 빠진 내용 / 추가 개선점

- **ASIO WebSocket 엔진(`src/asio/asio_ws_engine.cpp`) 동기화**: `process_output()` 구조가 유사합니다. TCP/IPC만 개선하면 WS 경로가 동일 병목을 유지할 가능성이 큽니다.
- **작은 메시지 기준 정의**: “payload < 1024B, message count == 1”은 현재 코드에서 message count를 직접 얻기 어렵습니다. 기준을 `_outsize`만으로 삼거나, 메시지 크기 정보를 전달할 수 있는 지점(encoder/load_msg)에서 추적하는 로직이 필요합니다.
- **테스트/검증 계획**: 새 옵션 추가 시 `tests/` 또는 `unittests/`에 옵션 동작/기본값 유지에 대한 테스트가 필요합니다.
- **실제 병목 검증**: 현재 코드에는 타이머가 없으므로 “타이머 대기”가 실제 원인인지 확인이 필요합니다. `out_batch_size` 기반 batch가 주요 원인인지 프로파일링(throughput/latency)로 먼저 확인해야 합니다.

## 4) 최종 판단: 이 계획으로 진행해도 되는가?

**현 상태 그대로는 진행 비권장.**
- 계획 문서가 현재 코드 구조와 불일치하며, 구현 불가능한 항목(타이머, `_write_batching_enabled`, stack buffer, message_ready)을 포함합니다.
- 먼저 실제 코드에 맞춘 수정 계획(옵션 이름/대상, 분기 기준, 버퍼 lifetime 보장)을 재정의해야 합니다.

**진행 가능 조건(권장 수정 요약)**
1. 타이머/배치 관련 서술을 **현재 코드의 `out_batch_size` 기반 로직**으로 재정렬.
2. “즉시성 경로”는 **멤버 버퍼를 사용하는 안전한 수명 관리**를 전제로 설계.
3. 분기 조건에 사용할 **실제 접근 가능한 상태**를 정의(예: `_outsize`, `_tx_msg.size()` 추적 등).
4. WS 엔진 포함 여부와 테스트 계획을 문서에 추가.

위 수정이 반영되면, 코드 레벨에서 실행 가능한 계획으로 전환 가능합니다.
