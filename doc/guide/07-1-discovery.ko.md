[English](07-1-discovery.md) | [한국어](07-1-discovery.ko.md)

# Service Discovery 기반 인프라

## 1. 개요

zlink Service Discovery는 마이크로서비스 환경에서
서비스 인스턴스를 동적으로 발견하고 연결하는 인프라를 제공한다.
Registry 클러스터 기반의 서비스 등록/발견 시스템이다.

### 핵심 개념

| 용어 | 설명 |
|------|------|
| **Registry** | 서비스 등록/해제 관리, 목록 브로드캐스트 (PUB+ROUTER) |
| **Discovery** | Registry 구독, 서비스 목록 관리 (SUB); 연결된 서비스의 lifecycle owner |
| **Gateway (서버)** | 서버 측 Gateway, Discovery를 통해 Registry에 등록 |
| **소켓 패밀리** | Discovery를 통해 피어를 등록·발견하는 raw ROUTER/DEALER/PUB/SUB 소켓 |
| **서비스 역할** | 소켓 패밀리 모드에서 피어 매칭에 사용되는 소켓 수준 역할 (ROUTER/DEALER/PUB/SUB) |
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
   ┌────┴─────────────────────────────┐
   │           Discovery (SUB)         │
   │  ┌─────────┬──────────┬────────┐ │
   │  │ Gateway │ SPOT     │ Socket │ │
   │  │(ROUTER) │(PUB+SUB) │Family  │ │
   │  │         │          │(R/D/P/S│ │
   │  └─────────┴──────────┴────────┘ │
   └───────────────────────────────────┘
```

## 2. Registry 설정 및 실행

```c
void *ctx = zlink_ctx_new();
void *registry = zlink_registry_new(ctx);

/* 클러스터 피어 추가 (선택, bind 전에 호출) */
zlink_registry_add_peer(registry, "tcp://registry2:5550");
zlink_registry_add_peer(registry, "tcp://registry3:5550");

/* Heartbeat 설정 (선택, bind 전에 호출) */
zlink_registry_set_heartbeat(registry, 5000, 15000);

/* 브로드캐스트 주기 (선택, 기본 30초) */
zlink_registry_set_broadcast_interval(registry, 30000);

/* bind + start
   첫 번째 인자: PUB endpoint — 서비스 목록 브로드캐스트 (Discovery SUB가 구독)
   두 번째 인자: ROUTER endpoint — 등록/하트비트/쿼리 수신 (Discovery가 bootstrap 연결) */
zlink_registry_bind(registry,
    "tcp://*:5550",    /* PUB (서비스 목록 브로드캐스트) */
    "tcp://*:5551"     /* ROUTER (등록/하트비트/쿼리) */
);

/* ... 애플리케이션 로직 ... */

/* 종료 */
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

## 3. Discovery 사용

```c
/* service_type: ZLINK_SERVICE_TYPE_GATEWAY, ZLINK_SERVICE_TYPE_SPOT,
   또는 ZLINK_SERVICE_TYPE_SOCKET */
void *discovery = zlink_discovery_new(ctx,
    ZLINK_SERVICE_TYPE_GATEWAY, "order-service");

/* Registry bootstrap/control 엔드포인트 연결 (여러 개 가능) */
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");
zlink_discovery_connect_registry(discovery, "tcp://registry2:5551");

/* 모니터로 서비스 상태 관찰 */
zlink_service_monitor_open_options_t opts = {
    .events = ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP
            | ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED,
};
void *mon = zlink_service_monitor_open(discovery, &opts);
zlink_service_monitor_handler(mon, on_discovery_event, NULL);

/* ... Discovery가 콜백을 통해 이벤트 전달 ... */

/* 정리 */
zlink_monitor_close(&mon);
zlink_discovery_destroy(&discovery);
```

## 3.1 소켓 패밀리 Discovery

raw ROUTER/DEALER/PUB/SUB 소켓은 Discovery를 사용하여 자동 피어 발견과
lifecycle 관리를 할 수 있다. Gateway나 SPOT 추상화 없이 소켓 수준에서
위치투명 통신을 가능하게 한다.

```c
/* SOCKET 타입으로 Discovery 생성 */
void *discovery = zlink_discovery_new(ctx,
    ZLINK_SERVICE_TYPE_SOCKET, "price-feed");
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

/* PUB 소켓을 생성하고 Discovery에 연결 */
void *pub = zlink_socket_new(ctx, ZLINK_PUB);
zlink_bind(pub, "tcp://*:9100");
zlink_socket_attach_discovery(pub, discovery);
/* Discovery가 PUB 엔드포인트를 등록하고 heartbeat를 관리한다.
   같은 서비스("price-feed")의 원격 SUB 소켓이 이 엔드포인트를
   자동으로 발견하고 연결한다. */

/* ... 메시지 발행 ... */

/* Discovery를 파괴하여 연결된 소켓 종료 */
zlink_discovery_destroy(&discovery);
```

**역할 매칭:** Discovery는 서비스 역할로 관련 원격 프로바이더를 결정한다.
PUB 소켓은 SUB 피어를 발견하고 그 반대도 마찬가지다. ROUTER와 DEALER는
서로를 발견한다. 이는 자동으로 동작한다 — 역할은 attach 시 소켓 타입에서
파생된다.

**Lifecycle:** 소켓이 연결되면 `connect`, `disconnect`, `unbind`, `close`
수동 호출이 실패한다. Discovery 인스턴스를 파괴하면 모든 연결된 소켓이
종료된다.

## 4. Liveness 및 Summary 업데이트

```
Gateway/SpotNode/Socket     Discovery               Registry
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
- Gateway, Spot, 그리고 소켓 패밀리 서비스는 로컬 registration/summary
  변경을 제출하지만, 주기적 uplink cadence는 Discovery가 담당합니다.
- Registry summary는 eventually consistent한 coarse/global view이며,
  strict final readiness gate로 사용하면 안 됩니다.

## 5. Registry 클러스터 HA

- 3노드 클러스터 권장
- flooding 방식 동기화 — 각 Registry가 수신한 서비스 목록 변경을 나머지
  모든 Registry에게 재전파하여, 최종적으로 전체 노드가 동일 정보를 갖게 되는
  브로드캐스트 전파 기법 (각 Registry가 다른 Registry의 PUB 구독)
- Eventually Consistent: 모든 Registry가 동일 상태 수렴
- `registry_id` + `list_seq`로 중복/역전 업데이트 무시

**서비스 가시성:** Registry 클러스터에서 서비스 목록은 flooding으로 전파된다.
Discovery가 하나의 Registry에만 연결해도 피어 Registry에 등록된 서비스가
브로드캐스트에 포함되어 전체 클러스터의 서비스를 볼 수 있다. 여러 Registry에
`connect_registry()`하는 것은 서비스 가시성이 아닌 **HA(장애 대응)**를 위한
것이다.

### Discovery Failover

- Discovery는 하나 이상의 Registry control endpoint에 bootstrap 연결합니다.
- bootstrap metadata로 내부 broadcast/uplink 경로를 학습합니다.
- 한 Registry 노드가 실패해도 다른 bootstrap control endpoint를 통해
  계속 동작할 수 있습니다.

## 6. 다음 단계

- [Gateway 서비스](07-2-gateway.ko.md) — Discovery 기반 위치투명 요청/응답
- [SPOT PUB/SUB](07-3-spot.ko.md) — Discovery 기반 위치투명 발행/구독
- [Registry 가이드](07-4-registry.ko.md) — 클러스터 구성, 토폴로지 조회, 운영 패턴

---
[← 서비스 개요](07-0-services.ko.md) | [Gateway →](07-2-gateway.ko.md)
