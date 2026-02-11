[English](discovery.md) | [한국어](discovery.ko.md)

# 디스커버리

Discovery는 Registry 브로드캐스트를 구독하고 로컬 서비스 디렉터리를 유지하는
클라이언트 측 캐시입니다. 애플리케이션은 Discovery를 사용하여 Registry에 직접
연락하지 않고도 서비스 이름으로 사용 가능한 Receiver 또는 SPOT 노드를
조회합니다.

## 타입

```c
typedef struct {
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    uint32_t weight;
    uint64_t registered_at;
} zlink_receiver_info_t;
```

각 `zlink_receiver_info_t`는 하나의 등록된 서비스 인스턴스를 설명합니다.
`service_name` 및 `endpoint` 필드는 서비스를 식별합니다. `routing_id`는
Receiver 또는 SPOT 노드가 할당한 고유 식별자입니다. `weight` 값은 가중
로드 밸런싱에 사용되며, `registered_at`은 등록 타임스탬프를 기록합니다.

## 상수

```c
#define ZLINK_SERVICE_TYPE_GATEWAY 1
#define ZLINK_SERVICE_TYPE_SPOT    2
#define ZLINK_DISCOVERY_SOCKET_SUB 1
```

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_SERVICE_TYPE_GATEWAY` | 1 | Gateway/Receiver 서비스를 위한 Discovery 타입 |
| `ZLINK_SERVICE_TYPE_SPOT` | 2 | SPOT 노드 서비스를 위한 Discovery 타입 |
| `ZLINK_DISCOVERY_SOCKET_SUB` | 1 | Registry 브로드캐스트 수신에 사용되는 SUB 소켓 |

## 함수

### zlink_discovery_new_typed

타입이 지정된 Discovery 인스턴스를 생성합니다.

```c
void *zlink_discovery_new_typed(void *ctx, uint16_t service_type);
```

지정된 서비스 타입으로 범위가 지정된 새 Discovery 인스턴스를 할당하고
초기화합니다. 타입은 생성 시 고정되며 변경할 수 없습니다. 모든 subscribe,
get, count 쿼리는 지정된 서비스 타입 범위 내에서 작동합니다.
Gateway/Receiver 서비스에는 `ZLINK_SERVICE_TYPE_GATEWAY`를, SPOT 노드
서비스에는 `ZLINK_SERVICE_TYPE_SPOT`을 사용합니다.

**반환값:** 성공 시 Discovery 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_discovery_connect_registry`, `zlink_discovery_destroy`

---

### zlink_discovery_connect_registry

Registry PUB 엔드포인트에 연결합니다.

```c
int zlink_discovery_connect_registry(void *discovery,
                                     const char *registry_pub_endpoint);
```

이 Discovery 인스턴스를 Registry의 PUB 소켓에 구독하여 주기적인 서비스 목록
브로드캐스트를 수신합니다. 브로드캐스트가 도착하면 Discovery 캐시가 자동으로
채워집니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. 동시 접근이 시작되기 전에 호출해야 합니다.

**참고:** `zlink_discovery_subscribe`

---

### zlink_discovery_subscribe

서비스 이름을 구독합니다.

```c
int zlink_discovery_subscribe(void *discovery,
                              const char *service_name);
```

특정 서비스 이름에 대한 관심을 등록합니다. Registry 브로드캐스트에서 구독된
서비스 이름과 일치하는 항목만 보유됩니다. Discovery 인스턴스는 여러 서비스
이름을 구독할 수 있습니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_discovery_unsubscribe`, `zlink_discovery_get_receivers`

---

### zlink_discovery_unsubscribe

서비스 이름 구독을 해제합니다.

```c
int zlink_discovery_unsubscribe(void *discovery,
                                const char *service_name);
```

지정된 서비스 이름에 대한 구독을 제거합니다. 이 서비스에 대한 캐시된
항목은 삭제되며 더 이상 업데이트를 수신하지 않습니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_discovery_subscribe`

---

### zlink_discovery_get_receivers

서비스에 대한 수신자 목록을 가져옵니다.

```c
int zlink_discovery_get_receivers(void *discovery,
                                  const char *service_name,
                                  zlink_receiver_info_t *providers,
                                  size_t *count);
```

`service_name`에 대해 현재 알려진 수신자를 호출자가 제공한 배열에 복사합니다.
입력 시 `*count`는 배열 용량을 지정합니다. 출력 시 `*count`는 실제로 기록된
항목 수로 설정됩니다. 배열이 너무 작으면 함수는 들어갈 수 있는 만큼의
항목을 기록하고 `*count`를 기록된 수로 설정합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_discovery_receiver_count`, `zlink_discovery_subscribe`

---

### zlink_discovery_receiver_count

서비스에 등록된 수신자 수를 반환합니다.

```c
int zlink_discovery_receiver_count(void *discovery,
                                   const char *service_name);
```

지정된 서비스 이름에 대해 현재 캐시된 수신자 수를 반환합니다. 데이터를
복사하지 않는 가벼운 확인입니다.

**반환값:** 성공 시 수신자 수(0 이상), 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_discovery_get_receivers`, `zlink_discovery_service_available`

---

### zlink_discovery_service_available

서비스가 사용 가능한지 확인합니다.

```c
int zlink_discovery_service_available(void *discovery,
                                      const char *service_name);
```

지정된 서비스 이름에 대해 최소 하나의 수신자가 등록되어 있는지 반환합니다.
이는 `zlink_discovery_receiver_count`가 0보다 큰 값을 반환하는지 확인하는
것과 동일하지만 불리언 결과로 표현됩니다.

**반환값:** 서비스가 사용 가능하면 `1`, 그렇지 않으면 `0`, 실패 시 `-1`
(errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_discovery_receiver_count`

---

### zlink_discovery_setsockopt

내부 Discovery 소켓의 소켓 옵션을 설정합니다.

```c
int zlink_discovery_setsockopt(void *discovery,
                               int socket_role,
                               int option,
                               const void *optval,
                               size_t optvallen);
```

`socket_role`로 식별되는 Discovery의 내부 SUB 소켓에 저수준 소켓 옵션을
적용합니다. 소켓 역할로 `ZLINK_DISCOVERY_SOCKET_SUB`을 사용합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 잘못된 소켓 역할 또는 알 수 없는 옵션.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_discovery_connect_registry`

---

### zlink_discovery_destroy

Discovery 인스턴스를 파괴하고 모든 리소스를 해제합니다.

```c
int zlink_discovery_destroy(void **discovery_p);
```

내부 SUB 소켓을 닫고, 모든 캐시된 데이터를 해제하며, Discovery 인스턴스를
해제합니다. 파괴 후 `*discovery_p`의 포인터는 `NULL`로 설정됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. 다른 Discovery 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_discovery_new_typed`
