[English](design-decisions.md) | [한국어](design-decisions.ko.md)

# 설계 결정 기록

이 문서는 zlink 주요 설계 결정의 근거와 대안 검토 내용을 기록한다.

---

## 1. Routing ID 정책

### 1.1 소켓 own routing_id: 16B UUID

**결정**: 모든 소켓의 자동 생성 own routing_id는 16B UUID(binary).

**근거**:
- 16B random UUID는 노드/프로세스 간 충돌 가능성을 사실상 무시할 수준으로 낮춘다
- 모니터링/디버깅에서 소켓 식별에 충분한 엔트로피를 제공한다
- 4B/5B 등 짧은 식별자로는 확보하기 어려운, 프로세스 간 충돌 회피 여유를 준다

### 1.2 STREAM peer/client routing_id: 4B uint32

**결정**: STREAM 연결별 peer routing_id는 4B uint32.

**근거**:
- msg_t 내부 routing_id 필드가 이미 uint32_t
- 연결 수 기준으로 uint32 범위면 충분
- own routing_id(식별)와 peer routing_id(라우팅)의 용도 구분

### 1.3 문자열 alias 유지

**결정**: `zlink_set_routing_id()` / `zlink_get_routing_id()`와 `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID`(`zlink_set_router_option()`으로 설정)는 가변 길이 byte routing id/alias(문자열로도 쓸 수 있음)로 유지한다.

**근거**:
- ROUTER에서 문자열 alias 기반 디버깅/로깅 패턴이 널리 쓰인다
- 연결별 alias 지정 기능이 필요하다
- routing_id 길이를 고정하지 않는다

### 1.4 기본 routing_id 생성 위치

**결정**: core/src/runtime/sockets/common/socket_base.cpp에서 생성한다(서비스 레이어가 아닌 코어).

**근거**:
- 코어에서 이미 socket_id 기반 자동 생성이 동작 중이다
- 서비스 유틸(routing_id_utils.hpp)은 override를 적용하거나, override가 없으면 서비스 소켓에 routing_id를 채우는 데 쓴다
- 상위 runtime에서 Core로 향하는 의존 방향을 유지

---

## 2. 모니터링 설계

### 2.1 Polling 방식 선택

**결정**: 모니터링은 기본적으로 direct receive 표면을 제공하고, 선택적으로
단방향 handler 콜백을 제공한다.

**근거**:
- 기본 recv 모델은 사용자 스레드에서 안전하게 처리되며 I/O 스레드 콜백 데드락
  위험을 피한다
- 콜백 기반 전달을 선호하면 handler를 부착할 수 있다(콜백 전용 모드로 단방향
  전환)
- poller로 다중 소켓 모니터링을 조합할 수 있다

### 2.2 CONNECTION_READY 이벤트

**결정**: 송수신 준비 시점은 `CONNECTION_READY` 이벤트로 알린다.

**근거**:
- CONNECTED/ACCEPTED는 transport 레벨이라 혼동을 유발한다
- 사용자에게 "실제 송수신 가능 시점"을 명확히 알려 준다
- 핸드셰이크 완료를 곧 연결 완료로 의미를 통일한다

### 2.3 DISCONNECTED reason 코드

**결정**: DISCONNECTED 이벤트에 reason 코드(`UNKNOWN=0`, `HANDSHAKE_FAILED=3`,
`TRANSPORT_ERROR=4`, `CTX_TERM=5`)를 추가한다.

**근거**:
- context 종료(`CTX_TERM`)와 transport 오류(`TRANSPORT_ERROR`), handshake 실패(`HANDSHAKE_FAILED`)를 구분해야 한다
- 운영 디버깅에서 종료 원인 파악이 필수다

### 2.4 단일 이벤트 포맷

**결정**: 모니터링 이벤트는 단일 포맷을 사용한다(포맷 버저닝 없음).

**근거**:
- 하위 호환 요구사항이 없다(호환성 미고려 방침)
- 포맷 분기 로직을 제거해 구현과 사용이 단순해진다

---
---

## 3. SPOT 설계

### 3.1 PUB/SUB 기반 mesh

**결정**: SPOT 클러스터는 PUB/SUB mesh(각 노드가 서로 연결된 그물망 구조)로 구성한다.

**근거**:
- 토픽 기반 fanout(한 발신자가 다수 수신자에게 보내기)에 PUB/SUB가 자연스럽다
- ROUTER 기반보다 구독 필터링이 효율적이다
- 명시적 peer 연결로 mesh 구성을 제어할 수 있다

### 3.2 재발행 없음 정책

**결정**: 원격 수신 메시지는 로컬로만 분배하고 재발행하지 않는다.

**근거**:
- 재발행은 메시지 루프와 중복을 유발한다
- full-mesh 연결이 1-hop 전달을 보장한다
- 네트워크 대역폭을 절약한다

---

## 5. 네이밍: zlink / ZLINK

**결정**: 공개 식별자는 zlink / ZLINK를 사용한다. ZMTP/ZeroMQ 와이어 호환성은 제공하지 않는다.

**근거**:
- 독립 프로젝트로서 명확한 아이덴티티를 확립한다
- ZMP 와이어 프로토콜이 ZMTP와 비호환이므로, API alias는 잘못된 호환성을 시사한다
- 단일 정식 명칭은 문서와 telemetry의 모호함을 없앤다
