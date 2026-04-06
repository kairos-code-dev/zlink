# 스레드 안전성 가이드

## 1. 한줄 요약

**네, zlink 핸들은 thread-safe입니다.** 소켓, SPOT, Discovery
핸들 하나를 여러 스레드에서 공유하면서 별도의 mutex나 lock 없이 API를
호출할 수 있습니다.

```
  Thread A --- zlink_send(socket, ...) ---+
  Thread B --- zlink_send(socket, ...) ---+--> same socket, no mutex needed
  Thread C --- zlink_send(socket, ...) ---+
```

**유일하게 thread-safe가 아닌 것:** `zlink_msg_t`. 메시지 객체는 한 번에
한 스레드에서만 사용해야 합니다
([5절](#5-유일한-예외-zlink_msg_t는-thread-safe가-아닙니다) 참고).

이 가이드의 나머지 부분에서는 `send`와 `connect`를 함께 쓸 때 어떤 일이
벌어지는지, `close`가 다른 스레드에서 아직 전송 중일 때 어떻게 동작하는지,
콜백에서 주의할 점은 무엇인지 설명합니다.

## 2. 세 가지 API 카테고리

모든 공개 API는 세 가지 카테고리로 나뉩니다:

| 카테고리 | 포함 API | Thread-safe? | 참고 |
|---|---|---|---|
| **전송** | `send`, `publish`, `send_rid` | 예 -- 동시 호출 가능 | 처리량 최적화 빠른 경로 |
| **설정·운영** | `bind`, `connect`, `set_option` 등 | 예 -- 순차 처리 | 메시지마다 호출은 비권장 |
| **정리** | `close`, `destroy` | 예 -- 명확한 에러 코드 | 사용 중이면 `EBUSY` 반환 |

**요약하면:** `send`는 어느 스레드에서든 마음껏 호출하세요. 연결 추가,
구독 변경, 옵션 변경도 전송 중에 자유롭게 할 수 있습니다. 다 끝나면
핸들을 닫고 반환 코드를 확인하세요.

### 2.1 전송 (빠른 경로)

다음 함수들은 같은 핸들에서 완전한 동시 호출을 허용합니다:

- `zlink_send()` — 원시 소켓
- `zlink_publish()` — SPOT
- 콜백 내에서 `send` / `publish` 호출도 안전

**순서:**
- 하나의 스레드에서는 `send`를 호출한 순서대로 메시지가 전달됩니다.
- 여러 스레드에서 동시에 보낼 때는 각 메시지가 온전하게 전달되지만,
  스레드 간 순서는 보장되지 않습니다 (먼저 도착한 스레드가 먼저 전송).

**예제 — 4개 워커 스레드, 1개 소켓 공유:**

=== "C"

    ```c
    #include <zlink.h>
    #include <pthread.h>
    #include <string.h>

    typedef struct { void *socket; int id; } worker_arg_t;

    void *worker(void *arg)
    {
        worker_arg_t *w = (worker_arg_t *)arg;
        char buf[64];
        for (int i = 0; i < 10000; i++) {
            int len = snprintf(buf, sizeof(buf), "worker-%d msg-%d", w->id, i);
            zlink_send(w->socket, buf, len, 0);  /* no mutex needed */
        }
        return NULL;
    }

    int main(void)
    {
        void *ctx = zlink_ctx_new();
        void *socket = zlink_socket(ctx, ZLINK_DEALER);
        zlink_connect(socket, "tcp://127.0.0.1:5555");

        pthread_t threads[4];
        worker_arg_t args[4];
        for (int i = 0; i < 4; i++) {
            args[i] = (worker_arg_t){socket, i};
            pthread_create(&threads[i], NULL, worker, &args[i]);
        }
        for (int i = 0; i < 4; i++)
            pthread_join(threads[i], NULL);

        zlink_close(socket);
        zlink_ctx_term(ctx);
        return 0;
    }
    ```

=== "C++"

    ```cpp
    #include <zlink/context.hpp>
    #include <zlink/socket_types.hpp>
    #include <thread>
    #include <cstdio>

    void worker(zlink::dealer_socket_t &socket, int id)
    {
        char buf[64];
        for (int i = 0; i < 10000; i++) {
            int len = std::snprintf(buf, sizeof(buf), "worker-%d msg-%d", id, i);
            zlink::message_t msg(static_cast<size_t>(len));
            std::memcpy(msg.data(), buf, len);
            socket.send(msg);  // no mutex needed
        }
    }

    int main()
    {
        zlink::context_t ctx;
        zlink::dealer_socket_t socket(ctx);
        socket.connect("tcp://127.0.0.1:5555");

        std::thread threads[4];
        for (int i = 0; i < 4; i++)
            threads[i] = std::thread(worker, std::ref(socket), i);
        for (auto &t : threads)
            t.join();

        socket.close();
        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class WorkerPool {
        public static void main(String[] args) throws Exception {
            try (Context ctx = new Context()) {
                DealerSocket socket = new DealerSocket(ctx);
                socket.connect("tcp://127.0.0.1:5555");

                Thread[] threads = new Thread[4];
                for (int i = 0; i < 4; i++) {
                    final int id = i;
                    threads[i] = new Thread(() -> {
                        for (int j = 0; j < 10000; j++) {
                            byte[] data = String.format("worker-%d msg-%d", id, j).getBytes();
                            socket.send(Message.copyOf(data));  // no mutex needed
                        }
                    });
                    threads[i].start();
                }
                for (Thread t : threads) t.join();
                socket.close();
            }
        }
    }
    ```

=== "Python"

    ```python
    import threading
    import zlink

    def worker(socket, worker_id):
        for i in range(10000):
            socket.send(f"worker-{worker_id} msg-{i}".encode())  # no lock needed

    ctx = zlink.Context()
    socket = zlink.DealerSocket(ctx)
    socket.connect("tcp://127.0.0.1:5555")

    threads = [threading.Thread(target=worker, args=(socket, i)) for i in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    socket.close()
    ctx.close()
    ```

=== "Node/TypeScript"

    ```typescript
    // Node.js runs on a single-threaded event loop, so concurrent
    // send() calls from the main thread are naturally serialized.
    // For true parallelism, use worker_threads — each worker gets
    // its own socket (handles cannot cross thread boundaries in V8).
    import { Context, DealerSocket, Message } from 'zlink';

    const ctx = new Context();
    const socket = new DealerSocket(ctx);
    socket.connect('tcp://127.0.0.1:5555');

    for (let id = 0; id < 4; id++) {
        for (let i = 0; i < 10000; i++) {
            socket.send(Buffer.from(`worker-${id} msg-${i}`));
        }
    }

    socket.close();
    ctx.close();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new Context();
    var socket = new DealerSocket(ctx);
    socket.Connect("tcp://127.0.0.1:5555");

    var tasks = new Task[4];
    for (int i = 0; i < 4; i++)
    {
        int id = i;
        tasks[i] = Task.Run(() =>
        {
            for (int j = 0; j < 10000; j++)
            {
                var msg = new Message($"worker-{id} msg-{j}"u8.ToArray());
                socket.Send(msg);  // no lock needed
            }
        });
    }
    Task.WaitAll(tasks);

    socket.Close();
    ```

=== "Rust"

    ```rust
    use zlink::{Context, DealerSocket, Message};
    use std::thread;

    fn main() -> Result<(), zlink::ZlinkError> {
        let ctx = Context::new()?;
        let socket = DealerSocket::new(&ctx)?;
        socket.connect("tcp://127.0.0.1:5555")?;

        let handle = socket.send_handle();  // cloneable, lightweight
        let threads: Vec<_> = (0..4).map(|id| {
            let h = handle.clone();
            thread::spawn(move || {
                for i in 0..10000 {
                    let data = format!("worker-{id} msg-{i}");
                    h.send(Message::from(data.as_bytes())).unwrap();
                }
            })
        }).collect();

        for t in threads { t.join().unwrap(); }
        socket.close()?;
        Ok(())
    }
    ```

=== "Go"

    ```go
    package main

    import (
        "fmt"
        "sync"
        "github.com/kairoscode/zlink"
    )

    func main() {
        ctx, _ := zlink.NewContext()
        socket, _ := ctx.DealerSocket()
        socket.Connect("tcp://127.0.0.1:5555")

        var wg sync.WaitGroup
        for id := 0; id < 4; id++ {
            wg.Add(1)
            go func(id int) {
                defer wg.Done()
                for i := 0; i < 10000; i++ {
                    msg := zlink.NewMessageFromBytes([]byte(fmt.Sprintf("worker-%d msg-%d", id, i)))
                    socket.Send(msg)  // no lock needed
                }
            }(id)
        }
        wg.Wait()

        socket.Close()
        ctx.Close()
    }
    ```

### 2.2 설정·운영 (설정 및 런타임 변경)

이 함수들은 thread-safe하며, 라이브러리가 한 번에 하나씩 처리합니다.
가끔 하는 작업(새 엔드포인트 연결, 옵션 변경)에는 문제 없지만,
메시지마다 호출하는 건 피하세요.

포함:
- `zlink_bind()` / `zlink_connect()` / `zlink_disconnect()`
- `zlink_set_option()` / `zlink_get_option()`
- `zlink_set_subscription()` / `zlink_unset_subscription()`
- `zlink_spot_node_attach_discovery()`
- `zlink_socket_monitor_open()` / `zlink_service_monitor_open()`
- `zlink_send_ready_handler()`
- `zlink_set_option()`
- `zlink_registry_add_peer()` / `zlink_registry_set_heartbeat()`
- 조회/스냅샷 함수

**전송과 설정을 자유롭게 섞을 수 있습니다.** 예를 들어, 한 스레드가
메시지를 보내는 동안 다른 스레드가 추가 엔드포인트를 연결할 수 있습니다:

=== "C"

    ```c
    void *send_thread(void *arg)
    {
        void *socket = arg;
        char buf[] = "data";
        for (int i = 0; i < 100000; i++)
            zlink_msg_t part;
            zlink_msg_init_size(&part, sizeof(buf) - 1);
            memcpy(zlink_msg_data(&part), buf, sizeof(buf) - 1);
            zlink_send(socket, &part, 1, 0);  /* hot path */
        return NULL;
    }

    void *setup_thread(void *arg)
    {
        void *socket = arg;
        /* These are safe to call while send_thread is running */
        zlink_connect(socket, "tcp://10.0.0.2:5555");
        zlink_connect(socket, "tcp://10.0.0.3:5555");

        int hwm = 5000;
        zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));
        return NULL;
    }
    ```

=== "C++"

    ```cpp
    void send_thread(zlink::dealer_socket_t &socket)
    {
        for (int i = 0; i < 100000; i++) {
            zlink::message_t part(4);
            std::memcpy(part.data(), "data", 4);
            socket.send(part);  // hot path
        }
    }

    void setup_thread(zlink::dealer_socket_t &socket)
    {
        // Safe to call while send_thread is running
        socket.connect("tcp://10.0.0.2:5555");
        socket.connect("tcp://10.0.0.3:5555");

        int hwm = 5000;
        socket.set_option(zlink::socket_option_key::sndhwm, hwm);
    }
    ```

=== "Java"

    ```java
    Thread sender = new Thread(() -> {
        for (int i = 0; i < 100000; i++)
            socket.send(Message.copyOf("data".getBytes()));  // hot path
    });

    Thread setup = new Thread(() -> {
        // Safe to call while sender is running
        socket.connect("tcp://10.0.0.2:5555");
        socket.connect("tcp://10.0.0.3:5555");
        socket.options().sendHwm(5000);
    });
    ```

=== "Python"

    ```python
    def send_thread(socket):
        for _ in range(100000):
            socket.send(b"data")  # hot path

    def setup_thread(socket):
        # Safe to call while send_thread is running
        socket.connect("tcp://10.0.0.2:5555")
        socket.connect("tcp://10.0.0.3:5555")
        socket.options.send_hwm = 5000
    ```

=== "Node/TypeScript"

    ```typescript
    // In Node.js, the single-threaded event loop means send()
    // and connect() calls are already serialized. No concurrency
    // conflict can occur within one thread.
    socket.connect('tcp://10.0.0.2:5555');
    socket.connect('tcp://10.0.0.3:5555');
    socket.options.sendHwm = 5000;

    for (let i = 0; i < 100000; i++) {
        socket.send(Buffer.from('data'));
    }
    ```

=== "C#/.NET"

    ```csharp
    var sender = Task.Run(() =>
    {
        for (int i = 0; i < 100000; i++)
            socket.Send(new Message("data"u8));  // hot path
    });

    var setup = Task.Run(() =>
    {
        // Safe to call while sender is running
        socket.Connect("tcp://10.0.0.2:5555");
        socket.Connect("tcp://10.0.0.3:5555");
        socket.CommonOptions.SendHwm = 5000;
    });
    ```

=== "Rust"

    ```rust
    let handle = socket.send_handle();
    let sender = std::thread::spawn(move || {
        for _ in 0..100_000 {
            handle.send(Message::from(b"data".as_slice())).unwrap();
        }
    });

    // Safe to call while sender is running
    socket.connect("tcp://10.0.0.2:5555")?;
    socket.connect("tcp://10.0.0.3:5555")?;
    socket.common_options().set_send_hwm(5000)?;
    ```

=== "Go"

    ```go
    go func() {
        for i := 0; i < 100000; i++ {
            msg := zlink.NewMessageFromBytes([]byte("data"))
            socket.Send(msg)  // hot path
        }
    }()

    // Safe to call while goroutine is sending
    socket.Connect("tcp://10.0.0.2:5555")
    socket.Connect("tcp://10.0.0.3:5555")
    socket.SetSendHWM(5000)
    ```

`ZLINK_OPT_EVENTS`, `ZLINK_OPT_LAST_ENDPOINT` 같은 경량 읽기도 이 카테고리에
속하지만, 무거운 조회/스냅샷 호출보다 오버헤드가 적습니다.

## 3. 핸들별 요약표

모든 핸들 타입이 동일한 세 가지 카테고리 모델을 따릅니다:

| 핸들 | 전송 | 설정·운영 | 정리 |
|---|---|---|---|
| Socket | `send` | bind, connect, set_option 등 | `close` |
| SPOT | `publish` | subscribe, unsubscribe 등 | `destroy` |
| SPOT Node | `publish` | bind, connect_peer 등 | `destroy` |
| Discovery | *(없음)* | connect_registry 등 | `destroy` |
| Registry | *(없음)* | bind, add_peer 등 | `destroy` |

## 4. 핸들 안전하게 닫기

다른 스레드가 아직 사용 중인 핸들을 닫아도 크래시하지 않습니다 —
zlink가 명확한 에러 코드를 반환합니다:

| 상황 | 동작 | 에러 코드 |
|---|---|---|
| 다른 스레드가 핸들 사용 중 `close` 호출 | 닫기 **거부** | `EBUSY` |
| `close` 수락 후 API 호출 | 호출 **거부** | `ESHUTDOWN` |
| `close`/`destroy` 두 번 호출 | 즉시 반환 | `EALREADY` |

`EBUSY` 이후에는 핸들이 정상 상태로 돌아갑니다 — 아무것도 손상되지
않았으며, 계속 사용하거나 나중에 다시 닫을 수 있습니다.

**권장 종료 패턴:**

=== "C"

    ```c
    #include <zlink.h>
    #include <errno.h>
    #include <stdatomic.h>

    atomic_int g_running = 1;

    /* Worker threads check g_running and also handle ESHUTDOWN */
    void *sender(void *arg)
    {
        void *socket = arg;
        while (atomic_load(&g_running)) {
            zlink_msg_t part;
            zlink_msg_init_size(&part, 32);
            int rc = zlink_send(socket, &part, 1, 0);
            if (rc == -1 && zlink_errno() == ESHUTDOWN)
                break;  /* handle is shutting down, stop gracefully */
        }
        return NULL;
    }

    void shutdown_socket(void *socket)
    {
        /* Step 1: tell workers to stop */
        atomic_store(&g_running, 0);

        /* Step 2: give workers a moment to finish, then close */
        msleep(50);
        zlink_close(socket);
    }
    ```

=== "C++"

    ```cpp
    #include <zlink/socket_types.hpp>
    #include <atomic>
    #include <thread>
    #include <cerrno>

    std::atomic<bool> g_running{true};

    void sender(zlink::dealer_socket_t &socket)
    {
        while (g_running.load()) {
            zlink::message_t part(32);
            try {
                socket.send(part);
            } catch (const zlink::error_t &e) {
                if (e.num() == ESHUTDOWN)
                    break;  // handle is shutting down
                throw;
            }
        }
    }

    void shutdown_socket(zlink::dealer_socket_t &socket)
    {
        g_running.store(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        socket.close();
    }
    ```

=== "Java"

    ```java
    import java.util.concurrent.atomic.AtomicBoolean;

    AtomicBoolean running = new AtomicBoolean(true);

    Thread sender = new Thread(() -> {
        while (running.get()) {
            try {
                socket.send(new Message(32));
            } catch (ZlinkException e) {
                if (e.errorCode() == ErrorCode.ESHUTDOWN)
                    break;  // handle is shutting down
                throw e;
            }
        }
    });

    // Shutdown
    running.set(false);
    Thread.sleep(50);
    socket.close();
    ```

=== "Python"

    ```python
    import threading

    running = threading.Event()
    running.set()

    def sender(socket):
        while running.is_set():
            try:
                socket.send(bytes(32))
            except zlink.ZlinkError as e:
                if e.errno == errno.ESHUTDOWN:
                    break  # handle is shutting down
                raise

    # Shutdown
    running.clear()
    time.sleep(0.05)
    socket.close()
    ```

=== "Node/TypeScript"

    ```typescript
    // Node.js is single-threaded — shutdown is straightforward.
    // Just stop sending and close the socket.
    let running = true;

    function sendLoop() {
        if (!running) return;
        socket.send(Buffer.alloc(32));
        setImmediate(sendLoop);
    }

    // Shutdown
    running = false;
    socket.close();
    ctx.close();
    ```

=== "C#/.NET"

    ```csharp
    using var cts = new CancellationTokenSource();

    var sender = Task.Run(() =>
    {
        while (!cts.Token.IsCancellationRequested)
        {
            try {
                socket.Send(new Message(32));
            } catch (ZlinkException e) when (e.ErrorCode == ErrorCode.Shutdown) {
                break;  // handle is shutting down
            }
        }
    });

    // Shutdown
    cts.Cancel();
    await Task.Delay(50);
    socket.Close();
    ```

=== "Rust"

    ```rust
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;

    let running = Arc::new(AtomicBool::new(true));

    let flag = running.clone();
    let handle = socket.send_handle();
    let sender = std::thread::spawn(move || {
        while flag.load(Ordering::Relaxed) {
            if let Err(e) = handle.send(Message::new(32)) {
                if e.is_shutdown() { break; }
            }
        }
    });

    // Shutdown
    running.store(false, Ordering::Relaxed);
    std::thread::sleep(std::time::Duration::from_millis(50));
    socket.close()?;
    ```

=== "Go"

    ```go
    import "sync/atomic"

    var running int32 = 1

    go func() {
        for atomic.LoadInt32(&running) == 1 {
            msg := zlink.NewMessage(32)
            err := socket.Send(msg)
            if err != nil && zlink.IsShutdown(err) {
                break  // handle is shutting down
            }
        }
    }()

    // Shutdown
    atomic.StoreInt32(&running, 0)
    time.Sleep(50 * time.Millisecond)
    socket.Close()
    ```

**콜백에서의 self-close:** send-ready 또는 monitor 콜백에서 자기 핸들의
`close`를 호출하면, 실제 닫기는 콜백이 반환될 때까지 지연됩니다.
콜백 내 use-after-free를 방지합니다.

## 5. 유일한 예외: zlink_msg_t는 Thread-Safe가 아닙니다

핸들은 thread-safe합니다. 메시지 객체는 아닙니다. 각 `zlink_msg_t`는
한 번에 하나의 스레드에서만 사용해야 합니다.

이는 보통 자연스럽습니다 — 각 스레드에서 스택이나 힙에 메시지를 만들면
됩니다:

=== "C"

    ```c
    /* WRONG — two threads sharing the same msg */
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 100);
    /* Thread A: */ zlink_send_msg(socket, &msg, 0);
    /* Thread B: */ zlink_send_msg(socket, &msg, 0);  /* data race! */
    ```

=== "C++"

    ```cpp
    /* WRONG — two threads sharing the same message_t */
    zlink::message_t msg(100);
    // Thread A:
    socket.send(msg);
    // Thread B:
    socket.send(msg);  // data race!
    ```

=== "Java"

    ```java
    /* WRONG — two threads sharing the same Message */
    Message msg = new Message(100);
    // Thread A:
    socket.send(msg);
    // Thread B:
    socket.send(msg);  // data race!
    ```

=== "Python"

    ```python
    # WRONG — two threads sharing the same Message
    msg = zlink.Message(100)
    # Thread A:
    socket.send(msg)
    # Thread B:
    socket.send(msg)  # data race!
    ```

=== "Node/TypeScript"

    ```typescript
    // Node.js is single-threaded, so this scenario does not
    // arise in practice. If using worker_threads, each worker
    // must create its own Message — Message objects cannot be
    // shared across V8 isolates.
    ```

=== "C#/.NET"

    ```csharp
    /* WRONG — two threads sharing the same Message */
    var msg = new Message(100);
    // Thread A:
    socket.Send(msg);
    // Thread B:
    socket.Send(msg);  // data race!
    ```

=== "Rust"

    ```rust
    // Rust prevents this at compile time — Message does not
    // implement Clone or Copy, and send() consumes the message.
    // let msg = Message::new(100);
    // socket.send(msg)?;      // msg moved here
    // socket.send(msg)?;      // compile error: use of moved value
    ```

=== "Go"

    ```go
    /* WRONG — two goroutines sharing the same Message */
    msg := zlink.NewMessage(100)
    // Goroutine A:
    socket.Send(msg)
    // Goroutine B:
    socket.Send(msg)  // data race!
    ```

=== "C"

    ```c
    /* RIGHT — each thread makes its own msg */
    /* Thread A */                       /* Thread B */
    zlink_msg_t msg_a;                   zlink_msg_t msg_b;
    zlink_msg_init_size(&msg_a, 100);    zlink_msg_init_size(&msg_b, 100);
    memcpy(zlink_msg_data(&msg_a),...);  memcpy(zlink_msg_data(&msg_b),...);
    zlink_send_msg(socket, &msg_a, 0);   zlink_send_msg(socket, &msg_b, 0);  /* safe */
    ```

=== "C++"

    ```cpp
    /* RIGHT — each thread makes its own message_t */
    // Thread A                          // Thread B
    zlink::message_t msg_a(100);         zlink::message_t msg_b(100);
    std::memcpy(msg_a.data(), ...);      std::memcpy(msg_b.data(), ...);
    socket.send(msg_a);                  socket.send(msg_b);  // safe
    ```

=== "Java"

    ```java
    /* RIGHT — each thread makes its own Message */
    // Thread A                           // Thread B
    Message msgA = new Message(100);      Message msgB = new Message(100);
    // ... fill msgA ...                  // ... fill msgB ...
    socket.send(msgA);                    socket.send(msgB);  // safe
    ```

=== "Python"

    ```python
    # RIGHT — each thread makes its own message
    # Thread A                            # Thread B
    msg_a = zlink.Message(100)            msg_b = zlink.Message(100)
    socket.send(msg_a)                    socket.send(msg_b)  # safe
    ```

=== "Node/TypeScript"

    ```typescript
    // In Node.js each Buffer is its own allocation — just create
    // separate buffers and send them. No sharing issue arises.
    socket.send(Buffer.alloc(100));  // each call owns its buffer
    ```

=== "C#/.NET"

    ```csharp
    /* RIGHT — each thread makes its own Message */
    // Thread A                           // Thread B
    var msgA = new Message(100);          var msgB = new Message(100);
    socket.Send(msgA);                    socket.Send(msgB);  // safe
    ```

=== "Rust"

    ```rust
    // RIGHT — each thread creates its own Message.
    // Rust enforces this: send() consumes the message, so you
    // must create a fresh one for each send call.
    // Thread A                           // Thread B
    let msg_a = Message::new(100);        let msg_b = Message::new(100);
    socket.send(msg_a)?;                  socket.send(msg_b)?;  // safe
    ```

=== "Go"

    ```go
    /* RIGHT — each goroutine makes its own Message */
    // Goroutine A                        // Goroutine B
    msgA := zlink.NewMessage(100)         msgB := zlink.NewMessage(100)
    socket.Send(msgA)                     socket.Send(msgB)  // safe
    ```

!!! note "C API definition -- each binding wraps this into its idiomatic type."

    ```c
    typedef struct { void *socket; int id; } socket_worker_t;

    void *socket_worker(void *arg)
    {
        socket_worker_t *w = (socket_worker_t *)arg;
        for (int i = 0; i < 50000; i++) {
            zlink_msg_t part;
            zlink_msg_init_size(&part, 64);
            snprintf(zlink_msg_data(&part), 64, "worker-%d-%d", w->id, i);
            zlink_send(w->socket, &part, 1, 0);
        }
        return NULL;
    }

    void run_socket_pool(void *socket)
    {
        pthread_t threads[4];
        socket_worker_t args[4];
        for (int i = 0; i < 4; i++) {
            args[i] = (socket_worker_t){socket, i};
            pthread_create(&threads[i], NULL, socket_worker, &args[i]);
        }
        for (int i = 0; i < 4; i++)
            pthread_join(threads[i], NULL);
    }
    ```

**콜백 소유권:** 콜백이 `zlink_msg_t *parts`를 받으면 소유권이 콜백으로
넘어갑니다. 반환 전에 각 part를 `zlink_msg_close()`하고, 다른 스레드에서
접근하면 안 됩니다.

## 6. 콜백 규칙

모든 콜백(메시지, SPOT, XPUB, 모니터, send-ready)은 I/O 스레드에서
실행됩니다. 알아야 할 것:

**콜백에서 할 수 있는 것:**
- 같은 핸들로 `send` / `publish` 호출 — 이것이 권장되는
  request-reply 패턴입니다.
- 메시지 데이터를 읽고 자체 큐에 넣기.

**콜백에서 하면 안 되는 것:**
- **블로킹** (sleep, lock, 무거운 연산) — 해당 스레드의 모든 I/O가
  멈춥니다. 작업을 큐에 넣고 워커 스레드에서 처리하세요.
- **send-ready 콜백 안에서 자기 핸들러 교체** — `EDEADLK`를
  반환합니다.

**오프로드 패턴 — 콜백을 빠르게 유지:**

=== "C"

    ```c
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        for (size_t i = 0; i < part_count; i++) {
            /* Push to your own thread-safe queue */
            app_queue_push(app_queue,
                           zlink_msg_data(&parts[i]),
                           zlink_msg_size(&parts[i]));
            zlink_msg_close(&parts[i]);
        }
        /* Return quickly — a worker thread processes the queue */
    }
    ```

=== "C++"

    ```cpp
    // Assuming app_queue is a thread-safe queue (e.g., from your application)
    socket.on_receive([](const zlink::routing_id_t &source_rid,
                         std::vector<zlink::message_t> &parts) {
        for (auto &part : parts) {
            // Copy data and push to your own thread-safe queue
            app_queue.push(std::string(
                static_cast<const char *>(part.data()), part.size()));
        }
        // Return quickly — a worker thread processes the queue
    });
    ```

=== "Java"

    ```java
    BlockingQueue<byte[]> queue = new LinkedBlockingQueue<>();

    socket.onReceive((routingId, parts) -> {
        for (Message part : parts) {
            // Copy data and push to your own thread-safe queue
            queue.offer(part.toByteArray());
            part.close();
        }
        // Return quickly — a worker thread processes the queue
    });
    ```

=== "Python"

    ```python
    import queue

    work_queue = queue.Queue()

    def on_message(received):
        for part in received.parts:
            # Copy data and push to your own thread-safe queue
            work_queue.put(bytes(part))
            part.close()
        # Return quickly — a worker thread processes the queue

    socket.on_receive(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    // Node callbacks run on the event loop — avoid blocking.
    const workQueue: Buffer[] = [];

    socket.onReceive((routingId, parts) => {
        for (const part of parts) {
            // Copy data to your own queue
            workQueue.push(Buffer.from(part.data));
        }
        // Return quickly — process the queue asynchronously
    });
    ```

=== "C#/.NET"

    ```csharp
    var queue = new ConcurrentQueue<byte[]>();

    socket.OnReceive((routingId, parts) =>
    {
        foreach (var part in parts)
        {
            // Copy data and push to your own thread-safe queue
            queue.Enqueue(part.ToArray());
            part.Dispose();
        }
        // Return quickly — a worker thread processes the queue
    });
    ```

=== "Rust"

    ```rust
    use std::sync::mpsc;

    let (tx, rx) = mpsc::channel::<Vec<u8>>();

    socket.on_receive(move |received| {
        for part in &received.parts {
            // Copy data and push to channel
            let _ = tx.send(part.as_slice().to_vec());
        }
        // Return quickly — a worker thread receives from rx
    })?;
    ```

=== "Go"

    ```go
    workCh := make(chan []byte, 1024)

    socket.OnReceive(func(received *zlink.Received) {
        for _, part := range received.Parts {
            // Copy data and push to channel
            data := make([]byte, part.Size())
            copy(data, part.Data())
            workCh <- data
            part.Close()
        }
        // Return quickly — a goroutine processes the channel
    })
    ```

## 7. 실용 패턴

### 7.1 멀티 스레드 워커 풀 (Socket)

여러 스레드가 하나의 소켓을 통해 전송 — 잠금 불필요:

!!! note "C API definition -- each binding wraps this into its idiomatic type."

    ```c
    typedef struct { void *socket; int id; } socket_worker_t;

    void *socket_worker(void *arg)
    {
        socket_worker_t *w = (socket_worker_t *)arg;
        for (int i = 0; i < 50000; i++) {
            zlink_msg_t part;
            zlink_msg_init_size(&part, 64);
            snprintf(zlink_msg_data(&part), 64, "worker-%d-%d", w->id, i);
            zlink_send(w->socket, &part, 1, 0);
        }
        return NULL;
    }

    void run_socket_pool(void *socket)
    {
        pthread_t threads[4];
        socket_worker_t args[4];
        for (int i = 0; i < 4; i++) {
            args[i] = (socket_worker_t){socket, i};
            pthread_create(&threads[i], NULL, socket_worker, &args[i]);
        }
        for (int i = 0; i < 4; i++)
            pthread_join(threads[i], NULL);
    }
    ```

### 7.2 전송 중 구독 변경

한 스레드가 발행하고, 다른 스레드가 런타임에 구독을 관리:

=== "C"

    ```c
    /* Data thread: publishes at high frequency */
    void *publisher(void *arg)
    {
        void *spot = arg;
        for (int i = 0; i < 100000; i++) {
            zlink_msg_t part;
            zlink_msg_init_size(&part, 16);
            zlink_publish(spot, "prices", &part, 1, 0);
        }
        return NULL;
    }

    /* Control thread: adjusts subscriptions while publisher is running */
    void *control(void *arg)
    {
        void *spot = arg;
        msleep(100);
        zlink_set_subscription(spot, "audit.*");      /* safe while publishing */
        msleep(200);
        zlink_unset_subscription(spot, "audit.*");    /* also safe */
        return NULL;
    }
    ```

=== "C++"

    ```cpp
    void publisher(zlink::service::spot_t &spot)
    {
        for (int i = 0; i < 100000; i++) {
            zlink::message_t part(16);
            spot.publish("prices", part);
        }
    }

    void control(zlink::service::spot_t &spot)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        spot.set_subscription("audit.*");      // safe while publishing
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        spot.unset_subscription("audit.*");    // also safe
    }
    ```

=== "Java"

    ```java
    Thread publisher = new Thread(() -> {
        for (int i = 0; i < 100000; i++)
            spot.publish("prices", new Message(16));
    });

    Thread control = new Thread(() -> {
        Thread.sleep(100);
        spot.setSubscription("audit.*");      // safe while publishing
        Thread.sleep(200);
        spot.unsetSubscription("audit.*");    // also safe
    });
    ```

=== "Python"

    ```python
    def publisher(spot):
        for _ in range(100000):
            spot.publish("prices", bytes(16))

    def control(spot):
        time.sleep(0.1)
        spot.set_subscription("audit.*")      # safe while publishing
        time.sleep(0.2)
        spot.unset_subscription("audit.*")    # also safe
    ```

=== "Node/TypeScript"

    ```typescript
    // Single-threaded — publish and subscription changes are
    // naturally serialized. Both are safe to interleave.
    spot.setSubscription('audit.*');

    for (let i = 0; i < 100000; i++) {
        spot.publish('prices', Buffer.alloc(16));
    }

    spot.unsetSubscription('audit.*');
    ```

=== "C#/.NET"

    ```csharp
    var publisher = Task.Run(() =>
    {
        for (int i = 0; i < 100000; i++)
            spot.Publish("prices", new Message(16));
    });

    var control = Task.Run(async () =>
    {
        await Task.Delay(100);
        spot.SetSubscription("audit.*");      // safe while publishing
        await Task.Delay(200);
        spot.UnsetSubscription("audit.*");    // also safe
    });
    ```

=== "Rust"

    ```rust
    let handle = spot.send_handle();
    let publisher = std::thread::spawn(move || {
        for _ in 0..100_000 {
            handle.publish("prices", Message::new(16)).unwrap();
        }
    });

    std::thread::sleep(Duration::from_millis(100));
    spot.set_subscription("audit.*")?;      // safe while publishing
    std::thread::sleep(Duration::from_millis(200));
    spot.unset_subscription("audit.*")?;    // also safe
    ```

=== "Go"

    ```go
    go func() {
        for i := 0; i < 100000; i++ {
            msg := zlink.NewMessage(16)
            spot.Publish("prices", msg)
        }
    }()

    time.Sleep(100 * time.Millisecond)
    spot.SetSubscription("audit.*")      // safe while publishing
    time.Sleep(200 * time.Millisecond)
    spot.UnsetSubscription("audit.*")    // also safe
    ```

## 8. 흔한 실수

| 실수 | 이유 | 해결 |
|---|---|---|
| 같은 `zlink_msg_t` 공유 | msg는 thread-safe 아님 | 스레드별 별도 msg 생성 |
| 콜백에서 무거운 작업 | I/O 스레드 블로킹 | 큐로 오프로드 |
| `close` 후 API 호출 | `ESHUTDOWN` 반환 | 반환 코드 확인 |
| 메시지마다 `set_option` | 순차 처리 오버헤드 | 변경 시에만 호출 |

## 9. 에러 코드 요약표

| 에러 | 발생 시점 | 의미 |
|---|---|---|
| `EBUSY` | 사용 중 `close` 호출 | 대기 후 재시도 |
| `ESHUTDOWN` | `close` 후 API 호출 | 핸들 종료 중 |
| `EDEADLK` | 콜백 내 핸들러 교체 | 외부에서 교체 필요 |
| `EALREADY` | `close` 두 번 호출 | 이미 종료 진행 중 |

> 구현 세부 사항(admission gate, 순서 의미론, 비용 모델)은
> [Thread-Safety Internals](../internals/thread-safety.ko.md)를 참고.

---
[← 성능](10-performance.ko.md) | [소켓 옵션 →](12-socket-options.ko.md)
