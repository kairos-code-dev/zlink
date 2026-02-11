[English](java.md) | [한국어](java.ko.md)

# Java Binding

## 1. Overview

- **FFM API** (Foreign Function & Memory API, Java 22+)
- Direct native library calls without JNI
- Memory management based on Arena/MemorySegment

## 2. Main Classes

| Class | Description |
|-------|-------------|
| `Context` | Context |
| `Socket` | Socket (send/recv/bind/connect) |
| `Message` | Message |
| `Poller` | Event poller |
| `Monitor` | Monitoring |
| `Discovery` | Service discovery |
| `Gateway` | Gateway |
| `Receiver` | Receiver |
| `SpotNode` / `Spot` | SPOT PUB/SUB |

## 3. Basic Example

```java
try (var ctx = new Context()) {
    try (var server = ctx.createSocket(SocketType.PAIR)) {
        server.bind("tcp://*:5555");

        try (var client = ctx.createSocket(SocketType.PAIR)) {
            client.connect("tcp://127.0.0.1:5555");
            client.send("Hello".getBytes());

            byte[] reply = server.recv();
            System.out.println(new String(reply));
        }
    }
}
```

## 4. Build

```groovy
// build.gradle
dependencies {
    implementation files('path/to/zlink.jar')
}
```

## 5. Native Library Loading

Platform-specific libraries are automatically loaded from the `src/main/resources/native/` directory.
