[English](design-decisions.md) | [한국어](design-decisions.ko.md)

# 설계 결정 기록

이 문서는 zlink 주요 설계 결정의 근거와 대안 검토 내용을 기록한다.

---

## 1. Routing ID 정책

### 1.1 소켓 own routing_id: 16B UUID

**결정**: 모든 소켓의 자동 생성 own routing_id 는 16B UUID(binary).

**근거**:
- 16B UUID 는 노드/프로세스 간 전역 유일성을 보장한다
- 모니터링/디버깅에서 소켓 식별에 충분한 엔트로피를 제공한다
- 4B/5B 등 짧은 식별자로는 확보하기 어려운 프로세스 간 충돌 회피 여유를 제공한다

### 1.2 STREAM peer/client routing_id: 4B uint32

**결정**: STREAM 연결별 peer routing_id는 4B uint32.

**근거**:
- msg_t 내부 routing_id 필드가 이미 uint32_t
- 연결 수 기준 uint32 범위 충분
- own routing_id(식별)와 peer routing_id(라우팅)의 용도 구분

### 1.3 문자열 alias 유지

**결정**: `zlink_set_routing_id()` / `zlink_get_routing_id()`와 `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` (`zlink_set_router_option()`으로 설정)는 가변 길이 문자열 유지.

**근거**:
- ROUTER에서 문자열 alias 기반 디버깅/로깅 패턴이 널리 사용됨
- 연결별 alias 지정 기능 필요
- routing_id 길이를 고정하지 않음

### 1.4 기본 routing_id 생성 위치

**결정**: core/src/sockets/socket_base.cpp에서 생성 (서비스 레이어가 아닌 코어).

**근거**:
- 코어에서 이미 socket_id 기반 자동 생성 동작 중
- 서비스 유틸(routing_id_utils.hpp)은 override 목적으로만 사용
- 계층 위반 방지 (services → core 의존 역전 없음)

---

## 2. 모니터링 설계

### 2.1 Polling 방식 선택

**결정**: 모니터링은 Polling(PAIR 소켓) 방식만 제공.

**근거**:
- Callback 방식은 I/O 스레드에서 호출되어 데드락 위험
- Polling은 사용자 스레드에서 안전하게 처리
- zlink_poll로 다중 소켓 모니터링 조합 가능

### 2.2 CONNECTION_READY 이벤트

**결정**: 송수신 준비 시점은 `CONNECTION_READY` 이벤트로 신호한다.

**근거**:
- CONNECTED/ACCEPTED 는 전송계층 레벨이라 혼동을 유발한다
- 사용자에게 "실제 송수신 가능 시점" 을 명확히 제공한다
- 핸드셰이크 완료 = 연결 완료로 의미를 통일한다

### 2.3 DISCONNECTED reason 코드

**결정**: DISCONNECTED 이벤트에 reason 코드(0~5) 추가.

**근거**:
- 의도적 종료(LOCAL)와 비의도적 종료(TRANSPORT_ERROR) 구분 필요
- 운영 디버깅에서 종료 원인 파악 필수

### 2.4 단일 이벤트 포맷

**결정**: 모니터링 이벤트는 단일 포맷을 사용한다 (포맷 버저닝 없음).

**근거**:
- 하위 호환 요구사항이 없다 (호환성 미고려 방침)
- 포맷 분기 로직을 제거해 구현/사용이 단순해진다

---

## 3. Service Discovery 설계

### 3.1 Discovery 서비스

**결정**: Discovery 는 서비스 위치 질의에 응답하고 단독으로도 사용된다 (서비스 목록 조회).

**근거**:
- 관심사 분리: "어디에 있는지" 와 "어떻게 보낼지" 는 독립이다
- Discovery 는 여러 downstream consumer 에 연결할 수 있다
- 하나의 Discovery 인스턴스가 여러 socket subscriber 에 조회 결과를 발행할 수 있다

---

## 4. SPOT 설계

### 4.1 PUB/SUB 기반 mesh

**결정**: SPOT 클러스터는 PUB/SUB mesh(각 노드가 서로 연결된 그물망 구조)로 구성한다.

**근거**:
- 토픽 기반 fanout(한 발신자 → 다수 수신자 분배)에 PUB/SUB 가 자연스럽다
- ROUTER 기반 대비 구독 필터링이 효율적이다
- Discovery 기반 자동 mesh 구성이 가능하다

### 4.2 재발행 없음 정책

**결정**: 원격 수신 메시지는 로컬로만 분배하며 재발행하지 않는다.

**근거**:
- 재발행은 메시지 루프/중복을 유발한다
- full-mesh 연결이 1-hop 전달을 보장한다
- 네트워크 대역폭을 절약한다

---

## 5. 네이밍: zlink / ZLINK

**결정**: 공개 식별자는 zlink / ZLINK 를 사용한다. ZMTP/ZeroMQ 와이어 호환성은 제공하지 않는다.

**근거**:
- 독립 프로젝트로서 명확한 아이덴티티를 확립한다
- ZMP 와이어 프로토콜이 ZMTP 와 비호환이므로 API alias 는 잘못된 호환성을 시사한다
- 단일 정식 명칭은 문서와 telemetry 의 모호함을 제거한다
