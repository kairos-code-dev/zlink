# Service Discovery 기반 인프라

## 1. 개요

zlink Service Discovery는 마이크로서비스 환경에서 서비스 인스턴스를 동적으로 발견하고 연결하는 인프라를 제공한다. Registry 클러스터 기반의 서비스 등록/발견 시스템이다.

### 핵심 개념

| 용어 | 설명 |
|------|------|
| **Registry** | 서비스 등록/해제 관리, 목록 브로드캐스트 (PUB+ROUTER) |
| **Discovery** | Registry 구독, 서비스 목록 관리 (SUB) |
| **Receiver** | 서비스 수신자, Registry에 등록 (DEALER+ROUTER) |
| **Heartbeat** | Receiver 생존 확인 (5초 주기, 15초 타임아웃) |

### 아키텍처

```
┌──────────────────────────────────────────┐
│            Registry Cluster               │
│  Registry1(PUB+ROUTER) ◄──► Registry2    │
│       │              ◄──► Registry3      │
│       │ (서비스 목록 브로드캐스트)         │
└───────┼──────────────────────────────────┘
        │
   ┌────┴────┐      ┌──────────┐
   │Discovery│      │ Receiver │
   │ (SUB)   │      │(DEALER+  │
   │    │    │      │ ROUTER)  │
   │    ▼    │      └──────────┘
   │ Gateway │
   │(ROUTER) │
   └─────────┘
```

## 2. Registry 설정 및 실행

```c
void *ctx = zlink_ctx_new();
void *registry = zlink_registry_new(ctx);

/* 엔드포인트 설정 */
zlink_registry_set_endpoints(registry,
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

/* 시작 */
zlink_registry_start(registry);

/* ... 애플리케이션 로직 ... */

/* 종료 */
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

## 3. Discovery 사용

```c
/* service_type: ZLINK_SERVICE_TYPE_GATEWAY 또는 ZLINK_SERVICE_TYPE_SPOT */
void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_GATEWAY);

/* Registry 연결 (여러 개 가능) */
zlink_discovery_connect_registry(discovery, "tcp://registry1:5550");
zlink_discovery_connect_registry(discovery, "tcp://registry2:5550");

/* 서비스 구독 */
zlink_discovery_subscribe(discovery, "payment-service");

/* 서비스 가용 확인 */
while (!zlink_discovery_service_available(discovery, "payment-service")) {
    printf("대기 중...\n");
    sleep(1);
}

/* Receiver 목록 조회 */
zlink_receiver_info_t receivers[10];
size_t count = 10;
zlink_discovery_get_receivers(discovery, "payment-service",
                              receivers, &count);
for (size_t i = 0; i < count; i++) {
    printf("Receiver: %s (weight=%u)\n",
           receivers[i].endpoint, receivers[i].weight);
}

/* Receiver 수 조회 */
int n = zlink_discovery_receiver_count(discovery, "payment-service");

zlink_discovery_destroy(&discovery);
```

## 4. Heartbeat 메커니즘

```
Receiver                    Registry
   │  REGISTER                 │
   │──────────────────────────►│
   │  REGISTER_ACK             │
   │◄──────────────────────────│
   │                           │
   │  HEARTBEAT (5초 주기)     │
   │──────────────────────────►│
   │  HEARTBEAT (5초 주기)     │
   │──────────────────────────►│
   │                           │
   │  (15초 미수신)            │
   │         X                 │ ← Receiver 제거 + 브로드캐스트
```

- 주기: 5초 (기본값, 설정 가능)
- 타임아웃: 15초 (3회 미수신 시 제거)
- 제거 시 모든 Discovery에 SERVICE_LIST 브로드캐스트

## 5. Registry 클러스터 HA

- 3노드 클러스터 권장
- flooding 방식 동기화 (각 Registry가 다른 Registry의 PUB 구독)
- Eventually Consistent: 모든 Registry가 동일 상태 수렴
- `registry_id` + `list_seq`로 중복/역전 업데이트 무시

### Receiver Failover

- Receiver는 **한 Registry에만** REGISTER/HEARTBEAT 전송
- 장애 감지 시 다음 Registry로 전환 후 재등록
- 지수 백오프: 200ms → 최대 5s (±20% 지터)
- Discovery는 여러 Registry PUB를 동시 구독하여 한 노드 장애에도 목록 수신 가능

## 6. 다음 단계

- [Gateway 서비스](07-2-gateway.md) — Discovery 기반 위치투명 요청/응답
- [SPOT PUB/SUB](07-3-spot.md) — Discovery 기반 위치투명 발행/구독
