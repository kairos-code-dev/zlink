[English](07-4-registry.md) | [한국어](07-4-registry.ko.md)

# Registry (중앙 서비스 디렉토리)

## 1. 개요

Registry는 zlink 서비스 계층의 중앙 서비스 디렉토리이자 토폴로지 요약 소스다.
Gateway와 SPOT 노드의 서비스 등록(Discovery를 통해)을 수락하고, 하트비트
기반 생존 확인을 관리하며, 집계된 서비스 목록을 모든 연결된 Discovery
인스턴스에 주기적으로 브로드캐스트한다.

### 두 가지 사용 모드

| 모드 | 설명 |
|------|------|
| **독립 프로세스** | Registry를 전용 서비스로 실행. 여러 애플리케이션이 Discovery를 통해 연결. |
| **임베디드** | 애플리케이션 프로세스 내에 Registry를 Discovery, Gateway/SPOT과 함께 직접 생성. |

**Registry는 thread-safe하다.** 하나의 Registry handle을 여러 스레드에서
동시에 사용할 수 있다. 구성 API(`set_id`, `add_peer`,
`set_heartbeat`, `set_broadcast_interval`, `setsockopt`)는 `bind` 전에
호출해야 한다. 토폴로지 조회 API(`topology_snapshot`, `topology_query`,
`gateway_peers_snapshot`, `gateway_peers_query`)는 bind 이후 어떤
스레드에서든 호출할 수 있다.

## 2. Quick Start

Registry를 실행하고 Discovery를 통해 Gateway를 연결하는 최소 예제.
Gateway는 메시지 수신에 callback과 recv 두 모델을 지원한다
(상세 내용은 [Gateway 가이드](07-2-gateway.ko.md)의 I/O 모델 참조).

```c
void *ctx = zlink_ctx_new();

/* === Registry === */
void *registry = zlink_registry_new(ctx);
/* PUB: 서비스 목록 브로드캐스트, ROUTER: 등록/하트비트/쿼리 */
zlink_registry_bind(registry, "tcp://*:5550", "tcp://*:5551");

/* === Discovery === */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://127.0.0.1:5551");

/* === Gateway (서버, callback 모드) === */
void *server = zlink_gateway_new(ctx, "my-service");
zlink_gateway_set_routing_id(server, "server-1", 8);
zlink_recv_handler(server, on_request, NULL);   /* callback 모드 */
zlink_gateway_attach_discovery(server, discovery);
zlink_gateway_bind(server, "tcp://*:5555");

/* ... 애플리케이션 로직 ... */

/* 정리 */
zlink_gateway_destroy(&server);
zlink_discovery_destroy(&discovery);
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

## 3. Registry 구성

모든 구성 API는 `zlink_registry_bind()` **전에** 호출해야 한다.

### 3.1 하트비트

```c
/* interval_ms: 서비스가 하트비트를 보내는 주기 (기본 5000ms)
   timeout_ms:  무응답 서비스 만료 시간         (기본 15000ms) */
zlink_registry_set_heartbeat(registry, 5000, 15000);
```

### 3.2 브로드캐스트 주기

```c
/* PUB에 전체 SERVICE_LIST를 발행하는 주기 (기본 30000ms) */
zlink_registry_set_broadcast_interval(registry, 30000);
```

### 3.3 소켓 옵션

Registry의 내부 소켓에 저수준 소켓 옵션을 적용한다:

```c
/* 예: PUB 소켓에 TLS 설정 */
zlink_registry_setsockopt(registry,
    ZLINK_REGISTRY_SOCKET_PUB,        /* 대상 소켓 */
    ZLINK_TLS_CA_CERT,                /* 옵션 */
    ca_pem, strlen(ca_pem));          /* 값 */
```

| 소켓 역할 | 상수 | 용도 |
|-----------|------|------|
| PUB | `ZLINK_REGISTRY_SOCKET_PUB` | 서비스 목록 브로드캐스트 |
| ROUTER | `ZLINK_REGISTRY_SOCKET_ROUTER` | 등록/하트비트 수신 |
| PEER_SUB | `ZLINK_REGISTRY_SOCKET_PEER_SUB` | 피어 Registry 브로드캐스트 구독 |

### 3.4 클러스터 ID

```c
/* 클러스터 동기화를 위한 고유 ID 할당 (노드마다 고유해야 함) */
zlink_registry_set_id(registry, 1);
```

### 3.5 TLS 설정

TLS는 해당 내부 소켓의 소켓 옵션을 통해 구성한다:

```c
/* PUB에 TLS 설정 (Discovery로의 브로드캐스트) */
zlink_registry_setsockopt(registry, ZLINK_REGISTRY_SOCKET_PUB,
    ZLINK_TLS_SERVER_CERT, cert_pem, strlen(cert_pem));
zlink_registry_setsockopt(registry, ZLINK_REGISTRY_SOCKET_PUB,
    ZLINK_TLS_SERVER_KEY, key_pem, strlen(key_pem));

/* ROUTER에 TLS 설정 (등록/하트비트) */
zlink_registry_setsockopt(registry, ZLINK_REGISTRY_SOCKET_ROUTER,
    ZLINK_TLS_SERVER_CERT, cert_pem, strlen(cert_pem));
zlink_registry_setsockopt(registry, ZLINK_REGISTRY_SOCKET_ROUTER,
    ZLINK_TLS_SERVER_KEY, key_pem, strlen(key_pem));
```

## 4. 배포 패턴

### 4.1 독립 프로세스 배포

Registry를 전용 서비스로 실행한다. 여러 애플리케이션이 각자의 Discovery
인스턴스를 통해 연결한다.

```
┌─────────────────────────────────────────┐
│         Registry 프로세스                │
│  Registry (PUB:5550 + ROUTER:5551)      │
└──────────────┬──────────────────────────┘
               │ SERVICE_LIST 브로드캐스트
       ┌───────┼───────┐
       │       │       │
       v       v       v
   ┌──────┐ ┌──────┐ ┌──────┐
   │App A │ │App B │ │App C │
   │Disc. │ │Disc. │ │Disc. │
   │ GW   │ │ GW   │ │ SPOT │
   └──────┘ └──────┘ └──────┘
```

프로덕션 배포에 권장하는 패턴:

- Registry 수명 주기가 애플리케이션 재시작과 독립적
- 여러 서비스가 단일 Registry(또는 클러스터)를 공유
- 인프라와 애플리케이션의 명확한 관심사 분리

### 4.2 임베디드 배포

Registry, Discovery, Gateway/SPOT이 모두 단일 프로세스에 존재한다.
개발, 테스트, 또는 단일 노드 배포에 유용하다.

```c
void *ctx = zlink_ctx_new();

/* Registry (임베디드) */
void *registry = zlink_registry_new(ctx);
/* PUB: 서비스 목록 브로드캐스트, ROUTER: 등록/하트비트/쿼리 */
zlink_registry_bind(registry, "tcp://*:5550", "tcp://*:5551");

/* Discovery (같은 프로세스) */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://127.0.0.1:5551");

/* 서버 Gateway (callback 모드) */
void *server = zlink_gateway_new(ctx, "echo-service");
zlink_gateway_set_routing_id(server, "echo-server-1", 13);
zlink_recv_handler(server, on_request, NULL);   /* callback 모드 */
zlink_gateway_attach_discovery(server, discovery);
zlink_gateway_bind(server, "tcp://*:5555");

/* 클라이언트 Gateway (같은 프로세스, recv 모드) */
void *client_disc = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(client_disc, "tcp://127.0.0.1:5551");

void *client = zlink_gateway_new(ctx, "echo-service");
zlink_gateway_set_routing_id(client, "client-1", 8);
/* recv 모드 -- zlink_recv_handler() 호출 없음 */
zlink_gateway_attach_discovery(client, client_disc);

/* 요청 전송 */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_gateway_send(client, &part, 1, 0);

/* 응답 수신 (recv 모드) */
zlink_routing_id_t source_rid;
zlink_msg_t *reply_parts = NULL;
size_t reply_count = 0;
zlink_gateway_recv(client, &source_rid, &reply_parts, &reply_count, 0);

/* 정리 (역순) */
zlink_gateway_destroy(&client);
zlink_discovery_destroy(&client_disc);
zlink_gateway_destroy(&server);
zlink_discovery_destroy(&discovery);
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

> **팁**: 모든 컴포넌트가 같은 프로세스에 있을 때 `inproc://` transport를
> 사용하면 Registry와 Discovery 간 zero-copy 통신이 가능하다.

## 5. 클러스터 구성 및 데이터 동기화

### 5.1 클러스터 구성

각 Registry 노드에 고유 ID와 피어의 PUB 엔드포인트가 필요하다:

```c
/* Node 1 */
void *reg1 = zlink_registry_new(ctx);
zlink_registry_set_id(reg1, 1);
zlink_registry_add_peer(reg1, "tcp://registry2:5550");
zlink_registry_add_peer(reg1, "tcp://registry3:5550");
/* PUB: 서비스 목록 브로드캐스트, ROUTER: 등록/하트비트/쿼리 */
zlink_registry_bind(reg1, "tcp://*:5550", "tcp://*:5551");
```

### 5.2 동기화 메커니즘

Registry는 PUB/SUB 기반 flooding 동기화를 사용한다:

```
┌────────────┐     PUB/SUB      ┌────────────┐
│ Registry 1 │◄────────────────►│ Registry 2 │
│ (id=1)     │                  │ (id=2)     │
│ PUB:5550   │                  │ PUB:5550   │
└────────────┘                  └────────────┘
      ▲                               ▲
      │           PUB/SUB             │
      └───────────────────────────────┘
                     │
              ┌────────────┐
              │ Registry 3 │
              │ (id=3)     │
              │ PUB:5550   │
              └────────────┘
```

- 각 Registry가 다른 모든 Registry의 PUB 엔드포인트를 구독
- 서비스 목록 변경이 flooding을 통해 즉시 전파
- **Eventually Consistent**: 모든 Registry가 동일한 상태로 수렴
- `registry_id` + `list_seq`를 통해 중복/역전 업데이트를 안전하게 무시

**Discovery 관점:** 서비스 목록이 flooding으로 전파되므로, Discovery는 클러스터의
**하나의** Registry에만 연결해도 전체 서비스를 발견할 수 있다. 여러 Registry에
연결하는 것은 장애 시 failover를 위한 것이다.

### 5.3 3노드 클러스터 전체 예제

```c
void *ctx = zlink_ctx_new();

/* === Node 1 === */
void *reg1 = zlink_registry_new(ctx);
zlink_registry_set_id(reg1, 1);
zlink_registry_add_peer(reg1, "tcp://registry2:5550");
zlink_registry_add_peer(reg1, "tcp://registry3:5550");
zlink_registry_set_heartbeat(reg1, 5000, 15000);
/* PUB: 서비스 목록 브로드캐스트, ROUTER: 등록/하트비트/쿼리 */
zlink_registry_bind(reg1, "tcp://*:5550", "tcp://*:5551");

/* === Node 2 === */
void *reg2 = zlink_registry_new(ctx);
zlink_registry_set_id(reg2, 2);
zlink_registry_add_peer(reg2, "tcp://registry1:5550");
zlink_registry_add_peer(reg2, "tcp://registry3:5550");
zlink_registry_set_heartbeat(reg2, 5000, 15000);
zlink_registry_bind(reg2, "tcp://*:5550", "tcp://*:5551");

/* === Node 3 === */
void *reg3 = zlink_registry_new(ctx);
zlink_registry_set_id(reg3, 3);
zlink_registry_add_peer(reg3, "tcp://registry1:5550");
zlink_registry_add_peer(reg3, "tcp://registry2:5550");
zlink_registry_set_heartbeat(reg3, 5000, 15000);
zlink_registry_bind(reg3, "tcp://*:5550", "tcp://*:5551");

/* Discovery가 여러 Registry에 연결 (HA — 서비스 가시성은 하나만으로도 충분) */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");
zlink_discovery_connect_registry(discovery, "tcp://registry2:5551");
zlink_discovery_connect_registry(discovery, "tcp://registry3:5551");

/* ... */

/* 정리 */
zlink_discovery_destroy(&discovery);
zlink_registry_destroy(&reg3);
zlink_registry_destroy(&reg2);
zlink_registry_destroy(&reg1);
zlink_ctx_term(ctx);
```

## 6. 토폴로지 조회 (Topology Introspection)

Registry는 글로벌 서비스 토폴로지를 조회하는 API를 제공한다. **로컬**(같은
프로세스)과 **원격**(다른 프로세스, 쿼리 클라이언트 사용) 두 가지 접근
방식이 있다.

### 6.1 로컬 조회 (같은 프로세스)

#### 전체 스냅샷

```c
/* 필요한 개수 먼저 조회 */
size_t count = 0;
zlink_registry_topology_snapshot(registry, NULL, &count);

/* 할당 후 조회 */
zlink_registry_topology_entry_t *entries = malloc(
    count * sizeof(zlink_registry_topology_entry_t));
zlink_registry_topology_snapshot(registry, entries, &count);

for (size_t i = 0; i < count; i++) {
    printf("service=%s endpoint=%s state=%d\n",
           entries[i].service_name,
           entries[i].endpoint,
           entries[i].state);
}
free(entries);
```

#### 필터 기반 조회

```c
/* "payment-service"의 READY 상태 Gateway 인스턴스만 조회 */
zlink_registry_topology_filter_t filter;
memset(&filter, 0, sizeof(filter));
filter.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
strncpy(filter.service_name, "payment-service",
        sizeof(filter.service_name) - 1);
filter.state = ZLINK_TOPOLOGY_STATE_READY;

size_t count = 64;
zlink_registry_topology_entry_t entries[64];
zlink_registry_topology_query(registry, &filter, entries, &count);

printf("READY 인스턴스: %zu\n", count);
for (size_t i = 0; i < count; i++) {
    printf("  %s (ready_count=%u)\n",
           entries[i].endpoint, entries[i].ready_count);
}
```

#### 토폴로지 엔트리 필드

| 필드 | 설명 |
|------|------|
| `routing_id` | 서비스 인스턴스의 라우팅 ID |
| `service_kind` | `GATEWAY`, `SPOT_PUB`, `SPOT_SUB`, 또는 `DISCOVERY` |
| `service_name` | 논리적 서비스 이름 |
| `endpoint` | 광고된 엔드포인트 |
| `source` | 엔트리 추가 방식 (`MANUAL`, `DISCOVERY`, `REGISTRY`) |
| `state` | `DISCOVERED`, `CONNECTING`, `READY`, `LOST`, `ERROR`, `STOPPED` |
| `desired_count` | 기대 피어 인스턴스 수 |
| `ready_count` | 현재 ready 상태 인스턴스 수 |
| `error_code` | `ERROR` 상태일 때 오류 코드 |
| `last_reported_ms` | 마지막 하트비트/업데이트 타임스탬프 (epoch ms) |

#### 필터 필드

필드를 0이 아닌 값으로 설정하면 해당 기준으로 필터링한다. 0 값 필드는
와일드카드(전체 매칭)로 처리된다.

| 필드 | 설명 |
|------|------|
| `service_kind` | 서비스 종류로 필터링 |
| `service_name` | 서비스 이름으로 필터링 |
| `routing_id` | 라우팅 ID로 필터링 |
| `state` | 토폴로지 상태로 필터링 |
| `source` | 토폴로지 소스로 필터링 |

### 6.2 원격 조회 (다른 프로세스)

쿼리 클라이언트를 사용하여 별도 프로세스의 Registry를 조회한다.
운영 도구나 CLI 유틸리티에서 사용하는 패턴이다.

```c
void *ctx = zlink_ctx_new();

/* 쿼리 클라이언트 생성 및 Registry ROUTER 엔드포인트 연결 */
void *client = zlink_registry_query_client_new(ctx);
zlink_registry_query_client_connect(client, "tcp://registry1:5551");

/* 필터 없는 스냅샷 (NULL 필터로 전체 조회) */
size_t count = 0;
zlink_registry_query_snapshot(client, NULL, NULL, &count);

zlink_registry_topology_entry_t *entries = malloc(
    count * sizeof(zlink_registry_topology_entry_t));
zlink_registry_query_snapshot(client, NULL, entries, &count);

/* 토폴로지 덤프 출력 */
for (size_t i = 0; i < count; i++) {
    printf("[%s] %s @ %s  state=%d  ready=%u/%u\n",
           entries[i].service_kind == ZLINK_SERVICE_KIND_GATEWAY
               ? "GW" : "SPOT",
           entries[i].service_name,
           entries[i].endpoint,
           entries[i].state,
           entries[i].ready_count,
           entries[i].desired_count);
}
free(entries);

/* 필터 기반 원격 조회 */
zlink_registry_topology_filter_t filter;
memset(&filter, 0, sizeof(filter));
filter.state = ZLINK_TOPOLOGY_STATE_LOST;

size_t lost_count = 64;
zlink_registry_topology_entry_t lost[64];
zlink_registry_query_snapshot(client, &filter, lost, &lost_count);
printf("LOST 엔트리: %zu\n", lost_count);

/* 정리 */
zlink_registry_query_destroy(&client);
zlink_ctx_term(ctx);
```

### 6.3 Gateway Peer 조회

서비스 수준의 토폴로지 외에도, Registry는 Gateway 서비스의 피어별 연결
상태를 추적한다. 운영 시 로드밸런싱 상태를 확인하는 데 유용하다.

#### 로컬 Gateway Peer 조회

```c
/* 전체 gateway peer 연결 스냅샷 */
size_t count = 0;
zlink_registry_gateway_peers_snapshot(registry, NULL, &count);

zlink_registry_gateway_peer_entry_t *peers = malloc(
    count * sizeof(zlink_registry_gateway_peer_entry_t));
zlink_registry_gateway_peers_snapshot(registry, peers, &count);

for (size_t i = 0; i < count; i++) {
    printf("gateway=%s peer=%s state=%d weight=%u\n",
           peers[i].gateway_endpoint,
           peers[i].peer_endpoint,
           peers[i].state,
           peers[i].weight);
}
free(peers);

/* 필터: 특정 서비스의 피어만 조회 */
zlink_registry_gateway_peer_filter_t peer_filter;
memset(&peer_filter, 0, sizeof(peer_filter));
strncpy(peer_filter.service_name, "payment-service",
        sizeof(peer_filter.service_name) - 1);

size_t filtered_count = 64;
zlink_registry_gateway_peer_entry_t filtered[64];
zlink_registry_gateway_peers_query(registry, &peer_filter,
                                   filtered, &filtered_count);
```

#### Gateway Peer 엔트리 필드

| 필드 | 설명 |
|------|------|
| `gateway_routing_id` | Gateway 인스턴스의 라우팅 ID |
| `gateway_endpoint` | Gateway 엔드포인트 |
| `service_name` | 서비스 이름 |
| `peer_routing_id` | 연결된 피어의 라우팅 ID |
| `peer_endpoint` | 피어 엔드포인트 |
| `state` | 현재 상태 (`ZLINK_TOPOLOGY_STATE_*`) |
| `weight` | 가중 로드밸런싱을 위한 피어 가중치 |
| `connected_since_ms` | 피어 연결 시점 타임스탬프 (epoch ms) |
| `last_reported_ms` | 마지막 하트비트/업데이트 타임스탬프 (epoch ms) |

#### 원격 Gateway Peer 조회

```c
/* 쿼리 클라이언트를 통해 원격 Registry의 gateway peer 조회 */
void *client = zlink_registry_query_client_new(ctx);
zlink_registry_query_client_connect(client, "tcp://registry1:5551");

size_t count = 128;
zlink_registry_gateway_peer_entry_t peers[128];
zlink_registry_query_gateway_peers_snapshot(client, NULL,
                                            peers, &count);

for (size_t i = 0; i < count; i++) {
    printf("[%s] %s → %s  weight=%u state=%d\n",
           peers[i].service_name,
           peers[i].gateway_endpoint,
           peers[i].peer_endpoint,
           peers[i].weight,
           peers[i].state);
}

zlink_registry_query_destroy(&client);
```

## 7. 운영 패턴

### 7.1 서비스 등록/해제 흐름

```
Gateway/SPOT          Discovery              Registry
    │                     │                      │
    │ attach_discovery    │                      │
    │ + bind              │                      │
    │────────────────────►│                      │
    │                     │ bootstrap + REGISTER │
    │                     │─────────────────────►│
    │                     │                      │ (서비스 목록에 추가)
    │                     │      REGISTER_ACK    │
    │                     │◄─────────────────────│
    │                     │                      │
    │                     │   HEARTBEAT (5초)    │
    │                     │─────────────────────►│
    │                     │                      │
    │                     │   HEARTBEAT (5초)    │
    │                     │─────────────────────►│
    │                     │                      │
    │ destroy             │                      │
    │────────────────────►│                      │
    │                     │     UNREGISTER       │
    │                     │─────────────────────►│
    │                     │                      │ (목록에서 제거)
```

### 7.2 하트비트 타임아웃 및 자동 제거

서비스가 `timeout_ms`(기본 15초) 이내에 하트비트를 보내지 않으면, Registry가
자동으로 서비스 목록에서 제거한다. 제거는 다음 SERVICE_LIST 발행 시 모든
Discovery 인스턴스에 브로드캐스트된다.

### 7.3 Discovery Failover

- Discovery는 하나 이상의 Registry ROUTER 엔드포인트에 bootstrap 연결
- bootstrap 메타데이터로 내부 broadcast/uplink 경로를 학습
- 한 Registry 노드가 실패해도 다른 bootstrap 엔드포인트를 통해 계속 동작
- Discovery의 failover 로직을 통해 서비스가 자동으로 재등록

### 7.4 클러스터 내 Registry 노드 장애

- 생존 Registry 노드가 독립적으로 계속 동작
- 각 노드는 자체 서비스 목록을 유지
- 생존 노드에 연결된 Discovery 클라이언트는 영향 없음
- 장애 노드가 복구되면 flooding 메커니즘으로 재동기화
- Eventually consistent: 모든 노드가 동일 상태로 수렴

## 8. 역할 분리: Registry vs Monitor

Registry와 로컬 서비스 모니터는 다른 목적을 가진다:

| 측면 | Registry 토폴로지 | 로컬 서비스 모니터 |
|------|-------------------|-------------------|
| **범위** | 전체 서비스에 대한 글로벌 요약 | 하나의 서비스 handle에 대한 상세 로컬 상태 |
| **세분도** | 대략적: `READY` / `LOST` / `ERROR` | 세밀: 개별 연결 이벤트, 필터 적용 |
| **최신성** | Eventually consistent (하트비트 + 브로드캐스트 주기) | 실시간 (즉시 콜백) |
| **접근** | 로컬 또는 쿼리 클라이언트를 통한 원격 | 로컬만 (같은 프로세스) |

### 언제 어느 것을 사용할 것인가

- **Registry 토폴로지**: "클러스터 전체에서 `payment-service` 인스턴스가 몇
  개 READY인가?" — 1차 운영 판단
- **로컬 모니터**: "이 특정 Gateway가 왜 peer X에 연결하지 못하는가?" — 상세
  원인 분석

권장 워크플로우:

1. Registry 토폴로지 스냅샷으로 글로벌 현황 파악
2. 이상 항목 식별 (`LOST`, `ERROR` 엔트리)
3. 해당 프로세스의 로컬 서비스 모니터로 상세 분석

## 9. 다음 단계

- [Service Discovery](07-1-discovery.ko.md) -- 기반 인프라
- [Gateway 서비스](07-2-gateway.ko.md) -- 위치투명 요청/응답
- [SPOT PUB/SUB](07-3-spot.ko.md) -- 위치투명 발행/구독
- [Registry API 레퍼런스](../api/registry.ko.md) -- 전체 API 문서

---
[← SPOT](07-3-spot.ko.md) | [Routing ID →](08-routing-id.ko.md)
