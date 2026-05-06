[Java Binding Specification](README.md) · [Bindings Policy](../README.md)

# Java Netty Extension Specification

This document defines the public contract for the Java Netty `ByteBuf`
extension. The adapter is separate from the core binding so Netty-specific
entrypoints do not become part of `dev.kairoscode.zlink`, and applications
opt in to the Netty dependency explicitly.

## Artifact And Package

- Maven `zlink-ext-netty`
- `dev.kairoscode.zlink.netty`

## Ownership Rules

- `copyOf(ByteBuf)` copies the readable bytes between `readerIndex` and
  `writerIndex`.
- `copyOf(ByteBuf)` must not change `readerIndex` or `writerIndex`.
- The extension must not call `retain()` or `release()` on caller-owned
  `ByteBuf`.
- `copyTo(Message, ByteBuf)` copies the full message at the current
  `writerIndex`.
- `copyTo(Message, ByteBuf)` may advance `writerIndex` by the copied byte
  count, but must not change `readerIndex`.
- `copyTo(Message, ByteBuf)` must not call `retain()` or `release()`.

## API

```java
package dev.kairoscode.zlink.netty;

public final class NettyMessages {
    public static dev.kairoscode.zlink.Message copyOf(
        io.netty.buffer.ByteBuf source);

    public static int copyTo(
        dev.kairoscode.zlink.Message message,
        io.netty.buffer.ByteBuf destination);
}
```
