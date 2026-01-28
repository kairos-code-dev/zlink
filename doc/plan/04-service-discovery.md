# 서비스 디스커버리 스펙 (Service Discovery)

> **우선순위**: 4 (Core Feature)
> **상태**: Draft
> **버전**: 3.4
> **의존성**:
> - [00-routing-id-unification.md](00-routing-id-unification.md)
> - [03-request-reply-api.md](03-request-reply-api.md) (msgv helper)

## 목차
1. [개요](#1-개요)
2. [아키텍처](#2-아키텍처)
3. [Registry 설계](#3-registry-설계)
4. [Discovery 설계](#4-discovery-설계)
5. [Gateway 설계](#5-gateway-설계)
6. [Provider 설계](#6-provider-설계)
7. [메시지 프로토콜](#7-메시지-프로토콜)
8. [C API 명세](#8-c-api-명세)
9. [사용 예시](#9-사용-예시)
10. [구현 계획](#10-구현-계획)
11. [검증 방법](#11-검증-방법)

---

## 1. 개요

### 1.1 배경

마이크로서비스 환경에서 서비스 인스턴스는 동적으로 생성/삭제된다.
클라이언트는 서비스 위치를 하드코딩하지 않고 동적으로 발견해야 한다.

### 1.2 목표

- **클라이언트 사이드 로드 밸런싱**: 클라이언트가 직접 서버 선택
- **자동 연결/해제**: Registry 브로드캐스트 기반
- **고가용성**: Registry 클러스터 (3노드)
- **임베디드 모드**: 별도 프로세스 없이 노드 내장
- **멀티 서비스**: 서비스별 독립 연결 풀 관리
- **관심사 분리**: Discovery(서비스 발견)와 Gateway(메시지 라우팅) 분리
- **외부 의존 최소화**: 별도 로드밸런서/API Gateway 없이도 빠른 서비스 구성
- **언어 바인딩 친화**: C API 기반으로 각 언어에서 동일한 흐름 제공

### 1.3 핵심 개념

| 용어 | 설명 |
|------|------|
| **Registry** | 서비스 등록/해제 관리, 목록 브로드캐스트 |
| **Discovery** | Registry 구독, 서비스 목록 관리 (어디에 있는지) |
| **Gateway** | Provider 연결, 메시지 라우팅 (어떻게 보낼지) |
| **Provider** | 서비스 제공자 (서버), Registry에 등록 |
| **Heartbeat** | Provider 생존 확인 메커니즘 |
| **Request ID** | 요청-응답 매핑용 식별자 |

---

## 2. 아키텍처

### 2.1 전체 구조

```
┌─────────────────────────────────────────────────────────────┐
│                    Registry Cluster                         │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐                 │
│  │Registry1│◄──►│Registry2│◄──►│Registry3│                 │
│  │PUB+ROUTER│   │PUB+ROUTER│   │PUB+ROUTER│                │
│  └────┬────┘    └────┬────┘    └────┬────┘                 │
│       │              │              │                       │
│       └──────────────┼──────────────┘                       │
│                      │ (서비스 목록 브로드캐스트)            │
└──────────────────────┼──────────────────────────────────────┘
                       │
        ┌──────────────┼──────────────┐
        │              │              │
        ▼              ▼              ▼
   ┌─────────┐    ┌─────────┐    ┌─────────┐
   │ Client  │    │ Client  │    │Provider │
   │Discovery│    │Discovery│    │(DEALER) │
   │  (SUB)  │    │  (SUB)  │    │(ROUTER) │
   │    │    │    │    │    │    └─────────┘
   │    ▼    │    │    ▼    │
   │ Gateway │    │ Gateway │
   │(ROUTER) │    │(ROUTER) │
   └─────────┘    └─────────┘
```

### 2.2 클라이언트 내부 구조

```
┌─────────────────────────────────────────────────────────────┐
│                      Application                             │
│                           │                                  │
│              ┌────────────┴────────────┐                    │
│              ▼                         ▼                    │
│      ┌─────────────┐           ┌─────────────┐             │
│      │  Discovery  │──────────►│   Gateway   │             │
│      │ (서비스 발견)│  서비스    │ (ROUTER 전송) │             │
│      │             │  목록 전달  │             │             │
│      │ - Registry  │           │ - Provider  │             │
│      │   SUB 연결  │           │   ROUTER 풀 │             │
│      │ - 서비스    │           │ - 로드밸런싱 │             │
│      │   목록 관리 │           │ - 요청/응답  │             │
│      └──────┬──────┘           └──────┬──────┘             │
│             │                          │                    │
└─────────────┼──────────────────────────┼────────────────────┘
              │                          │
              ▼                          ▼
        ┌──────────┐              ┌──────────┐
        │ Registry │              │ Provider │
        │  (PUB)   │              │ (ROUTER) │
        └──────────┘              └──────────┘
```

### 2.3 통신 패턴

| 경로 | 소켓 타입 | 프로토콜 |
|------|----------|----------|
| Registry → Discovery | PUB/SUB | 서비스 목록 브로드캐스트 |
| Provider → Registry | DEALER/ROUTER | 등록/해제/Heartbeat |
| Gateway → Provider | ROUTER/ROUTER | 비즈니스 메시지 |

### 2.3.1 구성 요소별 소켓 역할 요약

| 구성 요소 | 소켓 | 역할 |
|-----------|------|------|
| **Registry** | **PUB** | Discovery에 SERVICE_LIST 브로드캐스트 |
| **Registry** | **ROUTER** | Provider 등록/해제/Heartbeat 요청 처리 + ACK |
| **Discovery** | **SUB** | Registry의 SERVICE_LIST 수신 (캐시/필터) |
| **Gateway** | **ROUTER** | Provider로 요청 전송, 응답 수신 (로드밸런싱/대상 지정) |
| **Provider** | **ROUTER** | 비즈니스 요청 수신/응답 처리 |
| **Provider** | **DEALER** | Registry로 REGISTER/UNREGISTER/HEARTBEAT 전송 |

> Note:
> - Gateway/Provider의 ROUTER는 **양방향 소켓**이다.
>   Gateway는 요청 **송신 + 응답 수신**, Provider는 요청 **수신 + 응답 송신**을 수행한다.
> - ROUTER/ROUTER를 사용하는 이유는 **routing_id 기반 대상 지정/응답 매핑**과
>   **다중 Gateway/Provider 확장성**을 보장하기 위함이다.

### 2.4 Registry 클러스터 동기화

```
Registry1 ◄──────────► Registry2
    ▲                      ▲
    │                      │
    └──────► Registry3 ◄───┘
```

- 각 Registry는 **SUB 소켓으로 다른 Registry의 PUB를 구독**
- 등록/해제 이벤트를 다른 Registry로 **전파(flooding)**
- **Eventually Consistent**: 모든 Registry가 동일 상태 수렴
- 각 Registry는 고유 `registry_id`와 `list_seq`를 포함하여
  **중복/역전 업데이트를 무시**할 수 있게 한다.

> Note: 이는 Gossip 프로토콜이 아닌 단순 전파 방식이다.
> Gossip은 랜덤 피어 선택 + 상태 교환이 특징이며, 여기서는
> 모든 피어에게 즉시 전파하는 flooding 방식을 사용한다.

### 2.5 End-to-End 동작 시퀀스

#### 2.5.1 Provider 등록/갱신

1) Provider가 `zmq_provider_register()` 호출  
2) Registry는 `service_name + advertise_endpoint`로 엔트리 조회  
3) 기존 엔트리가 있으면 **갱신**(routing_id/weight/heartbeat)  
4) 변경 사항을 반영한 뒤 `list_seq` 증가  
5) `SERVICE_LIST` 브로드캐스트

#### 2.5.2 Discovery 갱신 및 Gateway 반영

1) Discovery가 `SERVICE_LIST` 수신  
2) `(registry_id, list_seq)` 기준으로 최신 스냅샷만 적용  
3) 서비스 디렉터리 갱신 후 이벤트 발생  
4) Gateway는 이벤트를 받아 서비스 풀을 갱신  
   - 신규 Provider: ROUTER connect  
   - 제거 Provider: ROUTER disconnect  

#### 2.5.3 요청/응답 흐름

1) 클라이언트가 `zmq_gateway_send()` 호출  
2) Gateway가 로드밸런싱으로 Provider 선택  
3) `request_id` 생성 후 ROUTER 프레임에 삽입  
4) Provider가 동일 `request_id`로 응답  
5) Gateway가 pending map에서 요청 매핑 후 응답 반환

#### 2.5.4 장애/복구

1) Provider Heartbeat 미수신 → Registry가 제거  
2) Registry가 `SERVICE_LIST` 브로드캐스트  
3) Discovery가 목록 갱신 → Gateway가 disconnect  
4) Provider 재등록 시 동일 흐름으로 복구

### 2.6 상태 머신 요약

#### 2.6.1 Registry 상태 (프로세스 수준)

```
[INIT]
  | start()
  v
[RUNNING] <-------------------------+
  | stop()/destroy()               |
  v                                 |
[STOPPED] --------------------------+
```

#### 2.6.2 Provider 상태 (서비스별)

```
[INIT]
  | bind()
  v
[BOUND]
  | connect_registry()
  v
[CONNECTED]
  | register()
  v
[REGISTERED] --heartbeat--> [REGISTERED]
  | unregister()/timeout
  v
[UNREGISTERED]
```

#### 2.6.3 Discovery 상태 (서비스별)

```
[EMPTY]
  | SERVICE_LIST 수신 (provider_count > 0)
  v
[AVAILABLE]
  | SERVICE_LIST 수신 (provider_count == 0)
  v
[UNAVAILABLE]
```

#### 2.6.4 Gateway 상태 (서비스별)

```
[NO_POOL]
  | PROVIDER_ADDED
  v
[POOL_READY] <---- provider 변화 ----+
  | PROVIDER_REMOVED (last)         |
  v                                 |
[NO_POOL] --------------------------+
```

### 2.7 API 호출 순서 표 (권장 흐름)

#### Provider

| 단계 | 호출 | 설명 |
|------|------|------|
| 1 | `zmq_provider_new()` | Provider 생성 |
| 2 | `zmq_provider_bind()` | 비즈니스 ROUTER bind |
| 3 | `zmq_provider_connect_registry()` | Registry ROUTER 연결 |
| 4 | `zmq_provider_register()` | 서비스 등록 + Heartbeat 시작 |
| 5 | `zmq_provider_register_result()` | 등록 결과 확인(선택) |
| 6 | `zmq_provider_threadsafe_router()` | 비즈니스 ROUTER 소켓(thread-safe) 획득 |
| 7 | `zmq_provider_unregister()` | 종료 전 해제(선택) |
| 8 | `zmq_provider_destroy()` | 리소스 정리 |

#### Client (Discovery + Gateway)

| 단계 | 호출 | 설명 |
|------|------|------|
| 1 | `zmq_discovery_new()` | Discovery 생성 |
| 2 | `zmq_discovery_connect_registry()` | Registry PUB 연결 |
| 3 | `zmq_discovery_subscribe()` | 관심 서비스 등록 |
| 4 | `zmq_gateway_new()` | Gateway 생성 |
| 5 | `zmq_gateway_set_lb_strategy()` | 로드밸런싱 설정(선택) |
| 6 | `zmq_gateway_send()` | 메시지 전송 |
| 7 | `zmq_gateway_recv()` | 응답 수신 |
| 8 | `zmq_gateway_destroy()` | Gateway 정리 |
| 9 | `zmq_discovery_destroy()` | Discovery 정리 |

#### SPOT Node (PUB/SUB Mesh)

| 단계 | 호출 | 설명 |
|------|------|------|
| 1 | `zmq_spot_node_new()` | Node 생성 (node_id 생성) |
| 2 | `zmq_spot_node_bind()` | PUB bind (클러스터 송신 endpoint) |
| 3 | `zmq_spot_node_connect_registry()` | Registry ROUTER 연결 |
| 4 | `zmq_spot_node_register()` | **service_name=spot-node** 등록 |
| 5 | `zmq_discovery_new()` | Discovery 생성 |
| 6 | `zmq_discovery_connect_registry()` | Registry PUB 구독 |
| 7 | `zmq_discovery_subscribe()` | `spot-node` 구독 |
| 8 | `zmq_spot_node_set_discovery()` | peer 자동 연결 시작 |

#### Registry

| 단계 | 호출 | 설명 |
|------|------|------|
| 1 | `zmq_registry_new()` | Registry 생성 |
| 2 | `zmq_registry_set_endpoints()` | PUB/ROUTER 설정 |
| 3 | `zmq_registry_add_peer()` | 클러스터 피어 추가(선택) |
| 4 | `zmq_registry_set_heartbeat()` | Heartbeat 정책 설정(선택) |
| 5 | `zmq_registry_set_broadcast_interval()` | 브로드캐스트 주기 설정(선택) |
| 6 | `zmq_registry_start()` | Registry 시작 |
| 7 | `zmq_registry_destroy()` | 종료 |

### 2.8 Thread-safety 정책

- **Registry / Discovery / Gateway / Provider**의 공개 API는
  내부에서 직렬화되어 **멀티스레드 호출을 허용**한다.
- Gateway/Provider의 내부 ROUTER는 **raw 소켓**이지만,
  외부에는 **thread-safe 핸들만 노출**한다.
  - `zmq_gateway_threadsafe_router()` / `zmq_provider_threadsafe_router()`
    반환값은 thread-safe 사용을 보장한다.
- `recv` 계열은 **단일 소비자(한 스레드)** 사용을 권장한다.
---

## 3. Registry 설계

### 3.1 Registry 모드

| 모드 | 설명 | 사용 사례 |
|------|------|----------|
| **Standalone** | 독립 프로세스로 실행 | 대규모 클러스터 |
| **Embedded** | 애플리케이션에 내장 | 소규모/테스트 환경 |

### 3.2 Registry 상태 관리

```cpp
struct service_entry_t {
    std::string service_name;       // 서비스 이름
    std::string endpoint;           // "tcp://host:port"
    zmq_routing_id_t routing_id;    // Provider routing_id
    uint64_t registered_at;         // 등록 시간
    uint64_t last_heartbeat;        // 마지막 Heartbeat
    uint32_t weight;                // 로드밸런싱 가중치
};

struct registry_state_t {
    uint32_t registry_id;            // Registry 식별자 (프로세스 시작 시 생성)
    uint64_t list_seq;               // SERVICE_LIST 증가 번호
    std::map<std::string, std::vector<service_entry_t>> services;
    // key: service_name, value: provider 목록
};
```

- `registry_id`는 설정하지 않으면 시작 시 랜덤으로 생성된다.

### 3.3 Heartbeat 메커니즘

```
┌──────────┐         ┌──────────┐
│ Provider │         │ Registry │
└────┬─────┘         └────┬─────┘
     │  REGISTER          │
     │───────────────────►│
     │  REGISTER_ACK      │
     │◄───────────────────│
     │                    │
     │  HEARTBEAT (5s)    │
     │───────────────────►│
     │                    │
     │  HEARTBEAT (5s)    │
     │───────────────────►│
     │                    │
     │  (timeout 15s)     │
     │         X          │ ← Provider 제거 + 브로드캐스트
     │                    │
```

- Heartbeat 주기: 5초 (기본값, 설정 가능)
- Timeout: 15초 (3회 미수신 시 제거)
- 제거 시 모든 Discovery에 SERVICE_LIST 브로드캐스트

### 3.5 Registry HA 전략 (Provider Failover 방식)

Registry HA는 **“Provider 단일 활성 Registry + 장애 시 재등록”** 방식으로 동작한다.

#### 3.5.1 기본 원칙

- Provider는 **Registry endpoint 목록을 보유**한다.
- Provider는 **한 Registry에만 REGISTER/HEARTBEAT**를 전송한다.
- 동시에 여러 Registry에 등록/Heartbeat 전송은 **금지**한다.
  (중복 엔트리/정합성/타임아웃 처리 복잡성 증가)

#### 3.5.2 장애 감지와 전환

- Provider는 Registry 연결 상태를 모니터링한다.
  - **권장**: monitor 이벤트(DISCONNECTED/CONNECT_FAILED)
  - **보조**: send 실패(EHOSTUNREACH/ECONNRESET 등)
  - Heartbeat는 **단방향**이므로 ACK 기반 타임아웃은 없음
- 장애 감지 시 **다음 Registry로 전환**한다.
  1) 기존 Registry 연결 해제
  2) 다음 후보 Registry로 connect
  3) `REGISTER` 재전송 (service_name/endpoint/weight)
  4) 성공 시 Heartbeat 재개

#### 3.5.2.1 Failover 타이밍/백오프 정책

- 전환은 **즉시 시도**하되, 연속 실패 시 **지수 백오프**를 적용한다.
- 기본값(권장):
  - 초기 지연: 200ms
  - 최대 지연: 5s
  - 지터: ±20%
- 재시도 순서는 **라운드로빈**으로 Registry 목록을 순회한다.
- 성공 시 백오프를 초기화한다.

#### 3.5.3 Discovery 동작

- Discovery는 **여러 Registry PUB를 동시에 구독**한다.
- 한 Registry 장애 시에도 다른 Registry에서 최신 SERVICE_LIST 수신 가능.

#### 3.5.4 Gateway 영향

- 기존 Provider 연결은 유지될 수 있어 **메시징은 지속**된다.
- 다만 신규 등록/해제 이벤트는 **활성 Registry를 통해** 반영된다.

### 3.4 SERVICE_LIST 브로드캐스트 트리거

| 트리거 | 설명 |
|--------|------|
| **등록 시** | Provider REGISTER 성공 후 즉시 |
| **해제 시** | Provider UNREGISTER 또는 Heartbeat 타임아웃 |
| **주기적** | 30초 (기본값, 설정 가능) - 신규 Discovery용 |

- `list_seq`는 브로드캐스트마다 증가하며, Discovery는 최신 스냅샷만 적용한다.

---

## 4. Discovery 설계

### 4.1 역할

Discovery는 **"서비스가 어디에 있는지"**를 담당한다.

| 책임 | 설명 |
|------|------|
| Registry 연결 | 여러 Registry의 PUB 소켓 구독 |
| 서비스 구독 | 관심 서비스 등록 (로컬 필터) |
| 목록 관리 | 서비스별 Provider 목록 유지 |
| 변경 알림 | Gateway에 목록 변경 전달 |

### 4.2 Discovery 구조

```
┌─────────────────────────────────────┐
│            Discovery                 │
│  ┌───────────────────────────────┐  │
│  │  Registry Connections (SUB)   │  │
│  │  - tcp://registry1:5550       │  │
│  │  - tcp://registry2:5550       │  │
│  └───────────────────────────────┘  │
│                                     │
│  ┌───────────────────────────────┐  │
│  │  Service Directory            │  │
│  │  ┌─────────────────────────┐  │  │
│  │  │ payment-service         │  │  │
│  │  │  - provider1: tcp://... │  │  │
│  │  │  - provider2: tcp://... │  │  │
│  │  ├─────────────────────────┤  │  │
│  │  │ user-service            │  │  │
│  │  │  - provider1: tcp://... │  │  │
│  │  └─────────────────────────┘  │  │
│  └───────────────────────────────┘  │
│                                     │
│  ┌───────────────────────────────┐  │
│  │  Listeners (Gateway 알림용)   │  │
│  └───────────────────────────────┘  │
└─────────────────────────────────────┘
```

### 4.3 Discovery 이벤트

Discovery는 서비스 목록 변경 시 등록된 Gateway에 알린다.

| 이벤트 | 설명 |
|--------|------|
| `PROVIDER_ADDED` | 새 Provider 추가 |
| `PROVIDER_REMOVED` | Provider 제거 |
| `SERVICE_AVAILABLE` | 서비스 최초 가용 |
| `SERVICE_UNAVAILABLE` | 서비스 Provider 0개 |

### 4.4 구독 동작 (SUB 필터링)

- Registry의 PUB 메시지는 **모든 Discovery가 전체 구독**한다.
- `zmq_discovery_subscribe()` / `zmq_discovery_unsubscribe()`는
  **Discovery 내부 필터**로 동작하며, Gateway 알림/조회 대상만 제한한다.
- 서비스별 네트워크 필터링은 하지 않는다 (topic 프레임 미사용).

---

## 5. Gateway 설계

### 5.1 역할

Gateway는 **"메시지를 어떻게 보낼지"**를 담당한다.

| 책임 | 설명 |
|------|------|
| Provider 연결 | ROUTER 소켓으로 Provider 연결 관리 |
| 로드밸런싱 | 전략에 따라 Provider 선택 |
| 메시지 송수신 | 비즈니스 메시지 라우팅 |
| Request ID 매핑 | 요청-응답 매핑 |

### 5.2 Gateway 구조

```
┌─────────────────────────────────────────────┐
│                 Gateway                      │
│  ┌───────────────────────────────────────┐  │
│  │  Discovery Reference                  │  │
│  │  (서비스 목록 참조)                    │  │
│  └───────────────────────────────────────┘  │
│                                             │
│  ┌───────────────────────────────────────┐  │
│  │  Service Pools (ROUTER connections)   │  │
│  │  ┌─────────────────────────────────┐  │  │
│  │  │ payment-service (RR)             │  │  │
│  │  │  - ROUTER → providerA           │  │  │
│  │  │  - ROUTER → providerB           │  │  │
│  │  │  - ROUTER → providerC           │  │  │
│  │  │  - LB: Round Robin              │  │  │
│  │  ├─────────────────────────────────┤  │  │
│  │  │ user-service (Weighted)         │  │  │
│  │  │  - ROUTER → providerX (w=5)     │  │  │
│  │  │  - ROUTER → providerY (w=1)     │  │  │
│  │  │  - LB: Weighted                 │  │  │
│  │  └─────────────────────────────────┘  │  │
│  └───────────────────────────────────────┘  │
│                                             │
│  ┌───────────────────────────────────────┐  │
│  │  Pending Requests                     │  │
│  │  (request_id → request info)      │  │
│  └───────────────────────────────────────┘  │
└─────────────────────────────────────────────┘
```

- 서비스별로 **ROUTER 소켓 1개**를 유지하고, 모든 Provider endpoint에 connect한다.
- 전송 시 Gateway가 선택한 Provider의 `routing_id`를 사용한다.

### 5.3 자동 연결/해제

Gateway는 Discovery로부터 이벤트를 받아 자동으로 연결을 관리한다.

```
Discovery ──(PROVIDER_ADDED)──► Gateway
                                   │
                                   ▼
                     ┌─────────────────────────┐
                     │ ROUTER 소켓 생성        │
                     │ Provider에 connect      │
                     └─────────────────────────┘

Discovery ──(PROVIDER_REMOVED)──► Gateway
                                     │
                                     ▼
                     ┌─────────────────────────┐
                     │ ROUTER 소켓 disconnect  │
                     │ 풀에서 제거             │
                     └─────────────────────────┘
```

- **Gateway는 Discovery가 제공한 Provider endpoint만 연결**한다.
- Discovery 외 endpoint에 수동 연결하는 API는 제공하지 않으며,
  그런 연결의 동작/프로토콜 호환성은 보장하지 않는다.

### 5.4 로드 밸런싱 전략

| 전략 | 설명 |
|------|------|
| **Round Robin** | 순차적 선택 (기본값) |
| **Weighted** | 가중치 기반 선택 |

- Gateway는 선택된 Provider의 `routing_id`로 ROUTER 전송한다.
- **커스텀 로드밸런싱**이 필요하면:
  - Discovery에서 Provider 목록을 조회하고(또는 이벤트로 유지)
  - `zmq_gateway_threadsafe_router()`로 ROUTER 소켓을 얻어
  - 애플리케이션이 직접 선택한 routing_id로 **직접 전송**한다.

**가중치 규칙:**
- Weighted는 **weight가 클수록 선택 확률이 높아진다** (내림차순 우선).
- `weight=0`은 **기본값 1로 처리**한다.
- 커스텀 라우팅은 **ROUTER 소켓 직접 사용**을 권장한다.

### 5.5 요청-응답 매핑 (Request ID)

여러 Provider에 요청 시 응답을 올바른 요청과 매핑하기 위해
**request_id**를 사용한다.

```
┌──────────┐                              ┌──────────┐
│ Gateway  │                              │ Provider │
└────┬─────┘                              └────┬─────┘
     │  REQUEST (request_id=0x1234)       │
     │───────────────────────────────────────►│
     │                                        │
     │  REQUEST (request_id=0x1235)       │  (다른 Provider)
     │───────────────────────────────────────►│
     │                                        │
     │  RESPONSE (request_id=0x1235)      │
     │◄───────────────────────────────────────│
     │                                        │
     │  RESPONSE (request_id=0x1234)      │
     │◄───────────────────────────────────────│
```

- `request_id`: uint64_t (Gateway가 자동 생성)
- 요청 전송 시 pending_requests에 저장
- 응답 수신 시 request_id로 매핑
- Request/Reply API(03번 문서)의 request_id와 동일한 의미로 사용한다.

### 5.6 Gateway Request/Reply (그룹 없음)

- Gateway는 **클라이언트 측** 컴포넌트로, 요청/응답은 **request_id**로 매핑된다.
- 별도의 group 기능은 제공하지 않는다.
- 실사용에서는 다음 흐름이 **Gateway의 Request/Reply API**로 간주된다.
  - `zmq_gateway_send()` → `request_id` 획득
  - `zmq_gateway_recv()` → `request_id` 기반 매핑
- 타임아웃/재시도 정책은 애플리케이션 레벨에서 관리한다.
- 03번 문서의 `zmq_request_send/recv`와 **네이밍을 맞춘 별칭 API**를 제공할 수 있다.
  - 기능은 동일하며 **Gateway 전용 thin wrapper**다.
- callback 기반 요청 API도 제공한다(그룹 없음).

---

## 6. Provider 설계

### 6.1 Provider 구조

서버는 **ROUTER 소켓 + Discovery 등록**을 함께 처리한다.

```
┌─────────────────────────────────────────────┐
│                 Provider                     │
│  ┌─────────────────────────────────────┐    │
│  │  ROUTER Socket (비즈니스 메시지)     │    │
│  │  - bind("tcp://*:5555")             │    │
│  └─────────────────────────────────────┘    │
│                                             │
│  ┌─────────────────────────────────────┐    │
│  │  Registry Client                     │    │
│  │  - DEALER: Registry 등록/Heartbeat   │    │
│  └─────────────────────────────────────┘    │
└─────────────────────────────────────────────┘
```

> Note: Provider는 다른 Provider 목록이 필요 없으므로
> Registry의 PUB를 구독하지 않는다.

### 6.2 서비스 등록 흐름

```
1. Provider 생성
   └─► zmq_provider_new()

2. 비즈니스 소켓 bind
   └─► zmq_provider_bind("tcp://*:5555")
   └─► (선택) ZMQ_ROUTING_ID 설정으로 안정적 routing_id 사용
   └─► ROUTER 소켓 생성 및 bind

3. Registry 연결
   └─► zmq_provider_connect_registry()
   └─► DEALER connect to Registry

4. 서비스 등록
   └─► zmq_provider_register()
   └─► REGISTER 메시지 전송
   └─► REGISTER_ACK 수신 (성공/실패 확인)
   └─► Heartbeat 타이머 시작

5. Heartbeat 루프 (자동)
   └─► 5초마다 HEARTBEAT 전송

6. 종료 시
   └─► zmq_provider_unregister() 또는 zmq_provider_destroy()
   └─► UNREGISTER 메시지 전송
```

### 6.3 Endpoint 설정

Provider는 두 가지 주소를 다룬다:
- **bind_endpoint**: ROUTER 소켓을 bind할 주소 (`zmq_provider_bind`)
- **advertise_endpoint**: Registry에 등록할 주소 (`zmq_provider_register`)

| bind_endpoint | advertise_endpoint | 결과 |
|---------------|-------------------|------|
| `tcp://*:5555` | `NULL` | bind 후 로컬 IP 자동 감지 → `tcp://192.168.1.10:5555` |
| `tcp://*:5555` | `tcp://payment-server:5555` | DNS 이름으로 광고 (호스트명 사용) |
| `tcp://192.168.1.10:5555` | `NULL` | bind 주소 그대로 광고 |

자동 감지 시 (`advertise_endpoint = NULL`):
- bind_endpoint에서 포트 추출
- 로컬 IP 주소 자동 감지
- 여러 NIC가 있는 경우 기본 라우팅 인터페이스 사용

#### 6.3.1 NAT/Advertise 운영 가이드

외부에서 접근 가능한 주소로 **advertise_endpoint를 명시**해야 한다.

- **NAT/컨테이너 환경**: `tcp://*:PORT`를 advertise로 등록하면 **외부에서 접근 불가**하다.
  - 예: `bind=tcp://0.0.0.0:5555`, `advertise=tcp://203.0.113.10:5555`
- **로드밸런서/프록시 사용**: LB가 제공하는 **공인 endpoint**를 advertise로 등록한다.
- **멀티 NIC**: 자동 감지는 잘못된 NIC를 고를 수 있으므로 advertise를 명시한다.
- **주소 변경(재배포/재시작)**: Provider/Node는 **재등록**해야 한다.

### 6.4 Provider 식별 규칙

- Registry는 **`service_name + advertise_endpoint`**를 Provider의 기본 키로 사용한다.
- 동일 키로 재등록되면 기존 엔트리를 **갱신**한다 (routing_id/weight/heartbeat).
- routing_id를 안정적으로 유지하려면 Provider가 비즈니스 소켓에
  `ZMQ_ROUTING_ID`를 명시적으로 설정해야 한다.
- weight 변경은 `zmq_provider_update_weight()` 또는 동일 키 재등록으로 갱신한다.
  (갱신 시 SERVICE_LIST가 브로드캐스트됨)

### 6.5 SPOT Node 등록 규칙 (PUB endpoint)

SPOT Node는 Provider와 동일한 Registry 등록 절차를 사용하지만,
**등록되는 endpoint의 의미가 다르다**.

- `service_name` 기본값은 **`spot-node`**이며, 클러스터 분리를 위해
  `spot-node.<cluster-id>` 형식을 허용한다.
- `advertise_endpoint`는 **Node의 PUB bind endpoint**여야 한다.
  - Discovery를 통해 다른 Node가 이 endpoint에 **SUB connect**한다.
- weight는 사용하지 않으며 기본값(1)을 유지한다.
- SPOT Node는 Gateway를 사용하지 않는다.
  - Discovery 목록만 이용해 PUB/SUB mesh를 구성한다.
- Registry 장애 시 전환 정책은 **Provider와 동일**하게 적용한다.
  - 단일 활성 Registry + 장애 시 재등록 (3.5절 참고)

---

## 7. 메시지 프로토콜

### 7.1 Discovery 메시지 형식

모든 메시지는 **msgId + payload** 형식으로 전송한다.

```
┌──────────────────────────────────────────┐
│ Frame 0: msgId (uint16_t)                │
├──────────────────────────────────────────┤
│ Frame 1~N: Payload (가변)                │
└──────────────────────────────────────────┘
```

### 7.2 메시지 타입

| msgId | 이름 | 방향 | 설명 |
|-------|------|------|------|
| 0x0001 | REGISTER | Provider → Registry | 서비스 등록 |
| 0x0002 | REGISTER_ACK | Registry → Provider | 등록 응답 |
| 0x0003 | UNREGISTER | Provider → Registry | 서비스 해제 |
| 0x0004 | HEARTBEAT | Provider → Registry | 생존 신호 |
| 0x0005 | SERVICE_LIST | Registry → Discovery | 서비스 목록 |
| 0x0006 | REGISTRY_SYNC | Registry → Registry | 클러스터 동기화 |
| 0x0007 | UPDATE_WEIGHT | Provider → Registry | 가중치 갱신 |

- `REGISTRY_SYNC`는 `SERVICE_LIST`와 동일한 포맷을 사용한다.

### 7.3 REGISTER 메시지

```
Frame 0: msgId = 0x0001 (2B)
Frame 1: service_name (string)
Frame 2: endpoint (string, e.g., "tcp://192.168.1.10:5555")
         - 빈 문자열이면 자동 감지 요청
Frame 3: weight (uint32_t, optional, default=1)
```

### 7.4 REGISTER_ACK 메시지

```
Frame 0: msgId = 0x0002 (2B)
Frame 1: status (uint8_t)
         - 0x00: 성공
         - 0x01: 실패 - 중복 서비스명
         - 0x02: 실패 - 잘못된 endpoint
         - 0xFF: 실패 - 기타
Frame 2: resolved_endpoint (string, 자동 감지 시 실제 주소)
Frame 3: error_message (string, 실패 시)
```

### 7.4.1 UPDATE_WEIGHT 메시지

```
Frame 0: msgId = 0x0007 (2B)
Frame 1: service_name (string)
Frame 2: endpoint (string, 기존 등록된 advertise_endpoint)
Frame 3: weight (uint32_t)
```

- Registry는 `service_name + endpoint`로 기존 Provider 엔트리를 찾는다.
- 존재하지 않으면 실패 처리하며, 필요 시 REGISTER를 요구한다.
- 갱신 성공 시 `SERVICE_LIST` 브로드캐스트.

### 7.5 SERVICE_LIST 메시지

```
Frame 0: msgId = 0x0005 (2B)
Frame 1: registry_id (uint32_t)
Frame 2: list_seq (uint64_t)  // Registry가 증가시키는 스냅샷 번호
Frame 3: service_count (uint32_t)
Frame 4~N: 서비스 엔트리들
  - service_name (string)
  - provider_count (uint32_t)
  - provider entries:
    - endpoint (string)
    - routing_id (zmq_routing_id_t)
    - weight (uint32_t)
```

- routing_id 프레임 인코딩:
  - 프레임 길이 = `routing_id.size` (0~255)
  - payload = `routing_id.data[0..size-1]`
  - `size=0`은 허용하지 않음 (Provider 등록 시 routing_id 필수)

- **중복/역전 처리 규칙**:
  - Discovery는 `(registry_id, list_seq)` 기준으로 최신 스냅샷만 적용한다.
  - 동일 registry_id에서 list_seq가 이전이면 무시한다.

### 7.6 비즈니스 메시지 형식 (Gateway ↔ Provider)

Gateway와 Provider 간 비즈니스 메시지는 request_id를 포함한다.

```
┌──────────────────────────────────────────┐
│ Frame 0: routing_id (Gateway가 대상 지정) │
├──────────────────────────────────────────┤
│ Frame 1: request_id (uint64_t)       │
├──────────────────────────────────────────┤
│ Frame 2: msgId (uint16_t)                │
├──────────────────────────────────────────┤
│ Frame 3~N: Payload (가변)                │
└──────────────────────────────────────────┘
```

- **요청 시**: Gateway가 request_id 생성
- **응답 시**: Provider가 동일 request_id 반환
- Frame 0의 routing_id는 7.5와 동일한 인코딩 규칙을 따른다.
- Payload는 multipart를 허용하며, Gateway API는 `parts + part_count`로 전달한다.

---

## 8. C API 명세

### 8.1 Registry API

```c
/* Registry 생성 */
ZMQ_EXPORT void *zmq_registry_new(void *ctx);

/* Registry 설정 */
ZMQ_EXPORT int zmq_registry_set_endpoints(
    void *registry,
    const char *pub_endpoint,      // PUB 소켓 (브로드캐스트)
    const char *router_endpoint    // ROUTER 소켓 (등록/Heartbeat)
);

/* Registry ID 설정 (선택) */
ZMQ_EXPORT int zmq_registry_set_id(
    void *registry,
    uint32_t registry_id
);

/* 클러스터 피어 추가 */
ZMQ_EXPORT int zmq_registry_add_peer(
    void *registry,
    const char *peer_pub_endpoint  // 다른 Registry의 PUB endpoint
);

/* Heartbeat 설정 */
ZMQ_EXPORT int zmq_registry_set_heartbeat(
    void *registry,
    uint32_t interval_ms,          // Heartbeat 주기 (기본 5000ms)
    uint32_t timeout_ms            // 타임아웃 (기본 15000ms)
);

/* 브로드캐스트 주기 설정 */
ZMQ_EXPORT int zmq_registry_set_broadcast_interval(
    void *registry,
    uint32_t interval_ms           // 주기적 브로드캐스트 (기본 30000ms)
);

/* Registry 시작 */
ZMQ_EXPORT int zmq_registry_start(void *registry);

/* Registry 종료 */
ZMQ_EXPORT int zmq_registry_destroy(void **registry_p);
```

### 8.2 Discovery API (서비스 발견)

```c
/* Discovery 생성 */
ZMQ_EXPORT void *zmq_discovery_new(void *ctx);

/* Registry 연결 */
ZMQ_EXPORT int zmq_discovery_connect_registry(
    void *discovery,
    const char *registry_pub_endpoint  // Registry의 PUB endpoint
);

/* 서비스 구독 (로컬 필터 등록) */
ZMQ_EXPORT int zmq_discovery_subscribe(
    void *discovery,
    const char *service_name
);

/* 서비스 구독 해제 (로컬 필터 해제) */
ZMQ_EXPORT int zmq_discovery_unsubscribe(
    void *discovery,
    const char *service_name
);

/* Provider 목록 조회 */
ZMQ_EXPORT int zmq_discovery_get_providers(
    void *discovery,
    const char *service_name,
    zmq_provider_info_t *providers,
    size_t *count                      // in: 배열 크기, out: 실제 개수
);

/* Provider 수 조회 */
ZMQ_EXPORT int zmq_discovery_provider_count(
    void *discovery,
    const char *service_name
);

/* 서비스 가용 여부 */
ZMQ_EXPORT int zmq_discovery_service_available(
    void *discovery,
    const char *service_name           // 반환: 1=가용, 0=불가
);

/* Discovery 종료 */
ZMQ_EXPORT int zmq_discovery_destroy(void **discovery_p);
```

### 8.3 Gateway API (메시지 라우팅)

```c
/* 공통 규칙:
 * - parts는 연속된 zmq_msg_t 배열 (parts[0..part_count-1])
 * - part_count는 1 이상
 * - send 계열 성공 시 msg 소유권은 라이브러리로 이전됨
 * - recv 계열은 라이브러리가 parts 배열을 할당하며,
 *   사용 후 zmq_msgv_close(parts, part_count) 필요
 * - recv 실패 시 *parts=NULL, *part_count=0
 */
/* zmq_msgv_close는 03-request-reply-api.md의 공용 헬퍼를 사용 */
/* Gateway 생성 (Discovery 연결) */
ZMQ_EXPORT void *zmq_gateway_new(void *ctx, void *discovery);

/* 서비스로 메시지 전송 (로드밸런싱, multipart) */
ZMQ_EXPORT int zmq_gateway_send(
    void *gateway,
    const char *service_name,
    zmq_msg_t *parts,
    size_t part_count,
    int flags,
    uint64_t *request_id_out       // 생성된 request_id 반환 (NULL 가능)
);

/* 메시지 수신 (multipart) */
ZMQ_EXPORT int zmq_gateway_recv(
    void *gateway,
    zmq_msg_t **parts,
    size_t *part_count,
    int flags,
    char *service_name_out,            // 서비스명 반환 (256B 버퍼, NULL 가능)
    uint64_t *request_id_out       // request_id 반환 (NULL 가능)
);

/* Gateway 내부 ROUTER 소켓(thread-safe) 획득 */
ZMQ_EXPORT void *zmq_gateway_threadsafe_router(void *gateway);

/* ============================================================
 * Gateway Request/Reply Alias (03 naming, group 미지원)
 * ============================================================ */
typedef struct {
    char service_name[256];
    uint64_t request_id;
    int error;                     // 0: 성공, 그 외 errno
    zmq_msg_t **parts;
    size_t part_count;
} zmq_gateway_completion_t;

/*
 * Gateway 요청 콜백
 *
 * @param request_id   요청 ID
 * @param reply_parts  응답 메시지 배열 (필요 시 zmq_msgv_close)
 * @param reply_count  응답 프레임 개수
 * @param error        0: 성공 (그 외 errno)
 */
typedef void (*zmq_gateway_request_cb_fn)(
    uint64_t request_id,
    zmq_msg_t *reply_parts,
    size_t reply_count,
    int error
);

/*
 * zmq_gateway_request - 요청 전송 (콜백 기반, 내부 LB)
 *
 * @param gateway      gateway 핸들
 * @param service_name 서비스 이름
 * @param parts        요청 메시지 배열
 * @param part_count   요청 프레임 개수 (>= 1)
 * @param callback     응답 수신 콜백
 * @param timeout_ms   타임아웃 (밀리초)
 *                    - ZMQ_REQUEST_TIMEOUT_DEFAULT: 기본값 사용
 *                    - -1: 무제한
 * @return             요청 ID (>0), 실패 시 0
 *
 * 참고:
 *   - reply_parts는 콜백 반환 전까지 유효, 사용 후 zmq_msgv_close 필요
 *   - 에러 시 reply_parts=NULL, reply_count=0
 */
ZMQ_EXPORT uint64_t zmq_gateway_request(
    void *gateway,
    const char *service_name,
    zmq_msg_t *parts,
    size_t part_count,
    zmq_gateway_request_cb_fn callback,
    int timeout_ms
);

/*
 * zmq_gateway_request_send - 요청 전송 (내부 LB)
 * - 반환: request_id (>0), 실패 시 0
 */
ZMQ_EXPORT uint64_t zmq_gateway_request_send(
    void *gateway,
    const char *service_name,
    zmq_msg_t *parts,
    size_t part_count,
    int flags
);

/*
 * zmq_gateway_request_recv - 완료된 응답 수신
 * - 내부적으로 zmq_gateway_recv를 사용
 * - 성공 시 completion->parts를 zmq_msgv_close로 정리 필요
 * - zmq_gateway_threadsafe_router()으로 직접 송신한 요청은
 *   completion.service_name을 보장하지 않는다
 */
ZMQ_EXPORT int zmq_gateway_request_recv(
    void *gateway,
    zmq_gateway_completion_t *completion,
    int timeout_ms
);

/* 로드밸런싱 전략 설정 */
#define ZMQ_GATEWAY_LB_ROUND_ROBIN   0
#define ZMQ_GATEWAY_LB_WEIGHTED      1

ZMQ_EXPORT int zmq_gateway_set_lb_strategy(
    void *gateway,
    const char *service_name,
    int strategy
);

/* 지원 전략:
 * - ZMQ_GATEWAY_LB_ROUND_ROBIN
 * - ZMQ_GATEWAY_LB_WEIGHTED
 */

/* 연결된 Provider 수 조회 */
ZMQ_EXPORT int zmq_gateway_connection_count(
    void *gateway,
    const char *service_name
);

/* Gateway 종료 */
ZMQ_EXPORT int zmq_gateway_destroy(void **gateway_p);
```

### 8.4 Provider API (서버)

```c
/* Provider 생성 */
ZMQ_EXPORT void *zmq_provider_new(void *ctx);

/* 비즈니스 소켓 bind (Gateway 연결 수신용) */
ZMQ_EXPORT int zmq_provider_bind(
    void *provider,
    const char *bind_endpoint         // "tcp://*:5555" 또는 "tcp://0.0.0.0:5555"
);

/* Registry 연결 */
ZMQ_EXPORT int zmq_provider_connect_registry(
    void *provider,
    const char *registry_router_endpoint  // Registry의 ROUTER endpoint
);

/* 서비스 등록 (bind 후 호출, Heartbeat 자동 시작) */
ZMQ_EXPORT int zmq_provider_register(
    void *provider,
    const char *service_name,
    const char *advertise_endpoint,   // NULL이면 bind 주소에서 자동 감지
    uint32_t weight                   // 로드밸런싱 가중치 (0=기본값 1)
);

/* 가중치 갱신 (등록된 서비스 대상) */
ZMQ_EXPORT int zmq_provider_update_weight(
    void *provider,
    const char *service_name,
    uint32_t weight                   // 0=기본값 1
);

/* 서비스 해제 */
ZMQ_EXPORT int zmq_provider_unregister(
    void *provider,
    const char *service_name
);

/* 등록 결과 확인 (비동기 등록 후) */
ZMQ_EXPORT int zmq_provider_register_result(
    void *provider,
    const char *service_name,
    int *status,                      // 0=성공, 그 외=실패코드
    char *resolved_endpoint,          // 실제 등록된 endpoint (256B)
    char *error_message               // 에러 메시지 (256B)
);

/* 비즈니스 ROUTER 소켓(thread-safe) 획득 */
ZMQ_EXPORT void *zmq_provider_threadsafe_router(void *provider);

/* Provider 종료 */
ZMQ_EXPORT int zmq_provider_destroy(void **provider_p);
```

### 8.5 구조체 정의

```c
typedef struct {
    char service_name[256];
    char endpoint[256];
    zmq_routing_id_t routing_id;
    uint32_t weight;
    uint64_t registered_at;
} zmq_provider_info_t;
```

---

## 9. 사용 예시

### 9.1 Registry 실행 (Embedded)

```c
void *ctx = zmq_ctx_new();
void *registry = zmq_registry_new(ctx);

// 엔드포인트 설정
zmq_registry_set_endpoints(registry,
    "tcp://*:5550",    // PUB (브로드캐스트)
    "tcp://*:5551"     // ROUTER (등록)
);

// 클러스터 피어 추가 (선택)
zmq_registry_add_peer(registry, "tcp://registry2:5550");
zmq_registry_add_peer(registry, "tcp://registry3:5550");

// Heartbeat 설정 (선택)
zmq_registry_set_heartbeat(registry, 5000, 15000);

// 시작
zmq_registry_start(registry);

// ... 애플리케이션 로직 ...

// 종료
zmq_registry_destroy(&registry);
zmq_ctx_term(ctx);
```

### 9.2 Provider (서버)

```c
void *ctx = zmq_ctx_new();
void *provider = zmq_provider_new(ctx);

// 비즈니스 소켓 bind (Gateway 연결 수신용)
zmq_provider_bind(provider, "tcp://*:5555");

// Registry 연결
zmq_provider_connect_registry(provider, "tcp://registry1:5551");

// 서비스 등록 (advertise_endpoint 자동 감지)
zmq_provider_register(provider,
    "payment-service",
    NULL,    // bind 주소에서 IP 자동 감지 → "tcp://192.168.1.10:5555"
    1        // weight
);

// 등록 결과 확인
int status;
char resolved_endpoint[256];
char error_msg[256];
zmq_provider_register_result(provider, "payment-service",
    &status, resolved_endpoint, error_msg);

if (status != 0) {
    fprintf(stderr, "등록 실패: %s\n", error_msg);
    return -1;
}
printf("등록 완료: %s\n", resolved_endpoint);

// 비즈니스 소켓으로 메시지 처리
void *socket = zmq_provider_threadsafe_router(provider);

while (running) {
    zmq_msg_t routing_id_frame, request_id_frame, msgid_frame;
    zmq_msg_init(&routing_id_frame);
    zmq_msg_init(&request_id_frame);
    zmq_msg_init(&msgid_frame);

    // ROUTER 소켓: [routing_id][request_id][msgId][payload...]
    zmq_msg_recv(&routing_id_frame, socket, 0);
    zmq_msg_recv(&request_id_frame, socket, 0);
    zmq_msg_recv(&msgid_frame, socket, 0);

    uint64_t request_id = *(uint64_t*)zmq_msg_data(&request_id_frame);
    uint16_t msgId = *(uint16_t*)zmq_msg_data(&msgid_frame);

    // dispatch
    zmq_msg_t response;
    zmq_msg_init(&response);
    // payload는 multipart 가능: 필요한 만큼 누적 처리
    while (1) {
        zmq_msg_t part;
        zmq_msg_init(&part);
        zmq_msg_recv(&part, socket, 0);
        bool more = zmq_msg_more(&part);

        switch (msgId) {
            case MSG_PAYMENT_REQUEST:
                handle_payment_part(&part, &response);
                break;
            // ...
            default:
                /* unknown msgId: payload 폐기 */
                break;
        }

        zmq_msg_close(&part);
        if (!more)
            break;
    }

    // 응답 전송 (동일 request_id 사용)
    zmq_msg_send(&routing_id_frame, socket, ZMQ_SNDMORE);
    zmq_msg_send(&request_id_frame, socket, ZMQ_SNDMORE);
    zmq_msg_send(&response, socket, 0);
    zmq_msg_close(&response);

    zmq_msg_close(&routing_id_frame);
    zmq_msg_close(&request_id_frame);
    zmq_msg_close(&msgid_frame);
}

zmq_provider_destroy(&provider);
zmq_ctx_term(ctx);
```

### 9.3 Client (Discovery + Gateway)

```c
void *ctx = zmq_ctx_new();

// ===== Discovery 설정 (서비스 발견) =====
void *discovery = zmq_discovery_new(ctx);

// Registry 연결 (여러 개 가능)
zmq_discovery_connect_registry(discovery, "tcp://registry1:5550");
zmq_discovery_connect_registry(discovery, "tcp://registry2:5550");

// 서비스 구독
zmq_discovery_subscribe(discovery, "payment-service");
zmq_discovery_subscribe(discovery, "user-service");

// ===== Gateway 설정 (메시지 라우팅) =====
void *gateway = zmq_gateway_new(ctx, discovery);

// 로드밸런싱 전략 설정
zmq_gateway_set_lb_strategy(gateway, "payment-service", ZMQ_GATEWAY_LB_ROUND_ROBIN);
zmq_gateway_set_lb_strategy(gateway, "user-service", ZMQ_GATEWAY_LB_WEIGHTED);

// 서비스 가용 확인
while (!zmq_discovery_service_available(discovery, "payment-service")) {
    printf("payment-service 대기 중...\n");
    sleep(1);
}

// 메시지 전송 (자동 로드밸런싱)
zmq_msg_t request;
zmq_msg_init_data(&request, data, size, NULL, NULL);

uint64_t request_id;
zmq_gateway_send(gateway, "payment-service", &request, 1, 0, &request_id);
printf("요청 전송: request_id=%lu\n", request_id);

// 응답 수신
zmq_msg_t *reply_parts = NULL;
size_t reply_count = 0;

char service_name[256];
uint64_t recv_request_id;
zmq_gateway_recv(gateway, &reply_parts, &reply_count, 0, service_name, &recv_request_id);

printf("응답 수신: service=%s, request_id=%lu\n",
       service_name, recv_request_id);

// request_id로 요청-응답 매핑 확인
assert(request_id == recv_request_id);
zmq_msgv_close(reply_parts, reply_count);

// 종료
zmq_gateway_destroy(&gateway);
zmq_discovery_destroy(&discovery);
zmq_ctx_term(ctx);
```

### 9.4 Discovery만 사용 (서비스 목록 조회)

```c
// Discovery만 사용하여 서비스 목록 조회
void *discovery = zmq_discovery_new(ctx);
zmq_discovery_connect_registry(discovery, "tcp://registry:5550");
zmq_discovery_subscribe(discovery, "payment-service");

// Provider 목록 조회
zmq_provider_info_t providers[10];
size_t count = 10;
zmq_discovery_get_providers(discovery, "payment-service", providers, &count);

printf("payment-service providers (%zu):\n", count);
for (size_t i = 0; i < count; i++) {
    printf("  - %s (weight=%u)\n", providers[i].endpoint, providers[i].weight);
}

zmq_discovery_destroy(&discovery);
```

### 9.5 Custom LB (수동 선택 + ROUTER 직접 사용)

```c
// Discovery에서 provider 목록 조회 후 직접 선택
void *discovery = zmq_discovery_new(ctx);
zmq_discovery_connect_registry(discovery, "tcp://registry:5550");
zmq_discovery_subscribe(discovery, "payment-service");

void *gateway = zmq_gateway_new(ctx, discovery);

zmq_provider_info_t providers[16];
size_t count = 16;
zmq_discovery_get_providers(discovery, "payment-service", providers, &count);

// 예: 가장 낮은 weight(혹은 사용자 정의 기준) 선택
size_t idx = 0;
uint32_t best = UINT32_MAX;
for (size_t i = 0; i < count; ++i) {
    if (providers[i].weight < best) {
        best = providers[i].weight;
        idx = i;
    }
}

// Gateway ROUTER 소켓 직접 사용 (커스텀 라우팅)
void *router = zmq_gateway_threadsafe_router(gateway);

// 프레임: [routing_id][request_id][msgId][payload...]
uint64_t request_id = next_request_id(); // 앱에서 생성
uint16_t msgId = MSG_PAYMENT_REQUEST;

zmq_msg_t rid, reqid, msgid, payload;
zmq_msg_init_size(&rid, providers[idx].routing_id.size);
memcpy(zmq_msg_data(&rid), providers[idx].routing_id.data, providers[idx].routing_id.size);

zmq_msg_init_size(&reqid, sizeof(uint64_t));
memcpy(zmq_msg_data(&reqid), &request_id, sizeof(uint64_t));

zmq_msg_init_size(&msgid, sizeof(uint16_t));
memcpy(zmq_msg_data(&msgid), &msgId, sizeof(uint16_t));

zmq_msg_init_data(&payload, data, size, NULL, NULL);

zmq_msg_send(&rid, router, ZMQ_SNDMORE);
zmq_msg_send(&reqid, router, ZMQ_SNDMORE);
zmq_msg_send(&msgid, router, ZMQ_SNDMORE);
zmq_msg_send(&payload, router, 0);

// 응답은 router에서 직접 수신하거나 별도 처리 필요
// (Gateway recv와 혼용 시 service_name 매핑은 보장되지 않음)

zmq_gateway_destroy(&gateway);
zmq_discovery_destroy(&discovery);
```

### 9.6 두 서비스 동시 사용 예시 (payment/user)

```c
void *ctx = zmq_ctx_new();

// Discovery
void *discovery = zmq_discovery_new(ctx);
zmq_discovery_connect_registry(discovery, "tcp://registry1:5550");
zmq_discovery_subscribe(discovery, "payment-service");
zmq_discovery_subscribe(discovery, "user-service");

// Gateway
void *gateway = zmq_gateway_new(ctx, discovery);
zmq_gateway_set_lb_strategy(gateway, "payment-service", ZMQ_GATEWAY_LB_ROUND_ROBIN);
zmq_gateway_set_lb_strategy(gateway, "user-service", ZMQ_GATEWAY_LB_WEIGHTED);

// 두 서비스에 동시에 요청 전송
zmq_msg_t pay_req;
zmq_msg_init_data(&pay_req, pay_data, pay_size, NULL, NULL);
uint64_t pay_request_id = 0;
zmq_gateway_send(gateway, "payment-service", &pay_req, 1, 0, &pay_request_id);

zmq_msg_t user_req;
zmq_msg_init_data(&user_req, user_data, user_size, NULL, NULL);
uint64_t user_request_id = 0;
zmq_gateway_send(gateway, "user-service", &user_req, 1, 0, &user_request_id);

// 응답 수신 (순서 무관)
int pending = 2;
while (pending > 0) {
    zmq_msg_t *reply_parts = NULL;
    size_t reply_count = 0;
    char service_name[256];
    uint64_t request_id = 0;

    if (zmq_gateway_recv(gateway, &reply_parts, &reply_count, 0,
                         service_name, &request_id) != 0) {
        continue;
    }

    if (strcmp(service_name, "payment-service") == 0 &&
        request_id == pay_request_id) {
        handle_payment_reply(reply_parts, reply_count);
        pending--;
    } else if (strcmp(service_name, "user-service") == 0 &&
               request_id == user_request_id) {
        handle_user_reply(reply_parts, reply_count);
        pending--;
    }

    zmq_msgv_close(reply_parts, reply_count);
}

zmq_gateway_destroy(&gateway);
zmq_discovery_destroy(&discovery);
zmq_ctx_term(ctx);
```

### 9.7 Gateway Request/Reply (콜백) 예시

```c
void *ctx = zmq_ctx_new();
void *discovery = zmq_discovery_new(ctx);
zmq_discovery_connect_registry(discovery, "tcp://registry1:5550");
zmq_discovery_subscribe(discovery, "payment-service");

void *gateway = zmq_gateway_new(ctx, discovery);

// 요청 전송 (콜백 기반)
zmq_msg_t req;
zmq_msg_init_data(&req, data, size, NULL, NULL);

uint64_t req_id = zmq_gateway_request(
    gateway,
    "payment-service",
    &req,
    1,
    on_reply_cb,
    ZMQ_REQUEST_TIMEOUT_DEFAULT
);

zmq_gateway_destroy(&gateway);
zmq_discovery_destroy(&discovery);
zmq_ctx_term(ctx);
```

```c
// 콜백 예시
void on_reply_cb(uint64_t request_id,
                 zmq_msg_t *reply_parts,
                 size_t reply_count,
                 int error) {
    if (error != 0)
        return;
    handle_reply(reply_parts, reply_count);
    zmq_msgv_close(reply_parts, reply_count);
}
```

### 9.8 SPOT Node (PUB/SUB Mesh) 예시

```c
void *ctx = zmq_ctx_new();

// SPOT Node 생성
void *node = zmq_spot_node_new(ctx);
void *discovery = zmq_discovery_new(ctx);

// Registry 연결 (Discovery + 등록)
zmq_discovery_connect_registry(discovery, "tcp://registry1:5550");
zmq_discovery_subscribe(discovery, "spot-node");

zmq_spot_node_bind(node, "tcp://*:9000");
zmq_spot_node_connect_registry(node, "tcp://registry1:5551");
zmq_spot_node_register(node, "spot-node", NULL);   // advertise는 PUB endpoint
zmq_spot_node_set_discovery(node, discovery, "spot-node");

// SPOT 인스턴스 사용
void *spot = zmq_spot_new(node);
zmq_spot_subscribe(spot, "metrics:zone1:*");
```

---

## 10. 구현 계획

### 10.1 수정 파일

| 파일 | 변경 내용 |
|------|----------|
| `include/zmq.h` | Discovery, Gateway, Provider API 선언 |
| `src/api/zmq.cpp` | API 구현 |
| `src/discovery/registry.hpp/cpp` | Registry 구현 (신규) |
| `src/discovery/discovery.hpp/cpp` | Discovery 구현 (신규) |
| `src/discovery/gateway.hpp/cpp` | Gateway 구현 (신규) |
| `src/discovery/provider.hpp/cpp` | Provider 구현 (신규) |
| `src/discovery/protocol.hpp` | 메시지 프로토콜 정의 (신규) |
| `src/discovery/request_id.hpp` | Request ID 관리 (신규) |

### 10.2 단계별 구현

```
Phase 1: 기본 구조
├─ Registry 단일 노드 구현
├─ Provider 등록/해제 + ACK
├─ Discovery 구독/목록관리
├─ Gateway 기본 송수신
└─ Request ID 기반 요청-응답

Phase 2: 고가용성
├─ Registry 클러스터 동기화
├─ 다중 Registry 연결
├─ Discovery → Gateway 이벤트 전달
└─ Failover 처리

Phase 3: 고급 기능
├─ 로드밸런싱 전략
├─ Endpoint 자동 감지
├─ 메트릭스 수집
└─ 모니터링 통합 (01 스펙 연동)
```

---

## 11. 검증 방법

### 11.1 단위 테스트

| 테스트 | 설명 |
|--------|------|
| `test_registry_basic` | Registry 생성/시작/종료 |
| `test_provider_bind` | Provider bind 및 소켓 획득 |
| `test_provider_register` | Provider 등록/해제 |
| `test_provider_update_weight` | Provider 가중치 갱신 |
| `test_update_weight_broadcast` | UPDATE_WEIGHT 반영 후 SERVICE_LIST 브로드캐스트 |
| `test_update_weight_invalid_target` | 잘못된 서비스/endpoint UPDATE_WEIGHT 실패 |
| `test_provider_register_ack` | REGISTER_ACK 응답 확인 |
| `test_discovery_subscribe` | Discovery 서비스 구독 |
| `test_discovery_get_providers` | Provider 목록 조회 |
| `test_gateway_send_recv` | Gateway 메시지 송수신 |
| `test_gateway_request_callback` | Gateway 콜백 기반 요청/응답 |
| `test_gateway_request_send_recv` | Gateway request_send/recv alias 동작 |
| `test_gateway_request_timeout` | Gateway 요청 타임아웃 처리 |
| `test_gateway_threadsafe_router` | gateway_threadsafe_router 직접 라우팅 |
| `test_gateway_router_mixing_guard` | thread-safe router 직접 송신/recv 혼용 규칙 |
| `test_gateway_auto_connect` | Gateway 자동 연결 |
| `test_gateway_auto_disconnect` | Provider 제거 시 자동 해제 |
| `test_heartbeat_timeout` | Heartbeat 타임아웃 처리 |
| `test_request_id` | 요청-응답 request_id 매핑 |
| `test_load_balancing` | 로드밸런싱 전략 동작 |
| `test_weighted_lb_update` | weight 갱신 후 Weighted 선택 반영 |
| `test_registry_cluster` | 클러스터 동기화 |
| `test_registry_failover` | Registry 장애 시 Failover |
| `test_endpoint_auto_detect` | Endpoint 자동 감지 |
| `test_service_list_seq` | list_seq 역전/중복 처리 |

**추가 성공 기준(요약)**:
- `test_update_weight_invalid_target`:
  - 존재하지 않는 service_name+endpoint에 대해 `UPDATE_WEIGHT`를 전송하면 실패 처리
  - Registry 상태 변경 없음 + SERVICE_LIST 브로드캐스트 없음
- `test_gateway_router_mixing_guard`:
  - `zmq_gateway_threadsafe_router()`로 직접 송신한 요청의 응답은
    `service_name` 매핑이 **보장되지 않음**
  - 응답 자체는 수신되며, `request_id` 기준 매핑만 검증

### 11.2 통합 테스트

```
시나리오 1: 기본 흐름
1. Registry 시작
2. Provider 3개 등록
3. REGISTER_ACK 성공 확인
4. Discovery가 서비스 구독
5. Discovery에서 Provider 목록 수신 확인
6. Gateway 생성 및 Discovery 연결
7. Gateway에서 자동 연결 확인
8. 메시지 라운드로빈 분배 확인
9. request_id로 응답 매핑 확인

시나리오 2: 장애 복구
1. Provider 1개 비정상 종료
2. Heartbeat 타임아웃 후 목록에서 제거 확인
3. Discovery가 Gateway에 이벤트 전달
4. Gateway 자동 disconnect 확인
5. 남은 Provider로 메시지 라우팅 확인

시나리오 3: 클러스터
1. Registry 3개 시작
2. Provider가 Registry1에 등록
3. Registry2, Registry3에도 전파 확인
4. Discovery가 Registry2 구독해도 동일 목록 수신

시나리오 4: 다중 요청-응답
1. Gateway가 3개 Provider에 동시 요청
2. 각기 다른 request_id 할당 확인
3. 응답 도착 순서 무관하게 올바른 매핑 확인

시나리오 5: Discovery/Gateway 분리
1. Discovery만 생성하여 서비스 목록 조회
2. 동일 Discovery로 여러 Gateway 생성
3. 각 Gateway 독립적 로드밸런싱 확인

시나리오 6: 가중치 갱신
1. Provider가 weight=1로 등록
2. `zmq_provider_update_weight()` 호출 (예: weight=5)
3. Registry가 UPDATE_WEIGHT 처리 후 SERVICE_LIST 브로드캐스트
4. Discovery가 갱신된 weight 수신 확인
5. Gateway Weighted 선택 비율 변화 확인

시나리오 7: Gateway 콜백 요청
1. Gateway에서 `zmq_gateway_request()` 호출
2. Provider가 응답 전송
3. 콜백이 호출되고 request_id 매핑 확인

시나리오 8: 커스텀 라우팅 (ROUTER 직접 사용)
1. Discovery에서 Provider 목록 조회
2. `zmq_gateway_threadsafe_router()`로 ROUTER 소켓 획득
3. 특정 Provider routing_id로 직접 전송
4. 응답 수신 및 매핑 처리 확인

시나리오 9: Registry 장애 (Provider Failover)
1. Registry 3개 시작 + 상호 peer 등록
2. Provider가 Registry1에 등록/Heartbeat 시작
3. Registry1 강제 종료
4. Provider가 Registry2로 전환 후 재등록
5. Registry2/3에서 SERVICE_LIST 갱신 확인
6. Discovery가 Registry2/3에서 최신 목록 수신 확인

시나리오 10: Discovery 다중 구독 지속성
1. Discovery가 Registry1/2/3 PUB 모두 구독
2. Registry1 장애 발생
3. Discovery가 Registry2/3에서 지속적으로 목록 수신 확인
4. Registry1 복구 후 목록 중복/역전 처리 확인(list_seq)

시나리오 11: Provider 재등록 중복 방지
1. Provider가 Registry1에 등록
2. 장애로 Registry2에 재등록
3. Registry 클러스터 동기화 이후
   동일 service_name+endpoint가 **단일 엔트리**로 유지되는지 확인
```

---

## 변경 이력

| 버전 | 날짜 | 변경 내용 |
|------|------|----------|
| 0.1 | 2026-01-26 | 초안 작성 |
| 0.2 | 2026-01-26 | 리뷰 반영: 의존성 추가, Provider SUB 제거, REGISTER_ACK 추가, request_id 추가, Gossip→flooding 용어 수정, SERVICE_LIST 트리거 명시, Endpoint 자동 감지 추가 |
| 0.3 | 2026-01-26 | Discovery/Gateway 분리: Discovery(서비스 발견)와 Gateway(메시지 라우팅) 역할 분리, API 재설계 |
| 0.4 | 2026-01-26 | Provider bind 명확화: zmq_provider_bind() API 추가, bind_endpoint/advertise_endpoint 구분, 예제 코드 수정 |
| 0.5 | 2026-01-26 | SERVICE_LIST routing_id 프레임 인코딩 규칙 추가 |
| 0.6 | 2026-01-26 | Gateway ROUTER 전환, request_id 용어 통일, SERVICE_LIST seq 추가, multipart 반영 |
| 0.7 | 2026-01-26 | End-to-End 동작 시퀀스 추가, 목표 보강 |
| 0.8 | 2026-01-26 | 상태 머신/호출 순서 표 추가 |
| 0.9 | 2026-01-26 | LB 전략 단순화(ROUND_ROBIN/WEIGHTED) + 커스텀 선택 가이드 추가 |
| 1.0 | 2026-01-26 | LB_MANUAL 추가 + 수동 선택 예제 추가 |
| 1.1 | 2026-01-26 | 구성 요소별 소켓 역할 표 추가 |
| 1.2 | 2026-01-26 | Gateway/Provider ROUTER 역할 양방향 명시 |
| 1.3 | 2026-01-26 | 두 서비스 동시 사용 예시 추가 |
| 1.4 | 2026-01-26 | Gateway 서비스 풀 다이어그램을 두 서비스 기준으로 보강 |
| 1.5 | 2026-01-26 | Gateway Request/Reply alias API 추가 |
| 1.6 | 2026-01-26 | Gateway는 Discovery 외 endpoint 연결 미지원 명시 |
| 1.7 | 2026-01-26 | send_to 제거, gateway_socket 기반 커스텀 라우팅 추가 |
| 1.8 | 2026-01-26 | Gateway callback 기반 request API 추가 |
| 1.9 | 2026-01-26 | gateway_socket → gateway_threadsafe_router 네이밍 명시화 |
| 2.0 | 2026-01-26 | LB_MANUAL 제거, provider_update_weight 추가 |
| 2.1 | 2026-01-26 | UPDATE_WEIGHT 메시지 정의, 가중치 규칙 명시 |
| 2.2 | 2026-01-26 | provider_socket → provider_threadsafe_router 네이밍 명시화 |
| 2.3 | 2026-01-26 | Gateway/UPDATE_WEIGHT 관련 테스트 항목 보강 |
| 2.4 | 2026-01-26 | 통합 테스트 시나리오(가중치/콜백/커스텀 라우팅) 추가 |
| 2.5 | 2026-01-26 | 구성요소 thread-safety 정책 명시 |
| 2.6 | 2026-01-26 | gateway/provider router API를 thread-safe 명시로 변경 |
| 2.7 | 2026-01-26 | gateway/provider thread-safe socket 네이밍 통일 |
| 2.8 | 2026-01-26 | gateway/provider thread-safe router 네이밍으로 재정리 |
| 2.9 | 2026-01-26 | Registry HA 전략 섹션 및 HA 시나리오 추가 |
| 3.0 | 2026-01-26 | Provider failover 백오프/지터 정책 추가 |
| 3.1 | 2026-01-26 | UPDATE_WEIGHT 실패/라우터 혼용 테스트 추가 |
| 3.2 | 2026-01-26 | 테스트 성공 기준 명시 |
| 3.3 | 2026-01-28 | SPOT Node 등록 규칙(PUB endpoint) 및 호출 순서 추가 |
| 3.4 | 2026-01-28 | SPOT Node 예시 + NAT/advertise 운영 가이드 추가 |
