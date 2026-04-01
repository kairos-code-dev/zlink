# Routing ID 개념 및 사용법

## 1. 개요

Routing ID는 zlink에서 소켓과 연결을 식별하는 바이너리 데이터이다.
ROUTER 소켓에서 메시지 라우팅에 사용되며,
STREAM 소켓에서 외부 클라이언트 식별에, 모니터링에서 피어 식별에 활용된다.

## 2. zlink_routing_id_t

!!! note "C API struct -- each binding wraps this into its idiomatic routing ID type."

    ```c
    typedef struct {
        uint8_t size;       /* 0~255 */
        uint8_t data[255];
    } zlink_routing_id_t;
    ```

## 3. 자동 생성 규칙

| 종류 | 포맷 | 크기 | 설명 |
|------|------|------|------|
| 소켓 own routing_id | UUID (binary) | 16B | 모든 소켓에서 자동 생성 |
| STREAM peer routing_id | uint32 | 4B | 연결별 자동 할당 |

- 사용자가 `zlink_set_routing_id()`를 호출하지 않으면 자동 생성
- 프로세스 내 전역 카운터 기반으로 유일성 보장

### own vs peer — 사용자가 알아야 할 차이

| 항목 | own routing_id | peer routing_id |
|------|---|---|
| **생성 시점** | 소켓 생성 시 | 피어 연결 시 |
| **크기** | 16B (UUID) | 가변 (ROUTER), 4B (STREAM) |
| **사용** | 핸드셰이크에서 전송 | 수신 메시지에 자동 추가 |
| **설정** | `zlink_set_routing_id()` | 피어가 설정한 값 사용 |

own routing_id는 소켓이 생성될 때 자동으로 UUID가 할당되며, 피어에게 핸드셰이크 시 전송된다. peer routing_id는 피어가 보낸 own routing_id이며, ROUTER/STREAM 소켓에서 수신 메시지의 첫 프레임에 자동으로 추가된다.

## 4. 사용자 지정 routing_id

### 소켓 Identity 설정

=== "C"

    ```c
    /* Set before bind/connect */
    const char *id = "router-A";
    zlink_set_routing_id(socket, id, strlen(id));
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

주의사항:
- 반드시 `zlink_bind()` 또는 `zlink_connect()` **이전에** 설정
- 연결 후 변경 불가
- 빈 문자열("")은 허용되지 않음
- 같은 ROUTER에 동일 routing_id를 가진 두 피어가 연결되면 충돌 발생

### 사용자 지정 시 고려사항

=== "C"

    ```c
    /* Good example: meaningful identifiers */
    zlink_set_routing_id(dealer, "worker-01", 9);
    zlink_set_routing_id(dealer, "D1", 2);

    /* Caution: potential collision with auto-generated routing_ids */
    /* Avoid UUID format (16B binary) */
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — `zlink_set_routing_id(dealer1, "D1", 2)`

### 조회

=== "C"

    ```c
    zlink_routing_id_t rid;
    zlink_get_routing_id(socket, &rid);

    printf("routing_id (%u bytes): ", rid.size);
    for (size_t i = 0; i < rid.size; ++i)
        printf("%02x", rid.data[i]);
    printf("\n");
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

## 5. Connection Alias 설정

`ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID`는 다음 `zlink_connect()` 호출에
적용되는 연결별 별칭이다.
`zlink_set_router_option()`으로 설정하며,
ROUTER에서 특정 연결을 의미 있는 이름으로 참조할 때 사용한다.

=== "C"

    ```c
    /* Apply alias to the next connect */
    const char *alias = "edge-1";
    zlink_set_router_option(socket, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, alias, strlen(alias));
    zlink_connect(socket, "tcp://server:5555");

    /* Different alias for another connection */
    const char *alias2 = "edge-2";
    zlink_set_router_option(socket, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, alias2, strlen(alias2));
    zlink_connect(socket, "tcp://server2:5556");
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

- `zlink_set_routing_id()`는 소켓 전체에 적용
- `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` (`zlink_set_router_option()`으로 설정)는 개별 연결에 적용
- 하나의 소켓에서 여러 연결에 각각 다른 alias 가능
- `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID`는 ROUTER 연결 경로용이다.
- `ZLINK_STREAM`에 설정하면 `EOPNOTSUPP`를 반환한다.

## 6. ROUTER 소켓에서 routing_id 사용법

ROUTER 소켓에서 `zlink_recv()`와 recv callback은 송신자의 routing_id를
**별도 파라미터**(`source_rid`)로 반환한다. 메시지 프레임에는 데이터만 포함된다.
응답 시 `zlink_send_rid()`에 동일 routing_id를 전달하여 올바른 피어에게 전송한다.

> **libzmq와의 차이:** libzmq ROUTER는 `zmq_recv()`의 첫 프레임으로
> routing_id를 반환했지만, zlink에서는 모든 소켓 타입에서 routing_id가
> 별도 파라미터로 분리되어 있다.

### 기본 요청-응답

=== "C"

    ```c
    /* ROUTER server (with handler) */
    void on_request(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* source_rid = "D1" (2 bytes), parts[0] = "Hello" (5 bytes) */

        /* Reply: use zlink_send_rid for directed send */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 5);
        memcpy(zlink_msg_data(&reply), "World", 5);
        zlink_send_rid(router, source_rid, &reply, 1, 0);

        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }

    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    /* Receive with zlink_recv() */
    zlink_bind(router, "tcp://127.0.0.1:*");

    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(router, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

    /* DEALER client (explicit routing_id) */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    zlink_set_routing_id(dealer, "D1", 2);
    zlink_connect(dealer, endpoint);

    /* DEALER send */
    zlink_msg_t req;
    zlink_msg_init_size(&req, 5);
    memcpy(zlink_msg_data(&req), "Hello", 5);
    zlink_send(dealer, &req, 1, 0);

    /* on_request callback receives the message and replies */
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

### 다중 클라이언트 구분

=== "C"

    ```c
    /* DEALER 1: routing_id = "D1" */
    zlink_set_routing_id(dealer1, "D1", 2);
    zlink_connect(dealer1, endpoint);

    /* DEALER 2: routing_id = "D2" */
    zlink_set_routing_id(dealer2, "D2", 2);
    zlink_connect(dealer2, endpoint);

    /* ROUTER handler distinguishes clients by source_rid */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* source_rid->data contains "D1" or "D2" */
        /* Reply to specific client */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 5);
        memcpy(zlink_msg_data(&reply), "reply", 5);
        zlink_send_rid(router, source_rid, &reply, 1, 0);
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — 다중 DEALER 예제

### zlink_msg_t를 사용한 routing_id 처리

=== "C"

    ```c
    /* Handler callback provides routing_id and data directly */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* Check routing_id size and content */
        printf("routing_id: %zu bytes\n", source_rid->size);

        /* Reply: use source_rid */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 5);
        memcpy(zlink_msg_data(&reply), "reply", 5);
        zlink_send_rid(router, source_rid, &reply, 1, 0);

        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

## 7. STREAM 소켓에서 routing_id 사용법

STREAM 소켓은 4B uint32 peer routing_id로 외부 클라이언트를 식별한다.

### 기본 사용

=== "C"

    ```c
    /* Callback dispatch */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        for (size_t i = 0; i < part_count; ++i) {
            void *data = zlink_msg_data(&parts[i]);
            size_t size = zlink_msg_size(&parts[i]);

            /* Reply: use the same routing_id */
            zlink_msg_t reply;
            zlink_msg_init_size(&reply, size);
            memcpy(zlink_msg_data(&reply), data, size);
            zlink_send_rid(stream, source_rid, &reply, 1, 0);
            zlink_msg_close(&parts[i]);
        }
    }

    zlink_recv_handler(stream, on_message, NULL);
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

### 연결/해제 이벤트의 routing_id

=== "C"

    ```c
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        for (size_t i = 0; i < part_count; ++i) {
            uint8_t *data = (uint8_t *)zlink_msg_data(&parts[i]);
            size_t size = zlink_msg_size(&parts[i]);

            if (size == 1 && data[0] == 0x01) {
                /* New client connected: identify by source_rid */
                printf("Connected: ");
                for (size_t j = 0; j < source_rid->size; j++)
                    printf("%02x", source_rid->data[j]);
                printf("\n");
            } else if (size == 1 && data[0] == 0x00) {
                /* Client disconnected: identify by source_rid and clean up */
                printf("Disconnected\n");
            }
            zlink_msg_close(&parts[i]);
        }
    }
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> 참고: `core/tests/test_stream_socket.cpp` — `recv_stream_event()`, `send_stream_msg()`

### ROUTER vs STREAM routing_id 비교

| 항목 | ROUTER | STREAM |
|------|---|---|
| **크기** | 가변 (사용자 설정 또는 16B UUID) | 고정 4B (uint32) |
| **생성** | 피어의 own routing_id | 서버가 자동 할당 |
| **설정 가능** | `zlink_set_routing_id()`로 피어가 설정 | 자동 할당만 (설정 불가) |
| **프레임 위치** | 수신 시 자동 추가 | 수신 시 자동 추가 |

## 8. routing_id 디버깅 팁

### hex 출력

routing_id는 바이너리 데이터이므로 문자열로 출력하면 깨질 수 있다. hex 형식을 사용한다.

=== "C"

    ```c
    void print_routing_id(const void *data, size_t size) {
        const uint8_t *bytes = (const uint8_t *)data;
        printf("routing_id[%zu]: ", size);
        for (size_t i = 0; i < size; i++)
            printf("%02x", bytes[i]);
        printf("\n");
    }

    /* In handler callback */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        print_routing_id(source_rid->data, source_rid->size);
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

### 문자열 routing_id

사용자가 설정한 routing_id가 ASCII 문자열이면 직접 출력 가능.

=== "C"

    ```c
    zlink_set_routing_id(dealer, "D1", 2);

    /* In ROUTER handler callback */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        char rid[256];
        memcpy(rid, source_rid->data, source_rid->size);
        rid[source_rid->size] = '\0';
        printf("routing_id: %s\n", rid);  /* "D1" */
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

### 자동 생성 routing_id 확인

=== "C"

    ```c
    /* Query the auto-assigned routing_id after socket creation */
    zlink_routing_id_t rid;
    zlink_get_routing_id(socket, &rid);
    printf("Auto-generated routing_id: %u bytes\n", rid.size);  /* 16 bytes (UUID) */
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

## 9. 바이너리 처리 원칙

- routing_id는 **바이너리 데이터**로 취급
- 문자열 변환은 애플리케이션 책임
- 자동 생성 routing_id는 내부 포맷이며 숫자 변환 API 미제공
- 비교 시 `memcmp()` 사용 (문자열 비교 함수 사용 불가)
- 로그 출력 시 hex 포맷 권장

=== "C"

    ```c
    /* routing_id 비교 */
    if (rid_size == 2 && memcmp(rid, "D1", 2) == 0) {
        /* D1 클라이언트의 메시지 */
    }
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

---
[← SPOT](07-3-spot.ko.md) | [Message API →](09-message-api.ko.md)
