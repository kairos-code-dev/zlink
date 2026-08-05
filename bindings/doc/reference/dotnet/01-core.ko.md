한국어 | [English](01-core.en.md)

[레퍼런스 목차](README.ko.md)

# 01. Core

이 category는 context lifecycle, context 옵션, routing identity, `Zlink` static facade —
library의 프로세스 전역 진입점과 utility resource를 다룬다. `IContext`의 socket 생성
메서드는 완결성을 위해 여기 나열하되 자세한 내용은 Sockets category에서 다룬다. `Zlink`의
poller/timer 생성도 여기 나열하되 자세한 내용은 Eventing category에서 다룬다. 정확한
signature는 [`Contracts/Core/`](../../../../bindings/dotnet/src/Zlink/Contracts/Core/)가
소유한다.

---

## `Zlink.CreateContext()`

Messaging context — socket의 factory이자 소유자 — 를 만든다. 이 레퍼런스의 다른 모든
항목의 전제 조건이다.

```csharp
using IContext context = Zlink.CreateContext();
```

**옵션.** 매개변수 없음.

**완료 결과.** 동기로 `IContext`를 반환한다. Caller가 반환된 context를 소유하며
해제해야 한다(`IDisposable`/`IAsyncDisposable`) — 해제하면 그 아래에서 만들어진 socket을
포함해 여전히 열려 있는 모든 것이 종료된다.

**선택 기준.** Application이 필요로 하는 context마다 한 번 호출한다 — 대부분의
application은 정확히 하나만 필요하다. 모든 socket은 context에서 만들어야 하며 context
자신의 해제와 독립적으로 caller가 소유하지만, context를 해제하면 그 아래에 남아 있는
socket은 종료된다.

---

## `IContext.Shutdown()` / `IContext.RecalculateAutoHwm()`

Context의 socket에 대한 blocking operation을 해제하지 않고 중단시키거나, automatic
high-water mark의 즉시 재계산을 강제한다.

```csharp
context.Shutdown();
context.RecalculateAutoHwm();
```

**옵션.** 둘 다 매개변수가 없다.

**완료 결과.** 둘 다 동기이며 `void`를 반환한다. `Shutdown`은 이 context 아래 socket의
blocking 호출을 중단시키지만 context나 그 socket을 해제하지 않는다 — 자원을 해제하려면
이후 context를 해제한다. `RecalculateAutoHwm`은 여전히 `AutoHwmProfile`로 구성된
socket에 대해서만 automatic HWM을 재계산한다(별도로 문서화된 실패 모드는 없다).

**선택 기준.** 여러 스레드에서 socket을 쓰는 context를 해제하기 전에 `Shutdown`을 호출해,
스레드가 socket 호출에 무한정 block되는 것을 피한다. Context나 socket의 auto-HWM
profile이나 message-unit 옵션을 바꾼 뒤, 일반 갱신 경로를 기다리지 않고 새 연결별
크기를 즉시 적용하려면 `RecalculateAutoHwm`을 호출한다.

---

## `IContext.Options`

Context 전역 옵션 facade를 읽는다 — 그 속성이 I/O thread와 context에서 만들어지는 모든
socket이 물려받는 기본값을 지배한다.

```csharp
context.Options.IoThreads = 8;
context.Options.AutoHwmProfile = AutoHwmProfile.LowLatency;
context.Options.AddThreadAffinityCpu(2);
```

**옵션.** `IContextOptions`는 get/set 속성을 노출한다 — `IoThreads`, `MaxSockets`,
`SocketLimit`(읽기 전용 — 빌드의 `MaxSockets` 상한), `ThreadPriority`,
`ThreadSchedulingPolicy`, `MaxMessageSize`, `MessageThreadSize`(읽기 전용), `Blocky`,
`AutoHwmProfile`, `AutoHwmMessageUnitBytes`(`ulong` — 회계된 바이트 HWM에 대해서는
Sockets category의 관련 설명 참고), `AutoHwmEnabled`, `AutoHwmRecalcDebounce`,
`ThreadNamePrefix`, 그리고 `AddThreadAffinityCpu(cpu)`/`RemoveThreadAffinityCpu(cpu)`
메서드.

**완료 결과.** 모든 속성 get/set과 두 affinity 메서드는 동기다.

**선택 기준.** 기본값이 배포 환경(thread 수, HWM 크기 profile, message-size 상한)에
맞지 않으면 socket을 만들기 전에 조정한다. `AutoHwmProfile`/`AutoHwmEnabled`는 실행 중인
context에서 바꿀 수 있다 — 일반 갱신 경로를 기다리지 않고 즉시 적용하려면 위
`RecalculateAutoHwm`과 짝지어 쓴다.

---

## `IContext.CreatePairSocket()` / `CreateDealerSocket()` / `CreateRouterSocket()` / `CreatePubSocket()` / `CreateSubSocket()` / `CreateXPubSocket()` / `CreateXSubSocket()` / `CreateStreamSocket()`

주어진 타입의 socket을 만들며, caller가 소유한다.

```csharp
using IDealerSocket dealer = context.CreateDealerSocket();
```

**옵션.** 여덟 factory 메서드 모두 매개변수가 없다.

**완료 결과.** 각각 해당 socket interface(`IPairSocket`, `IDealerSocket`, `IRouterSocket`,
`IPubSocket`, `ISubSocket`, `IXPubSocket`, `IXSubSocket`, `IStreamSocket`)를 동기로
반환한다. Caller가 반환된 socket을 context와 독립적으로 소유·해제해야 한다.

**선택 기준.** 각 socket interface의 연산·옵션·역할은 Sockets category를 참고한다 — 이
항목은 각각을 어떻게 만드는지만 다룬다.

---

## `RoutingId`

Messaging peer나 route를 식별하는 1~255바이트 binary-safe value type이다.

```csharp
RoutingId fromString = RoutingId.From("worker-3");
RoutingId fromBytes = RoutingId.From(rawBytes);
RoutingId fromUint = RoutingId.From(42u);
RoutingId fromGuid = RoutingId.From(Guid.NewGuid());
RoutingId restored = RoutingId.FromHex(previouslyPrinted.ToHex());
```

**옵션.** Static factory — `From(ReadOnlySpan<byte>)`/`From(byte[])`(raw 바이트를 그대로
복사), `From(string)`(UTF-8 인코딩), `From(uint)`(4바이트 big-endian), `From(Guid)`(16바이트
big-endian), `FromHex(string)`(`ToHex()`가 출력한 바이트를 복원). Instance member —
`Size`/`IsEmpty`, `ToBytes()`(내부 storage 기반 view), `ToHex()`, `TryToUInt32(out uint)`,
`TryToGuid(out Guid)`, `ToString()`(표시용 형태 — printable UTF-8, 그다음 `uint`, 그다음
`Guid`, 그다음 `hex:` prefix fallback), 값 동등성(`Equals`/`==`/`!=`/`GetHashCode`).

**완료 결과.** 모든 factory와 accessor는 동기다. 범위 밖 바이트 길이(1..255 아님)는
`ArgumentOutOfRangeException`을 던진다. `FromHex`에 잘못된 형식의 hex 문자열은
`ArgumentException`을 던진다.

**선택 기준.** 사람이 부여한 identity에는 `From(string)`, 숫자나 GUID 형태의 identity에는
`From(uint)`/`From(Guid)`, identity가 이미 binary면 raw `From(byte[])`/
`From(ReadOnlySpan<byte>)`를 쓴다. 안정적인 raw-byte round trip에는 특히
`ToHex()`/`FromHex()`를 쓴다 — `ToString()`은 표시 전용이며 역변환을 보장하지 않는다(숫자/
GUID/hex 형태로 넘어가기 전에 printable UTF-8 해석을 우선한다).

---

## `Zlink.Version()` / `Zlink.Strerror(int)` / `Zlink.Has(string)`

Native library의 빌드 버전을 읽거나, native error code를 메시지로 바꾸거나, 선택적 빌드
capability를 확인한다.

```csharp
var (major, minor, patch) = Zlink.Version();
string message = Zlink.Strerror(errnum);
bool hasTls = Zlink.Has("tls");
```

**옵션.** `Strerror`는 `errnum`(`int` error code)을 받는다. `Has`는 `capability`(문자열 —
`"tcp"`, `"ipc"`, `"tls"`, `"ws"`, `"wss"`가 인식되는 이름이며 그 밖의 문자열은 `false`)를
받는다.

**완료 결과.** 셋 다 동기다. `Version()`은 `(int Major, int Minor, int Patch)` tuple을
반환한다. `Strerror`는 `string`을 반환한다. `Has`는 `bool`을 반환한다.

**선택 기준.** 링크된 native library가 application이 기대하는 것과 일치하는지 확인하려면
`Version()`을 쓴다(특히 동적으로 로드될 때). 모든 transport가 컴파일에 포함되어 있다고
가정하는 대신 startup에 `Has(...)`로 선택적 transport를 분기한다. `Strerror`는 다른
곳에서 드러난 native error code와 함께 진단할 때 쓴다.

---

## `Zlink.CreateAtomicCounter()` / `Zlink.CreateStopwatch()` / `Zlink.CreateThread(Action)`

스레드에 안전한 정수 counter, 고해상도 stopwatch, 또는 실행 중인 백그라운드 thread — 세
가지 독립적인 utility resource를 만든다.

```csharp
using IAtomicCounter counter = Zlink.CreateAtomicCounter();
int newValue = counter.Increment();

using IZlinkStopwatch watch = Zlink.CreateStopwatch();
ulong partialUs = watch.Intermediate();
ulong totalUs = watch.Stop();

using IZlinkThread thread = Zlink.CreateThread(() => DoWork());
thread.Join();
```

**옵션.** `CreateAtomicCounter()`/`CreateStopwatch()`는 매개변수가 없다. `CreateThread(Action
task)`는 새 thread에서 즉시 실행할 작업을 받는다. `IAtomicCounter`는 `Value`(get),
`Set(value)`, `Increment()`, `Decrement()`를 제공한다 — 뒤의 둘은 operation 이전 값이
아니라 이후의 *새* 값을 반환한다. `IZlinkStopwatch`는 `Intermediate()`와 `Stop()`을
제공하며 둘 다 마이크로초 단위다. `IZlinkThread`는 `Join()`(작업이 끝날 때까지
block하며 반복 호출은 no-op)과 `Close()`(여전히 실행 중이면 먼저 join한 뒤 handle을
해제)를 제공한다.

**완료 결과.** 세 factory 모두 자신의 resource interface를 동기로 반환하며, caller가
각각을 소유하고 해제해야 한다(`IDisposable`/`IAsyncDisposable`).

**선택 기준.** 스레드 사이에서 안전한 공유 count에는 `CreateAtomicCounter`를 쓴다.
Benchmarking·profiling에는 `CreateStopwatch`를 쓴다 — 연속 읽기가 필요한 만큼
`Intermediate()`를 부르고, 마치고 해제할 때 정확히 한 번 `Stop()`을 부른다. 플랫폼
전용 API 대신 portable 백그라운드 thread에는 `CreateThread`를 쓴다 — 해제하려면(또는
`Join()` 뒤 `Close()`) 반드시 부른다.

---

## `Zlink.Proxy(...)` / `Zlink.ProxySteerable(...)` / `Zlink.Sleep(TimeSpan)` / `Zlink.MultipartClose(...)`

두 socket 사이의 양방향 메시지 forwarding loop를 실행하거나(선택적으로 control
socket으로 조종 가능), 호출한 스레드를 재우거나, multipart payload의 모든 메시지를
해제한다.

```csharp
Zlink.Proxy(frontend, backend, capture); // capture는 null일 수 있다; context 종료까지 block한다
Zlink.ProxySteerable(frontend, backend, capture, control); // control이 런타임 명령을 받는다
Zlink.Sleep(TimeSpan.FromSeconds(1));
Zlink.MultipartClose(parts);
```

**옵션.** `Proxy`/`ProxySteerable`은 `frontend`/`backend`(필수 `IZlinkSocket`),
`capture`(선택적 — 전달되는 모든 메시지의 복사본을 받음)를 받는다. `ProxySteerable`은
추가로 필수 `control` socket을 받는다. `Sleep`은 `TimeSpan` duration을 받는다.
`MultipartClose`는 `IReadOnlyList<Message>`를 받는다.

**완료 결과.** 넷 다 동기이며 반환값이 없다. `Proxy`/`ProxySteerable`은 context가
종료될 때까지(또는 `ProxySteerable`의 경우 `TERMINATE` control 명령이나 오류가 loop를
끝낼 때까지) 호출한 스레드를 block한다 — 둘 다 전용 스레드에서 실행한다.

**선택 기준.** 단순한 fire-and-forget forwarding loop를 자신의 스레드에서 실행하려면
`Proxy`를 쓴다. Application이 다른 스레드에서 control socket을 통해 loop를 멈추거나·
재개하거나·종료하거나 통계를 뽑아야 하면 `ProxySteerable`을 쓴다. 손으로 짠 loop
대신 한 번의 호출로 수신·구성된 multipart 배열의 모든 `Message`를 해제하려면
`MultipartClose`를 쓴다.

---

## `Zlink.CreatePoller()` / `Zlink.CreateTimer()`

재사용 가능한 poller나 독립 timer를 만든다.

```csharp
using IPoller poller = Zlink.CreatePoller();
using IZlinkTimer timer = Zlink.CreateTimer();
```

**옵션.** 두 factory 모두 매개변수가 없다(timer를 Spot의 lifecycle에 묶는
`CreateTimer(ISpot)` overload도 있다 — Service category).

**완료 결과.** 둘 다 자신의 resource interface(`IPoller`, `IZlinkTimer`)를 동기로
반환한다 — caller가 각각을 소유하고 해제해야 한다.

**선택 기준.** `IPoller`와 `IZlinkTimer` 자신의 연산은 Eventing category를 참고한다 — 이
항목은 생성만 다룬다.

---

## `Zlink.UnhandledCallbackException`

사용자 callback이 예외를 던질 때 발생하는 static event다.

```csharp
Zlink.UnhandledCallbackException += ex => logger.LogError(ex, "callback failed");
```

**옵션.** `Action<Exception>`을 구독·구독 해제한다.

**완료 결과.** 동기 add/remove다. 이 event는 예외를 던진 callback을 실행하는 백그라운드
dispatch thread에서 발생한다 — 그 callback이 등록한 쪽에 대해 비동기로 실행되므로 그
스레드는 예외를 원래 caller에게 전파할 수 없다.

**선택 기준.** 등록된 어떤 callback(stream packet handler, monitor handler, poll
handler, SPOT dispatch handler, request/reply callback 등 — Sockets/Eventing/Service
category)에서든 예외를 관찰하려면 여기를 구독한다 — 그 callback 중 어느 것도 caller에게
예외를 직접 드러낼 수 있는 스레드에서 실행되지 않으므로, 구독하지 않으면 조용히
사라진다.

---

전체 근거는 [`Contracts/Core/`](../../../../bindings/dotnet/src/Zlink/Contracts/Core/)와
[.NET 바인딩 스펙](../../spec/dotnet/README.ko.md)을 참고한다.
