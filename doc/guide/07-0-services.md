# 서비스 계층 개요

## 1. 서비스 계층이란

zlink의 서비스 계층은 7종 소켓(PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER, STREAM) 위에 구축된 **고수준 분산 서비스 기능**이다. 소켓 수준의 연결/라우팅을 직접 다루지 않고도 서비스 등록, 발견, 위치투명 통신을 수행할 수 있다.

## 2. 아키텍처

```
┌─────────────────────────────────────────────────────────┐
│                    Application                           │
│         Gateway (요청/응답)  ·  SPOT (발행/구독)          │
├─────────────────────────────────────────────────────────┤
│                  Discovery (서비스 발견)                   │
│            subscribe · get · service_available            │
├─────────────────────────────────────────────────────────┤
│                  Registry (서비스 등록소)                  │
│        register · heartbeat · broadcast SERVICE_LIST      │
├─────────────────────────────────────────────────────────┤
│              zlink Core (7종 소켓 + 6종 Transport)        │
└─────────────────────────────────────────────────────────┘
```

- **Registry**는 서비스 엔트리를 관리하고, 주기적으로 SERVICE_LIST를 브로드캐스트한다.
- **Discovery**는 Registry를 구독하여 서비스 목록을 로컬 캐시로 유지한다.
- **Gateway**와 **SPOT**은 Discovery를 통해 대상을 자동 발견하고 연결한다.

## 서비스 명칭

| 서비스 | 명칭 의미 | 한줄 설명 |
|--------|-----------|-----------|
| **Registry** | 서비스 등록소 | 서비스 엔트리를 등록·관리하는 중앙 저장소 |
| **Discovery** | 서비스 발견 | Registry를 구독하여 서비스 목록을 로컬 캐시로 유지 |
| **Gateway** | 서비스 게이트웨이 | 서비스에 대한 접근점 + 클라이언트 사이드 로드밸런서. API Gateway(인증, rate limiting 등)와는 다른 개념 |
| **Receiver** | 서비스 수신자 | Gateway로부터 요청을 받아 처리하는 백엔드 |
| **SPOT** | 위치(spot) 투명 pub/sub | 객체 단위의 위치투명한 토픽 기반 발행/구독 메시 |

## 3. 서비스 구성 요소

### 3.1 Service Discovery — 기반 인프라

Registry 클러스터 기반의 서비스 등록/발견 시스템. Receiver가 Registry에 등록하면 Discovery가 이를 구독하여 서비스 목록을 관리한다.

- Registry 클러스터 HA (flooding 동기화)
- Heartbeat 기반 생존 확인
- Client-side 서비스 목록 캐싱

자세한 내용은 [Service Discovery 가이드](07-1-discovery.md)를 참고.

### 3.2 Gateway — 위치투명 요청/응답

Discovery 기반으로 서비스 Receiver를 자동 발견하고, 로드밸런싱된 메시지 전송을 처리한다. send 전용으로 설계되어 **thread-safe**하며, 여러 스레드에서 동시에 전송할 수 있다.

- **Thread-safe** — send 전용 설계로 경합이 적고, 여러 스레드에서 동시 전송 가능
- Round Robin / Weighted 로드밸런싱
- 자동 연결/해제 (Discovery 이벤트 기반)

자세한 내용은 [Gateway 가이드](07-2-gateway.md)를 참고.

### 3.3 SPOT — 위치투명 토픽 PUB/SUB

Discovery 기반으로 PUB/SUB Mesh를 자동 구성하여 클러스터 전체에서 토픽 메시지를 발행/구독한다.

- 토픽 기반 발행/구독
- 패턴(와일드카드) 구독
- Discovery 기반 자동 Mesh 구성

자세한 내용은 [SPOT 가이드](07-3-spot.md)를 참고.

## 4. 서비스 간 관계

```
                    ┌──────────┐
                    │ Registry │
                    │ (PUB+    │
                    │  ROUTER) │
                    └────┬─────┘
                         │ SERVICE_LIST 브로드캐스트
            ┌────────────┼────────────┐
            │            │            │
            v            v            v
      ┌──────────┐ ┌──────────┐ ┌──────────┐
      │Discovery │ │Discovery │ │Discovery │
      │(Gateway용)│ │(SPOT 용) │ │(직접 사용)│
      └────┬─────┘ └────┬─────┘ └──────────┘
           │             │
           v             v
      ┌──────────┐ ┌──────────┐
      │ Gateway  │ │   SPOT   │
      │ (ROUTER) │ │(PUB+SUB) │
      └──────────┘ └──────────┘
```

- **Discovery가 기반 인프라**: Gateway와 SPOT 모두 Discovery를 통해 대상을 발견한다.
- **Gateway**는 DEALER/ROUTER 패턴으로 요청/응답을 처리한다.
- **SPOT**은 PUB/SUB 패턴으로 토픽 메시지를 전파한다.
- Gateway와 SPOT은 독립적으로 동작하며, 동일한 Registry 클러스터를 공유할 수 있다.
