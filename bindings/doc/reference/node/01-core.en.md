[한국어](01-core.ko.md) | English

[Reference index](README.en.md)

# 01. Core

This category covers the context lifecycle, context options, routing identity, and the top-level
factory/utility functions exported from the package root. **Unlike every other wrapper binding
covered so far, these factory functions are not declared under `contracts/core/`** — they are
plain functions exported from
[`src/index.ts`](../../../../bindings/node/src/index.ts), following the Node/JS module-export
idiom rather than a static class facade. `AtomicCounter`/`Stopwatch`/`Thread` are also declared
physically in `contracts/eventing/timer.ts`, not `contracts/core/` — they are grouped into this
Core category by convention (matching every other language's placement), not by Node's own source
layout. The exact signatures are owned by
[`contracts/core/`](../../../../bindings/node/src/zlink/contracts/core/) and
[`src/index.ts`](../../../../bindings/node/src/index.ts).

---

## `createContext()`

Creates a messaging context — the factory and owner of sockets.

```ts
const ctx = createContext();
```

**Options.** No parameters.

**Completion result.** Returns `Context` synchronously. The caller owns it and must call
`ctx.close()` — closing terminates anything still open under it, including sockets created from it.

**When to use.** Call once per context the application needs; most applications need exactly one.

---

## `Context.shutdown()` / `Context.recalculateAutoHwm()`

Interrupts blocking operations on the context's sockets without closing them, or forces an
immediate recalculation of automatic high-water marks.

```ts
ctx.shutdown();
ctx.recalculateAutoHwm();
```

**Options.** Neither takes parameters.

**Completion result.** Both are synchronous, returning `void`. `shutdown()` interrupts blocking
calls on sockets under this context but does not close the context or its sockets.
`recalculateAutoHwm()` recomputes automatic HWM only for sockets still configured with an
`AutoHwmProfile`.

**When to use.** Call `shutdown()` before `close()` on a context with sockets in use across
multiple threads/workers. Call `recalculateAutoHwm()` after changing the auto-HWM profile or a
message-unit option, to apply new sizing immediately.

---

## `Context.options`

The context-wide options facade, read via the `options` getter on `Context`.

```ts
ctx.options.ioThreads = 8;
ctx.options.autoHwmProfile = AutoHwmProfile.LowLatency;
ctx.options.addThreadAffinity(2);
```

**Options.** Plain properties (get/set except where noted): `ioThreads` (`number`), `maxSockets`
(`number`), `socketLimit` (`number`, read-only), `maxMsgSize` (`number`), `msgTSize` (`number`,
read-only), `threadPriority`/`threadSchedulingPolicy` (`number`), `blocky` (`boolean`),
`autoHwmEnabled` (`boolean`), `autoHwmRecalcDebounceMs` (`number`), `autoHwmProfile`
(`AutoHwmProfileValue`), `autoHwmMsgUnitBytes` (`bigint` — the unsigned 64-bit planning unit; `0n`
selects the socket-type default), `threadNamePrefix` (`string`). Methods:
`addThreadAffinity(cpu: number)`/`removeThreadAffinity(cpu: number)`.

**Completion result.** All property reads/writes and both methods are synchronous.

**When to use.** Adjust before creating sockets when the defaults don't fit the deployment. Pair an
`autoHwmProfile`/`autoHwmEnabled` change with `Context.recalculateAutoHwm()` to apply it
immediately.

---

## `createPairSocket(ctx)` / `createDealerSocket(ctx)` / `createRouterSocket(ctx)` / `createPubSocket(ctx)` / `createSubSocket(ctx)` / `createXPubSocket(ctx)` / `createXSubSocket(ctx)` / `createStreamSocket(ctx)`

Creates a socket of the given type from a context, owned by the caller.

```ts
const dealer = createDealerSocket(ctx);
```

**Options.** Each factory takes the owning `Context`.

**Completion result.** Each returns its corresponding socket interface synchronously. The caller
owns and must `close()` the returned socket independently of the context.

**When to use.** See the Sockets category for each socket interface's operations, options, and
capability roles — this entry only covers how each is created.

---

## `RoutingId`

A binary-safe value type identifying a messaging peer or route, 1 to 255 bytes.

```ts
const fromString = RoutingId.from('worker-3');
const fromBytes = RoutingId.from(rawBuffer);
const fromUint32 = RoutingId.from(42);
const restored = RoutingId.fromHex(previouslyPrinted.toHex());
```

**Options.** Static factories: `from(value: string | Buffer | Uint8Array | number)` — a `string`
encodes as UTF-8, a `number` becomes a 4-byte big-endian uint32 (must be an integer in
`0..4294967295`), a `Buffer`/`Uint8Array` is copied as-is. `fromHex(value: string)` restores bytes
`toHex()` printed. Unlike dotnet/java, there is no dedicated UUID-typed factory overload — a 16-byte
value is still constructed via the `Buffer`/`Uint8Array` overload of `from(...)`. Instance members:
`size` (getter), `toBytes()` (copy), `toHex()`, `toString()` (printable UTF-8, then
4-byte-as-uint32, then 16-byte-as-UUID-format, then a `hex:`-prefixed fallback), `equals(other)`.
Instances are frozen (`Object.freeze`) and only constructible via `from`/`fromHex` — the
constructor itself is private and guarded by an internal token.

**Completion result.** Every factory and accessor is synchronous. Out-of-range length throws
`TypeError`/`RangeError`; a malformed hex string to `fromHex` throws the same.

**When to use.** Use `from(string)` for a human-assigned identity, `from(number)` for a numeric
identity, and the `Buffer`/`Uint8Array` overload (including for a 16-byte UUID's raw bytes) when the
identity is already binary. Use `toHex()`/`fromHex()` specifically for a durable raw-byte round
trip — `toString()` is for display only and is not guaranteed reversible.

---

## `version()` / `strerror(code)` / `has(capability)`

Reads the native library's build version, converts a native error code to a message, or checks for
an optional build capability.

```ts
const [major, minor, patch] = version();
const message = strerror(errnum);
const hasTls = has('tls');
```

**Options.** `strerror(code: number)`. `has(capability: string)` — recognized names include
`'tcp'`, `'ipc'`, `'tls'`, `'ws'`, `'wss'`; any other string returns `false`.

**Completion result.** All are synchronous. `version()` returns a `[number, number, number]` tuple
(major/minor/patch). `strerror` returns a `string`. `has` returns `boolean`.

**When to use.** Use `version()` to confirm the linked native library matches what the application
expects. Use `has(...)` at startup to branch on optional transports. `strerror` is for diagnostics
alongside a native error code surfaced elsewhere (Errors category).

---

## `createAtomicCounter(initialValue?)` / `createStopwatch()` / `createThread(handler)`

Creates a thread-safe integer counter, a high-resolution stopwatch, or a running background
thread — three independent utility resources. Their interfaces (`AtomicCounter`, `Stopwatch`,
`Thread`) are declared in `contracts/eventing/timer.ts` in source, not `contracts/core/`.

```ts
const counter = createAtomicCounter(); // or createAtomicCounter(10)
const newValue = counter.inc();

const watch = createStopwatch();
const partialUs = watch.intermediate();
const totalUs = watch.stop();

const thread = createThread(() => doWork());
thread.join();
```

**Options.** `createAtomicCounter(initialValue = 0)` — unlike dotnet/java/cpp, this takes an
optional starting value directly at creation instead of always starting at zero.
`createStopwatch()` takes no parameters. `createThread(handler: () => void)` runs the handler
immediately on the new thread. `AtomicCounter`: `set(value)`, `inc()`/`dec()` (return the *new*
value), `value()`, `close()`. `Stopwatch`: `intermediate()`/`stop()` (both `number` microseconds),
`close()`. `Thread`: **only `join()`** — unlike every other language's thread handle, this
interface declares no `close()`/dispose method.

**Completion result.** All three factories return their resource synchronously.

**When to use.** Use `createAtomicCounter(initial)` when a shared counter needs a non-zero starting
value without a separate `set()` call. Use `createStopwatch()` for benchmarking — call
`intermediate()` any number of times, then `stop()` exactly once. Use `createThread` for a portable
background thread instead of Node's `worker_threads` directly, when the zlink runtime needs to own
its lifecycle.

---

## `proxy(...)` / `proxySteerable(...)` / `sleep(seconds)` / `multipartClose(parts)`

Runs a bidirectional message-forwarding loop between two sockets (optionally steerable via a
control socket), sleeps the calling thread, or closes every message in a multipart array.

```ts
proxy(frontend, backend, capture); // capture is optional; blocks until context termination
proxySteerable(frontend, backend, capture, control);
sleep(1); // seconds, not milliseconds
multipartClose(parts);
```

**Options.** `proxy(frontend, backend, capture?)` — `capture` is optional. `proxySteerable(frontend,
backend, capture, control)` — `capture` may be `null`. `sleep(seconds: number)` — takes whole
seconds directly; unlike dotnet (where only the `Duration` overload is public) there is no separate
sub-second option here. `multipartClose(parts: Message[])`.

**Completion result.** All are synchronous with no return value. `proxy`/`proxySteerable` block the
calling thread until the context is terminated (or, for `proxySteerable`, until a control command or
error ends the loop) — run either on a dedicated worker thread.

**When to use.** Use `proxy` for a simple fire-and-forget forwarding loop. Use `proxySteerable` when
the application needs to pause/resume/terminate the loop from another thread via the control
socket. Use `multipartClose` to release every `Message` in a received or constructed multipart
array in one call instead of a hand-written loop.

---

See [`contracts/core/`](../../../../bindings/node/src/zlink/contracts/core/),
[`src/index.ts`](../../../../bindings/node/src/index.ts), and the
[Node binding spec](../../spec/node/README.en.md) for the full rationale.
