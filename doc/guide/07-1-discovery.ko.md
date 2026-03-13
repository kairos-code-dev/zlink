[English](07-1-discovery.md) | [한국어](07-1-discovery.ko.md)

# Service Discovery 기반 인프라

## 1. 개요

zlink Service Discovery는 마이크로서비스 환경에서 서비스 인스턴스를 동적으로 발견하고 연결하는 인프라를 제공한다. Registry 클러스터 기반의 서비스 등록/발견 시스템이다.

### 핵심 개념

| 용어 | 설명 |
|------|------|
| **Registry** | 서비스 등록/해제 관리, 목록 브로드캐스트 (PUB+ROUTER) |
| **Discovery** | Registry 구독, 서비스 목록 관리 (SUB) |
| **Gateway (서버)** | 서버 측 Gateway, Discovery를 통해 Registry에 등록 |
| **Heartbeat** | 서비스 생존 확인 (5초 주기, 15초 타임아웃) |

### 아키텍처

```
┌──────────────────────────────────────────┐
│            Registry Cluster               │
│  Registry1(PUB+ROUTER) ◄──► Registry2    │
│       │              ◄──► Registry3      │
│       │ (서비스 목록 브로드캐스트)         │
└───────┼──────────────────────────────────┘
        │
   ┌────┴────┐
   │Discovery│
   │ (SUB)   │
   │    │    │
   │    ▼    │
   │ Gateway │  (클라이언트 또는 bind 서버)
   │(ROUTER) │
   └─────────┘
```

## 2. Registry 설정 및 실행

```c
void *ctx = zlink_ctx_new();
void *registry = zlink_registry_new(ctx);

/* bind + start */
zlink_registry_bind(registry,
    "tcp://*:5550",    /* PUB (브로드캐스트) */
    "tcp://*:5551"     /* ROUTER (등록/Heartbeat) */
);

/* 클러스터 피어 추가 (선택) */
zlink_registry_add_peer(registry, "tcp://registry2:5550");
zlink_registry_add_peer(registry, "tcp://registry3:5550");

/* Heartbeat 설정 (선택) */
zlink_registry_set_heartbeat(registry, 5000, 15000);

/* 브로드캐스트 주기 (선택, 기본 30초) */
zlink_registry_set_broadcast_interval(registry, 30000);

/* ... 애플리케이션 로직 ... */

/* 종료 */
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

## 3. Discovery 사용

```c
/* service_type: ZLINK_SERVICE_TYPE_GATEWAY 또는 ZLINK_SERVICE_TYPE_SPOT */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);

/* Registry bootstrap/control 엔드포인트 연결 (여러 개 가능) */
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");
zlink_discovery_connect_registry(discovery, "tcp://registry2:5551");

/* 모니터로 서비스 상태 관찰 */
void *mon = zlink_discovery_monitor_open(
    discovery,
    ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP
      | ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED,
    on_discovery_event);

/* ... Discovery가 콜백을 통해 이벤트 전달 ... */

/* 정리 */
zlink_service_monitor_close(&mon);
zlink_discovery_destroy(&discovery);
```

## 4. Liveness 및 Summary 업데이트

```
Gateway/SpotNode            Discovery               Registry
   │  REGISTER / summary        │                      │
   │──────────────────────────► │                      │
   │                            │ bootstrap + uplink   │
   │                            │─────────────────────►│
   │                            │  heartbeat / summary │
   │                            │─────────────────────►│
   │                            │                      │
   │                            │ (summary timeout)    │
   │                            │         X            │ ← entry 만료 / LOST
```

- Registry visibility는 Discovery가 소유하는 heartbeat/topology uplink로
  유지됩니다.
- Gateway와 Spot 서비스는 로컬 registration/summary 변경을 제출하지만,
  주기적 uplink cadence는 Discovery가 담당합니다.
- Registry summary는 eventually consistent한 coarse/global view이며,
  strict final readiness gate로 사용하면 안 됩니다.

## 5. Registry 클러스터 HA

- 3노드 클러스터 권장
- flooding 방식 동기화 (각 Registry가 다른 Registry의 PUB 구독)
- Eventually Consistent: 모든 Registry가 동일 상태 수렴
- `registry_id` + `list_seq`로 중복/역전 업데이트 무시

### Discovery Failover

- Discovery는 하나 이상의 Registry control endpoint에 bootstrap 연결합니다.
- bootstrap metadata로 내부 broadcast/uplink 경로를 학습합니다.
- 한 Registry 노드가 실패해도 다른 bootstrap control endpoint를 통해
  계속 동작할 수 있습니다.

## 6. 다음 단계

- [Gateway 서비스](07-2-gateway.ko.md) — Discovery 기반 위치투명 요청/응답
- [SPOT PUB/SUB](07-3-spot.ko.md) — Discovery 기반 위치투명 발행/구독

---
[← 서비스 개요](07-0-services.ko.md) | [Gateway →](07-2-gateway.ko.md)
