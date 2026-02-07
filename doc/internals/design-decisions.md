# 설계 결정 기록

이 문서는 zlink 주요 설계 결정의 근거와 대안 검토 내용을 기록한다.

---

## 1. Routing ID 정책

### 1.1 소켓 own routing_id: 16B UUID

**결정**: 모든 소켓의 자동 생성 own routing_id를 16B UUID(binary)로 통일.

**근거**:
- 기존 5B `[0x00][uint32]` 포맷은 프로세스 간 충돌 가능성 존재
- 16B UUID는 노드/프로세스 간 전역 유일성 보장
- 모니터링/디버깅에서 소켓 식별에 충분한 엔트로피 제공

**대안 검토**:
- 4B uint32 통일안 → 충돌 가능성으로 폐기
- 5B 통일안 → 16B UUID 대비 유일성 부족으로 폐기

### 1.2 STREAM peer/client routing_id: 4B uint32

**결정**: STREAM 연결별 peer routing_id는 4B uint32.

**근거**:
- msg_t 내부 routing_id 필드가 이미 uint32_t
- 연결 수 기준 uint32 범위 충분
- own routing_id(식별)와 peer routing_id(라우팅)의 용도 구분

### 1.3 문자열 alias 유지

**결정**: ZLINK_ROUTING_ID, ZLINK_CONNECT_ROUTING_ID는 가변 길이 문자열 유지.

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

**결정**: HANDSHAKE_SUCCEEDED를 CONNECTION_READY로 대체.

**근거**:
- CONNECTED/ACCEPTED는 전송계층 레벨이라 혼동 유발
- 사용자에게 "실제 송수신 가능 시점"을 명확히 제공
- 핸드셰이크 완료 = 연결 완료로 의미 통일

### 2.3 DISCONNECTED reason 코드

**결정**: DISCONNECTED 이벤트에 reason 코드(0~5) 추가.

**근거**:
- 의도적 종료(LOCAL)와 비의도적 종료(TRANSPORT_ERROR) 구분 필요
- 운영 디버깅에서 종료 원인 파악 필수

### 2.4 단일 이벤트 포맷

**결정**: Version 1/2 대신 단일 포맷 사용.

**근거**:
- 기존 호환성 불필요 (호환성 미고려 방침)
- 포맷 분기 로직 제거로 구현/사용 단순화

---

## 3. 폐기된 기능

### 3.1 Thread-safe Socket (02번)

**폐기 사유**:
- Asio Strand 직렬화의 복잡성 대비 실질 이점 부족
- 소켓별 단일 스레드 접근 패턴이 실전에서 충분
- 내부 복잡성 증가로 디버깅 어려움

### 3.2 Request/Reply API (03번)

**폐기 사유**:
- Gateway API가 동일 기능을 더 완전하게 제공
- 독립 API 유지 시 중복 코드/개념 발생
- Gateway의 request_id 기반 매핑이 REQ/REP 패턴 대체

### 3.3 Metrics API (07번)

**폐기 사유**:
- 소켓 레벨 통계 수집의 성능 오버헤드
- 애플리케이션 레벨에서 필요한 메트릭만 수집하는 것이 효율적
- 외부 모니터링 시스템(Prometheus 등)과의 통합이 더 적합

---

## 4. Service Discovery 설계

### 4.1 Discovery/Gateway 분리

**결정**: Discovery(서비스 발견)와 Gateway(메시지 라우팅)를 별도 컴포넌트로 분리.

**근거**:
- 관심사 분리: "어디에 있는지"와 "어떻게 보낼지" 독립
- Discovery만 단독 사용 가능 (서비스 목록 조회)
- 동일 Discovery로 여러 Gateway 생성 가능

### 4.2 ROUTER/ROUTER 패턴

**결정**: Gateway↔Receiver 통신에 ROUTER/ROUTER 사용.

**근거**:
- routing_id 기반 대상 지정 가능
- 다중 Gateway/Receiver 확장성 보장
- 양방향 소켓으로 요청 송신 + 응답 수신 처리

### 4.3 request_id 기반 매핑

**결정**: 요청-응답 매핑에 uint64_t request_id 사용.

**근거**:
- 여러 Receiver에 동시 요청 시 응답 순서 무관하게 매핑
- Gateway가 자동 생성하여 관리 부담 최소화

---

## 5. SPOT 설계

### 5.1 PUB/SUB 기반 mesh

**결정**: SPOT 클러스터는 PUB/SUB mesh로 구성.

**근거**:
- 토픽 기반 fanout에 PUB/SUB가 자연스러움
- ROUTER 기반 대비 구독 필터링이 효율적
- Discovery 기반 자동 mesh 구성 가능

### 5.2 재발행 없음 정책

**결정**: 원격 수신 메시지는 로컬 분배만, 재발행하지 않음.

**근거**:
- 재발행 시 메시지 루프/중복 발생
- 전체 mesh 연결(full-mesh)로 1홉 전달 보장
- 네트워크 대역폭 절약

---

## 6. ZMQ→ZLINK 네이밍 전환

**결정**: zmq/ZMQ/ZeroMQ 명칭을 전면 zlink/ZLINK로 전환. 호환성 미제공.

**근거**:
- 독립 프로젝트로서 명확한 아이덴티티 확립
- ZMTP 비호환이므로 API 호환 유지 의미 없음
- 전면 치환으로 혼란 제거
