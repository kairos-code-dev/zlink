[English](07-0-services.md) | [한국어](07-0-services.ko.md)

# 서비스 계층 개요

## 1. 서비스 계층이란

zlink의 서비스 계층은 7종 소켓(PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER, STREAM)
위에 구축된 **고수준 분산 서비스 기능**이다.
소켓 수준의 연결/라우팅을 직접 다루지 않고도
서비스 등록, 발견, 위치투명 통신을 수행할 수 있다.

## 2. 아키텍처

```
┌─────────────────────────────────────────────────────────┐
│                    Application                           │
│              SPOT (발행/구독)  ·  소켓 패밀리              │
├─────────────────────────────────────────────────────────┤
│  Public API Facade  (service_api · service_*_api)        │
│  validate + delegate → service-local access seam         │
├─────────────────────────────────────────────────────────┤
│  Service Access Layer                                    │
│  discovery_access · registry_access                      │
│  spot_node_access · spot_subject_access                  │
│  service_public_api_guard (admission/close guard)        │
├─────────────────────────────────────────────────────────┤
│  Service Runtime                                         │
│  Discovery: bootstrap·state·update·uplink·registry_client│
│  SPOT: node·data_plane(forwarding·protocol)·pub·sub      │
├─────────────────────────────────────────────────────────┤
│  Discovery (서비스 발견) · Registry (서비스 등록소)        │
│  subscribe · heartbeat · broadcast SERVICE_LIST           │
├─────────────────────────────────────────────────────────┤
│              zlink Core (8종 소켓 + 6종 Transport)        │
└─────────────────────────────────────────────────────────┘
```

- **Public API Facade**는 C API 진입점으로, handle validation 후 service-local access seam으로 위임한다. concrete service 세부를 직접 알지 않는다.
- **Service Access Layer**는 각 서비스가 제공하는 service-local seam이다. `*_access.hpp`가 API 계층과 service runtime 사이의 계약을 정의한다.
- **Service Runtime**은 각 서비스의 내부 구현이다. SPOT은 node/data_plane(forwarding/protocol)/pub/sub으로 모듈화되어 있다.
- **Registry**는 서비스 엔트리를 관리하고, 주기적으로 SERVICE_LIST를 브로드캐스트한다.
- **Discovery**는 Registry를 구독하여 서비스 목록을 로컬 캐시로 유지한다.
- **SPOT**은 Discovery를 통해 대상을 자동 발견하고 연결한다.

## 서비스 명칭

| 서비스 | 명칭 의미 | 한줄 설명 |
|--------|-----------|-----------|
| **Registry** | 서비스 등록소 | 서비스 엔트리를 등록·관리하는 중앙 저장소 |
| **Discovery** | 서비스 발견 | Registry를 구독하여 서비스 목록을 로컬 캐시로 유지 |
| **SPOT** | 위치 투명 pub/sub | 위치투명 토픽 기반 발행/구독 메시 |

## 3. 서비스 구성 요소

### 3.1 Service Discovery — 기반 인프라

Registry 클러스터 기반의 서비스 등록/발견 시스템. 서비스가 Registry에 등록하면 Discovery가 이를 구독하여 서비스 목록을 관리한다.

- Registry 클러스터 HA (flooding 동기화)
- Heartbeat 기반 생존 확인
- Client-side 서비스 목록 캐싱
- 내부 모듈:
  - `discovery_access` (API seam)
  - `discovery_bootstrap` · `discovery_state`
  - `discovery_update` · `discovery_uplink`
  - `discovery_registry_client`

자세한 내용은 [Service Discovery 가이드](07-1-discovery.ko.md) 및
[Registry 가이드](07-4-registry.ko.md)를 참고.

### 3.2 SPOT — 위치투명 토픽 PUB/SUB

Discovery 기반으로 PUB/SUB Mesh를 자동 구성하여 클러스터 전체에서 토픽 메시지를 발행/구독한다.

- 토픽 기반 발행/구독
- 패턴(와일드카드) 구독
- Discovery 기반 자동 Mesh 구성
- **Thread-safe** — 하나의 `spot` / `spot_node` handle에서 operational API를 여러 스레드가 동시 호출 가능
- 내부 모듈:
  - `spot_node_access` · `spot_subject_access` (API seam)
  - `spot_handle` · `spot_node`
  - `spot_data_plane` (forwarding · protocol)
  - `spot_pub` · `spot_sub` (option · recv)

자세한 내용은 [SPOT 가이드](07-3-spot.ko.md)를 참고.

### 3.3 소켓 패밀리 — Discovery 관리 raw 소켓

raw ROUTER/DEALER/PUB/SUB 소켓을 Discovery 인스턴스(서비스 타입
`ZLINK_SERVICE_TYPE_SOCKET`)에 연결하여 자동 피어 발견과 lifecycle
관리를 할 수 있다. SPOT 추상화 없이 소켓 수준에서 위치투명 통신을
제공한다.

- Discovery를 통한 자동 엔드포인트 등록 및 heartbeat
- 역할 기반 피어 매칭 (PUB↔SUB, ROUTER↔DEALER)
- Lifecycle 위임 — Discovery가 연결된 소켓을 소유
- 내부 모듈: `socket_discovery_attachment` (소켓 측 통합) · `discovery_owned_service` (등록 편의 API)

자세한 내용은 [Service Discovery 가이드](07-1-discovery.ko.md)를 참고.

### 3.4 Registry — 중앙 서비스 등록소

서비스 엔트리를 등록·관리하는 중앙 저장소. SPOT 노드/소켓 패밀리의 등록, 하트비트, 토폴로지 브로드캐스트를 담당한다.

- 내부 모듈: `registry_access` (API seam) · `registry_query_access` (원격 조회 seam)

자세한 내용은 [Registry 가이드](07-4-registry.ko.md)를 참고.

## 4. Service Access Layer 패턴

모든 서비스는 공통된 access layer 패턴을 따른다.

```
C API (zlink_spot_publish 등)
    → service_api.cpp (validate + delegate)
    → *_access.hpp (service-local seam)
    → service runtime (concrete 구현)
```

| 서비스 | Access Seam | 역할 |
|--------|-------------|------|
| Discovery | `discovery_access_t` | lifecycle, connect_registry, option, monitor |
| Registry | `registry_access_t` | lifecycle, bind, config, snapshot/query |
| Registry Query | `registry_query_access_t` | 원격 topology query |
| SPOT Node | `spot_node_access_t` | lifecycle, bind, peer connect, discovery attach |
| SPOT Subject | `spot_subject_access_t` | publish, subscribe, option, handler, monitor |

각 access seam은 `service_public_api_guard_t`와 통합되어 callback 모드 추적과
lifecycle gate(destroy 시 `EBUSY`/`ESHUTDOWN` 계약)를 제공한다.

이 구조 덕분에 API 계층은 concrete service 구현을 직접 알지 않고,
service 추가 시 `api/service_*_api.cpp`, 해당 `*_access` 파일,
해당 service 구현 파일만 수정하면 된다.

## 5. 서비스 간 관계

```
                    ┌──────────┐
                    │ Registry │
                    │ (PUB+    │
                    │  ROUTER) │
                    └────┬─────┘
                         │ SERVICE_LIST 브로드캐스트
                ┌────────┴────────┐
                │                 │
                v                 v
          ┌──────────┐     ┌──────────┐
          │Discovery │     │Discovery │
          │(SPOT 용) │     │(Socket용)│
          └────┬─────┘     └────┬─────┘
               │                │
               v                v
          ┌──────────┐     ┌──────────┐
          │   SPOT   │     │  Socket  │
          │(PUB+SUB) │     │(R/D/P/S) │
          └──────────┘     └──────────┘
```

- **Discovery가 기반 인프라**: SPOT, 소켓 패밀리 모두 Discovery를 통해 대상을 발견한다.
- **SPOT**은 PUB/SUB 패턴으로 토픽 메시지를 전파한다.
- **소켓 패밀리**는 raw ROUTER/DEALER/PUB/SUB 소켓이 Discovery를 통해 피어를 등록·발견하여 소켓 수준의 위치투명 통신을 제공한다.
- 모든 서비스는 독립적으로 동작하며, 동일한 Registry 클러스터를 공유할 수 있다.

---
[← 모니터링](06-monitoring.ko.md) | [Discovery →](07-1-discovery.ko.md)
