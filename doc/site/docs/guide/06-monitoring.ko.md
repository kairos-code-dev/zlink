# 모니터링 API 사용법

## 1. 개요

zlink 모니터링 API는 소켓의 연결/해제/핸드셰이크 등 이벤트를 실시간으로 관찰할 수 있다.
다른 소켓과 동일하게 recv 모드(pull)와 callback 모드를 지원한다.

## 2. 모니터 활성화

### 2.1 콜백 모드

I/O 스레드에서 이벤트 발생 즉시 핸들러가 호출된다.
이벤트 유실 없이 실시간으로 처리하려면 콜백 모드가 적합하다.

=== "C"

    ```c
    /* Define event handler */
    void on_monitor_event(const zlink_monitor_event_t *ev, void *userdata)
    {
        printf("Event: 0x%llx\n", (unsigned long long)ev->event);
        printf("Local: %s\n", ev->local_addr);
        printf("Remote: %s\n", ev->remote_addr);

        if (ev->routing_id.size > 0) {
            printf("routing_id: ");
            for (uint8_t i = 0; i < ev->routing_id.size; ++i)
                printf("%02x", ev->routing_id.data[i]);
            printf("\n");
        }
    }

    void *server = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(server, "tcp://*:5555");

    /* Create monitor with options */
    zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
    void *mon = zlink_socket_monitor_open(server, &opts);
    zlink_socket_monitor_handler(mon, on_monitor_event, NULL);
    ```

=== "C++"

    ```cpp
    // Define event handler
    auto server = zlink::socket(ctx, zlink::socket_type::router);
    server.bind("tcp://*:5555");

    // Create monitor with options
    zlink::monitor_options opts;
    opts.events = zlink::event::all;
    auto mon = server.monitor_open(opts);
    mon.set_handler([](const zlink::monitor_event& ev) {
        std::println("Event: 0x{:x}", ev.event());
        std::println("Local: {}", ev.local_addr());
        std::println("Remote: {}", ev.remote_addr());
        if (ev.routing_id().size() > 0)
            std::println("routing_id: {}", ev.routing_id().to_hex());
    });
    ```

=== "Java"

    ```java
    var server = ctx.socket(SocketType.ROUTER);
    server.bind("tcp://*:5555");

    var opts = new MonitorOptions(MonitorEvent.ALL);
    var mon = server.monitorOpen(opts);
    mon.setHandler((ev) -> {
        System.out.printf("Event: 0x%x%n", ev.event());
        System.out.printf("Local: %s%n", ev.localAddr());
        System.out.printf("Remote: %s%n", ev.remoteAddr());
        if (ev.routingId().size() > 0)
            System.out.printf("routing_id: %s%n", ev.routingId().toHex());
    });
    ```

=== "Python"

    ```python
    server = ctx.socket(zlink.ROUTER)
    server.bind("tcp://*:5555")

    opts = zlink.MonitorOptions(events=zlink.EVENT_ALL)
    mon = server.monitor_open(opts)

    def on_monitor_event(ev):
        print(f"Event: 0x{ev.event:x}")
        print(f"Local: {ev.local_addr}")
        print(f"Remote: {ev.remote_addr}")
        if ev.routing_id:
            print(f"routing_id: {ev.routing_id.hex()}")

    mon.set_handler(on_monitor_event)
    ```

=== "Node/TypeScript"

    ```typescript
    const server = ctx.socket(zlink.ROUTER);
    server.bind("tcp://*:5555");

    const opts = { events: zlink.EVENT_ALL };
    const mon = server.monitorOpen(opts);
    mon.setHandler((ev) => {
        console.log(`Event: 0x${ev.event.toString(16)}`);
        console.log(`Local: ${ev.localAddr}`);
        console.log(`Remote: ${ev.remoteAddr}`);
        if (ev.routingId.length > 0)
            console.log(`routing_id: ${ev.routingId.toString("hex")}`);
    });
    ```

=== "C#/.NET"

    ```csharp
    using var server = ctx.CreateSocket(SocketType.Router);
    server.Bind("tcp://*:5555");

    var opts = new MonitorOptions { Events = MonitorEvent.All };
    using var mon = server.MonitorOpen(opts);
    mon.SetHandler((ev) => {
        Console.WriteLine($"Event: 0x{ev.Event:x}");
        Console.WriteLine($"Local: {ev.LocalAddr}");
        Console.WriteLine($"Remote: {ev.RemoteAddr}");
        if (ev.RoutingId.Size > 0)
            Console.WriteLine($"routing_id: {ev.RoutingId.ToHex()}");
    });
    ```

=== "Rust"

    ```rust
    let server = ctx.socket(zlink::ROUTER)?;
    server.bind("tcp://*:5555")?;

    let opts = zlink::MonitorOptions::new(zlink::EVENT_ALL);
    let mon = server.monitor_open(&opts)?;
    mon.set_handler(|ev| {
        println!("Event: 0x{:x}", ev.event());
        println!("Local: {}", ev.local_addr());
        println!("Remote: {}", ev.remote_addr());
        if !ev.routing_id().is_empty() {
            println!("routing_id: {:x}", ev.routing_id());
        }
    });
    ```

이벤트 발생 시 `on_monitor_event` 콜백이 자동으로 호출된다.

### 이벤트 구조체

!!! note "C API struct -- each binding wraps this into its idiomatic event type."

    ```c
    typedef struct {
        uint64_t event;               /* Event type */
        uint64_t value;               /* Auxiliary value (fd, errno, reason, etc.) */
        zlink_routing_id_t routing_id; /* Peer routing_id */
        char local_addr[256];         /* Local address */
        char remote_addr[256];        /* Remote address */
    } zlink_monitor_event_t;
    ```

## 4. Socket Monitor 이벤트

`zlink_socket_monitor_open()`으로 관찰하는 이벤트다.
raw 소켓의 transport/session 상태를 알려준다.

### 이벤트 전체 표

| 상수 | 값 | 설명 | `value` | 발생 측 |
|---|---|---|---|---|
| `CONNECTION_READY_CHANGED` | `0x1000` | 핸드셰이크 완료, 메시징 가능 | ready 수 | 양쪽 |
| `CONNECTED` | `0x0001` | TCP 연결 성립 (핸드셰이크 전) | fd | 클라이언트 |
| `ACCEPTED` | `0x0020` | 수신 연결 accept | fd | 서버 |
| `DISCONNECTED` | `0x0200` | 세션 종료 | reason 코드 | 양쪽 |
| `LISTENING` | `0x0008` | bind 성공, 수신 대기 중 | fd | 서버 |
| `CLOSED` | `0x0080` | 의도적 close 완료 | — | 양쪽 |
| `CONNECT_DELAYED` | `0x0002` | 비동기 연결 재시도 예약 | errno | 클라이언트 |
| `CONNECT_RETRIED` | `0x0004` | 비동기 재연결 진행 중 | — | 클라이언트 |
| `BIND_FAILED` | `0x0010` | bind 실패 | errno | 서버 |
| `ACCEPT_FAILED` | `0x0040` | accept 실패 | errno | 서버 |
| `CLOSE_FAILED` | `0x0100` | close 실패 | errno | 양쪽 |
| `HANDSHAKE_FAILED_NO_DETAIL` | `0x0800` | 핸드셰이크 실패 (일반) | errno | 양쪽 |
| `HANDSHAKE_FAILED_PROTOCOL` | `0x2000` | 프로토콜 오류로 실패 | 에러 코드 | 양쪽 |
| `HANDSHAKE_FAILED_AUTH` | `0x4000` | 인증 실패 | — | 양쪽 |
| `SUB_DELIVERY_READY_CHANGED` | `0x8000` | SUB 전파 완료 | `0`/`1` | SUB 측 |
| `PUB_DELIVERY_READY_CHANGED` | `0x10000` | PUB subscriber 준비 | ready 수 | PUB 측 |
| `MONITOR_STOPPED` | `0x0400` | 모니터 종료 | — | 양쪽 |

- `CONNECTION_READY_CHANGED`: 모든 소켓에서 `routing_id`에 peer id 포함
- `SUB_DELIVERY_READY_CHANGED` 이후 `zlink_subscribe()` 수신 시작 가능
- `PUB_DELIVERY_READY_CHANGED` 이후 `zlink_publish()` delivery 시작 가능

### 연결 흐름

```
클라이언트: CONNECT_DELAYED(선택) → CONNECTED → CONNECTION_READY_CHANGED → send/recv 시작
서버:       LISTENING → ACCEPTED → CONNECTION_READY_CHANGED → send/recv 시작
종료:       CONNECTION_READY_CHANGED → DISCONNECTED → CONNECT_DELAYED → 재연결...
```

### CONNECTION_READY_CHANGED 상세

핸드셰이크 완료 후 발생한다. 이 이벤트를 받으면 즉시 메시지를 보내고 받을 수 있다.
`value` 필드에는 `current_ready_count` -- 현재 ready 피어의 절대 수가 포함된다.

- ROUTER/STREAM에서는 `ev->routing_id`에 peer identity가 포함된다.
- PAIR/DEALER에서는 `routing_id`가 비어 있다.

### DISCONNECTED reason 코드

| 코드 | 이름 | 의미 |
|------|------|------|
| 0 | `UNKNOWN` | 원인 불명 |
| 3 | `HANDSHAKE_FAILED` | 핸드셰이크 실패 |
| 4 | `TRANSPORT_ERROR` | 전송계층 오류 |
| 5 | `CTX_TERM` | 컨텍스트 종료 |

### 프로토콜 에러 코드

`HANDSHAKE_FAILED_PROTOCOL` 발생 시 `value` 필드에 다음 코드 중 하나가 포함된다:

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | 잘못된 형식의 ZMP HELLO 커맨드. |

## 5. 이벤트 흐름 다이어그램

### 연결 성공

```
  클라이언트 측:

  ┌─────────────────┐       ┌───────────┐       ┌──────────────────────────┐
  │ CONNECT_DELAYED │──────►│ CONNECTED │──────►│ CONNECTION_READY_CHANGED │
  │    (선택)       │       └───────────┘       └──────────────────────────┘
  └─────────────────┘


  서버 측:

  ┌──────────┐       ┌──────────────────────────┐
  │ ACCEPTED │──────►│ CONNECTION_READY_CHANGED │
  └──────────┘       └──────────────────────────┘
```

### 핸드셰이크 실패

```
  클라이언트 측:

  ┌───────────┐       ┌─────────────────────┐       ┌──────────────┐
  │ CONNECTED │──────►│ HANDSHAKE_FAILED_*  │──────►│ DISCONNECTED │
  └───────────┘       └─────────────────────┘       └──────────────┘


  서버 측:

  ┌──────────┐       ┌─────────────────────┐       ┌──────────────┐
  │ ACCEPTED │──────►│ HANDSHAKE_FAILED_*  │──────►│ DISCONNECTED │
  └──────────┘       └─────────────────────┘       └──────────────┘
```

### 정상 해제

```
  ┌──────────────────────────┐       ┌──────────────┐
  │ CONNECTION_READY_CHANGED │──────►│ DISCONNECTED │
  └──────────────────────────┘       └──────────────┘
```

### 재연결

```
  ┌───────────┐       ┌──────────────────────────┐       ┌──────────────┐
  │ CONNECTED │──────►│ CONNECTION_READY_CHANGED │──────►│ DISCONNECTED │
  └───────────┘       └──────────────────────────┘       └──────┬───────┘
                                                                │
                      ┌─────────────────┐                       │
                      │ CONNECT_DELAYED │◄──────────────────────┘
                      └────────┬────────┘
                               │
                      ┌────────▼─────────┐
                      │ CONNECT_RETRIED  │
                      └────────┬─────────┘
                               │
                      ┌────────▼────┐       ┌──────────────────────────┐
                      │  CONNECTED  │──────►│ CONNECTION_READY_CHANGED │
                      └─────────────┘       └──────────────────────────┘
```

## 6. DISCONNECTED reason 코드

`DISCONNECTED` 이벤트의 `value` 필드에 해제 사유가 포함된다.

| 코드 | 이름 | 의미 | 대응 방법 |
|------|------|------|-----------|
| 0 | UNKNOWN | 원인 불명 | 로그 기록 후 관찰 |
| 3 | HANDSHAKE_FAILED | 핸드셰이크 실패 | TLS/프로토콜 설정 확인 |
| 4 | TRANSPORT_ERROR | 전송계층 오류 | 네트워크 상태 확인 |
| 5 | CTX_TERM | 컨텍스트 종료 | 종료 처리 |

### reason 코드 처리 예제

=== "C"

    ```c
    void on_monitor(const zlink_monitor_event_t *ev, void *userdata)
    {
        if (ev->event == ZLINK_EVENT_DISCONNECTED) {
            switch (ev->value) {
                case ZLINK_DISCONNECT_REASON_UNKNOWN:
                    printf("Unknown disconnection\n");
                    break;
                case ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED:
                    printf("Handshake failed -- check TLS configuration\n");
                    break;
                case ZLINK_DISCONNECT_REASON_TRANSPORT_ERROR:
                    printf("Transport error -- check network\n");
                    break;
                case ZLINK_DISCONNECT_REASON_CTX_TERM:
                    printf("Context terminated\n");
                    break;
                default:
                    printf("Unknown reason=%llu\n", (unsigned long long)ev->value);
                    break;
            }
        }
    }
    ```

=== "C++"

    ```cpp
    mon.set_handler([](const zlink::monitor_event& ev) {
        if (ev.event() == zlink::event::disconnected) {
            switch (ev.value()) {
                case zlink::disconnect_reason::unknown:
                    std::println("Unknown disconnection"); break;
                case zlink::disconnect_reason::handshake_failed:
                    std::println("Handshake failed -- check TLS configuration"); break;
                case zlink::disconnect_reason::transport_error:
                    std::println("Transport error -- check network"); break;
                case zlink::disconnect_reason::ctx_term:
                    std::println("Context terminated"); break;
                default:
                    std::println("Unknown reason={}", ev.value()); break;
            }
        }
    });
    ```

=== "Java"

    ```java
    mon.setHandler((ev) -> {
        if (ev.event() == MonitorEvent.DISCONNECTED) {
            switch (ev.disconnectReason()) {
                case UNKNOWN -> System.out.println("Unknown disconnection");
                case HANDSHAKE_FAILED -> System.out.println("Handshake failed -- check TLS configuration");
                case TRANSPORT_ERROR -> System.out.println("Transport error -- check network");
                case CTX_TERM -> System.out.println("Context terminated");
                default -> System.out.printf("Unknown reason=%d%n", ev.value());
            }
        }
    });
    ```

=== "Python"

    ```python
    def on_monitor(ev):
        if ev.event == zlink.EVENT_DISCONNECTED:
            reason = ev.value
            if reason == zlink.DISCONNECT_REASON_UNKNOWN:
                print("Unknown disconnection")
            elif reason == zlink.DISCONNECT_REASON_HANDSHAKE_FAILED:
                print("Handshake failed -- check TLS configuration")
            elif reason == zlink.DISCONNECT_REASON_TRANSPORT_ERROR:
                print("Transport error -- check network")
            elif reason == zlink.DISCONNECT_REASON_CTX_TERM:
                print("Context terminated")
            else:
                print(f"Unknown reason={reason}")
    ```

=== "Node/TypeScript"

    ```typescript
    mon.setHandler((ev) => {
        if (ev.event === zlink.EVENT_DISCONNECTED) {
            switch (ev.value) {
                case zlink.DISCONNECT_REASON_UNKNOWN:
                    console.log("Unknown disconnection"); break;
                case zlink.DISCONNECT_REASON_HANDSHAKE_FAILED:
                    console.log("Handshake failed -- check TLS configuration"); break;
                case zlink.DISCONNECT_REASON_TRANSPORT_ERROR:
                    console.log("Transport error -- check network"); break;
                case zlink.DISCONNECT_REASON_CTX_TERM:
                    console.log("Context terminated"); break;
                default:
                    console.log(`Unknown reason=${ev.value}`); break;
            }
        }
    });
    ```

=== "C#/.NET"

    ```csharp
    mon.SetHandler((ev) => {
        if (ev.Event == MonitorEvent.Disconnected) {
            var reason = (DisconnectReason)ev.Value;
            Console.WriteLine(reason switch {
                DisconnectReason.Unknown => "Unknown disconnection",
                DisconnectReason.HandshakeFailed => "Handshake failed -- check TLS configuration",
                DisconnectReason.TransportError => "Transport error -- check network",
                DisconnectReason.CtxTerm => "Context terminated",
                _ => $"Unknown reason={ev.Value}"
            });
        }
    });
    ```

=== "Rust"

    ```rust
    mon.set_handler(|ev| {
        if ev.event() == zlink::EVENT_DISCONNECTED {
            match ev.disconnect_reason() {
                zlink::DisconnectReason::Unknown => println!("Unknown disconnection"),
                zlink::DisconnectReason::HandshakeFailed => println!("Handshake failed -- check TLS configuration"),
                zlink::DisconnectReason::TransportError => println!("Transport error -- check network"),
                zlink::DisconnectReason::CtxTerm => println!("Context terminated"),
                _ => println!("Unknown reason={}", ev.value()),
            }
        }
    });
    ```

## 7. 이벤트 필터링 및 구독 프리셋

### 특정 이벤트만 구독

=== "C"

    ```c
    /* Connection/disconnection events only */
    zlink_socket_monitor_open_options_t opts = {
        .events = ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED,
    };
    void *mon = zlink_socket_monitor_open(server, &opts);
    zlink_socket_monitor_handler(mon, on_monitor_event, NULL);
    ```

=== "C++"

    ```cpp
    zlink::monitor_options opts;
    opts.events = zlink::event::connection_ready_changed | zlink::event::disconnected;
    auto mon = server.monitor_open(opts);
    mon.set_handler(on_monitor_event);
    ```

=== "Java"

    ```java
    var opts = new MonitorOptions(
        MonitorEvent.CONNECTION_READY_CHANGED | MonitorEvent.DISCONNECTED);
    var mon = server.monitorOpen(opts);
    mon.setHandler(this::onMonitorEvent);
    ```

=== "Python"

    ```python
    opts = zlink.MonitorOptions(
        events=zlink.EVENT_CONNECTION_READY_CHANGED | zlink.EVENT_DISCONNECTED)
    mon = server.monitor_open(opts)
    mon.set_handler(on_monitor_event)
    ```

=== "Node/TypeScript"

    ```typescript
    const opts = {
        events: zlink.EVENT_CONNECTION_READY_CHANGED | zlink.EVENT_DISCONNECTED
    };
    const mon = server.monitorOpen(opts);
    mon.setHandler(onMonitorEvent);
    ```

=== "C#/.NET"

    ```csharp
    var opts = new MonitorOptions {
        Events = MonitorEvent.ConnectionReadyChanged | MonitorEvent.Disconnected
    };
    using var mon = server.MonitorOpen(opts);
    mon.SetHandler(OnMonitorEvent);
    ```

=== "Rust"

    ```rust
    let opts = zlink::MonitorOptions::new(
        zlink::EVENT_CONNECTION_READY_CHANGED | zlink::EVENT_DISCONNECTED);
    let mon = server.monitor_open(&opts)?;
    mon.set_handler(on_monitor_event);
    ```

### 권장 구독 프리셋

| 프리셋 | 이벤트 마스크 | 용도 |
|--------|-------------|------|
| 기본 | `CONNECTION_READY_CHANGED \| DISCONNECTED` | 연결 상태 추적 |
| 디버깅 | 기본 + `CONNECTED \| ACCEPTED \| CONNECT_DELAYED \| CONNECT_RETRIED` | 연결 과정 상세 |
| 보안 | 기본 + `HANDSHAKE_FAILED_*` | 인증 실패 감지 |
| 전체 | `ZLINK_EVENT_ALL` | 모든 이벤트 |

### 프리셋 구현 예제

!!! note "C API preset macros -- each binding defines equivalent constants."

    ```c
    /* Basic preset */
    #define MONITOR_PRESET_BASIC \
        (ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED)

    /* Debug preset */
    #define MONITOR_PRESET_DEBUG \
        (MONITOR_PRESET_BASIC | ZLINK_EVENT_CONNECTED | \
         ZLINK_EVENT_ACCEPTED | ZLINK_EVENT_CONNECT_DELAYED | \
         ZLINK_EVENT_CONNECT_RETRIED)

    /* Security preset */
    #define MONITOR_PRESET_SECURITY \
        (MONITOR_PRESET_BASIC | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL | \
         ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL | \
         ZLINK_EVENT_HANDSHAKE_FAILED_AUTH)

    zlink_socket_monitor_open_options_t sec_opts = { .events = MONITOR_PRESET_SECURITY };
    void *mon = zlink_socket_monitor_open(server, &sec_opts);
    zlink_socket_monitor_handler(mon, on_monitor_event, NULL);
    ```

## 8. Socket Monitor Snapshot

monitor handle에서 현재 aggregate 상태를 바로 조회할 수 있다.

| 필드 | 설명 |
|------|------|
| `ready_count` | 현재 ready 상태인 피어 수 |
| `snd_pending_msgs` | 송신 큐에 대기 중인 메시지 수 (SNDHWM에 의해 상한 제한) |
| `rcv_pending_msgs` | 수신 큐에 대기 중인 메시지 수 (RCVHWM에 의해 상한 제한, approximate) |

`snd_pending_msgs`와 `rcv_pending_msgs`는 HWM 설정과 직접 관련된다.
이 값이 HWM에 근접하면 백프레셔가 발생하고 있다는 의미이다.

**주의 — pending 값이 HWM 설정보다 클 수 있는 경우:**

1. **inproc transport**: inproc은 중간에 세션/엔진이 없으므로 양쪽 HWM을 합산한다.
   예를 들어 양쪽 모두 SNDHWM=1000, RCVHWM=1000이면 실제 pipe HWM은
   `1000 + 1000 = 2000`이 된다. pending 값이 설정값의 2배로 보일 수 있다.

2. **HWM을 약간 초과하는 경우**: write 측이 보는 read 카운트(`_peers_msgs_read`)는
   실시간 값이 아니라 read 측이 LWM 도달 시에만 비동기로 알려주는 스냅샷이다.
   매 메시지마다 동기화하면 lock-free pipe의 성능 이점이 사라지므로,
   배치 알림 방식을 사용한다. 그 결과 HWM은 정확한 hard limit이 아닌
   **approximate limit**이며, 알림 사이에 소폭 초과할 수 있다.

| transport | 실제 pipe HWM | 이유 |
|-----------|--------------|------|
| tcp/ipc/tls/ws/wss | `SNDHWM` (설정값 그대로) | 세션이 양쪽을 독립 관리 |
| inproc | `SNDHWM + peer.RCVHWM` | 세션 없이 직접 연결, 양쪽 버퍼를 합산 |

=== "C"

    ```c
    zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
    void *monitor = zlink_socket_monitor_open(socket, &opts);
    zlink_monitor_snapshot_t snapshot;
    zlink_monitor_snapshot(monitor, &snapshot);
    printf("Ready peers: %u, sndq=%llu, rcvq=%llu\n",
           snapshot.ready_count,
           (unsigned long long) snapshot.snd_pending_msgs,
           (unsigned long long) snapshot.rcv_pending_msgs);
    ```

=== "C++"

    ```cpp
    auto mon = socket.monitor_open({zlink::event::all});
    auto snapshot = mon.snapshot();
    std::println("Ready peers: {}, sndq={}, rcvq={}",
                 snapshot.ready_count, snapshot.snd_pending_msgs,
                 snapshot.rcv_pending_msgs);
    ```

=== "Java"

    ```java
    var mon = socket.monitorOpen(new MonitorOptions(MonitorEvent.ALL));
    var snapshot = mon.snapshot();
    System.out.printf("Ready peers: %d, sndq=%d, rcvq=%d%n",
        snapshot.readyCount(), snapshot.sndPendingMsgs(), snapshot.rcvPendingMsgs());
    ```

=== "Python"

    ```python
    mon = socket.monitor_open(zlink.MonitorOptions(events=zlink.EVENT_ALL))
    snapshot = mon.snapshot()
    print(f"Ready peers: {snapshot.ready_count}, "
          f"sndq={snapshot.snd_pending_msgs}, rcvq={snapshot.rcv_pending_msgs}")
    ```

=== "Node/TypeScript"

    ```typescript
    const mon = socket.monitorOpen({ events: zlink.EVENT_ALL });
    const snapshot = mon.snapshot();
    console.log(`Ready peers: ${snapshot.readyCount}, ` +
                `sndq=${snapshot.sndPendingMsgs}, rcvq=${snapshot.rcvPendingMsgs}`);
    ```

=== "C#/.NET"

    ```csharp
    using var mon = socket.MonitorOpen(new MonitorOptions { Events = MonitorEvent.All });
    var snapshot = mon.Snapshot();
    Console.WriteLine($"Ready peers: {snapshot.ReadyCount}, " +
        $"sndq={snapshot.SndPendingMsgs}, rcvq={snapshot.RcvPendingMsgs}");
    ```

=== "Rust"

    ```rust
    let mon = socket.monitor_open(&zlink::MonitorOptions::new(zlink::EVENT_ALL))?;
    let snapshot = mon.snapshot()?;
    println!("Ready peers: {}, sndq={}, rcvq={}",
             snapshot.ready_count, snapshot.snd_pending_msgs,
             snapshot.rcv_pending_msgs);
    ```

event 콜백 안에서 snapshot을 조합해 쓸 수도 있다.

=== "C"

    ```c
    void on_monitor(const zlink_monitor_event_t *ev, void *userdata)
    {
        if (ev->event == ZLINK_EVENT_CONNECTION_READY_CHANGED) {
            zlink_monitor_snapshot_t snapshot;
            zlink_monitor_snapshot(g_monitor, &snapshot);
            printf("Ready peers now: %u\n", snapshot.ready_count);
        }
    }
    ```

=== "C++"

    ```cpp
    mon.set_handler([&](const zlink::monitor_event& ev) {
        if (ev.event() == zlink::event::connection_ready_changed) {
            auto snapshot = mon.snapshot();
            std::println("Ready peers now: {}", snapshot.ready_count);
        }
    });
    ```

=== "Java"

    ```java
    mon.setHandler((ev) -> {
        if (ev.event() == MonitorEvent.CONNECTION_READY_CHANGED) {
            var snapshot = mon.snapshot();
            System.out.printf("Ready peers now: %d%n", snapshot.readyCount());
        }
    });
    ```

=== "Python"

    ```python
    def on_monitor(ev):
        if ev.event == zlink.EVENT_CONNECTION_READY_CHANGED:
            snapshot = mon.snapshot()
            print(f"Ready peers now: {snapshot.ready_count}")
    ```

=== "Node/TypeScript"

    ```typescript
    mon.setHandler((ev) => {
        if (ev.event === zlink.EVENT_CONNECTION_READY_CHANGED) {
            const snapshot = mon.snapshot();
            console.log(`Ready peers now: ${snapshot.readyCount}`);
        }
    });
    ```

=== "C#/.NET"

    ```csharp
    mon.SetHandler((ev) => {
        if (ev.Event == MonitorEvent.ConnectionReadyChanged) {
            var snapshot = mon.Snapshot();
            Console.WriteLine($"Ready peers now: {snapshot.ReadyCount}");
        }
    });
    ```

=== "Rust"

    ```rust
    mon.set_handler(|ev| {
        if ev.event() == zlink::EVENT_CONNECTION_READY_CHANGED {
            let snapshot = mon.snapshot().unwrap();
            println!("Ready peers now: {}", snapshot.ready_count);
        }
    });
    ```

## 8.1 서비스 모니터

서비스 모니터는 SPOT, Discovery 같은 서비스 핸들의
상태 변화를 관찰한다. socket monitor와는 별도 API다.

- **이벤트 타입**: `zlink_service_event_t` (socket monitor의 `zlink_monitor_event_t`와 다름)
- **콜백 타입**: `zlink_service_monitor_handler_fn`
- **열기**: `zlink_service_monitor_open(target, &options)`
- **닫기**: `zlink_monitor_close(&mon)` (socket monitor와 동일)

### 서비스 모니터 열기

=== "C"

    ```c
    /* SPOT service monitor */
    zlink_service_monitor_open_options_t opts = {
        .events = ZLINK_SERVICE_MONITOR_EVENT_ALL
    };
    void *mon = zlink_service_monitor_open(spot_node, &opts);
    ```

=== "C++"

    ```cpp
    zlink::service_monitor_options opts;
    opts.events = zlink::service_monitor_event::all;
    auto mon = spot_node.service_monitor_open(opts);
    ```

=== "Java"

    ```java
    var opts = new ServiceMonitorOptions(ServiceMonitorEvent.ALL);
    var mon = spotNode.serviceMonitorOpen(opts);
    ```

=== "Python"

    ```python
    opts = zlink.ServiceMonitorOptions(events=zlink.SERVICE_MONITOR_EVENT_ALL)
    mon = spot_node.service_monitor_open(opts)
    ```

=== "Node/TypeScript"

    ```typescript
    const opts = { events: zlink.SERVICE_MONITOR_EVENT_ALL };
    const mon = spotNode.serviceMonitorOpen(opts);
    ```

=== "C#/.NET"

    ```csharp
    var opts = new ServiceMonitorOptions { Events = ServiceMonitorEvent.All };
    using var mon = spotNode.ServiceMonitorOpen(opts);
    ```

=== "Rust"

    ```rust
    let opts = zlink::ServiceMonitorOptions::new(zlink::SERVICE_MONITOR_EVENT_ALL);
    let mon = spot_node.service_monitor_open(&opts)?;
    ```

대상 handle(discovery, spot, spot_node)을 넘기면 된다.
handle 종류는 런타임에 자동 판별된다.

### 콜백 모드

=== "C"

    ```c
    void on_spot_event(const zlink_service_event_t *ev, void *userdata)
    {
        if (ev->event_type & ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED) {
            printf("sub delivery ready\n");
        }
        if (ev->event_type & ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED) {
            printf("pub first delivery ready\n");
        }
    }

    zlink_service_monitor_handler(mon, on_spot_event, NULL);
    ```

=== "C++"

    ```cpp
    mon.set_handler([](const zlink::service_event& ev) {
        if (ev.event_type() & zlink::spot_monitor_event::sub_delivery_ready_changed)
            std::println("sub delivery ready");
        if (ev.event_type() & zlink::spot_monitor_event::pub_first_delivery_ready_changed)
            std::println("pub first delivery ready");
    });
    ```

=== "Java"

    ```java
    mon.setHandler((ev) -> {
        if ((ev.eventType() & SpotMonitorEvent.SUB_DELIVERY_READY_CHANGED) != 0)
            System.out.println("sub delivery ready");
        if ((ev.eventType() & SpotMonitorEvent.PUB_FIRST_DELIVERY_READY_CHANGED) != 0)
            System.out.println("pub first delivery ready");
    });
    ```

=== "Python"

    ```python
    def on_spot_event(ev):
        if ev.event_type & zlink.SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED:
            print("sub delivery ready")
        if ev.event_type & zlink.SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED:
            print("pub first delivery ready")

    mon.set_handler(on_spot_event)
    ```

=== "Node/TypeScript"

    ```typescript
    mon.setHandler((ev) => {
        if (ev.eventType & zlink.SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED)
            console.log("sub delivery ready");
        if (ev.eventType & zlink.SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED)
            console.log("pub first delivery ready");
    });
    ```

=== "C#/.NET"

    ```csharp
    mon.SetHandler((ev) => {
        if (ev.EventType.HasFlag(SpotMonitorEvent.SubDeliveryReadyChanged))
            Console.WriteLine("sub delivery ready");
        if (ev.EventType.HasFlag(SpotMonitorEvent.PubFirstDeliveryReadyChanged))
            Console.WriteLine("pub first delivery ready");
    });
    ```

=== "Rust"

    ```rust
    mon.set_handler(|ev| {
        if ev.event_type().contains(zlink::SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED) {
            println!("sub delivery ready");
        }
        if ev.event_type().contains(zlink::SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED) {
            println!("pub first delivery ready");
        }
    });
    ```

### Recv 모드

=== "C"

    ```c
    zlink_service_event_t ev;
    int rc = zlink_service_monitor_recv(mon, &ev);
    if (rc == 0) {
        printf("event: 0x%x, value: %u\n", ev.event_type, ev.value);
    }
    ```

=== "C++"

    ```cpp
    auto ev = mon.recv();
    if (ev) {
        std::println("event: 0x{:x}, value: {}", ev->event_type(), ev->value());
    }
    ```

=== "Java"

    ```java
    var ev = mon.recv();
    if (ev != null) {
        System.out.printf("event: 0x%x, value: %d%n", ev.eventType(), ev.value());
    }
    ```

=== "Python"

    ```python
    ev = mon.recv()
    if ev is not None:
        print(f"event: 0x{ev.event_type:x}, value: {ev.value}")
    ```

=== "Node/TypeScript"

    ```typescript
    const ev = mon.recv();
    if (ev) {
        console.log(`event: 0x${ev.eventType.toString(16)}, value: ${ev.value}`);
    }
    ```

=== "C#/.NET"

    ```csharp
    var ev = mon.Recv();
    if (ev != null) {
        Console.WriteLine($"event: 0x{ev.EventType:x}, value: {ev.Value}");
    }
    ```

=== "Rust"

    ```rust
    if let Some(ev) = mon.recv()? {
        println!("event: 0x{:x}, value: {}", ev.event_type(), ev.value());
    }
    ```

### 서비스 이벤트 전체 표

`zlink_service_monitor_open()`으로 관찰하는 이벤트다.
서비스별로 발생하는 이벤트가 다르다.

#### SPOT 이벤트

| 상수 | 설명 | `value` | 이후 가능한 동작 |
|---|---|---|---|
| `SUB_DELIVERY_READY_CHANGED` | sub delivery 준비 상태 변화 | — | **수신 시작 가능** |
| `PUB_FIRST_DELIVERY_READY_CHANGED` | 최소 1개 subscriber 준비 | — | **`zlink_publish()` delivery 시작** |
| `SPOT_SUB_FILTER_APPLIED` | 구독 필터가 peer에 전파됨 | — | — |
| `SPOT_SUB_SUBSCRIPTION_READY` | 구독 수신 준비 완료 | — | — |
| `SPOT_PUB_DELIVERY_READY_CHANGED` | subject별 remote delivery-ready 변화 | — | — |

#### Discovery 이벤트

| 상수 | 설명 | `value` | 이후 가능한 동작 |
|---|---|---|---|
| `DISCOVERY_SERVICE_UP` | 검색된 서비스 활성화 | — | — |
| `DISCOVERY_SERVICE_DOWN` | 검색된 서비스 비활성화 | — | — |
| `DISCOVERY_PROVIDERS_CHANGED` | provider 집합 변경 | — | — |

#### 공통 이벤트 (모든 서비스)

| 상수 | 설명 |
|---|---|
| `MONITOR_EVENT_ERROR` | 에러 발생 |
| `MONITOR_EVENT_CLOSED` | 모니터 닫힘 |

상세 이벤트 목록은 [events.ko.md](../api/events.ko.md)를 참고한다.

## 9. 다중 소켓 모니터링

여러 소켓의 이벤트를 각각의 콜백 핸들러로 처리.

=== "C"

    ```c
    void on_event_a(const zlink_monitor_event_t *ev, void *userdata)
    {
        printf("Socket A event: 0x%llx\n", (unsigned long long)ev->event);
    }

    void on_event_b(const zlink_monitor_event_t *ev, void *userdata)
    {
        printf("Socket B event: 0x%llx\n", (unsigned long long)ev->event);
    }

    zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
    void *mon_a = zlink_socket_monitor_open(sock_a, &opts);
    zlink_socket_monitor_handler(mon_a, on_event_a, NULL);
    void *mon_b = zlink_socket_monitor_open(sock_b, &opts);
    zlink_socket_monitor_handler(mon_b, on_event_b, NULL);

    /* ... application logic ... */

    /* Cleanup */
    zlink_monitor_close(&mon_a);
    zlink_monitor_close(&mon_b);
    ```

=== "C++"

    ```cpp
    auto opts = zlink::monitor_options{zlink::event::all};
    auto mon_a = sock_a.monitor_open(opts);
    mon_a.set_handler([](const zlink::monitor_event& ev) {
        std::println("Socket A event: 0x{:x}", ev.event());
    });
    auto mon_b = sock_b.monitor_open(opts);
    mon_b.set_handler([](const zlink::monitor_event& ev) {
        std::println("Socket B event: 0x{:x}", ev.event());
    });

    // ... application logic ...

    mon_a.close();
    mon_b.close();
    ```

=== "Java"

    ```java
    var opts = new MonitorOptions(MonitorEvent.ALL);
    var monA = sockA.monitorOpen(opts);
    monA.setHandler(ev -> System.out.printf("Socket A event: 0x%x%n", ev.event()));
    var monB = sockB.monitorOpen(opts);
    monB.setHandler(ev -> System.out.printf("Socket B event: 0x%x%n", ev.event()));

    // ... application logic ...

    monA.close();
    monB.close();
    ```

=== "Python"

    ```python
    opts = zlink.MonitorOptions(events=zlink.EVENT_ALL)
    mon_a = sock_a.monitor_open(opts)
    mon_a.set_handler(lambda ev: print(f"Socket A event: 0x{ev.event:x}"))
    mon_b = sock_b.monitor_open(opts)
    mon_b.set_handler(lambda ev: print(f"Socket B event: 0x{ev.event:x}"))

    # ... application logic ...

    mon_a.close()
    mon_b.close()
    ```

=== "Node/TypeScript"

    ```typescript
    const opts = { events: zlink.EVENT_ALL };
    const monA = sockA.monitorOpen(opts);
    monA.setHandler((ev) => console.log(`Socket A event: 0x${ev.event.toString(16)}`));
    const monB = sockB.monitorOpen(opts);
    monB.setHandler((ev) => console.log(`Socket B event: 0x${ev.event.toString(16)}`));

    // ... application logic ...

    monA.close();
    monB.close();
    ```

=== "C#/.NET"

    ```csharp
    var opts = new MonitorOptions { Events = MonitorEvent.All };
    using var monA = sockA.MonitorOpen(opts);
    monA.SetHandler(ev => Console.WriteLine($"Socket A event: 0x{ev.Event:x}"));
    using var monB = sockB.MonitorOpen(opts);
    monB.SetHandler(ev => Console.WriteLine($"Socket B event: 0x{ev.Event:x}"));

    // ... application logic ...
    ```

=== "Rust"

    ```rust
    let opts = zlink::MonitorOptions::new(zlink::EVENT_ALL);
    let mon_a = sock_a.monitor_open(&opts)?;
    mon_a.set_handler(|ev| println!("Socket A event: 0x{:x}", ev.event()));
    let mon_b = sock_b.monitor_open(&opts)?;
    mon_b.set_handler(|ev| println!("Socket B event: 0x{:x}", ev.event()));

    // ... application logic ...

    mon_a.close();
    mon_b.close();
    ```

## 10. 주의사항

### 모니터 스레드 안전성

`zlink_socket_monitor_open()`과 monitor handle close는 raw/service handle의
저빈도 control path(설정/관리 경로) 계약에 속한다.
즉 애플리케이션 스레드에서 호출할 수 있고,
같은 handle과 섞여도 correctness(동시 사용 시 데이터 무결성)가 유지된다.
다만 monitor callback은 I/O 경로
에서 실행되므로 callback 내부의 느린 작업은 사용자 큐로 넘기는 편이 좋다.

=== "C"

    ```c
    /* Open a monitor from an application thread */
    void *socket = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
    void *mon = zlink_socket_monitor_open(socket, &opts);
    zlink_socket_monitor_handler(mon, on_monitor_event, NULL);

    /* Snapshot reads may happen later from another worker thread */
    zlink_monitor_snapshot_t snapshot;
    zlink_monitor_snapshot(mon, &snapshot);
    ```

=== "C++"

    ```cpp
    auto socket = zlink::socket(ctx, zlink::socket_type::router);
    auto mon = socket.monitor_open({zlink::event::all});
    mon.set_handler(on_monitor_event);

    // Snapshot reads may happen later from another worker thread
    auto snapshot = mon.snapshot();
    ```

=== "Java"

    ```java
    var socket = ctx.socket(SocketType.ROUTER);
    var mon = socket.monitorOpen(new MonitorOptions(MonitorEvent.ALL));
    mon.setHandler(this::onMonitorEvent);

    // Snapshot reads may happen later from another worker thread
    var snapshot = mon.snapshot();
    ```

=== "Python"

    ```python
    socket = ctx.socket(zlink.ROUTER)
    mon = socket.monitor_open(zlink.MonitorOptions(events=zlink.EVENT_ALL))
    mon.set_handler(on_monitor_event)

    # Snapshot reads may happen later from another worker thread
    snapshot = mon.snapshot()
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = ctx.socket(zlink.ROUTER);
    const mon = socket.monitorOpen({ events: zlink.EVENT_ALL });
    mon.setHandler(onMonitorEvent);

    // Snapshot reads may happen later from another worker thread
    const snapshot = mon.snapshot();
    ```

=== "C#/.NET"

    ```csharp
    using var socket = ctx.CreateSocket(SocketType.Router);
    using var mon = socket.MonitorOpen(new MonitorOptions { Events = MonitorEvent.All });
    mon.SetHandler(OnMonitorEvent);

    // Snapshot reads may happen later from another worker thread
    var snapshot = mon.Snapshot();
    ```

=== "Rust"

    ```rust
    let socket = ctx.socket(zlink::ROUTER)?;
    let mon = socket.monitor_open(&zlink::MonitorOptions::new(zlink::EVENT_ALL))?;
    mon.set_handler(on_monitor_event);

    // Snapshot reads may happen later from another worker thread
    let snapshot = mon.snapshot()?;
    ```

### 동시 모니터 제한

동일 소켓에 동시에 여러 모니터를 설정할 수 없다.

### 콜백 처리 속도

콜백 핸들러에서 블로킹 작업을 수행하면 I/O 진행에 영향을 줄 수 있다.
느린 처리가 필요하면 콜백 안에서 사용자 queue로 넘기고 별도 thread에서 처리한다.

### 원격 모니터링

모니터 API는 **inproc 전용**이다. tcp/wss 등 원격 transport는 지원하지 않는다.
원격 모니터링이 필요하면 콜백에서 이벤트를 수신하고 PUB 소켓으로 중계한다.

```
  ┌──────────┐   inproc (PAIR)   ┌─────────────────┐   tcp/wss (PUB)   ┌─────────────┐
  │ 대상소켓  │─────────────────►│ monitor 콜백    │──────────────────►│  원격 SUB   │
  │          │    이벤트 수집     │ → PUB 중계      │    이벤트 발행    │ (모니터링)   │
  └──────────┘                   └─────────────────┘                   └─────────────┘
```

=== "C"

    ```c
    /* PUB 소켓으로 이벤트 중계 */
    void *pub = zlink_socket(ctx, ZLINK_PUB);
    zlink_bind(pub, "tcp://0.0.0.0:9090");

    void on_monitor_relay(const zlink_monitor_event_t *ev, void *userdata)
    {
        void *pub = userdata;
        zlink_msg_t msg;
        zlink_msg_init_size(&msg, sizeof(*ev));
        memcpy(zlink_msg_data(&msg), ev, sizeof(*ev));
        zlink_publish_msg(pub, "monitor", 7, &msg, 1, ZLINK_DONTWAIT);
    }

    zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
    void *mon = zlink_socket_monitor_open(server, &opts);
    zlink_socket_monitor_handler(mon, on_monitor_relay, pub);
    /* 원격에서 SUB으로 모니터링 이벤트 수신 */
    ```

=== "C++"

    ```cpp
    auto pub = zlink::socket(ctx, zlink::socket_type::pub_);
    pub.bind("tcp://0.0.0.0:9090");

    auto mon = server.monitor_open({zlink::event::all});
    mon.set_handler([&](const zlink::monitor_event& ev) {
        zlink::msg msg(sizeof(ev));
        std::memcpy(msg.data(), &ev, sizeof(ev));
        pub.publish("monitor", &msg, 1, zlink::DONTWAIT);
    });
    ```

=== "Java"

    ```java
    var pub = ctx.socket(SocketType.PUB);
    pub.bind("tcp://0.0.0.0:9090");

    var mon = server.monitorOpen(new MonitorOptions(MonitorEvent.ALL));
    mon.setHandler(ev -> pub.publish("monitor", ev.toBytes()));
    ```

=== "Python"

    ```python
    pub = ctx.socket(zlink.PUB)
    pub.bind("tcp://0.0.0.0:9090")

    mon = server.monitor_open(zlink.MonitorOptions(events=zlink.EVENT_ALL))
    mon.set_handler(lambda ev: pub.publish("monitor", ev.to_bytes()))
    ```

=== "Node/TypeScript"

    ```typescript
    const pub = ctx.socket(zlink.PUB);
    pub.bind("tcp://0.0.0.0:9090");

    const mon = server.monitorOpen({ events: zlink.EVENT_ALL });
    mon.setHandler((ev) => pub.publish("monitor", ev.toBuffer()));
    ```

=== "C#/.NET"

    ```csharp
    using var pub = ctx.CreateSocket(SocketType.Pub);
    pub.Bind("tcp://0.0.0.0:9090");

    using var mon = server.MonitorOpen(new MonitorOptions { Events = MonitorEvent.All });
    mon.SetHandler(ev => pub.Publish("monitor", ev.ToBytes()));
    ```

=== "Rust"

    ```rust
    let pub_sock = ctx.socket(zlink::PUB)?;
    pub_sock.bind("tcp://0.0.0.0:9090")?;

    let mon = server.monitor_open(&zlink::MonitorOptions::new(zlink::EVENT_ALL))?;
    mon.set_handler(move |ev| {
        let bytes = ev.to_bytes();
        pub_sock.publish("monitor", &bytes, zlink::DONTWAIT).ok();
    });
    ```

### 모니터 종료 절차

=== "C"

    ```c
    /* Close the monitor handle */
    zlink_monitor_close(&mon);
    ```

=== "C++"

    ```cpp
    mon.close();
    ```

=== "Java"

    ```java
    mon.close();
    ```

=== "Python"

    ```python
    mon.close()
    ```

=== "Node/TypeScript"

    ```typescript
    mon.close();
    ```

=== "C#/.NET"

    ```csharp
    mon.Dispose(); // or using statement
    ```

=== "Rust"

    ```rust
    mon.close();
    ```

## 11. 메시징 시작 전 준비 확인 (Ready Gate)

소켓 또는 서비스가 실제로 메시지를 보내고 받을 수 있는 시점을 알아야 할 때,
어떤 이벤트를 기다리면 되는지 정리한다. 예를 들어 서버가 bind 후 클라이언트에
데이터를 보내기 전에, 또는 PUB이 SUB에 구독이 전파된 것을 확인한 뒤에
메시징을 시작하는 경우다.

**ready 판정 규칙:**

- 아래 명시된 monitor event를 기다린다.
- `sleep`/고정 지연으로 ready를 추정하지 않는다.
- `CONNECTED`, `ACCEPTED`, `LISTENING`은 progress/debug 이벤트일 뿐,
  메시징 시작 기준으로 쓰지 않는다.
- delivery-ready event를 받은 뒤에 메시징을 시작한다.

### 11.1 Raw 소켓 — PAIR, DEALER, ROUTER

`CONNECTION_READY_CHANGED` 이벤트를 받으면 즉시 send/recv가 가능하다.

=== "C"

    ```c
    /* DEALER/ROUTER example */
    zlink_socket_monitor_open_options_t opts = {
        .events = ZLINK_EVENT_CONNECTION_READY_CHANGED
    };
    void *mon = zlink_socket_monitor_open(router, &opts);

    void on_ready(const zlink_monitor_event_t *ev, void *userdata) {
        if (ev->event & ZLINK_EVENT_CONNECTION_READY_CHANGED) {
            /* ROUTER: ev->routing_id contains the peer identity */
            /* routed send is possible now */
        }
    }
    zlink_socket_monitor_handler(mon, on_ready, NULL);
    ```

=== "C++"

    ```cpp
    auto mon = router.monitor_open({zlink::event::connection_ready_changed});
    mon.set_handler([](const zlink::monitor_event& ev) {
        if (ev.event() & zlink::event::connection_ready_changed) {
            // ROUTER: ev.routing_id() contains the peer identity
            // routed send is possible now
        }
    });
    ```

=== "Java"

    ```java
    var mon = router.monitorOpen(
        new MonitorOptions(MonitorEvent.CONNECTION_READY_CHANGED));
    mon.setHandler(ev -> {
        if ((ev.event() & MonitorEvent.CONNECTION_READY_CHANGED) != 0) {
            // ROUTER: ev.routingId() contains the peer identity
        }
    });
    ```

=== "Python"

    ```python
    mon = router.monitor_open(
        zlink.MonitorOptions(events=zlink.EVENT_CONNECTION_READY_CHANGED))
    def on_ready(ev):
        if ev.event & zlink.EVENT_CONNECTION_READY_CHANGED:
            # ROUTER: ev.routing_id contains the peer identity
            pass
    mon.set_handler(on_ready)
    ```

=== "Node/TypeScript"

    ```typescript
    const mon = router.monitorOpen({
        events: zlink.EVENT_CONNECTION_READY_CHANGED
    });
    mon.setHandler((ev) => {
        if (ev.event & zlink.EVENT_CONNECTION_READY_CHANGED) {
            // ROUTER: ev.routingId contains the peer identity
        }
    });
    ```

=== "C#/.NET"

    ```csharp
    using var mon = router.MonitorOpen(
        new MonitorOptions { Events = MonitorEvent.ConnectionReadyChanged });
    mon.SetHandler(ev => {
        if (ev.Event.HasFlag(MonitorEvent.ConnectionReadyChanged)) {
            // ROUTER: ev.RoutingId contains the peer identity
        }
    });
    ```

=== "Rust"

    ```rust
    let mon = router.monitor_open(
        &zlink::MonitorOptions::new(zlink::EVENT_CONNECTION_READY_CHANGED))?;
    mon.set_handler(|ev| {
        if ev.event() & zlink::EVENT_CONNECTION_READY_CHANGED != 0 {
            // ROUTER: ev.routing_id() contains the peer identity
        }
    });
    ```

| 패밀리 | 기다릴 이벤트 | 이후 가능한 동작 |
|---|---|---|
| PAIR | 양쪽 `CONNECTION_READY_CHANGED` | 양방향 send/recv |
| DEALER | `CONNECTION_READY_CHANGED` | send/recv |
| ROUTER | `CONNECTION_READY_CHANGED` | `ev->routing_id`로 routed send/recv |

### 11.2 Raw 소켓 — STREAM

STREAM은 ROUTER와 동일하게 동작한다 — routing_id는 TCP 연결이 수립되는
시점에 할당되며, 첫 payload 도착과 무관하다. `CONNECTION_READY_CHANGED`
이벤트가 routing_id와 함께 payload보다 먼저 발생한다. 순서:

1. 클라이언트가 raw TCP로 연결한다
2. 서버에서 `CONNECTION_READY_CHANGED` 이벤트를 받으며 `ev->routing_id` 확보
3. 해당 routing_id로 즉시 send 가능
4. 클라이언트의 payload(있는 경우)는 ready 이벤트 이후에 도착

=== "C"

    ```c
    /* STREAM server: CONNECTION_READY_CHANGED -> routing_id available -> send/recv */
    void on_ready(const zlink_monitor_event_t *ev, void *userdata) {
        if (ev->event & ZLINK_EVENT_CONNECTION_READY_CHANGED) {
            /* ev->routing_id contains the peer's routing_id */
            /* send to this peer immediately, or wait for inbound payload */
        }
    }

    zlink_socket_monitor_open_options_t opts = {
        .events = ZLINK_EVENT_CONNECTION_READY_CHANGED
    };
    void *mon = zlink_socket_monitor_open(stream_server, &opts);
    zlink_socket_monitor_handler(mon, on_ready, NULL);
    ```

=== "C++"

    ```cpp
    auto mon = stream_server.monitor_open({zlink::event::connection_ready_changed});
    mon.set_handler([](const zlink::monitor_event& ev) {
        if (ev.event() & zlink::event::connection_ready_changed) {
            // ev.routing_id() contains the peer's routing_id
        }
    });
    ```

=== "Java"

    ```java
    var mon = streamServer.monitorOpen(
        new MonitorOptions(MonitorEvent.CONNECTION_READY_CHANGED));
    mon.setHandler(ev -> {
        if ((ev.event() & MonitorEvent.CONNECTION_READY_CHANGED) != 0) {
            // ev.routingId() contains the peer's routing_id
        }
    });
    ```

=== "Python"

    ```python
    mon = stream_server.monitor_open(
        zlink.MonitorOptions(events=zlink.EVENT_CONNECTION_READY_CHANGED))
    def on_ready(ev):
        if ev.event & zlink.EVENT_CONNECTION_READY_CHANGED:
            # ev.routing_id contains the peer's routing_id
            pass
    mon.set_handler(on_ready)
    ```

=== "Node/TypeScript"

    ```typescript
    const mon = streamServer.monitorOpen({
        events: zlink.EVENT_CONNECTION_READY_CHANGED
    });
    mon.setHandler((ev) => {
        if (ev.event & zlink.EVENT_CONNECTION_READY_CHANGED) {
            // ev.routingId contains the peer's routing_id
        }
    });
    ```

=== "C#/.NET"

    ```csharp
    using var mon = streamServer.MonitorOpen(
        new MonitorOptions { Events = MonitorEvent.ConnectionReadyChanged });
    mon.SetHandler(ev => {
        if (ev.Event.HasFlag(MonitorEvent.ConnectionReadyChanged)) {
            // ev.RoutingId contains the peer's routing_id
        }
    });
    ```

=== "Rust"

    ```rust
    let mon = stream_server.monitor_open(
        &zlink::MonitorOptions::new(zlink::EVENT_CONNECTION_READY_CHANGED))?;
    mon.set_handler(|ev| {
        if ev.event() & zlink::EVENT_CONNECTION_READY_CHANGED != 0 {
            // ev.routing_id() contains the peer's routing_id
        }
    });
    ```

| 패밀리 | 기다릴 이벤트 | 이후 가능한 동작 |
|---|---|---|
| STREAM | `CONNECTION_READY_CHANGED` | `ev->routing_id`로 send/recv |

STREAM도 다른 raw 소켓과 동일하게
`CONNECTION_READY_CHANGED` 이벤트만으로 ready를 판정한다.

### 11.3 Raw 소켓 — PUB/SUB

raw PUB/SUB 소켓은 `CONNECTION_READY_CHANGED`가 아니라 **delivery-ready**
이벤트를 기다려야 한다. 연결만으로는 구독 전파가 완료되지 않기 때문이다.
PUB과 SUB 각각에 별도 모니터를 열어서 양쪽 모두 ready를
확인한 뒤 메시징한다.

- `SUB_DELIVERY_READY_CHANGED(value=1)` — subscription이 전파되어 수신 가능 (0/1 boolean)
- `PUB_DELIVERY_READY_CHANGED(value>0)` — ready subscriber 수 (absolute count). 기대하는 subscriber 수 이상인지 확인

=== "C"

    ```c
    zlink_set_subscription(sub, "topic");

    /* SUB monitor: wait for subscription propagation */
    zlink_socket_monitor_open_options_t sub_opts = {
        .events = ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED
    };
    void *sub_mon = zlink_socket_monitor_open(sub, &sub_opts);

    /* PUB monitor: wait for subscriber readiness */
    zlink_socket_monitor_open_options_t pub_opts = {
        .events = ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED
    };
    void *pub_mon = zlink_socket_monitor_open(pub, &pub_opts);

    /* Start messaging after both delivery-ready events */
    /* SUB_DELIVERY_READY_CHANGED(value=1) + PUB_DELIVERY_READY_CHANGED(value>=expected_subs) */
    zlink_publish(pub, NULL, &part, 1, 0);  /* raw PUB: topic_id is NULL */
    zlink_subscribe(sub, &parts, &count, 0, topic_buf, &topic_len);

    zlink_monitor_close(&pub_mon);
    zlink_monitor_close(&sub_mon);
    ```

=== "C++"

    ```cpp
    sub.set_subscription("topic");

    auto sub_mon = sub.monitor_open({zlink::event::sub_delivery_ready_changed});
    auto pub_mon = pub.monitor_open({zlink::event::pub_delivery_ready_changed});

    // Start messaging after both delivery-ready events
    pub.publish(nullptr, part, 1, 0);
    sub.subscribe(parts, count, 0, topic_buf, topic_len);

    pub_mon.close();
    sub_mon.close();
    ```

=== "Java"

    ```java
    sub.setSubscription("topic");

    var subMon = sub.monitorOpen(
        new MonitorOptions(MonitorEvent.SUB_DELIVERY_READY_CHANGED));
    var pubMon = pub.monitorOpen(
        new MonitorOptions(MonitorEvent.PUB_DELIVERY_READY_CHANGED));

    // Start messaging after both delivery-ready events
    pub.publish(null, part);
    sub.subscribe(topicBuf);

    pubMon.close();
    subMon.close();
    ```

=== "Python"

    ```python
    sub.set_subscription("topic")

    sub_mon = sub.monitor_open(
        zlink.MonitorOptions(events=zlink.EVENT_SUB_DELIVERY_READY_CHANGED))
    pub_mon = pub.monitor_open(
        zlink.MonitorOptions(events=zlink.EVENT_PUB_DELIVERY_READY_CHANGED))

    # Start messaging after both delivery-ready events
    pub.publish(None, part)
    sub.subscribe()

    pub_mon.close()
    sub_mon.close()
    ```

=== "Node/TypeScript"

    ```typescript
    sub.setSubscription("topic");

    const subMon = sub.monitorOpen({
        events: zlink.EVENT_SUB_DELIVERY_READY_CHANGED });
    const pubMon = pub.monitorOpen({
        events: zlink.EVENT_PUB_DELIVERY_READY_CHANGED });

    // Start messaging after both delivery-ready events
    pub.publish(null, part);
    sub.subscribe(topicBuf);

    pubMon.close();
    subMon.close();
    ```

=== "C#/.NET"

    ```csharp
    sub.SetSubscription("topic");

    using var subMon = sub.MonitorOpen(
        new MonitorOptions { Events = MonitorEvent.SubDeliveryReadyChanged });
    using var pubMon = pub.MonitorOpen(
        new MonitorOptions { Events = MonitorEvent.PubDeliveryReadyChanged });

    // Start messaging after both delivery-ready events
    pub.Publish(null, part);
    sub.Subscribe(topicBuf);
    ```

=== "Rust"

    ```rust
    sub.set_subscription("topic")?;

    let sub_mon = sub.monitor_open(
        &zlink::MonitorOptions::new(zlink::EVENT_SUB_DELIVERY_READY_CHANGED))?;
    let pub_mon = pub.monitor_open(
        &zlink::MonitorOptions::new(zlink::EVENT_PUB_DELIVERY_READY_CHANGED))?;

    // Start messaging after both delivery-ready events
    pub.publish(None, &part, 1, 0)?;
    sub.subscribe(&mut parts, &mut count, 0, &mut topic_buf, &mut topic_len)?;

    pub_mon.close();
    sub_mon.close();
    ```

| 패밀리 | 기다릴 이벤트 | 이후 가능한 동작 |
|---|---|---|
| PUB | `PUB_DELIVERY_READY_CHANGED(value>=expected_subs)` | `zlink_publish()` delivery |
| SUB | `SUB_DELIVERY_READY_CHANGED(value=1)` | `zlink_subscribe()` 수신 |

### 11.4 서비스 — SPOT

SPOT은 sub과 pub에 각각 별도 service monitor를 열어서 다른 이벤트를 구독한다.

=== "C"

    ```c
    /* Sub monitor: subscribe to SUB_DELIVERY_READY_CHANGED */
    zlink_service_monitor_open_options_t sub_opts = {
        .events = ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED
                  | ZLINK_MONITOR_EVENT_ERROR
    };
    void *sub_mon = zlink_service_monitor_open(sub_node, &sub_opts);

    /* Pub monitor: subscribe to PUB_FIRST_DELIVERY_READY_CHANGED */
    zlink_service_monitor_open_options_t pub_opts = {
        .events = ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED
                  | ZLINK_MONITOR_EVENT_ERROR
    };
    void *pub_mon = zlink_service_monitor_open(pub_node, &pub_opts);

    /* Start messaging after both are ready */
    ```

=== "C++"

    ```cpp
    auto sub_mon = sub_node.service_monitor_open(
        {zlink::spot_monitor_event::sub_delivery_ready_changed
         | zlink::monitor_event::error});
    auto pub_mon = pub_node.service_monitor_open(
        {zlink::spot_monitor_event::pub_first_delivery_ready_changed
         | zlink::monitor_event::error});

    // Start messaging after both are ready
    ```

=== "Java"

    ```java
    var subMon = subNode.serviceMonitorOpen(new ServiceMonitorOptions(
        SpotMonitorEvent.SUB_DELIVERY_READY_CHANGED | MonitorEvent.ERROR));
    var pubMon = pubNode.serviceMonitorOpen(new ServiceMonitorOptions(
        SpotMonitorEvent.PUB_FIRST_DELIVERY_READY_CHANGED | MonitorEvent.ERROR));

    // Start messaging after both are ready
    ```

=== "Python"

    ```python
    sub_mon = sub_node.service_monitor_open(zlink.ServiceMonitorOptions(
        events=zlink.SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED
               | zlink.MONITOR_EVENT_ERROR))
    pub_mon = pub_node.service_monitor_open(zlink.ServiceMonitorOptions(
        events=zlink.SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED
               | zlink.MONITOR_EVENT_ERROR))

    # Start messaging after both are ready
    ```

=== "Node/TypeScript"

    ```typescript
    const subMon = subNode.serviceMonitorOpen({
        events: zlink.SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED
              | zlink.MONITOR_EVENT_ERROR
    });
    const pubMon = pubNode.serviceMonitorOpen({
        events: zlink.SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED
              | zlink.MONITOR_EVENT_ERROR
    });

    // Start messaging after both are ready
    ```

=== "C#/.NET"

    ```csharp
    using var subMon = subNode.ServiceMonitorOpen(new ServiceMonitorOptions {
        Events = SpotMonitorEvent.SubDeliveryReadyChanged | MonitorEvent.Error });
    using var pubMon = pubNode.ServiceMonitorOpen(new ServiceMonitorOptions {
        Events = SpotMonitorEvent.PubFirstDeliveryReadyChanged | MonitorEvent.Error });

    // Start messaging after both are ready
    ```

=== "Rust"

    ```rust
    let sub_mon = sub_node.service_monitor_open(&zlink::ServiceMonitorOptions::new(
        zlink::SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED
        | zlink::MONITOR_EVENT_ERROR))?;
    let pub_mon = pub_node.service_monitor_open(&zlink::ServiceMonitorOptions::new(
        zlink::SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED
        | zlink::MONITOR_EVENT_ERROR))?;

    // Start messaging after both are ready
    ```

| 서비스 | 기다릴 이벤트 | 이후 가능한 동작 |
|---|---|---|
| SPOT sub | `SUB_DELIVERY_READY_CHANGED` | `zlink_subscribe()` 수신 시작 |
| SPOT pub | `PUB_FIRST_DELIVERY_READY_CHANGED` | `zlink_publish()` delivery 시작 |

snapshot/status 조회는 운영 관찰/디버깅용이며, 메시징 시작 판정에는
위 이벤트를 사용한다.

### 11.5 Snapshot

`zlink_monitor_snapshot()`과 `zlink_*_status_snapshot()`은
현재 상태를 조회하는 용도다. 운영 대시보드, health check, 디버깅에 활용한다.

=== "C"

    ```c
    /* Check current discovery health */
    zlink_discovery_status_t status;
    zlink_discovery_status_snapshot(discovery, &status);
    printf("state=%d\n", status.state);
    ```

=== "C++"

    ```cpp
    auto status = discovery.status_snapshot();
    std::println("state={}", status.state);
    ```

=== "Java"

    ```java
    var status = discovery.statusSnapshot();
    System.out.printf("state=%d%n", status.state());
    ```

=== "Python"

    ```python
    status = discovery.status_snapshot()
    print(f"state={status.state}")
    ```

=== "Node/TypeScript"

    ```typescript
    const status = discovery.statusSnapshot();
    console.log(`state=${status.state}`);
    ```

=== "C#/.NET"

    ```csharp
    var status = discovery.StatusSnapshot();
    Console.WriteLine($"state={status.State}");
    ```

=== "Rust"

    ```rust
    let status = discovery.status_snapshot()?;
    println!("state={}", status.state);
    ```

---
[← TLS 보안](05-tls-security.ko.md) | [서비스 개요 →](07-0-services.ko.md)
