[English](discovery.md) | [한국어](discovery.ko.md)

# 디스커버리

Discovery는 Registry 브로드캐스트를 구독하고 로컬 서비스 디렉터리를 유지하는
클라이언트 측 서비스 뷰입니다. 애플리케이션은 Discovery를 사용하여 Registry에
직접 연락하지 않고도 서비스 이름으로 사용 가능한 프로바이더를 조회합니다.
Discovery는 연결된 서비스의 lifecycle owner 역할을 합니다 — Gateway, SPOT
Node, 그리고 raw 소켓 패밀리(ROUTER/DEALER/PUB/SUB) 모두 프로바이더 등록,
피어 갱신, 종료를 자신의 Discovery 인스턴스에 위임합니다.

## 스레드 안전성 요약

하나의 Discovery handle을 여러 스레드에서 동시에 사용할 수 있습니다 (thread-safe).
다만 모든 호출이 같은 시점 제약을 갖는 것은 아닙니다.

- `zlink_discovery_connect_registry()`, monitor, query성 조회는 runtime에 호출할 수
  있습니다.
- `zlink_set_routing_id()`는 first subscribe/query/connect 전에만
  의미가 있는 init-only 성격의 API입니다.
- `zlink_discovery_destroy()`는 fail-fast lifecycle gate를 사용합니다. 다른
  스레드가 같은 handle에서 callback이나 admitted API를 실행 중이면 `EBUSY`,
  destroy가 accepted된 뒤 새 API 진입은 `ESHUTDOWN`입니다.

## 현재 권장 API 방향

- Discovery identity는 `zlink_set_routing_id(discovery, data, size)` /
  `zlink_get_routing_id(discovery, &out)`로 다룹니다.
- Discovery registry 링크의 TLS 설정은
  `zlink_set_tls_client(discovery, ca_cert, hostname, trust_system)`로
  구성합니다.
- `zlink_discovery_connect_registry()` 하나만 Registry bootstrap 연결로
  사용하고, 브로드캐스트/uplink 경로는 내부에서 자동 구성합니다.
- `ZLINK_DISCOVERY_SERVICE_UP`,
  `ZLINK_DISCOVERY_PROVIDERS_CHANGED` 같은 상태 전이는
  `zlink_service_monitor_open(discovery, &options)`으로 관찰합니다.
  `zlink_monitor_close()`로 닫습니다.
- 전역 요약 상태는 registry topology snapshot/query API로 조회합니다.
- Discovery는 `zlink_set_option(discovery, ZLINK_OPT_*, ...)`을 지원하며,
  내부 관리 소켓 세트에 fan-out으로 적용됩니다. getter(`zlink_get_option`)는
  Discovery에 제공되지 않습니다 (단일 source-of-truth가 없음).

## 상수

### 서비스 타입

```c
typedef enum zlink_service_type_t
{
    ZLINK_SERVICE_TYPE_GATEWAY = 0x3001,
    ZLINK_SERVICE_TYPE_SPOT    = 0x3002,
    ZLINK_SERVICE_TYPE_SOCKET  = 0x3003
} zlink_service_type_t;
```

| 상수 | 설명 |
|------|------|
| `ZLINK_SERVICE_TYPE_GATEWAY` | Gateway 서비스를 위한 Discovery 타입 |
| `ZLINK_SERVICE_TYPE_SPOT` | SPOT 노드 서비스를 위한 Discovery 타입 |
| `ZLINK_SERVICE_TYPE_SOCKET` | raw 소켓 패밀리(ROUTER/DEALER/PUB/SUB)를 위한 Discovery 타입 |

### 서비스 역할

```c
typedef enum zlink_service_role_t
{
    ZLINK_SERVICE_ROLE_INVALID = 0,
    ZLINK_SERVICE_ROLE_GATEWAY = 1,
    ZLINK_SERVICE_ROLE_SPOT    = 2,
    ZLINK_SERVICE_ROLE_ROUTER  = 3,
    ZLINK_SERVICE_ROLE_DEALER  = 4,
    ZLINK_SERVICE_ROLE_PUB     = 5,
    ZLINK_SERVICE_ROLE_SUB     = 6
} zlink_service_role_t;
```

| 상수 | 설명 |
|------|------|
| `ZLINK_SERVICE_ROLE_GATEWAY` | Gateway 서비스 타입의 고정 역할 |
| `ZLINK_SERVICE_ROLE_SPOT` | SPOT 서비스 타입의 고정 역할 |
| `ZLINK_SERVICE_ROLE_ROUTER` | 소켓 패밀리: ROUTER 소켓 |
| `ZLINK_SERVICE_ROLE_DEALER` | 소켓 패밀리: DEALER 소켓 |
| `ZLINK_SERVICE_ROLE_PUB` | 소켓 패밀리: PUB 소켓 |
| `ZLINK_SERVICE_ROLE_SUB` | 소켓 패밀리: SUB 소켓 |

Gateway와 SPOT은 고정 역할을 가진다(서비스 타입에서 자동 파생). 소켓 패밀리
서비스는 소켓 타입에 맞는 명시적 역할이 필요하다. 역할 매칭 규칙: PUB은 SUB과
짝, ROUTER와 DEALER는 서로 짝을 이룬다.

## 함수

### zlink_discovery_new

고정 서비스 뷰로 Discovery 인스턴스를 생성합니다.

```c
void *zlink_discovery_new (void *ctx,
                           zlink_service_type_t service_type,
                           const char *service_name);
```

지정된 서비스 타입과 논리 서비스 이름으로 범위가 지정된 새 Discovery
인스턴스를 할당하고 초기화합니다. 두 값 모두 생성 시 고정되며 변경할 수
없습니다. 모든 subscribe/get/count 조회는 해당 논리 서비스 뷰 안에서
동작합니다.

Gateway 서비스에는 `ZLINK_SERVICE_TYPE_GATEWAY`를, SPOT 노드 서비스에는
`ZLINK_SERVICE_TYPE_SPOT`을, raw 소켓 패밀리 서비스(ROUTER/DEALER/PUB/SUB)
에는 `ZLINK_SERVICE_TYPE_SOCKET`을 사용합니다.

**매개변수:**
- `ctx` -- Context 핸들.
- `service_type` -- 이 핸들의 서비스 패밀리.
- `service_name` -- 이 핸들의 고정 논리 서비스 이름.

**반환값:** 성공 시 Discovery 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_discovery_connect_registry`, `zlink_discovery_destroy`

---

### zlink_discovery_connect_registry

Registry bootstrap/control 엔드포인트에 연결합니다.

```c
int zlink_discovery_connect_registry (void *discovery,
                                      const char *registry_endpoint);
```

이 Discovery 인스턴스를 Registry control plane에 bootstrap 연결합니다.
Registry 응답에서 내부 broadcast/uplink 엔드포인트를 학습하고, Discovery가
그 소켓들을 자동으로 구성한 뒤 주기적인 서비스 목록 브로드캐스트를 수신합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 하나의 Discovery handle을 여러 스레드에서 동시에 사용할 수
있습니다 (thread-safe). 이 호출도 일반 lifecycle/state 제약을 지키는 한 다른
Discovery 작업과 병행할 수 있습니다.

**참고:** `zlink_discovery_destroy`

---

### zlink_set_tls_client

Discovery registry 연결에 TLS 설정을 구성합니다.

```c
int zlink_set_tls_client (void *discovery,
                          const char *ca_cert,
                          const char *hostname,
                          int trust_system);
```

Discovery 서비스가 내부적으로 관리하는 registry bootstrap 및 uplink 연결에
TLS 클라이언트 설정을 적용합니다. `zlink_discovery_connect_registry()` 전에
호출해야 합니다.

**매개변수:**
- `ca_cert` -- PEM 인코딩된 CA 인증서 번들 경로.
- `hostname` -- TLS SNI 및 인증서 검증에 사용할 호스트명.
- `trust_system` -- 0이 아니면 시스템 CA 인증서 저장소를 신뢰합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_discovery_connect_registry`

---

### zlink_set_routing_id

첫 subscribe/query/connect 전에 대표 routing id를 재정의합니다.

```c
int zlink_set_routing_id (void *discovery,
                          const void *data,
                          size_t size);
```

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_get_routing_id`

---

### zlink_get_routing_id

이 Discovery의 대표 routing id를 반환합니다.

```c
int zlink_get_routing_id (void *discovery,
                          zlink_routing_id_t *out);
```

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_set_routing_id`

---

### zlink_socket_attach_discovery

raw ROUTER/DEALER/PUB/SUB 소켓을 discovery 서비스 뷰에 연결합니다.

```c
int zlink_socket_attach_discovery (void *socket, void *discovery);
```

소켓을 지정된 Discovery 인스턴스에 연결합니다. Discovery 서비스 타입은
`ZLINK_SERVICE_TYPE_SOCKET`이어야 하며, 소켓 타입은 ROUTER, DEALER, PUB,
SUB 중 하나여야 합니다. 서비스 역할은 소켓 타입에서 자동으로 파생됩니다.

연결되면 소켓은 프로바이더 등록, 피어 갱신, 종료를 Discovery 인스턴스에
위임합니다. 연결된 소켓에서 `connect`, `disconnect`, `unbind`, `close`
수동 호출은 실패합니다. 연결된 소켓의 lifecycle을 종료하려면 Discovery
인스턴스를 파괴하십시오.

**매개변수:**
- `socket` -- 소켓 핸들 (ROUTER, DEALER, PUB, SUB 중 하나).
- `discovery` -- `ZLINK_SERVICE_TYPE_SOCKET`으로 생성된 Discovery 핸들.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**오류:**
- `EINVAL` -- 잘못된 소켓 또는 discovery 핸들.
- `ENOTSUP` -- 지원하지 않는 소켓 타입 (ROUTER, DEALER, PUB, SUB만 가능).
- `EBUSY` -- 소켓이 이미 discovery 인스턴스에 연결되어 있거나, 기존
  connect 엔드포인트 또는 attached pipe가 있음.

**스레드 안전성:** 동일 핸들 호출 시 thread-safe.

**참고:** `zlink_discovery_new`, `zlink_discovery_destroy`

---

### zlink_discovery_destroy

Discovery 인스턴스를 파괴하고 모든 리소스를 해제합니다.

```c
int zlink_discovery_destroy (void **discovery_p);
```

내부 SUB 소켓을 닫고, 모든 캐시된 데이터를 해제하며, Discovery 인스턴스를
해제합니다. Discovery를 파괴하면 이 서비스 뷰에 lifecycle 소유권을 위임한
모든 참여자(Gateway, SPOT Node, 소켓)도 함께 종료됩니다. 파괴 후
`*discovery_p`의 포인터는 `NULL`로 설정됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** Discovery destroy는 lifecycle gate를 사용합니다. 같은
handle에서 다른 스레드가 콜백 또는 admitted API를 실행 중이면 `errno=EBUSY`로
실패합니다. destroy가 accepted된 뒤 새 API 진입은 `errno=ESHUTDOWN`입니다.
destroy가 성공한 경우에만 `*discovery_p`가 `NULL`로 정리됩니다.

**참고:** `zlink_discovery_new`
