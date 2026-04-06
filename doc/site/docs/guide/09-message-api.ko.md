# Message API 상세

## 1. 개요

zlink message는 `zlink_msg_t` struct로 표현되며, 64 byte 고정 크기이다.
작은 data는 struct 내부에 inline 저장(VSM)하고,
큰 data는 heap buffer를 reference counting으로 관리(LMSG)한다.

## 2. Message Type

| Type | 조건 | Memory | 사용 시점 |
|------|------|--------|-----------|
| VSM (Very Small Message) | ≤33B (64-bit) | msg_t 내부 inline 저장 | 소형 data, 가장 빈번 |
| LMSG (Large Message) | >33B | malloc'd buffer, reference counted | 대형 data |
| CMSG (Constant Message) | constant data | 외부 pointer 참조 (copy 없음) | `zlink_msg_init_data(..., NULL, NULL)` |
| ZCLMSG (Zero-copy Large) | zero-copy | 외부 buffer + free callback | `zlink_msg_init_data()` |

> 내부 memory layout(VSM/LMSG struct 상세)은 [architecture.md](../internals/architecture.ko.md)를 참고.

## 3. 동작 원리

### 3.1 Memory Model

`zlink_msg_t`는 항상 64 byte 고정 struct이다. data 크기에 따라
내부 저장 전략이 자동으로 결정된다:

    ```c
    /* Handler callback receives all frames as parts array */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        for (size_t i = 0; i < part_count; i++) {
            printf("Frame[%zu bytes]: %.*s\n",
                   zlink_msg_size(&parts[i]),
                   (int)zlink_msg_size(&parts[i]),
                   (char *)zlink_msg_data(&parts[i]));
            zlink_msg_close(&parts[i]);
        }
    }
    ```

핵심: `zlink_msg_t` struct 자체는 stack/배열에 놓이고, 큰 data만 heap을
사용한다. 이 구조 덕분에 message array를 stack에 선언하고 바로 send할 수
있다.

### 3.2 Function Overview

| Function | 동작 | Ownership 변화 |
|----------|------|----------------|
| `zlink_msg_init` | 빈 message 생성 | caller가 소유 |
| `zlink_msg_init_size` | size만큼 buffer 할당 | caller 소유, `memcpy`로 채움 |
| `zlink_msg_init_data` | 외부 buffer 연결 (zero-copy) | ownership이 message로 이전 |
| `zlink_msg_close` | message 해제 (refcount=0이면 free) | 소유 포기 |
| `zlink_msg_move` | src -> dest 이동, src는 빈 상태 | dest로 이전 |
| `zlink_msg_copy` | src -> dest 복사, LMSG는 refcount 증가 | dest도 공동 소유 |
| `zlink_msg_data` | data buffer pointer 반환 | 변화 없음 (읽기 전용) |
| `zlink_msg_size` | data size(byte) 반환 | 변화 없음 |
| `zlink_msg_refcnt` | storage reference count 반환 | 변화 없음 |
| `zlink_msg_gets` | message metadata property를 string으로 반환 | 변화 없음 |

### 3.3 Move vs Copy

두 함수 모두 message 내용을 다른 `zlink_msg_t`로 옮기지만 동작이 다르다:

    ```cpp
    socket.on_receive(
        [](const zlink_routing_id_t *, zlink_msg_t *parts, size_t count, void *) {
            for (size_t i = 0; i < count; i++) {
                std::cout << "Frame[" << zlink_msg_size(&parts[i]) << " bytes]: "
                          << std::string((const char *)zlink_msg_data(&parts[i]),
                                         zlink_msg_size(&parts[i])) << std::endl;
                zlink_msg_close(&parts[i]);
            }
        });
    ```

- **move**: ownership 이전. src를 더 이상 사용할 수 없다. refcount 변화 없음.
- **copy**: ownership 공유. VSM이면 byte copy, LMSG이면 refcount를 증가시켜 같은 buffer를 가리킨다. 양쪽 모두 `zlink_msg_close()` 필요.

### 3.4 Close와 Reference Counting

`zlink_msg_close()`는 message type에 따라 다르게 동작한다:

| Type | `zlink_msg_close()` 동작 |
|------|--------------------------|
| VSM | struct를 빈 상태로 reset (heap 해제 없음) |
| LMSG | refcount 감소. 0이 되면 heap buffer `free()` |
| CMSG | struct를 빈 상태로 reset (외부 buffer는 건드리지 않음) |
| ZCLMSG | refcount 감소. 0이 되면 `ffn(data, hint)` callback 호출 |

=== "C"

    ```c
    zlink_msg_t original, copy;
    zlink_msg_init_size(&original, 1024);
    memcpy(zlink_msg_data(&original), data, 1024);

    zlink_msg_init(&copy);
    zlink_msg_copy(&copy, &original);

    /* Both original and copy reference the same data */
    /* storage refcount is now 2 */
    int refcnt = zlink_msg_refcnt(&copy);
    /* refcnt == 2 */

    zlink_msg_close(&original);
    zlink_msg_close(&copy);  /* Actual memory freed when last reference is released */
    ```

=== "C++"

    ```cpp
    auto original = zlink::message_t::from_bytes(data, 1024);
    auto copy = original;  // Copy constructor: refcount incremented
    int refcnt = copy.refcnt();  // 2

    original.close();
    copy.close();  // Memory freed when last reference released
    ```

=== "Java"

    ```java
    // Java Message does not expose copy; use copyOf for a new frame
    Message original = Message.copyOf(data);
    // To share: pass the original reference or create a new copy
    ```

=== "Python"

    ```python
    # Python: copy creates a new independent message
    original = zlink.Message.from_bytes(data)
    # For sharing, pass the original reference
    ```

=== "Node/TypeScript"

    ```typescript
    // Node: Buffer.from() creates a copy of the underlying data
    const original = Message.copyOf(data);
    const copy = Message.copyOf(original.toBuffer());
    ```

=== "C#/.NET"

    ```csharp
    var original = new Message(data);
    var copy = original.Copy();  // Reference-counted copy
    int refcnt = copy.RefCount;  // 2

    original.Dispose();
    copy.Dispose();  // Memory freed when last reference released
    ```

=== "Rust"

    ```rust
    // Rust Message does not expose copy; use from_bytes for a new frame
    let original = Message::from_bytes(&data)?;
    // Ownership is exclusive; clone via from_bytes if needed
    ```

=== "Go"

    ```go
    original, _ := zlink.NewMessage(data)
    // Go: clone creates a reference-counted copy
    // copy, _ := original.clone()  // internal API
    // For sharing, pass the original pointer
    defer original.Close()
    ```

## 4. Message Lifecycle

### 4.1 Init — zlink_msg_init vs zlink_msg_init_size vs zlink_msg_init_data

#### zlink_msg_init — Empty Message

Recv용 message나 `zlink_msg_copy()` target으로 사용. Data 없이 생성.

=== "C"

    ```c
    zlink_msg_t msg;
    zlink_msg_init(&msg);
    /* Used for initialization or as a target for zlink_msg_copy(). Free with zlink_msg_close() */
    ```

=== "C++"

    ```cpp
    zlink::message_t msg;  // RAII — empty message, automatically closed on destruction
    ```

=== "Java"

    ```java
    Message msg = new Message();  // AutoCloseable — empty message for receiving
    ```

=== "Python"

    ```python
    msg = zlink.Message()  # Empty message, supports context manager (with statement)
    ```

=== "Node/TypeScript"

    ```typescript
    const msg = Message.empty();  // Empty zero-length message
    ```

=== "C#/.NET"

    ```csharp
    using var msg = new Message();  // IDisposable — empty message
    ```

=== "Rust"

    ```rust
    let msg = Message::new()?;  // RAII — empty message, dropped automatically
    ```

=== "Go"

    ```go
    msg, err := zlink.NewMessage([]byte{})  // Empty message
    if err != nil { log.Fatal(err) }
    defer msg.Close()
    ```

#### zlink_msg_init_size — Size 지정 (copy 필요)

지정된 size의 buffer를 할당한 후, `zlink_msg_data()`로 data를 직접 채운다.
≤33B이면 VSM(inline), >33B이면 LMSG(heap)로 자동 결정된다.

=== "C"

    ```c
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 1024);
    memcpy(zlink_msg_data(&msg), source_data, 1024);
    zlink_send(socket, &msg, 1, 0);
    ```

=== "C++"

    ```cpp
    auto msg = zlink::message_t::from_bytes(source_data, 1024);
    socket.send(msg);
    ```

=== "Java"

    ```java
    Message msg = Message.copyOf(sourceData, 0, 1024);
    socket.send(msg);
    ```

=== "Python"

    ```python
    msg = zlink.Message.from_bytes(source_data[:1024])
    socket.send(msg)
    ```

=== "Node/TypeScript"

    ```typescript
    const msg = Message.copyOf(sourceData.subarray(0, 1024));
    socket.send(msg);
    ```

=== "C#/.NET"

    ```csharp
    var msg = new Message(sourceData.AsSpan(0, 1024));
    socket.Send(msg);
    ```

=== "Rust"

    ```rust
    let msg = Message::from_bytes(&source_data[..1024])?;
    socket.send(msg)?;
    ```

=== "Go"

    ```go
    msg, _ := zlink.NewMessage(sourceData[:1024])
    socket.Send(msg)
    ```

**사용 시점:** 자체 buffer의 data를 message로 만들 때. 원본 buffer를 바로 해제해도 안전.

#### zlink_msg_init_data — 외부 Buffer 참조 (zero-copy)

외부 buffer의 ownership을 message에 이전. Copy 없이 전송.
Free callback(ffn)으로 buffer 정리.

=== "C"

    ```c
    void my_free(void *data, void *hint) {
        free(data);
    }

    void *buf = malloc(4096);
    memcpy(buf, source_data, 4096);

    zlink_msg_t msg;
    zlink_msg_init_data(&msg, buf, 4096, my_free, NULL);
    /* buf is now owned by the message. Do not free it directly */
    zlink_send(socket, &msg, 1, 0);
    /* my_free(buf, NULL) is called automatically after sending completes */
    ```

=== "C++"

    ```cpp
    void *buf = malloc(4096);
    std::memcpy(buf, source_data, 4096);

    auto msg = zlink::message_t::from_external(buf, 4096, my_free, nullptr);
    /* buf is now owned by the message */
    socket.send(msg);
    ```

=== "Java"

    ```java
    // Java: wrapDirect borrows a direct ByteBuffer without copying
    ByteBuffer buf = ByteBuffer.allocateDirect(4096);
    buf.put(sourceData, 0, 4096).flip();

    Message msg = Message.wrapDirect(buf);
    /* buf must remain valid while msg is in use */
    socket.send(msg);
    ```

=== "Python"

    ```python
    # Python: wrap_buffer borrows without copying (caller keeps reference)
    buf = bytearray(source_data[:4096])
    msg = zlink.Message.wrap_buffer(buf)
    socket.send(msg)
    ```

=== "Node/TypeScript"

    ```typescript
    // Node: wrap borrows an existing Buffer without copying
    const buf = Buffer.from(sourceData.buffer, 0, 4096);
    const msg = Message.wrap(buf);
    socket.send(msg);
    ```

=== "C#/.NET"

    ```csharp
    // C#: no direct zero-copy factory; use copy-based constructor
    var msg = new Message(sourceData.AsSpan(0, 4096));
    socket.Send(msg);
    ```

=== "Rust"

    ```rust
    // Rust: from_bytes copies; zero-copy requires unsafe FFI
    let msg = Message::from_bytes(&source_data[..4096])?;
    socket.send(msg)?;
    ```

=== "Go"

    ```go
    // Go: NewMessage copies data; zero-copy requires direct C FFI
    msg, _ := zlink.NewMessage(sourceData[:4096])
    socket.Send(msg)
    ```

**`ffn=NULL`인 경우 (CMSG):** buffer를 해제하지 않고 borrowed reference로
유지한다. String literal이나 static data를 copy 없이 전송할 때 사용.
이 경우 `zlink_msg_refcnt()`는 항상 1을 반환한다.

=== "C"

    ```c
    /* Single frame */
    zlink_msg_t msg;
    zlink_msg_init_data(&msg, (void *)"Hello", 5, NULL, NULL);
    zlink_send(socket, &msg, 1, 0);

    /* Multipart — parts array */
    zlink_msg_t parts[2];
    zlink_msg_init_data(&parts[0], (void *)"foo", 3, NULL, NULL);
    zlink_msg_init_data(&parts[1], (void *)"foobar", 6, NULL, NULL);
    zlink_send(socket, parts, 2, 0);
    ```

=== "C++"

    ```cpp
    void *data = msg.data();
    size_t size = msg.size();
    ```

=== "Java"

    ```java
    byte[] data = msg.data();
    int size = msg.size();
    ```

=== "Python"

    ```python
    data = msg.data()   # returns bytes
    size = msg.size()
    ```

=== "Node/TypeScript"

    ```typescript
    const buf: Buffer = msg.toBuffer();
    const size: number = msg.byteLength();
    ```

=== "C#/.NET"

    ```csharp
    ReadOnlySpan<byte> data = msg.AsReadOnlySpan();
    int size = msg.Size;
    ```

=== "Rust"

    ```rust
    let data: &[u8] = msg.data();
    let size: usize = msg.len();
    ```

=== "Go"

    ```go
    data := msg.Data()   // []byte
    size := msg.Size()
    ```

**사용 시점:** 대용량 data의 copy를 피하고 싶을 때. Buffer 해제 시점을 library에 위임.

> 참고: `core/tests/test_msg_ffn.cpp` — free function callback 동작 검증

### 4.2 Data Access

=== "C"

    ```c
    void *data = zlink_msg_data(&msg);
    size_t size = zlink_msg_size(&msg);
    ```

=== "C++"

    ```cpp
    // Single frame — from_string copies the literal
    auto msg = zlink::message_t::from_string("Hello");
    socket.send(msg);

    // Multipart
    std::vector<zlink::message_t> parts;
    parts.push_back(zlink::message_t::from_string("foo"));
    parts.push_back(zlink::message_t::from_string("foobar"));
    socket.send(parts);
    ```

=== "Java"

    ```java
    // Single frame
    socket.send(Message.copyOfUtf8("Hello"));

    // Multipart
    socket.send(List.of(
        Message.copyOfUtf8("foo"),
        Message.copyOfUtf8("foobar")
    ));
    ```

=== "Python"

    ```python
    # Single frame
    socket.send(b"Hello")

    # Multipart
    socket.send([b"foo", b"foobar"])
    ```

=== "Node/TypeScript"

    ```typescript
    // Single frame
    socket.send('Hello');

    // Multipart
    socket.send([Message.copyOf('foo'), Message.copyOf('foobar')]);
    ```

=== "C#/.NET"

    ```csharp
    // Single frame
    socket.Send(Message.FromString("Hello"));

    // Multipart
    socket.Send(new List<Message> {
        Message.FromString("foo"),
        Message.FromString("foobar")
    });
    ```

=== "Rust"

    ```rust
    // Single frame
    socket.send(Message::from_bytes(b"Hello")?)?;

    // Multipart
    socket.send(vec![
        Message::from_bytes(b"foo")?,
        Message::from_bytes(b"foobar")?,
    ])?;
    ```

=== "Go"

    ```go
    // Single frame
    msg, _ := zlink.NewMessage([]byte("Hello"))
    socket.Send(msg)

    // Multipart
    p1, _ := zlink.NewMessage([]byte("foo"))
    p2, _ := zlink.NewMessage([]byte("foobar"))
    socket.Send(p1, p2)
    ```

> **제거됨:** `zlink_msg_more()`와 `ZLINK_MORE`는 header에서 제거되었다.
> Multipart parts-array API를 사용하면 `more` flag가 application
> code에서 더 이상 필요하지 않다.

### 4.3 Move와 Copy

#### zlink_msg_move — Ownership 이전 (zero-copy)

Message 내용을 dest로 이동시키고 src는 빈 상태가 된다.
LMSG의 경우 refcount를 증가시키지 않고 pointer만 이전한다.

=== "C"

    ```c
    /* Multipart send: pass an array of msg parts */
    zlink_msg_t parts[2];
    zlink_msg_init_size(&parts[0], 6);
    memcpy(zlink_msg_data(&parts[0]), "header", 6);
    zlink_msg_init_size(&parts[1], 4);
    memcpy(zlink_msg_data(&parts[1]), "body", 4);

    int rc = zlink_send(socket, parts, 2, 0);
    if (rc == -1) {
        /* Failure: caller still owns parts */
        for (size_t i = 0; i < 2; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    std::vector<zlink::message_t> parts;
    parts.push_back(zlink::message_t::from_string("header"));
    parts.push_back(zlink::message_t::from_string("body"));
    socket.send(parts);
    /* On failure, send() throws; parts remain valid for retry */
    ```

=== "Java"

    ```java
    List<Message> parts = List.of(
        Message.copyOfUtf8("header"),
        Message.copyOfUtf8("body")
    );
    socket.send(parts);
    /* On failure, ZlinkException is thrown; caller must close parts */
    ```

=== "Python"

    ```python
    socket.send([b"header", b"body"])
    # On failure, ZlinkError is raised
    ```

=== "Node/TypeScript"

    ```typescript
    socket.send([
        Message.copyOf('header'),
        Message.copyOf('body')
    ]);
    ```

=== "C#/.NET"

    ```csharp
    var parts = new List<Message> {
        Message.FromString("header"),
        Message.FromString("body")
    };
    socket.Send(parts);
    /* On failure, ZlinkException is thrown */
    ```

=== "Rust"

    ```rust
    let parts = vec![
        Message::from_bytes(b"header")?,
        Message::from_bytes(b"body")?,
    ];
    socket.send(parts)?;
    ```

=== "Go"

    ```go
    header, _ := zlink.NewMessage([]byte("header"))
    body, _ := zlink.NewMessage([]byte("body"))
    err := socket.Send(header, body)
    /* On failure, caller still owns messages */
    ```

**사용 시점:** message를 다른 변수로 넘길 때. `zlink_msg_copy()`와 달리
refcount 증가가 없어 단순하다.

#### zlink_msg_copy — Reference Counted Copy

VSM은 byte 단위 value copy, LMSG/ZCLMSG는 같은 buffer를 공유하며
refcount를 증가시킨다. 양쪽 모두 `zlink_msg_close()` 필요.

=== "C"

    ```c
    /* Reference-counted message */
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 1024);
    int refcnt = zlink_msg_refcnt(&msg);  /* 1: single owner */

    zlink_msg_t copy;
    zlink_msg_init(&copy);
    zlink_msg_copy(&copy, &msg);
    refcnt = zlink_msg_refcnt(&copy);  /* 2: shared by msg and copy */

    /* Constant data message */
    zlink_msg_t const_msg;
    zlink_msg_init_data(&const_msg, (void *)"TEST", 5, NULL, NULL);
    refcnt = zlink_msg_refcnt(&const_msg);  /* 1: not internally refcounted */
    ```

=== "C++"

    ```cpp
    zlink::message_t msg(1024);
    int refcnt = msg.refcnt();  // 1: single owner

    auto copy = msg;  // copy constructor increments refcount
    refcnt = copy.refcnt();  // 2: shared by msg and copy
    ```

=== "Java"

    ```java
    Message msg = new Message(1024);
    int refcnt = msg.refCount();  // 1: single owner
    ```

=== "Python"

    ```python
    # Python: refcount is not directly exposed in the binding
    # Use the C API for diagnostics if needed
    ```

=== "Node/TypeScript"

    ```typescript
    // Node: refcount is not exposed; Buffer uses JS GC
    ```

=== "C#/.NET"

    ```csharp
    var msg = new Message(1024);
    int refcnt = msg.RefCount;  // 1: single owner

    var copy = msg.Copy();
    refcnt = copy.RefCount;  // 2: shared
    ```

=== "Rust"

    ```rust
    // Rust: refcount is not directly exposed; ownership is exclusive
    let msg = Message::with_size(1024)?;
    // Single owner — no shared references
    ```

=== "Go"

    ```go
    msg, _ := zlink.NewMessage(make([]byte, 1024))
    refcnt := msg.RefCount()  // 1: single owner
    ```

> 참고: `core/tests/test_msg_flags.cpp` — `test_shared_refcounted()`

### 4.4 Metadata Property — zlink_msg_gets

Message에 부착된 metadata property를 string으로 반환한다.
해당 property가 없으면 `NULL`을 반환한다.

=== "C"

    ```rust
    let parts = vec![
        Message::from_bytes(b"header")?,
        Message::from_bytes(b"body")?,
    ];
    socket.send(parts)?;
    ```

=== "C++"

    ```typescript
    // Node: GC handles Buffer lifecycle
    const msg = Message.wrap(buf);
    socket.send(msg);
    // buf can be reused after send
    ```

=== "Java"

    ```go
    msg, err := zlink.NewMessage([]byte{})  // Empty message
    if err != nil { log.Fatal(err) }
    defer msg.Close()
    ```

=== "Python"

    ```python
    # Python: refcount is not directly exposed in the binding
    # Use the C API for diagnostics if needed
    ```

=== "Node/TypeScript"

    ```typescript
    // Node: refcount is not exposed; Buffer uses JS GC
    ```

=== "C#/.NET"

    ```csharp
    // Callback mode
    socket.OnReceive(received => {
        Console.WriteLine($"Received: {received.Parts[0].GetString()}");
        received.Dispose();
    });
    ```

=== "Rust"

    ```cpp
    msg.close();     // Explicit close
    // Or let destructor handle it — RAII cleans up automatically
    ```

=== "Go"

    ```rust
    drop(msg);       // Explicit drop, or let scope handle it — RAII
    ```

### 4.5 Send

=== "C"

    ```c
    /* Pattern 1: Send succeeds → msg parts automatically cleaned up */
    zlink_msg_t part;
    zlink_msg_init_size(&part, 5);
    memcpy(zlink_msg_data(&part), "Hello", 5);
    int rc = zlink_send(socket, &part, 1, 0);
    if (rc != -1) {
        /* Success: part is now empty. Calling close is safe but unnecessary */
    }

    /* Pattern 2: Send fails → manual cleanup required */
    zlink_msg_t part2;
    zlink_msg_init_size(&part2, 5);
    memcpy(zlink_msg_data(&part2), "Hello", 5);
    rc = zlink_send(socket, &part2, 1, ZLINK_DONTWAIT);
    if (rc == -1) {
        /* Failure: part2 is still valid. Must close */
        zlink_msg_close(&part2);
    }

    /* Pattern 3: Accessing msg data after send — dangerous! */
    zlink_send(socket, &part, 1, 0);
    /* zlink_msg_data(&part);  ← undefined behavior! */
    ```

=== "C++"

    ```cpp
    // Pattern 1: send() transfers ownership; msg is empty after success
    auto msg = zlink::message_t::from_string("Hello");
    socket.send(msg);
    // msg is now empty — do not access data

    // Pattern 2: try_send() returns result; msg still valid on backpressure
    auto msg2 = zlink::message_t::from_string("Hello");
    auto result = socket.try_send(msg2);
    // If result != sent, msg2 is still valid for retry
    ```

=== "Java"

    ```java
    // Pattern 1: send() transfers ownership; throws on failure
    try (Message msg = Message.copyOfUtf8("Hello")) {
        socket.send(msg);
        // msg is consumed — do not access data
    }

    // Pattern 2: try_send() preserves msg on backpressure
    Message msg2 = Message.copyOfUtf8("Hello");
    SendResult result = socket.trySend(msg2);
    if (!result.sent()) msg2.close();
    ```

=== "Python"

    ```python
    # Pattern 1: send() transfers ownership
    socket.send(b"Hello")

    # Pattern 2: try_send() returns SendResult
    result = socket.try_send(b"Hello")
    ```

=== "Node/TypeScript"

    ```typescript
    // Pattern 1: send() transfers ownership
    socket.send(Message.copyOf('Hello'));

    // Pattern 2: trySend() returns SendResult
    const result = socket.trySend(Message.copyOf('Hello'));
    ```

=== "C#/.NET"

    ```csharp
    // Pattern 1: Send() transfers ownership; throws on failure
    using var msg = Message.FromString("Hello");
    socket.Send(msg);

    // Pattern 2: TrySend() preserves msg on backpressure
    using var msg2 = Message.FromString("Hello");
    var result = socket.TrySend(msg2);
    ```

=== "Rust"

    ```rust
    // Pattern 1: send() moves ownership; msg is consumed
    let msg = Message::from_bytes(b"Hello")?;
    socket.send(msg)?;
    // msg is moved — cannot be used

    // Pattern 2: try_send() returns result
    let msg2 = Message::from_bytes(b"Hello")?;
    let result = socket.try_send(msg2)?;
    ```

=== "Go"

    ```go
    // Pattern 1: Send() transfers ownership on success
    msg, _ := zlink.NewMessage([]byte("Hello"))
    err := socket.Send(msg)
    // On success, msg is consumed

    // Pattern 2: TrySend() preserves msg on backpressure
    msg2, _ := zlink.NewMessage([]byte("Hello"))
    result, err := socket.TrySend(msg2)
    if !result.Sent() { msg2.Close() }
    ```

> **Legacy:** `zlink_msg_send()`는 아직 header에 존재하지만 제거 예정이다.
> `zlink_send()`에 parts array를 전달하는 방식으로 대체한다.

### 4.6 Recv

Message는 socket에 등록한 handler callback으로 수신된다.
Callback이 `zlink_msg_t` part를 직접 제공한다:

=== "C"

    ```c
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        printf("Received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));

        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    // Callback mode: register handler, library delivers received_t
    socket.on_receive(
        [](const zlink_routing_id_t *, zlink_msg_t *parts, size_t count, void *) {
            std::cout << "Received: " << std::string(
                (const char *)zlink_msg_data(&parts[0]),
                zlink_msg_size(&parts[0])) << std::endl;
            for (size_t i = 0; i < count; i++)
                zlink_msg_close(&parts[i]);
        });
    ```

=== "Java"

    ```java
    // Callback mode
    socket.onReceive(received -> {
        System.out.println("Received: " + received.part(0).toUtf8String());
        received.close();
    });
    ```

=== "Python"

    ```python
    # Callback mode
    def on_message(received):
        source_rid, parts = received.routing_id, received.parts
        print(f"Received: {parts[0].decode()}")
        received.close()
    socket.on_receive(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    // Callback mode
    socket.onReceive((routingId: Buffer | null, parts: Message[]) => {
        console.log(`Received: ${parts[0].toBuffer().toString()}`);
    });
    ```

=== "C#/.NET"

    ```go
    // Callback mode
    socket.OnReceive(func(received *zlink.Received) {
        fmt.Printf("Received: %s\n", received.Parts[0].Data())
        received.Close()
    })
    ```

=== "Rust"

    ```rust
    // Callback mode
    socket.on_receive(|received| {
        let data = received.parts()[0].data();
        println!("Received: {}", String::from_utf8_lossy(data));
    })?;
    ```

=== "Go"

    ```java
    // Callback mode
    socket.onReceive(received -> {
        System.out.println("Received: " + received.part(0).toUtf8String());
        received.close();
    });
    ```

### 4.7 Close

=== "C"

    ```c
    zlink_msg_close(&msg);
    ```

=== "C++"

    ```cpp
    zlink::message_t msg;  // RAII — empty message, automatically closed on destruction
    ```

=== "Java"

    ```java
    msg.close();     // AutoCloseable — or use try-with-resources
    ```

=== "Python"

    ```python
    msg.close()      # Or use context manager: with zlink.Message() as msg: ...
    ```

=== "Node/TypeScript"

    ```typescript
    // No explicit close needed — GC handles Buffer lifecycle
    ```

=== "C#/.NET"

    ```csharp
    msg.Dispose();   // IDisposable — or use 'using' statement
    ```

=== "Rust"

    ```go
    msg.Close()      // Explicit close required
    ```

=== "Go"

    ```csharp
    msg.Dispose();   // IDisposable — or use 'using' statement
    ```

## 5. Ownership 규칙

| 상황 | Ownership | 이후 동작 |
|------|-----------|-----------|
| `zlink_send` 성공 | library로 이전 | msg part는 빈 상태, access 불가 |
| `zlink_send` 실패 | caller가 여전히 소유 | 각 part에 `zlink_msg_close()` 호출 필요 |
| Handler callback이 msg 전달 | library가 msg part 제공 | 각 part에 `zlink_msg_close()` 호출 필요 |
| `zlink_msg_close` | resource 해제 | msg 재사용 가능 (재init 필요) |

### Ownership 규칙 실전

=== "C"

    ```c
    zlink_msg_t part;
    zlink_msg_init_size(&part, 100);
    memcpy(zlink_msg_data(&part), data, 100);

    int rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
    if (rc == -1) {
        if (errno == EAGAIN) {
            /* HWM exceeded: retry later */
        } else if (errno == ENOTSUP) {
            /* Send not supported on this socket (e.g., SUB socket) */
        } else if (errno == ETERM) {
            /* Context terminated */
        }
        /* On failure, part is still valid -> must close */
        zlink_msg_close(&part);
    }
    ```

=== "C++"

    ```cpp
    auto msg = zlink::message_t::from_bytes(data, 100);
    auto result = socket.try_send(msg);
    // result is sent, backpressured, or not_ready
    // On backpressure, msg is still valid for retry
    // On error, send() throws zlink::error_t
    ```

=== "Java"

    ```java
    Message msg = Message.copyOf(data, 0, 100);
    try {
        SendResult result = socket.trySend(msg);
        if (!result.sent()) {
            // Backpressured — retry later; msg still valid
        }
    } catch (ZlinkException e) {
        msg.close();  // On failure, manual cleanup required
    }
    ```

=== "Python"

    ```python
    try:
        result = socket.try_send(data[:100])
    except zlink.ZlinkError as e:
        pass  # Handle error (EAGAIN, ENOTSUP, ETERM)
    ```

=== "Node/TypeScript"

    ```typescript
    try {
        const result = socket.trySend(Message.copyOf(data.subarray(0, 100)));
    } catch (e) {
        // Handle error
    }
    ```

=== "C#/.NET"

    ```csharp
    using var msg = new Message(data.AsSpan(0, 100));
    try {
        var result = socket.TrySend(msg);
        // result: Sent, Backpressured, or NotReady
    } catch (ZlinkException e) {
        // Handle error
    }
    ```

=== "Rust"

    ```rust
    let msg = Message::from_bytes(&data[..100])?;
    match socket.try_send(msg) {
        Ok(result) => { /* result: Sent, Backpressured, NotReady */ }
        Err(e) => { /* Handle ZlinkError */ }
    }
    ```

=== "Go"

    ```go
    msg, _ := zlink.NewMessage(data[:100])
    result, err := socket.TrySend(msg)
    if err != nil {
        msg.Close()  // On failure, manual cleanup required
    }
    ```

## 6. Zero-Copy Pattern 상세

### Free Function Callback 작성법

=== "C"

    ```c
    /* Basic free callback */
    void simple_free(void *data, void *hint) {
        free(data);
    }

    /* Callback using hint */
    void pool_free(void *data, void *hint) {
        struct memory_pool *pool = (struct memory_pool *)hint;
        pool_return(pool, data);
    }

    /* Notification callback (does not free the data itself) */
    void notify_free(void *data, void *hint) {
        /* Notify that the data is no longer in use */
        memcpy(hint, "freed", 5);
        /* data is managed externally */
    }
    ```

=== "C++"

    ```cpp
    // C++ uses the same C callbacks with from_external()
    void simple_free(void *data, void *hint) { free(data); }

    void *buf = malloc(4096);
    auto msg = zlink::message_t::from_external(buf, 4096, simple_free);
    ```

=== "Java"

    ```java
    // Java: zero-copy via wrapDirect; JVM GC handles lifecycle
    // No explicit free callback — use wrapDirect(ByteBuffer) to borrow
    ByteBuffer direct = ByteBuffer.allocateDirect(4096);
    Message msg = Message.wrapDirect(direct);
    // direct must remain valid while msg is in use
    ```

=== "Python"

    ```python
    # Python: wrap_buffer borrows without copying; caller manages lifetime
    buf = bytearray(4096)
    msg = zlink.Message.wrap_buffer(buf)
    # buf must remain valid while msg is in use
    ```

=== "Node/TypeScript"

    ```typescript
    // Node: wrap borrows an existing Buffer; GC manages lifecycle
    const buf = Buffer.alloc(4096);
    const msg = Message.wrap(buf);
    // buf must remain referenced while msg is in use
    ```

=== "C#/.NET"

    ```csharp
    // C#: no direct free-callback API; use copy-based constructor
    // For zero-copy patterns, pin memory and use native interop
    var msg = new Message(data);
    ```

=== "Rust"

    ```rust
    // Rust: from_bytes copies; zero-copy requires unsafe FFI
    // RAII Drop handles deallocation automatically
    let msg = Message::from_bytes(&data)?;
    ```

=== "Go"

    ```go
    // Go: NewMessage copies; zero-copy requires direct C FFI
    // Close() handles deallocation
    msg, _ := zlink.NewMessage(data)
    defer msg.Close()
    ```

> 참고: `core/tests/test_msg_ffn.cpp` — `ffn()` callback이 hint에 "freed" 기록

### Free Function 호출 시점

=== "C"

    ```c
    /* 1. Called on message close */
    zlink_msg_t msg;
    zlink_msg_init_data(&msg, buf, size, my_free, NULL);
    zlink_msg_close(&msg);  /* → my_free(buf, NULL) called */

    /* 2. Called after sending completes */
    zlink_msg_init_data(&msg, buf, size, my_free, NULL);
    zlink_send(socket, &msg, 1, 0);
    /* my_free(buf, NULL) called when sending completes */

    /* 3. Called when original is freed after copy */
    zlink_msg_t copy;
    zlink_msg_init(&copy);
    zlink_msg_copy(&copy, &msg);
    zlink_msg_close(&msg);
    zlink_msg_close(&copy);  /* my_free called when last reference is released */
    ```

=== "C++"

    ```cpp
    // 1. Called on close (destructor or explicit)
    {
        auto msg = zlink::message_t::from_external(buf, size, my_free);
    }  // destructor calls close → my_free(buf, nullptr)

    // 2. Called after send completes
    auto msg = zlink::message_t::from_external(buf, size, my_free);
    socket.send(msg);  // my_free called when send completes

    // 3. Copy shares refcount; freed when last handle closes
    auto original = zlink::message_t::from_external(buf, size, my_free);
    auto copy = original;  // refcount incremented
    original.close();
    copy.close();  // my_free called here
    ```

=== "Java"

    ```java
    // Java: lifecycle managed by GC + close()
    // wrapDirect keeps the anchor reference; no explicit free callback
    try (Message msg = Message.wrapDirect(directBuf)) {
        socket.send(msg);
    }
    // directBuf can be reused after msg is closed
    ```

=== "Python"

    ```python
    # Python: lifecycle managed by close() / context manager
    with zlink.Message.wrap_buffer(buf) as msg:
        socket.send(msg)
    # buf can be reused after context exit
    ```

=== "Node/TypeScript"

    ```csharp
    // C#: IDisposable handles lifecycle
    using (var msg = new Message(data)) {
        socket.Send(msg);
    }
    // Native memory freed on Dispose
    ```

=== "C#/.NET"

    ```rust
    // Rust: Drop trait handles deallocation
    {
        let msg = Message::from_bytes(&data)?;
        socket.send(msg)?;
    }  // msg dropped, native storage freed
    ```

=== "Rust"

    ```rust
    let msg = Message::from_bytes(&data[..100])?;
    match socket.try_send(msg) {
        Ok(result) => { /* result: Sent, Backpressured, NotReady */ }
        Err(e) => { /* Handle ZlinkError */ }
    }
    ```

=== "Go"

    ```go
    // Go: explicit Close() required
    msg, _ := zlink.NewMessage(data)
    socket.Send(msg)
    // On success, msg is consumed; on failure, msg.Close() needed
    ```

> 참고: `core/tests/test_msg_ffn.cpp` — close/send/copy 각 시나리오

### Constant Data 전송 (CMSG)

`zlink_msg_init_data()`를 `ffn=NULL`로 호출하면 constant(literal, static) data를
copy 없이 전송할 수 있다.

!!! note "C API signature -- each binding wraps this into its idiomatic method."

    ```c
    int zlink_send(void *s_, zlink_msg_t *parts_, size_t part_count_, zlink_send_flags_t flags_);
    ```

=== "C"

    ```c
    /* Single-part send */
    zlink_msg_t part;
    zlink_msg_init_size(&part, 5);
    memcpy(zlink_msg_data(&part), "Hello", 5);
    zlink_send(socket, &part, 1, 0);

    /* Zero-copy send */
    zlink_msg_t zcmsg;
    zlink_msg_init_data(&zcmsg, large_buf, large_size, my_free, NULL);
    zlink_send(socket, &zcmsg, 1, 0);

    /* Multipart send */
    zlink_msg_t parts[2];
    zlink_msg_init_size(&parts[0], 6);
    memcpy(zlink_msg_data(&parts[0]), "header", 6);
    zlink_msg_init_size(&parts[1], 4);
    memcpy(zlink_msg_data(&parts[1]), "body", 4);
    zlink_send(socket, parts, 2, 0);
    ```

=== "C++"

    ```cpp
    // Single frame — from_string copies the literal
    auto msg = zlink::message_t::from_string("Hello");
    socket.send(msg);

    // Multipart
    std::vector<zlink::message_t> parts;
    parts.push_back(zlink::message_t::from_string("foo"));
    parts.push_back(zlink::message_t::from_string("foobar"));
    socket.send(parts);
    ```

=== "Java"

    ```java
    // Single frame
    socket.send(Message.copyOfUtf8("Hello"));

    // Multipart
    socket.send(List.of(
        Message.copyOfUtf8("foo"),
        Message.copyOfUtf8("foobar")
    ));
    ```

=== "Python"

    ```python
    # Single frame
    socket.send(b"Hello")

    # Multipart
    socket.send([b"foo", b"foobar"])
    ```

=== "Node/TypeScript"

    ```typescript
    // Single frame
    socket.send('Hello');

    // Multipart
    socket.send([Message.copyOf('foo'), Message.copyOf('foobar')]);
    ```

=== "C#/.NET"

    ```csharp
    // Single frame
    socket.Send(Message.FromString("Hello"));

    // Multipart
    socket.Send(new List<Message> {
        Message.FromString("foo"),
        Message.FromString("foobar")
    });
    ```

=== "Rust"

    ```rust
    // Single frame
    socket.send(Message::from_bytes(b"Hello")?)?;

    // Multipart
    socket.send(vec![
        Message::from_bytes(b"foo")?,
        Message::from_bytes(b"foobar")?,
    ])?;
    ```

=== "Go"

    ```go
    // Single frame
    msg, _ := zlink.NewMessage([]byte("Hello"))
    socket.Send(msg)

    // Multipart
    p1, _ := zlink.NewMessage([]byte("foo"))
    p2, _ := zlink.NewMessage([]byte("foobar"))
    socket.Send(p1, p2)
    ```

> 참고: `core/tests/test_msg_flags.cpp` — `test_shared_const()`

## 7. Multipart Message 실전 Pattern

Multipart message는 `zlink_send()` 한 번의 호출로 parts array를 전송한다.

### Pattern 1: Request-Reply (DEALER/ROUTER)

=== "C"

    ```c
    /* DEALER → ROUTER: send single frame */
    zlink_msg_t req;
    zlink_msg_init_size(&req, 7);
    memcpy(zlink_msg_data(&req), "request", 7);
    zlink_send(dealer, &req, 1, 0);

    /* ROUTER handler callback receives: source_rid + parts */
    void on_request(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* parts[0] = "request", source_rid = DEALER's routing_id */

        /* ROUTER reply: directed send via zlink_send_rid */
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
    // DEALER → ROUTER: send single frame
    auto req = zlink::message_t::from_string("request");
    dealer.send(req);

    // ROUTER handler callback receives: source_rid + parts
    router.on_receive(
        [&](const zlink_routing_id_t *source_rid,
            zlink_msg_t *parts, size_t count, void *) {
            // Reply to sender
            auto reply = zlink::message_t::from_string("reply");
            router.send(zlink::routing_id_t(*source_rid), reply);
            for (size_t i = 0; i < count; i++)
                zlink_msg_close(&parts[i]);
        });
    ```

=== "Java"

    ```java
    // DEALER → ROUTER: send single frame
    dealer.send(Message.copyOfUtf8("request"));

    // ROUTER callback receives routing id + parts
    router.onReceive(received -> {
        RoutingId source = received.routingId();
        router.send(source, Message.copyOfUtf8("reply"));
        received.close();
    });
    ```

=== "Python"

    ```python
    # DEALER -> ROUTER: send single frame
    dealer.send(b"request")

    # ROUTER callback receives routing id + parts
    def on_request(received):
        source_rid = received.routing_id
        router.send(b"reply", routing_id=source_rid)
        received.close()
    router.on_receive(on_request)
    ```

=== "Node/TypeScript"

    ```typescript
    // DEALER -> ROUTER: send single frame
    dealer.send('request');

    // ROUTER callback receives routing id + parts
    router.onReceive((routingId, parts) => {
        router.send(routingId!, Message.copyOf('reply'));
    });
    ```

=== "C#/.NET"

    ```csharp
    // DEALER -> ROUTER: send single frame
    dealer.Send(Message.FromString("request"));

    // ROUTER callback receives routing id + parts
    router.OnReceive(received => {
        var source = received.RoutingId;
        router.Send(source, Message.FromString("reply"));
        received.Dispose();
    });
    ```

=== "Rust"

    ```rust
    // DEALER -> ROUTER: send single frame
    dealer.send(Message::from_bytes(b"request")?)?;

    // ROUTER callback receives routing id + parts
    router.on_receive(|received| {
        let source = received.routing_id().unwrap();
        let reply = Message::from_bytes(b"reply").unwrap();
        router.send(&source, reply).unwrap();
    })?;
    ```

=== "Go"

    ```go
    // DEALER -> ROUTER: send single frame
    req, _ := zlink.NewMessage([]byte("request"))
    dealer.Send(req)

    // ROUTER callback receives routing id + parts
    router.OnReceive(func(received *zlink.Received) {
        source := received.RoutingID
        reply, _ := zlink.NewMessage([]byte("reply"))
        router.SendTo(source, reply)
        received.Close()
    })
    ```

> 참고: `core/tests/test_msg_flags.cpp` — `test_more()`: DEALER→ROUTER multipart

### Pattern 2: Topic + Data (PUB/SUB)

=== "C"

    ```c
    /* PUB: [topic][payload] as parts array */
    zlink_msg_t pub_parts[2];
    zlink_msg_init_size(&pub_parts[0], 7);
    memcpy(zlink_msg_data(&pub_parts[0]), "weather", 7);
    zlink_msg_init_size(&pub_parts[1], 5);
    memcpy(zlink_msg_data(&pub_parts[1]), "sunny", 5);
    zlink_send(pub, pub_parts, 2, 0);

    /* SUB handler callback receives topic and payload separately */
    void on_spot(const zlink_routing_id_t *source_rid,
                 const char *topic, size_t topic_len,
                 zlink_msg_t *parts, size_t part_count,
                 void *userdata)
    {
        /* topic = "weather", parts[0] = "sunny" */
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    // PUB: publish topic + payload
    auto payload = zlink::message_t::from_string("sunny");
    pub.publish("weather", payload);

    // SUB: on_subscribe callback receives topic + parts
    sub.on_subscribe(
        [](const zlink_routing_id_t *, const char *topic, size_t topic_len,
           zlink_msg_t *parts, size_t count, void *) {
            // topic = "weather", parts[0] = "sunny"
            for (size_t i = 0; i < count; i++)
                zlink_msg_close(&parts[i]);
        });
    ```

=== "Java"

    ```java
    // PUB: publish topic + payload
    pub.publish("weather", Message.copyOfUtf8("sunny"));

    // SUB: callback receives topic + parts
    sub.onSubscribe(subscribed -> {
        // subscribed.topic() = "weather"
        // subscribed.part(0) = "sunny"
        subscribed.close();
    });
    ```

=== "Python"

    ```python
    # PUB: publish topic + payload
    pub.publish("weather", b"sunny")

    # SUB: callback receives topic + parts
    def on_topic(subscribed):
        # subscribed.topic = "weather", subscribed.parts[0] = b"sunny"
        subscribed.close()
    sub.on_subscribe(on_topic)
    ```

=== "Node/TypeScript"

    ```typescript
    // PUB: publish topic + payload
    pub.publish('weather', Message.copyOf('sunny'));

    // SUB: callback receives topic + parts
    sub.onSubscribe((routingId, topic, parts) => {
        // topic = "weather", parts[0] = "sunny"
    });
    ```

=== "C#/.NET"

    ```csharp
    // PUB: publish topic + payload
    pub.Publish("weather", Message.FromString("sunny"));

    // SUB: callback receives topic + parts
    sub.OnSubscribe(subscribed => {
        // subscribed.Topic = "weather", subscribed.Parts[0] = "sunny"
        subscribed.Dispose();
    });
    ```

=== "Rust"

    ```rust
    // PUB: publish topic + payload
    pub_socket.publish("weather", Message::from_bytes(b"sunny")?)?;

    // SUB: callback receives topic + parts
    sub_socket.on_subscribe(|subscribed| {
        // subscribed.topic() = "weather"
        // subscribed.parts()[0].data() = b"sunny"
    })?;
    ```

=== "Go"

    ```go
    // PUB: publish topic + payload
    payload, _ := zlink.NewMessage([]byte("sunny"))
    pub.Publish("weather", payload)

    // SUB: callback receives topic + parts
    sub.OnSubscribe(func(subscribed *zlink.Subscribed) {
        // subscribed.Topic = "weather"
        // subscribed.Parts[0].Data() = "sunny"
        subscribed.Close()
    })
    ```

### Pattern 3: Handler Callback에서 Multipart 처리

=== "C"

    ```cpp
    // Callback mode: register handler, library delivers received_t
    socket.on_receive(
        [](const zlink_routing_id_t *, zlink_msg_t *parts, size_t count, void *) {
            std::cout << "Received: " << std::string(
                (const char *)zlink_msg_data(&parts[0]),
                zlink_msg_size(&parts[0])) << std::endl;
            for (size_t i = 0; i < count; i++)
                zlink_msg_close(&parts[i]);
        });
    ```

=== "C++"

    ```c
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        printf("Received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));

        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "Java"

    ```java
    socket.onReceive(received -> {
        for (int i = 0; i < received.partCount(); i++) {
            Message part = received.part(i);
            System.out.printf("Frame[%d bytes]: %s%n", part.size(), part.toUtf8String());
        }
        received.close();
    });
    ```

=== "Python"

    ```python
    def on_message(received):
        for part in received.parts:
            print(f"Frame[{len(part)} bytes]: {part.decode()}")
        received.close()
    socket.on_receive(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.onReceive((routingId, parts) => {
        for (const part of parts) {
            const buf = part.toBuffer();
            console.log(`Frame[${buf.length} bytes]: ${buf.toString()}`);
        }
    });
    ```

=== "C#/.NET"

    ```csharp
    socket.OnReceive(received => {
        foreach (var part in received.Parts) {
            Console.WriteLine($"Frame[{part.Size} bytes]: {part.GetString()}");
        }
        received.Dispose();
    });
    ```

=== "Rust"

    ```rust
    socket.on_receive(|received| {
        for part in received.parts() {
            println!("Frame[{} bytes]: {}", part.len(),
                     String::from_utf8_lossy(part.data()));
        }
    })?;
    ```

=== "Go"

    ```go
    socket.OnReceive(func(received *zlink.Received) {
        for _, part := range received.Parts {
            fmt.Printf("Frame[%d bytes]: %s\n", part.Size(), part.Data())
        }
        received.Close()
    })
    ```

## 8. Storage Refcount — zlink_msg_refcnt

`zlink_msg_refcnt()`는 message storage의 reference count를 반환한다.
Refcounted storage가 아니면 1을 반환한다.

내부 reference count는 atomic 연산으로 관리된다. `zlink_msg_copy()`로
만든 별도 handle을 서로 다른 스레드에서 copy/close하는 것은 안전하다.
`zlink_msg_refcnt()` 반환값은 시점 스냅샷이므로 진단/assertion 용도로 쓴다.
단일 `zlink_msg_t` 인스턴스를 여러 스레드에서 동시에 접근하면 안 된다.

| 상황 | `refcnt` 반환값 |
|------|-----------------|
| `init_size` 직후 (단독 소유) | 1 |
| `copy` 후 (refcount > 1) | 2 이상 |
| `init_data(..., ffn, ...)` (ZCLMSG) | 단독 1, copy 후 2 이상 |
| `init_data(..., NULL, NULL)` (CMSG) | 항상 1 |

=== "C"

    ```c
    /* Reference-counted message */
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 1024);
    int refcnt = zlink_msg_refcnt(&msg);  /* 1: single owner */

    zlink_msg_t copy;
    zlink_msg_init(&copy);
    zlink_msg_copy(&copy, &msg);
    refcnt = zlink_msg_refcnt(&copy);  /* 2: shared by msg and copy */

    /* Constant data message */
    zlink_msg_t const_msg;
    zlink_msg_init_data(&const_msg, (void *)"TEST", 5, NULL, NULL);
    refcnt = zlink_msg_refcnt(&const_msg);  /* 1: not internally refcounted */
    ```

=== "C++"

    ```cpp
    zlink::message_t msg(1024);
    int refcnt = msg.refcnt();  // 1: single owner

    auto copy = msg;  // copy constructor increments refcount
    refcnt = copy.refcnt();  // 2: shared by msg and copy
    ```

=== "Java"

    ```java
    Message msg = new Message(1024);
    int refcnt = msg.refCount();  // 1: single owner
    ```

=== "Python"

    ```typescript
    // No explicit close needed — GC handles Buffer lifecycle
    ```

=== "Node/TypeScript"

    ```java
    msg.close();     // AutoCloseable — or use try-with-resources
    ```

=== "C#/.NET"

    ```csharp
    var msg = new Message(1024);
    int refcnt = msg.RefCount;  // 1: single owner

    var copy = msg.Copy();
    refcnt = copy.RefCount;  // 2: shared
    ```

=== "Rust"

    ```rust
    // Rust: refcount is not directly exposed; ownership is exclusive
    let msg = Message::with_size(1024)?;
    // Single owner — no shared references
    ```

=== "Go"

    ```go
    msg, _ := zlink.NewMessage(make([]byte, 1024))
    refcnt := msg.RefCount()  // 1: single owner
    ```

> 참고: `core/tests/test_msg_flags.cpp` — `test_shared_const()`: constant message의 shared property

## 9. Error 처리

### Send 실패

=== "C"

    ```c
    zlink_msg_t part;
    zlink_msg_init_size(&part, 100);
    memcpy(zlink_msg_data(&part), data, 100);

    int rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
    if (rc == -1) {
        if (errno == EAGAIN) {
            /* HWM exceeded: retry later */
        } else if (errno == ENOTSUP) {
            /* Send not supported on this socket (e.g., SUB socket) */
        } else if (errno == ETERM) {
            /* Context terminated */
        }
        /* On failure, part is still valid -> must close */
        zlink_msg_close(&part);
    }
    ```

=== "C++"

    ```cpp
    auto msg = zlink::message_t::from_bytes(data, 100);
    auto result = socket.try_send(msg);
    // result is sent, backpressured, or not_ready
    // On backpressure, msg is still valid for retry
    // On error, send() throws zlink::error_t
    ```

=== "Java"

    ```java
    Message msg = Message.copyOf(data, 0, 100);
    try {
        SendResult result = socket.trySend(msg);
        if (!result.sent()) {
            // Backpressured — retry later; msg still valid
        }
    } catch (ZlinkException e) {
        msg.close();  // On failure, manual cleanup required
    }
    ```

=== "Python"

    ```python
    try:
        result = socket.try_send(data[:100])
    except zlink.ZlinkError as e:
        pass  # Handle error (EAGAIN, ENOTSUP, ETERM)
    ```

=== "Node/TypeScript"

    ```typescript
    try {
        const result = socket.trySend(Message.copyOf(data.subarray(0, 100)));
    } catch (e) {
        // Handle error
    }
    ```

=== "C#/.NET"

    ```csharp
    using var msg = new Message(data.AsSpan(0, 100));
    try {
        var result = socket.TrySend(msg);
        // result: Sent, Backpressured, or NotReady
    } catch (ZlinkException e) {
        // Handle error
    }
    ```

=== "Rust"

    ```csharp
    using var msg = new Message(data.AsSpan(0, 100));
    try {
        var result = socket.TrySend(msg);
        // result: Sent, Backpressured, or NotReady
    } catch (ZlinkException e) {
        // Handle error
    }
    ```

=== "Go"

    ```go
    msg, _ := zlink.NewMessage(data[:100])
    result, err := socket.TrySend(msg)
    if err != nil {
        msg.Close()  // On failure, manual cleanup required
    }
    ```

## 10. zlink_send (Multipart Msg 기반)

`zlink_send()`는 `zlink_msg_t` parts array와 part count를 받는다:

=== "C"

    ```c
    int zlink_send(void *s_, zlink_msg_t *parts_, size_t part_count_, zlink_send_flags_t flags_);
    ```

=== "C++"

    ```cpp
    router.send(target_rid, parts);
    ```

=== "Java"

    ```java
    router.send(targetRid, parts);
    ```

=== "Python"

    ```python
    msg = zlink.Message.from_bytes(source_data[:1024])
    socket.send(msg)
    ```

=== "Node/TypeScript"

    ```typescript
    router.send(targetRid, parts);
    ```

=== "C#/.NET"

    ```csharp
    router.Send(targetRid, parts);
    ```

=== "Rust"

    ```rust
    router.send(&target_rid, parts)?;
    ```

=== "Go"

    ```go
    router.SendTo(targetRID, parts...)
    ```

=== "C"

    ```c
    /* Single-part send */
    zlink_msg_t part;
    zlink_msg_init_size(&part, 5);
    memcpy(zlink_msg_data(&part), "Hello", 5);
    zlink_send(socket, &part, 1, 0);

    /* Zero-copy send */
    zlink_msg_t zcmsg;
    zlink_msg_init_data(&zcmsg, large_buf, large_size, my_free, NULL);
    zlink_send(socket, &zcmsg, 1, 0);

    /* Multipart send */
    zlink_msg_t parts[2];
    zlink_msg_init_size(&parts[0], 6);
    memcpy(zlink_msg_data(&parts[0]), "header", 6);
    zlink_msg_init_size(&parts[1], 4);
    memcpy(zlink_msg_data(&parts[1]), "body", 4);
    zlink_send(socket, parts, 2, 0);
    ```

=== "C++"

    ```cpp
    std::vector<zlink::message_t> parts;
    parts.push_back(zlink::message_t::from_string("header"));
    parts.push_back(zlink::message_t::from_string("body"));
    socket.send(parts);
    /* On failure, send() throws; parts remain valid for retry */
    ```

=== "Java"

    ```java
    // Single-part send
    socket.send(Message.copyOfUtf8("Hello"));

    // Multipart send
    socket.send(List.of(
        Message.copyOfUtf8("header"),
        Message.copyOfUtf8("body")
    ));
    ```

=== "Python"

    ```python
    # Single-part send
    socket.send(b"Hello")

    # Multipart send
    socket.send([b"header", b"body"])
    ```

=== "Node/TypeScript"

    ```typescript
    // Single-part send
    socket.send('Hello');

    // Multipart send
    socket.send([Message.copyOf('header'), Message.copyOf('body')]);
    ```

=== "C#/.NET"

    ```csharp
    // Single-part send
    socket.Send(Message.FromString("Hello"));

    // Multipart send
    socket.Send(new List<Message> {
        Message.FromString("header"),
        Message.FromString("body")
    });
    ```

=== "Rust"

    ```rust
    // Single-part send
    socket.send(Message::from_bytes(b"Hello")?)?;

    // Multipart send
    socket.send(vec![
        Message::from_bytes(b"header")?,
        Message::from_bytes(b"body")?,
    ])?;
    ```

=== "Go"

    ```go
    // Single-part send
    msg, _ := zlink.NewMessage([]byte("Hello"))
    socket.Send(msg)

    // Multipart send
    header, _ := zlink.NewMessage([]byte("header"))
    body, _ := zlink.NewMessage([]byte("body"))
    socket.Send(header, body)
    ```

ROUTER directed send에는 `zlink_send_rid()`를 사용한다:

=== "C"

    ```c
    zlink_send_rid(router, &target_rid, parts, part_count, 0);
    ```

=== "C++"

    ```python
    router.send(parts, routing_id=target_rid)
    ```

=== "Java"

    ```c
    zlink_send_rid(router, &target_rid, parts, part_count, 0);
    ```

=== "Python"

    ```python
    data = msg.data()   # returns bytes
    size = msg.size()
    ```

=== "Node/TypeScript"

    ```typescript
    const msg = Message.empty();  // Empty zero-length message
    ```

=== "C#/.NET"

    ```cpp
    void *data = msg.data();
    size_t size = msg.size();
    ```

=== "Rust"

    ```c
    zlink_msg_close(&msg);
    ```

=== "Go"

    ```go
    msg, _ := zlink.NewMessage(sourceData[:1024])
    socket.Send(msg)
    ```

> **Legacy:** `zlink_msg_send()`는 아직 header에 존재하지만 제거 예정이다.
> 모든 call site를 `zlink_send()`에 parts array를 전달하는 방식으로 migration한다.

---
[← Routing ID](08-routing-id.ko.md) | [Performance →](10-performance.ko.md)
