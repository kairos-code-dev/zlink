한국어 | [English](01-core.en.md)

[레퍼런스 목차](README.ko.md)

# 01. Core

이 category는 context 수명주기, context option, routing identity, `Zlink` static
facade를 다룬다. `Context`의 socket 생성 메서드는 완결성을 위해 여기 나열하지만
Sockets category에서 상세히 다룬다. `Zlink`의 poller/timer 생성도 여기 나열하지만
Eventing category에서 상세히 다룬다. 정확한 signature는
[`contracts/core/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/core/)가
소유한다.

---

## `Zlink.createContext()`

메시징 context를 생성한다 — socket의 factory이자 소유자.

```java
try (Context context = Zlink.createContext()) {
    // ...
}
```

**Options.** 인자 없음.

**Completion result.** `Context`를 동기로 반환한다. `Context extends
AutoCloseable`이며, caller가 소유하고 반드시 close해야 한다 — close하면 그 하위에
아직 열려 있는 모든 것(context에서 생성된 socket 포함)이 종료된다.

**선택 기준.** application이 필요로 하는 context마다 한 번 호출한다 — 대부분의
application은 정확히 하나가 필요하다.

---

## `Context.shutdown()` / `Context.recalculateAutoHwm()`

context의 socket이 닫히지 않은 채 blocking operation을 인터럽트하거나, automatic
high-water mark의 즉시 재계산을 강제한다.

```java
context.shutdown();
context.recalculateAutoHwm();
```

**Options.** 둘 다 인자 없음.

**Completion result.** 둘 다 반환값 없이 동기다. `shutdown()`은 이 context 하위
socket의 blocking 호출을 인터럽트하지만 context나 그 socket을 닫지 않는다.
`recalculateAutoHwm()`은 아직 `AutoHwmProfile`이 설정된 socket에 대해서만
automatic HWM을 재계산한다.

**선택 기준.** 여러 스레드에서 socket을 쓰는 중인 context를 닫기 전엔
`shutdown()`을 호출해 socket 호출을 기다리는 스레드가 무기한 block되는 걸
막는다. auto-HWM profile이나 message-unit option을 바꾼 후엔 새 sizing을 즉시
적용하려고 `recalculateAutoHwm()`을 호출한다.

---

## `Context.options()` / `ContextOptions`

I/O thread와, context에서 생성되는 모든 socket이 상속하는 기본값을 관장하는
context-wide option facade를 읽는다.

```java
ContextOptions options = context.options();
options.ioThreads(8);
options.autoHwmProfile(AutoHwmProfile.LOW_LATENCY);
options.addThreadAffinityCpu(2);
```

**Options.** `ContextOptions`는 public 생성자를 가진 concrete class다(`new
ContextOptions(context)`), 다만 `context.options()`가 일반적인 경로다. 짝을
이루는 getter/setter 메서드: `ioThreads()`/`ioThreads(int)`,
`maxSockets()`/`maxSockets(int)`, `threadPriority()`/`threadPriority(int)`,
`threadSchedulingPolicy()`/`threadSchedulingPolicy(int)`,
`threadNamePrefix()`/`threadNamePrefix(String)`,
`maxMessageSize()`/`maxMessageSize(int)`, `blocky()`/`blocky(boolean)`,
`autoHwmEnabled()`/`autoHwmEnabled(boolean)`,
`autoHwmRecalcDebounce()`/`autoHwmRecalcDebounce(Duration)`,
`autoHwmProfile()`/`autoHwmProfile(AutoHwmProfile)`,
`autoHwmMessageUnitBytes()`/`autoHwmMessageUnitBytes(long)`(unsigned 64-bit
bit pattern; 0은 socket-type 기본값을 선택). 읽기 전용: `socketLimit()`,
`messageThreadSize()`. setter만: `addThreadAffinityCpu(int)`/
`removeThreadAffinityCpu(int)`.

**Completion result.** 모든 property get/set은 동기다.

**선택 기준.** 기본값이 배포 환경에 맞지 않을 때 socket 생성 전에 조정한다.
`autoHwmProfile`/`autoHwmEnabled` 변경은 `Context.recalculateAutoHwm()`과 짝지어
즉시 적용한다.

---

## `Context.createPairSocket()` / `createDealerSocket()` / `createRouterSocket()` / `createPubSocket()` / `createSubSocket()` / `createXPubSocket()` / `createXSubSocket()` / `createStreamSocket()`

주어진 타입의 socket을 생성한다, caller가 소유.

```java
try (DealerSocket dealer = context.createDealerSocket()) {
    // ...
}
```

**Options.** 8개 factory 메서드 모두 인자 없음.

**Completion result.** 각각 대응하는 socket interface를 동기로 반환한다.
caller가 소유하며 context와 독립적으로 반드시 close해야 한다.

**선택 기준.** 각 socket interface의 operation·option·역할은 Sockets
category를 참고한다 — 이 항목은 각각이 어떻게 생성되는지만 다룬다.

---

## `RoutingId`

메시징 peer나 route를 식별하는 1~255바이트의 binary-safe value type.

```java
RoutingId fromString = RoutingId.from("worker-3");
RoutingId fromBytes = RoutingId.from(rawBytes);
RoutingId fromRange = RoutingId.from(buffer, offset, length);
RoutingId fromUint32 = RoutingId.from(42L);
RoutingId fromUuid = RoutingId.from(UUID.randomUUID());
RoutingId restored = RoutingId.fromHex(previouslyPrinted.toHex());
```

**Options.** Static factory: `from(byte[])`(전체 배열 복사), `from(byte[]
value, int offset, int length)`(선택한 byte 범위 복사 — dotnet/cpp엔 없는
Java 고유 overload), `from(String)`(UTF-8 인코딩), `from(long)`(unsigned
32-bit 값에서 4-byte big-endian — 32비트에 안 맞으면
`IllegalArgumentException`), `from(UUID)`(16-byte), `fromHex(String)`
(`toHex()`가 출력한 byte를 복원). Instance member: `size()`, `toBytes()`(방어적
복사), `toHex()`, `toString()`(printable UTF-8, 그 다음 4-byte를 unsigned int로,
그 다음 16-byte를 UUID로, 마지막 `hex:` 접두 fallback), `equals`/`hashCode`.
`MAX_LENGTH`(`255`)는 public 상수다. 내부적으로 `RoutingId`는 receive hot
path에서 재할당을 피하려고 스레드별 trusted-bytes 캐시를 유지한다 — 이는 public
contract 표면이 아니다.

**Completion result.** 모든 factory·accessor는 동기다. 범위를 벗어난 길이는
`IllegalArgumentException`을 던진다. `fromHex`에 잘못된 hex 문자열을 주면
마찬가지다.

**선택 기준.** 사람이 부여한 identity엔 `from(String)`을, 숫자나 UUID 형태
identity엔 `from(long)`/`from(UUID)`를, identity가 이미 binary이거나 더 큰
buffer의 slice일 땐 raw byte overload(범위 overload 포함)를 쓴다. 내구성 있는
raw-byte round trip 전용으로 `toHex()`/`fromHex()`를 쓴다 — `toString()`은
표시 전용이며 가역성이 보장되지 않는다.

---

## `Zlink.strerror(int)` / `Zlink.has(String)` / `Zlink.version()` / `ZlinkVersion.get()`

native error code를 메시지로 변환하거나, 선택적 빌드 역할을 확인하거나, native
library의 빌드 버전을 읽는다.

```java
String message = Zlink.strerror(errnum);
boolean hasTls = Zlink.has("tls");
int[] version = Zlink.version();
```

**Options.** `strerror(int errnum)`. `has(String capability)` — 인식하는 이름은
`"tcp"`, `"ipc"`, `"tls"`, `"ws"`, `"wss"`; 다른 문자열은 `false`를 반환한다.
`version()`/`ZlinkVersion.get()`은 동등하다 — `ZlinkVersion`은
`Zlink.version()`에 위임하는 얇은 편의 wrapper다. `Zlink.errno()`는 소스에
존재하지만 `public` 수식어가 없어 application 코드에서 도달할 수 없다.

**Completion result.** 모두 동기다. `strerror`는 `String`을 반환한다. `has`는
`boolean`을 반환한다. `version()`/`ZlinkVersion.get()`은 `int[]`(major/minor/
patch)를 반환한다.

**선택 기준.** 링크된 native library 버전이 application이 기대하는 버전과
일치하는지 확인하려면 `version()`을 쓴다. 기동 시점에 `has(...)`로 선택적
transport에 분기한다. `strerror`는 다른 곳(Errors category)에서 드러난 native
error code와 함께 진단용으로 쓴다.

---

## `Zlink.createAtomicCounter()` / `Zlink.createStopwatch()` / `Zlink.createThread(Runnable)`

thread-safe 정수 counter, 고해상도 stopwatch, 실행 중인 background thread를
생성한다 — 세 개의 독립된 utility resource.

```java
try (AtomicCounter counter = Zlink.createAtomicCounter()) {
    int newValue = counter.increment();
}

try (ZlinkStopwatch watch = Zlink.createStopwatch()) {
    Duration partial = watch.intermediate();
    Duration total = watch.stop();
}

try (ZlinkThread thread = Zlink.createThread(() -> doWork())) {
    thread.join();
}
```

**Options.** `createAtomicCounter()`/`createStopwatch()`는 인자 없음.
`createThread(Runnable task)`는 새 스레드에서 즉시 실행할 task를 받는다.
`AtomicCounter`는 `value()`(get), `set(int)`, `increment()`/`decrement()`
(counter의 *새* 값을 반환)를 제공한다. `ZlinkStopwatch`는 `intermediate()`/
`stop()`을 제공하며 둘 다 `Duration`을 반환한다. `ZlinkThread`는 `join()`
(task가 끝날 때까지 block)을 제공한다.

**Completion result.** 세 factory 모두 자신의 resource interface를 동기로
반환한다. 각각 `AutoCloseable`이며 caller가 반드시 close해야 한다.

**선택 기준.** 스레드 전체에서 안전한 공유 count엔 `AtomicCounter`를 쓴다.
벤치마킹엔 `ZlinkStopwatch`를 쓴다 — `intermediate()`는 몇 번이든 호출하고,
`stop()`은 정확히 한 번 호출한다. zlink 런타임이 수명주기를 소유해야 할 땐
`java.lang.Thread`를 직접 쓰는 대신 `createThread`를 쓴다.

---

## `Zlink.proxy(...)` / `Zlink.proxySteerable(...)` / `Zlink.sleep(Duration)`

두 socket 사이의 양방향 message-forwarding loop을 실행하거나(선택적으로 control
socket을 통해 조종 가능), 호출 스레드를 sleep한다.

```java
Zlink.proxy(frontend, backend, capture); // capture는 null 가능; context 종료까지 block
Zlink.proxySteerable(frontend, backend, capture, control);
Zlink.sleep(Duration.ofSeconds(1));
```

**Options.** `proxy(Socket frontend, Socket backend, Socket capture)` —
`capture`는 `null` 가능. `proxySteerable(Socket frontend, Socket backend,
Socket capture, Socket control)`. `sleep(Duration)`만 public이다 —
`Zlink.sleep(int seconds)`와 `Zlink.multipartClose(Message[])`는 소스에
존재하지만 `public` 수식어가 없어 application 코드에서 도달할 수 없다, dotnet의
public `Zlink.Sleep(TimeSpan)`/`Zlink.MultipartClose(...)` 짝과 다르다.

**Completion result.** 모두 반환값 없이 동기다. `proxy`/`proxySteerable`은
context가 종료될 때까지(또는 `proxySteerable`의 경우 control 명령이나 에러가
loop을 끝낼 때까지) 호출 스레드를 block한다 — 둘 중 하나를 전용 스레드에서
실행한다.

**선택 기준.** 단순한 fire-and-forget forwarding loop엔 `proxy`를,
application이 다른 스레드에서 control socket을 통해 loop을 일시정지·재개·종료해야
할 땐 `proxySteerable`을 쓴다.

---

[`contracts/core/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/core/)와
[Java 바인딩 스펙](../../spec/java/README.ko.md)에서 전체 근거를 확인한다.
