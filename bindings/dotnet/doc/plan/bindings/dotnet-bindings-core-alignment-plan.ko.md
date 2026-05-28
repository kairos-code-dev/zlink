# zlink .NET Bindings Core 최신 정합화 작업 계획

작성일: 2026-03-26

## 1. 목적

`bindings/dotnet`의 managed surface를 현재 `core` 최신 네이티브 라이브러리와
`doc/guide` 기준 공개 계약에 맞게 재정렬한다.

이번 작업의 핵심은 "일부 함수 추가"가 아니라, 오래된 바인딩 전제를 걷어내고
최신 `core`의 공개 모델에 맞는 얕지 않은 .NET facade를 다시 세우는 것이다.

목표는 다음과 같다.

1. 현재 교체된 최신 `libzlink`와 실제로 동작하는 P/Invoke 계층 복구
2. `doc/guide`에 문서화된 최신 공개 계약을 .NET API로 자연스럽게 반영
3. 구형 service/stream/poller 모델 제거 또는 명시적 정리
4. 테스트를 최신 계약 중심으로 재구성
5. 기존 바인딩이 의도하던 성능 특성을 유지하거나 더 나쁘지 않게 재정렬

## 2. 입력 자료와 기준

이번 계획은 아래를 기준으로 잡는다.

1. 최신 네이티브 라이브러리: `bindings/dotnet/runtimes/linux-x64/native/libzlink.so`
2. 최신 C 공개 헤더: `core/include/zlink.h`
3. 최신 가이드 문서:
   - `doc/guide/02-core-api.ko.md`
   - `doc/guide/06-monitoring.ko.md`
   - `doc/guide/07-0-services.ko.md`
   - `doc/guide/07-1-discovery.ko.md`
   - `doc/guide/07-3-spot.ko.md`
   - `doc/guide/09-message-api.ko.md`
   - `doc/guide/11-thread-safety.ko.md`
   - `doc/guide/12-socket-options.ko.md`

우선순위는 `core/include/zlink.h` -> `doc/guide` -> 기존 `.NET` surface 순서로 둔다.
기존 `.NET` API가 이 둘과 충돌하면 `.NET` API를 바꾼다.

## 3. 현재 상태 요약

현재 `.NET` 바인딩은 최신 네이티브와 구조적으로 어긋나 있다.

실측 기준:

1. `NativeMethods.cs`에 선언된 `zlink_*` 항목 145개 중 82개가 현재 `libzlink.so`에 없다.
2. 반대로 현재 `libzlink.so`가 export하는 `zlink_*` 심볼 129개 중 66개는 `.NET`에서 바인딩하지 않는다.

즉, 지금 문제는 "빠진 함수 몇 개" 수준이 아니라 바인딩 계약 자체가 한 세대 뒤처진 상태다.

주요 어긋남은 아래와 같다.

### 3.1 Core 데이터 평면

1. `.NET` `Socket.Send/Receive`는 바이트 버퍼 기반 단일 프레임 모델에 강하게 묶여 있다.
2. 최신 `core` 공개 계약은 `zlink_send_part()` / `zlink_recv_part()` 중심의
   multipart substrate와 `zlink_recv_handler()` / `zlink_subscribe_handler()`
   기반 callback 모델을 중심으로 설명한다.
3. `Message.More`, `zlink_msg_more`, `zlink_msg_get`, `zlink_msg_set`,
   `zlink_msg_send`, `zlink_msg_recv` 같은 구형 전제가 남아 있다.
4. 최신 header에는 `zlink_msg_init_data`, `zlink_msg_refcnt`,
   `zlink_multipart_close`가 있고 `more` 기반 모델은 공용 surface에서 빠졌다.

### 3.2 Socket option / monitor

1. `.NET`은 `zlink_setsockopt` / `zlink_getsockopt` 전제를 갖고 있다.
2. 최신 `core`는 `zlink_set_option` / `zlink_get_option`과
   `set/get_pub_option`, `set/get_sub_option`, `set/get_router_option`,
   `set_dealer_option`, `set/get_stream_option`, `set/get_routing_id`,
   `set_tls_*`로 구조가 재편되었다.
3. monitor도 구형 `zlink_socket_monitor(...addr, events)` /
   `zlink_monitor_recv()` 모델이 아니라,
   `zlink_socket_monitor_open(options)` + `handler/recv/snapshot/close`
   모델이다.

### 3.3 Service 계층

1. `.NET`은 `Receiver` 중심의 예전 service 모델을 노출한다.
2. 최신 `core`/문서는 `Registry`, `Discovery`, `Spot`, `SpotNode`,
   `socket_attach_discovery`, `service_monitor`, `status/topology snapshot`
   중심이다.
3. `Receiver` 관련 네이티브 심볼은 현재 라이브러리에 존재하지 않는다.
4. `SpotPub/SpotSub`를 독립 핸들처럼 다루는 `.NET` 설계도 최신 공개 계약과 맞지 않는다.

### 3.4 Stream / Poller / 부가 유틸리티

1. `.NET`의 `AttachStreamLen32Be`, `zlink_stream_send`,
   `zlink_stream_send_msg`는 현재 export와 맞지 않는다.
2. `Poller`는 `spot_pub/sub`, `receiver` 전용 add/modify/remove API에 의존하지만
   현재 export는 generic poller 중심이다.
3. `Timers`는 로딩 시점에 필요한 export 자체가 없다.
4. thread-safe 가이드 기준의 `EBUSY` / `ESHUTDOWN` / callback 제약이
   managed API에 충분히 반영되어 있지 않다.

## 4. 설계 원칙

이번 정합화는 아래 원칙으로 진행한다.

1. 문서화된 최신 공개 계약을 기준으로 surface를 재설계한다.
2. 구형 API를 억지로 유지하는 얕은 shim보다, 명확한 새 managed 모델을 우선한다.
3. 네이티브 handle 수명, callback ownership, multipart ownership을 .NET에서
   설명 가능한 수준으로 단순화한다.
4. optional 기능은 "동작하지 않는 wrapper"로 남기지 말고
   제거 또는 명시적 feature-gate 중 하나로 정리한다.
5. POSD 관점에서 모듈 경계를 분명히 한다.
   `NativeMethods`는 선언만, marshalling/ownership은 별도 helper,
   public facade는 사용성 중심으로 분리한다.
6. public surface는 최신 core를 그대로 번역하는 데 그치지 않고,
   `.NET` 사용자가 자연스럽게 쓰는 형태로 다시 다듬는다.
7. 네트워크 hot path에서는 "편해 보이는 API"보다 예측 가능한 비용 모델을 우선한다.
   `.NET`스럽다는 이유만으로 per-call allocation, hidden copy, boxing을 늘리지 않는다.

## 4.1 .NET API 스타일 규칙

public API는 아래 규칙을 따른다.

1. public 타입/메서드/프로퍼티 이름은 모두 PascalCase를 사용한다.
2. public API에 `IntPtr`, native struct, raw pointer, native enum 이름을 직접 노출하지 않는다.
3. cheap read는 프로퍼티, 동작은 메서드로 노출한다.
4. overload로 표현 가능한 차이는 이름을 늘리지 않고 시그니처 차이로 해결한다.
5. 성공/실패를 예외로 다루는 mainline API와, bool 반환의 `Try*` convenience를 분리한다.
6. argument 오류는 `ArgumentNullException`, `ArgumentException`, `ArgumentOutOfRangeException`을 사용한다.
7. dispose 이후 접근은 `ObjectDisposedException`을 사용한다.
8. native 오류는 `ZlinkException`으로 노출한다.
9. public API는 `string`, `Message`, `Message[]`, `IReadOnlyList<Message>`,
   `byte[]`, `ReadOnlySpan<byte>` 같은 .NET 친화 타입을 사용한다.
10. callback은 .NET delegate 또는 `Action` 기반으로 노출하고,
    callback 내부에서 native 수명 규칙을 직접 다루게 하지 않는다.
11. fill-buffer style보다 반환값 style을 우선한다.
    예: snapshot/query는 caller-allocated buffer가 아니라 managed 배열/struct 반환
12. public API는 `get_*/set_*` 식 이름을 쓰지 않는다.
    예: `SetSubscription`, `UnsetSubscription`, `OpenMonitor`, `AttachDiscovery`
13. bytes/string 편의는 `Message`에 집중시키고, `Socket`은 메시징 행위에 집중한다.
14. async/`Task` 기반 API는 이번 범위에 포함하지 않는다.
    우선 동기 public contract와 callback contract를 안정화한다.

## 4.2 타입 설계 규칙

타입 설계는 아래 규칙으로 고정한다.

1. native handle을 소유하는 public 타입은 `sealed class` + `IDisposable`로 만든다.
   예: `Context`, `Socket`, `Message`, `Registry`, `Discovery`, `SpotNode`, `Spot`,
   `SocketMonitor`, `ServiceMonitor`
2. snapshot/event/query entry처럼 값을 전달하는 타입은 `readonly struct` 또는
   `readonly record struct`로 만든다.
3. public API에서 mutable DTO class를 남발하지 않는다.
4. caller가 소유권을 가져가는 데이터는 배열 또는 value type으로 반환한다.
5. caller가 버퍼를 할당해서 채워 넣는 C 스타일 API는 public surface에 두지 않는다.
6. public API에서 `ref IntPtr`, `ref size`, `void*` 개념을 직접 노출하지 않는다.
7. enum은 .NET 이름 규칙에 맞춘 새 enum을 사용하고 native enum 이름을 그대로 노출하지 않는다.

## 4.3 public API anti-goals

다음 형태는 public surface에 두지 않는다.

1. `get_*`, `set_*`, `open_*`, `close_*` 같은 C 스타일 함수명
2. caller-allocated buffer + `out length` 패턴
3. native 구조체를 그대로 반환하는 패턴
4. `IntPtr`를 직접 받거나 반환하는 패턴
5. transport/socket family별로 이름만 다른 중복 메서드
6. `SendMultipart`, `ReceiveMultipart`, `SendRid`, `StreamSend`처럼 이름이 퍼지는 surface
7. 최신 core에 없는 legacy abstraction 유지
8. `IEnumerable<Message>` 기반 송수신처럼 열거 비용과 다중 순회를 숨기는 surface
9. serializer/reflection/generic object 전송 convenience처럼 비용 모델이 불투명한 surface
10. payload decoding 의미를 `object.ToString()` override에 숨기는 surface

## 4.4 성능 원칙

이번 작업은 API를 정리하되 네트워크 라이브러리로서의 hot path 특성을 해치지 않는
것을 전제로 한다. 구현자는 아래 규칙을 따른다.

1. `Send`, `Receive`, `Publish`, `Subscribe` mainline에는 avoidable allocation을 넣지 않는다.
   호출 1회마다 새 `List<>`, LINQ iterator, boxing object, 임시 serializer state를 만들지 않는다.
2. raw socket 계층은 계속 `Message` 중심으로 유지한다.
   `byte[]`, `string` 편의는 `Socket`이 아니라 `Message` factory/helper에만 둔다.
   이 경계를 지켜야 송수신 hot path의 변환 비용을 예측할 수 있다.
3. `Message`는 `byte[]`, `ReadOnlySpan<byte>`, `string` 입력을 받을 수 있어야 한다.
   다만 encode/copy는 메시지 생성 시 한 번만 일어나고, `Socket.Send`에서 다시 같은 변환을 반복하지 않는다.
4. mainline recv/send 경로에서는 managed 추가 복사를 만들지 않는다.
   예외는 callback safety를 위한 "managed copy 후 native close" 한 번뿐이다.
5. multipart marshalling은 `Message[]`와 `List<Message>` fast-path를 우선 지원하고,
   일반 `IReadOnlyList<Message>`는 그 다음 경로로 처리한다.
   어떤 경우에도 public API에서 `IEnumerable<Message>`를 받아 재열거하지 않는다.
6. callback delegate rooting/pinning은 attach 시점에 한 번만 해결한다.
   이벤트 1건마다 새 delegate wrapper나 closure를 만들지 않는다.
7. routing id, topic, metadata 변환은 native 경계에서 한 번만 수행한다.
   동일한 값에 대해 send loop 내부에서 중복 encode/decode를 하지 않는다.
8. property getter, snapshot model 변환, monitor event 변환에서 숨은 대량 복사를 만들지 않는다.
   값 타입으로 충분한 모델은 value type으로 두고, 참조 타입이 필요한 경우만 class를 사용한다.
9. public zero-copy API는 이번 범위에서 제외하지만, 내부 구현은 이후 zero-copy 확장이 가능하도록
   copy 책임을 `Message`와 marshalling helper 내부에 국한한다.
10. `.NET`스러운 convenience가 steady-state 성능을 의미 있게 떨어뜨리면
    그 convenience는 sample/helper/doc 영역으로 내리고 mainline public API에 두지 않는다.

## 4.5 성능 민감 API 설계 규칙

성능과 사용성의 균형을 위해 아래 shape를 고정한다.

1. raw socket public API는 계속 `Message`와 `IReadOnlyList<Message>`만 받는다.
   `byte[]`, `ArraySegment<byte>`, `Memory<byte>`, `object` 등으로 `Send` overload를 늘리지 않는다.
2. `Message`는 payload convenience와 ownership 경계를 담당하고,
   `Socket`은 네트워크 행위만 담당한다.
   이 분리가 성능과 설계 복잡도 양쪽에서 가장 균형이 좋다.
3. `TrySend` / `TryReceive`는 예외 비용을 피하려는 non-throwing convenience로만 제공한다.
   숨은 재시도, 내부 sleep, fallback path를 넣지 않는다.
4. `async` / `Task` API는 이번 범위에 넣지 않는다.
   callback threading 모델과 completion allocation 비용을 섞기 전에 동기 계약과 callback 계약을 먼저 고정한다.
5. `Message[]`와 `List<Message>` 입력에 대해서는 내부 fast-path helper를 두되,
   public surface 이름은 overload 남발 없이 유지한다.
6. topic/service 계층도 serializer convenience를 public API에 두지 않는다.
   `Publish(string topic, Message ...)` / `Subscribe(out string topic, out Message ...)`
   형태를 유지해 비용 모델을 드러낸다.

## 5. 작업 범위

### 5.1 포함

1. `src/Zlink/Native/*`
2. `src/Zlink/Context.cs`, `Message.cs`, `Socket.cs`, `Monitor.cs`,
   `Poller.cs`, `Runtime.cs`, `Enums.cs`, `SocketOptions.cs`,
   `SocketOptionValidation.cs`, `RoutingIdCodec.cs`
3. `src/Zlink/Service/*`
4. `tests/Zlink.Tests/*`
5. `.csproj` 패키징 메타데이터가 최신 surface와 맞지 않는 부분

### 5.2 제외

1. 별도 벤치 코드와 실행 흐름 정비
2. `core/` 변경
3. 임시 호환 계층을 위한 별도 네이티브 shim 추가

### 5.3 이번 작업에서 고정하는 결정

이 문서는 아래 결정을 이미 확정한 상태로 본다. 구현 중 다시 열지 않는다.

1. `Receiver`는 유지하지 않는다.
   최신 `core` 공개 계약에 없는 서비스이므로 `.NET` public surface에서 제거한다.
2. `Timers`는 유지하지 않는다.
   최신 네이티브 export가 없으므로 public API에서 제거한다.
3. `AttachStreamLen32Be`는 public API로 유지하지 않는다.
   framing convenience는 `samples/`에서만 둔다.
4. `Poller` 1차 목표는 raw `Socket`과 `fd`만 지원하는 generic poller로 축소한다.
   `SpotPub`, `SpotSub`, `Receiver` 전용 poller entrypoint는 제거한다.
5. callback ownership 모델은 "managed copy 후 native close"로 고정한다.
   callback 내부에서 native `zlink_msg_t*`의 수명을 외부로 넘기지 않는다.
6. `Message.InitData` 기반 public zero-copy API는 1차 범위에서 제외한다.
   우선 `Message`, multipart marshalling, callback safety를 먼저 안정화한다.
7. single-part 편의 API는 유지한다.
   내부 구현은 모두 multipart 공용 경로를 사용한다.
8. `Discovery.GetReceivers()`류의 구형 convenience는 유지하지 않는다.
   discovery member/query 기반 새 모델만 남긴다.
9. 이번 작업에서 호환성 레이어를 두기보다 breaking change를 명시하고 surface를 정리한다.
10. raw socket 계층의 public 이름은 `Send` / `Receive`로 통일한다.
    single-part, multipart, routing id 유무는 overload로만 구분한다.
11. topic/service 계층의 public 이름은 `Publish` / `Subscribe`를 유지한다.
12. bytes/string 편의성은 `Socket`이 아니라 `Message`에만 둔다.
13. public `routingId` 타입은 `string`으로 고정한다.
    표현 형식은 기존 `RoutingIdCodec`의 public string form을 사용한다.
14. `Send`는 성공 시 전달된 `Message` ownership을 소비한다.
    실패 시 ownership은 caller에 남는다.
15. `Receive`는 반환된 `Message` ownership을 caller에게 넘긴다.
16. callback mode는 native message를 managed `Message`로 복사한 뒤 native를 닫는다.
    callback은 전달받은 managed `Message`를 dispose해야 한다.

### 5.4 삭제 대상

아래 항목은 이번 작업에서 제거 대상으로 본다.

1. `src/Zlink/Service/Receiver.cs`
2. `src/Zlink/Timers.cs`
3. `tests/Zlink.Tests/test_service_discovery.cs`
4. `src/Zlink/Native/NativeMethods*.cs`의 `zlink_receiver_*` 선언 전부
5. `src/Zlink/Native/NativeMethods*.cs`의 `zlink_setsockopt`, `zlink_getsockopt`
6. `src/Zlink/Native/NativeMethods*.cs`의 `zlink_stream_attach_len32be`, `zlink_stream_send`, `zlink_stream_send_msg`
7. `src/Zlink/Native/NativeMethods*.cs`의 `zlink_spot_pub_*`, `zlink_spot_sub_*`
8. `src/Zlink/Native/NativeMethods*.cs`의 `zlink_poller_add_spot_*`, `modify_spot_*`, `remove_spot_*`, `*_receiver`

### 5.5 신설 또는 대체 대상

아래 항목은 새로 추가하거나 역할을 분리한다.

1. `src/Zlink/Service/ServiceMonitor.cs`
2. `src/Zlink/Service/RegistryQueryClient.cs`
3. `src/Zlink/Service/TopologyModels.cs`
4. `src/Zlink/Native/NativeServiceModels.cs`
5. `samples/` 하위 각 콘솔 프로젝트
6. 샘플 공통 프로젝트 `samples/SampleCommon/`

### 5.6 기존 public API와 목표 public API 매핑

구현자는 아래 매핑을 기준으로 이름과 책임을 정리한다.

| 기존 surface | 목표 surface | 처리 방식 |
|---|---|---|
| `Receiver` | 없음 | 제거 |
| `Registry.SetEndpoints()` + `Start()` | `Registry.Bind(pub, router)` | 교체 |
| `Discovery(Context, DiscoveryServiceType)` | `Discovery(Context, ServiceType, serviceName)` | 교체 |
| `Discovery.GetReceivers()` | `Discovery.MemberPeers()` 등 새 query API | 제거 후 대체 |
| `Discovery.ReceiverCount()` | 없음 | 제거 |
| `Discovery.ServiceAvailable()` | 없음 | 제거 |
| `SpotNode.ConnectPeerPub()` | `SpotNode.ConnectPeer()` | 교체 |
| `SpotNode.DisconnectPeerPub()` | `SpotNode.DisconnectPeer()` | 교체 |
| `SpotNode.SetDiscovery()` | `SpotNode.AttachDiscovery()` | 교체 |
| `SpotPub` / `SpotSub` 전용 surface | unified `Spot` | 제거 후 통합 |
| `Socket.SetOption()` 내부 `setsockopt` 경로 | `set_option/get_option` 및 family-specific option | 교체 |
| 분산된 raw `Send*` 이름 | `Socket.Send(...)` overload | 통합 |
| 분산된 raw `Recv*` 이름 | `Socket.Receive(...)` overload | 통합 |
| `MonitorSocket` 단일 타입 | `SocketMonitor`, `ServiceMonitor` | 분리 |
| `AttachStreamLen32Be()` | 없음 | 제거 |
| `Timers` | 없음 | 제거 |

이 표는 문서상 참고가 아니라 실제 코드 정리 기준이다.

### 5.7 raw socket public API 이름 정책

raw socket 계층은 `.NET`에서도 이름을 쪼개지 않는다.

고정 정책:

1. raw socket 송신은 모두 `Send`
2. raw socket 수신은 모두 `Receive`
3. single-part, multipart, routing id 유무는 overload로 구분
4. topic/service 의미가 들어가는 API만 `Publish`, `Subscribe` 유지
5. bytes/string 편의는 `Message` factory/helper에서만 제공
6. public `routingId`는 모두 `string`으로 노출하고 내부에서 `RoutingIdCodec`으로 encode/decode

목표 시그니처 초안:

```csharp
public void Send(Message message, SendFlags flags = SendFlags.None);
public void Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);

public void Send(string routingId, Message message,
    SendFlags flags = SendFlags.None);
public void Send(string routingId, IReadOnlyList<Message> parts,
    SendFlags flags = SendFlags.None);

public void Receive(out Message message,
    ReceiveFlags flags = ReceiveFlags.None);
public void Receive(out Message[] parts,
    ReceiveFlags flags = ReceiveFlags.None);

public void Receive(out string routingId, out Message message,
    ReceiveFlags flags = ReceiveFlags.None);
public void Receive(out string routingId, out Message[] parts,
    ReceiveFlags flags = ReceiveFlags.None);
```

보조 정책:

1. `TrySend` / `TryReceive`는 non-throwing convenience로만 둔다.
2. mainline API 이름은 `Send` / `Receive` 외에 늘리지 않는다.
3. `SendMultipart`, `ReceiveMultipart`, `SendRid`, `StreamSend` 같은 이름은 public surface에 두지 않는다.
4. `Send` 성공 시 전달된 `Message` 또는 `Message[]`는 더 이상 유효하지 않은 객체로 본다.
5. `Receive`와 callback이 반환한 `Message`는 caller 또는 callback이 dispose한다.
6. 구현 내부에는 `Message[]` / `List<Message>` fast-path를 둔다.
   interface dispatch를 이유로 public 이름을 더 늘리지는 않는다.

### 5.7.1 Message public API 목표 shape

`Message`는 raw bytes wrapper가 아니라 ownership을 가진 payload object로 설계한다.
다만 public shape는 `.NET` 사용자에게 자연스럽고 비용 모델이 드러나야 한다.

목표 시그니처 초안:

```csharp
public sealed class Message : IDisposable
{
    public int Size { get; }
    public int RefCount { get; }

    public static Message From(byte[] data);
    public static Message From(ReadOnlySpan<byte> data);
    public static Message From(ReadOnlyMemory<byte> data);
    public static Message From(ReadOnlySequence<byte> data);
    public static Message From(string value);
    public static Message From(string value, Encoding encoding);

    public byte[] ToArray();
    public int CopyTo(Span<byte> destination);
    public string GetString();
    public string GetString(Encoding encoding);
}
```

고정 정책:

1. `Message`는 `Socket`이 아니라 payload convenience를 담당한다.
2. `From(ReadOnlySpan<byte>)`는 입력 복사를 한 번만 수행한다.
3. `CopyTo(Span<byte>)`는 새 배열 할당을 피하려는 caller를 위한 경로로 둔다.
4. payload 문자열화는 `GetString()` / `GetString(Encoding)`으로 제공한다.
   `object.ToString()`은 payload decode API로 사용하지 않는다.
5. public zero-copy 생성 API는 이번 범위에서 제외한다.
6. `Message`는 immutable value object처럼 보이게 만들지 않는다.
   ownership 소비와 dispose가 있는 타입이라는 사실이 API 이름과 문서에 드러나야 한다.

### 5.8 Message/Socket ownership 계약

이 계약은 구현 중 바꾸지 않는다.

1. `Message`는 native `zlink_msg_t` ownership wrapper다.
2. `Socket.Send(Message)` 성공 시 message ownership은 socket/native로 넘어가며,
   managed `Message`는 즉시 invalid 상태가 된다.
3. `Socket.Send(IReadOnlyList<Message>)` 성공 시 전달된 모든 `Message`는 invalid 상태가 된다.
4. `Socket.Send(...)`가 예외를 던지면 전달된 `Message`는 caller가 계속 dispose할 책임이 있다.
5. `Socket.Receive(...)`는 새 managed `Message`를 생성해 caller에게 ownership을 넘긴다.
6. `RecvHandler` / `SubscribeHandler` callback은 managed `Message` 또는 `Message[]`를 받는다.
   callback이 처리를 마치면 dispose해야 한다.
7. wrapper는 callback 안전성을 위해 native parts를 managed `Message`로 복사한 뒤
   즉시 native `zlink_multipart_close` 또는 각 `zlink_msg_close` 경로를 정리한다.
8. public API는 borrowed `Message` view를 노출하지 않는다.
9. send/recv mainline 경로는 callback 경로와 달리 managed 추가 복사를 만들지 않는다.
   성능상 필요한 fast-path는 `Message`와 marshalling helper 내부에만 둔다.

### 5.9 Routing ID 계약

1. public API에서 routing id는 모두 `string`이다.
2. 이 문자열은 `RoutingIdCodec`의 public string form과 동일하다.
3. raw byte routing id를 public surface에 직접 노출하지 않는다.
4. native 호출 직전 encode, native 결과 수신 직후 decode를 수행한다.
5. `Socket.Send(string routingId, ...)`와
   `Socket.Receive(out string routingId, ...)`는 이 규약을 공유한다.

### 5.10 Service public API 목표 shape

service 계층도 구현 전에 public shape를 고정한다.

`Registry` 목표 메서드:

```csharp
public sealed class Registry : IDisposable
{
    public Registry(Context context);
    public void Bind(string pubEndpoint, string routerEndpoint);
    public void SetId(uint registryId);
    public void AddPeer(string peerPubEndpoint);
    public void SetHeartbeat(uint intervalMs, uint timeoutMs);
    public void SetBroadcastInterval(uint intervalMs);
    public RegistryStatus Snapshot();
    public RegistryServiceSummaryEntry[] ServiceSummary(...);
    public RegistryTopologyEntry[] Topology(...);
    public RegistryTopologyEntry[] Topology(...);
    public MemberPeerEntry[] MemberPeers(...);
    public Message GetMemberPeerMetadata(...);
}
```

`Discovery` 목표 메서드:

```csharp
public sealed class Discovery : IDisposable
{
    public Discovery(Context context, ServiceType serviceType, string serviceName);
    public void ConnectRegistry(string registryEndpoint);
    public void SetValue(long value);
    public long GetValue();
    public void SetMetadata(Message metadata);
    public Message GetMetadata();
    public MemberPeerEntry[] MemberPeers();
    public Message GetMemberPeerMetadata(...);
    public ServiceMonitor OpenMonitor(...);
}
```

`SpotNode` 목표 메서드:

```csharp
public sealed class SpotNode : IDisposable
{
    public SpotNode(Context context);
    public void Bind(string endpoint);
    public void ConnectPeer(string peerEndpoint);
    public void DisconnectPeer(string peerEndpoint);
    public void AttachDiscovery(Discovery discovery);
    public SpotNodeStatus Snapshot();
    public SpotNodePeerEntry[] Peers(...);
    public SpotNodeSubjectEntry[] Subjects(...);
}
```

`Spot` 목표 메서드:

```csharp
public sealed class Spot : IDisposable
{
    public Spot(Context context);
    public void Publish(string topic, Message message, SendFlags flags = SendFlags.None);
    public void Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    public void SetSubscription(string topicOrPattern);
    public void UnsetSubscription(string topicOrPattern);
    public void Subscribe(out string topic, out Message message,
        ReceiveFlags flags = ReceiveFlags.None);
    public void Subscribe(out string topic, out Message[] parts,
        ReceiveFlags flags = ReceiveFlags.None);
    public void SubscribeHandler(SpotSubHandler handler);
    public void SendReadyHandler(Action handler);
    public ServiceMonitor OpenMonitor(...);
}
```

monitor 목표 메서드:

```csharp
public sealed class SocketMonitor : IDisposable
{
    public void AttachHandler(...);
    public SocketMonitorEvent Receive();
    public MonitorStatus Snapshot();
}

public sealed class ServiceMonitor : IDisposable
{
    public void AttachHandler(...);
    public ServiceMonitorEvent Receive();
    public void Close();
}
```

고정 규칙:

1. `Registry`, `Discovery`, `SpotNode`, `Spot`는 모두 `IDisposable`을 구현한다.
2. snapshot/query 메서드는 managed 배열 또는 struct로 반환한다.
3. metadata는 raw byte 배열이 아니라 `Message`로 주고받는다.
4. service monitor는 `Monitor.cs`에 섞지 않고 `Service/ServiceMonitor.cs`로 분리한다.
5. `Spot`은 unified handle만 노출하고 별도 `SpotPub`/`SpotSub` 타입을 public으로 두지 않는다.

### 5.11 목표 public 타입 inventory

최종적으로 public surface에 남겨야 하는 핵심 타입은 아래로 제한한다.

소유 타입:

1. `Context`
2. `Message`
3. `Socket`
4. `SocketMonitor`
5. `Registry`
6. `Discovery`
7. `ServiceMonitor`
8. `RegistryQueryClient`
9. `SpotNode`
10. `Spot`
11. `AtomicCounter`
12. `ZlinkStopwatch`

값 타입:

1. `ZlinkVersionInfo`
2. `SocketMonitorEvent`
3. `ServiceMonitorEvent`
4. `MonitorStatus`
5. `PeerRecord`
6. `MemberPeerEntry`
7. `RegistryStatus`
8. `RegistryServiceSummaryEntry`
9. `RegistryTopologyEntry`
10. `SpotNodeStatus`
11. `SpotNodePeerEntry`
12. `SpotNodeSubjectEntry`

제거 또는 비공개 전환 대상:

1. `Receiver`
2. `MonitorSocket`
3. `ReceiverInfoRecord`
4. standalone `SpotPub` / `SpotSub` 성격 타입
5. native interop helper가 새어 나온 public 타입 전부

## 6. 상세 실행 계획

## Phase 0. 계약 동결과 갭 매핑

목표: "무엇을 맞출지"를 구현 전에 확정한다.

작업:

1. 최신 `core/include/zlink.h`의 공개 함수, enum, struct, open-options,
   monitor/snapshot struct를 목록화한다.
2. `doc/guide` 기준으로 public behavior를 분류한다.
   - core send/recv
   - message ownership
   - option/tls/routing id
   - socket monitor
   - service monitor
   - discovery/registry/spot
   - topology/snapshot/query
3. 현재 `.NET` public surface를 아래 세 그룹으로 분류한다.
   - 유지
   - 이름/동작 변경
   - 즉시 제거
4. 심볼 매핑표를 만든다.
   - old `.NET` -> latest native
   - latest native -> target `.NET`
5. 아래 고정 결정을 문서와 코드 양쪽에 반영한다.
   - `Receiver` 제거
   - `Timers` 제거
   - stream LEN32BE helper 제거
   - generic poller 축소
   - callback ownership 모델 고정

산출물:

1. 함수/타입 매핑표
2. breaking-change 목록
3. 제거 대상 목록
4. 파일 삭제 목록
5. 새 파일 추가 목록

완료 기준:

1. `Receiver`, 구형 stream helper, 구형 poller/timer 전용 API의 처리 방향이 확정됨
2. 구현 중 "이전 surface를 계속 살릴지"를 다시 논의하지 않아도 됨

## Phase 1. Native interop 계층 전면 재작성

목표: 최신 `libzlink`와 정확히 일치하는 P/Invoke/struct 계약을 만든다.

작업:

1. `NativeMethods.cs`를 최신 export 기준으로 정리한다.
   - 선언 순서는 `core/include/zlink.h` 공개 순서를 최대한 따른다.
   - core, socket/message, monitor, service, poller, misc로 블록을 나눈다.
2. 존재하지 않는 선언을 제거한다.
   - `zlink_setsockopt/getsockopt`
   - `zlink_receiver_*`
   - `zlink_spot_pub_*`, `zlink_spot_sub_*`
   - `zlink_stream_attach_len32be`, `zlink_stream_send*`
   - 구형 monitor/poller/timer 전용 항목
3. 새 export를 추가한다.
   - `zlink_set_option/get_option`
   - `zlink_set/get_*_option`
   - `zlink_set/get_routing_id`
   - `zlink_set_tls_server/client`
   - `zlink_recv_handler`, `zlink_subscribe_handler`,
     `zlink_send_ready_handler`
   - `zlink_socket_attach_discovery`
   - `zlink_socket_monitor_open/handler/recv`,
     `zlink_service_monitor_open/handler/recv`,
     `zlink_monitor_status`, `zlink_monitor_close`
   - `zlink_discovery_set/get_value`, `set/get_metadata`
   - `zlink_registry_*_snapshot`, `topology_*`,
     `member_peers`, `member_peer_metadata`
   - `zlink_spot_node_*_snapshot/query`
   - `zlink_publish_part`, `zlink_subscribe_part`,
     `zlink_set_subscription`, `zlink_unset_subscription`
   - `zlink_send_part_rid`
4. `NativeTypes.cs`에 최신 struct/enum mirror를 추가한다.
   - routing id
   - socket/service monitor open options
   - socket monitor event / snapshot
   - service event
   - discovery/registry/spot snapshot entries
   - topology filter/entry
5. UTF-8 string marshalling과 fixed-buffer decoding 공통 helper를 정리한다.
6. `NativeMethods`는 아래 partial 파일로 분리한다.
   - `NativeMethods.Core.cs`
   - `NativeMethods.Socket.cs`
   - `NativeMethods.Monitor.cs`
   - `NativeMethods.Service.cs`
   이 분할은 선택이 아니라 이번 작업의 기본 구조로 본다.

산출물:

1. 최신 네이티브와 1:1 대응되는 interop layer
2. old export 의존 제거

완료 기준:

1. `NativeLibraryLoader` 기준 누락 export 예외가 없어야 함
2. interop 선언이 최신 `.so` export와 수동 diff 시 맞아야 함

## Phase 2. Core managed facade 재설계

목표: `.NET` 사용자가 최신 `core` 계약을 자연스럽게 쓰게 만든다.

작업:

1. `Message` 재설계
   - multipart 모델 중심 보조 API 추가
   - public zero-copy API는 이번 범위에서 제외
   - `RefCount` 지원
   - `More`/`msg_get`/`msg_set` 제거
   - `Of`, `ToArray`, `CopyTo`는 유지하되 내부는 최신 `zlink_msg_*`만 사용
   - `From(string)`, `GetString` 같은 bytes/string 편의는 `Message`에 둔다.
2. `Socket` 재설계
   - 단일 바이트 배열 API 위에 message/multipart 중심 API 추가
   - raw socket public API 이름은 `Send` / `Receive` overload로 통일
   - `RecvHandler`와 `SendReadyHandler` 도입
   - callback 부착 후 recv/poll 충돌 시 `EBUSY` surface 명확화
   - routing id 유무는 `Send` / `Receive` overload에서 처리
   - `AttachStreamRaw`는 raw callback helper로 유지한다.
   - stream 전용 framing helper는 제거한다.
3. `Context` 업데이트
   - 최신 ctx option 반영
   - `ZLINK_CTX_OPT_BLOCKY` 반영
4. `SocketOptions` / `Enums` 정리
   - 최신 enum 값과 타입 분류를 다시 맞춤
   - 공용 option과 family-specific option을 분리
5. `Monitor` 재설계
   - `SocketMonitor`와 `ServiceMonitor`를 분리한다.
   - 각 타입에 `Receive`, `AttachHandler`, `Snapshot`, `Close`를 제공한다.
6. 예외 규약을 고정한다.
   - native rc < 0 또는 NULL은 항상 `ZlinkException`
   - `EBUSY`, `ESHUTDOWN`, `ENOTSUP`는 wrapper에서 숨기지 않는다.

설계 기준:

1. 구형 "byte[] send/recv가 기본이고 multipart는 보조" 구조보다
   "message/multipart가 기본, single-part helper는 convenience"가 더 맞다.
2. raw callback ownership은 managed copy 후 native close 모델로 통일한다.
   다른 ownership 모델은 도입하지 않는다.
3. raw socket 계층의 public 이름은 overload 기반 `Send` / `Receive`로 통일한다.
4. topic/service 계층만 `Publish` / `Subscribe` 이름을 유지한다.

파일 단위 작업 순서:

1. `src/Zlink/Message.cs`
2. `src/Zlink/Socket.cs`
3. `src/Zlink/Monitor.cs`
4. `src/Zlink/Enums.cs`
5. `src/Zlink/SocketOptions.cs`
6. `src/Zlink/Context.cs`
7. `src/Zlink/Errors.cs`

완료 기준:

1. core/socket/message/monitor API가 최신 문서 용어와 대응됨
2. public XML-doc 또는 요약 주석에서 ownership/EBUSY/ESHUTDOWN 계약이 설명됨

## Phase 3. Service 계층 전면 정렬

목표: 구형 service 모델을 걷어내고 최신 Discovery/Registry/SPOT 모델을 반영한다.

작업:

1. `Receiver` 제거 계획 수립 및 실행
   - 최신 core에 없는 서비스이므로 새 구현을 억지로 유지하지 않음
   - `Receiver.cs`와 관련 테스트를 삭제한다.
2. `Registry` 재작성
   - `SetEndpoints + Start` 모델 제거
   - `Bind(pubEndpoint, routerEndpoint)` 모델로 교체
   - `SetId`, `AddPeer`, `SetHeartbeat`, `SetBroadcastInterval` 유지
   - `Status`, `ServiceSummary`,
     `Topology`, `Topology`,
     `MemberPeers`, `MemberPeerMetadata` 추가
3. `Discovery` 재작성
   - 생성자 시그니처를 `service_type + service_name` 고정 모델로 변경
   - `ConnectRegistry()`는 control endpoint 기준으로 설명
   - `SetValue/GetValue`, `SetMetadata/GetMetadata`,
     `MemberPeers/MemberPeerMetadata` 추가
   - `ServiceMonitor` 지원 추가
   - 기존 `ReceiverCount/GetReceivers/ServiceAvailable`은 제거한다.
4. `SpotNode` 재작성
   - `ConnectPeerPub/DisconnectPeerPub`를
     `ConnectPeer/DisconnectPeer`로 전환
   - `AttachDiscovery(discovery)` 모델 반영
   - 최신 snapshot/query API 추가
5. `Spot` 재작성
   - unified handle 중심으로 정리
   - `Publish`, `SetSubscription`, `UnsetSubscription`,
     `Subscribe`, `SubscribeHandler`, `SendReadyHandler`,
     `ServiceMonitor` 지원
   - old standalone pub/sub handle 가정 제거
6. raw socket family discovery attach API 추가
   - `Socket.AttachDiscovery(Discovery)` 도입
   - attach 후 connect/disconnect/unbind/close 제약 문서화

완료 기준:

1. `Service` 네임스페이스가 최신 core 개념과 일치
2. `.NET`에서 더 이상 존재하지 않는 네이티브 service에 의존하지 않음

## Phase 4. Stream / Poller / 부가 API 정리

목표: 애매하게 남은 구형 부가 surface를 최신 core 기준으로 정리한다.

작업:

1. STREAM API
   - `AttachStreamLen32Be` 제거
   - 최신 native가 제공하는 raw stream callback + `send_rid` 기반으로 재설계
   - framing helper는 library public surface가 아니라 sample helper로 둔다
2. `Poller`
   - 최신 export가 허용하는 generic 등록 모델로 재작성
   - `Socket`과 `fd`만 지원
   - `Receiver`, `SpotPub`, `SpotSub` 전용 entrypoint 제거
   - callback mode와 poller의 `EBUSY` 충돌 계약 반영
3. `Timers`
   - public surface에서 제거
4. `Runtime`, `AtomicCounter`, `Stopwatch` 계열은 유지한다.
   단, interop 시그니처만 최신 export 기준으로 다시 맞춘다.
5. `Thread` wrapper는 현재 `.NET` 바인딩 public surface에 없으므로 이번 범위에 포함하지 않는다.

파일 단위 작업 순서:

1. `src/Zlink/Poller.cs`
2. `src/Zlink/Runtime.cs`
3. `src/Zlink/AtomicCounter.cs`
4. `src/Zlink/ZlinkStopwatch.cs`
5. `src/Zlink/Timers.cs` 삭제

완료 기준:

1. "빌드는 되지만 실행하면 MissingExport" 같은 상태가 남지 않음
2. unsupported 기능은 남겨도 의도적으로 남긴 것임이 코드에 드러남

## 테스트 전략 재정의

현재 방향은 "core 테스트를 .NET에서 최대한 재현"하는 것이 아니라,
".NET 바인딩이 최신 core 계약을 제대로 감싸는지"만 검증하는 쪽으로 바꿔야 한다.

즉, 역할 분리는 아래처럼 가져간다.

1. `core/tests`: transport/protocol/reconnect/HWM/corner case의 본체 검증
2. `bindings/dotnet/samples`: 사용자-facing 사용 예제와 실행 확인
3. `bindings/dotnet/tests`: 바인딩 전용 contract/smoke 자동 검증

이 구조를 택하는 이유는 다음과 같다.

1. core 동작 재검증을 바인딩 레이어에서 반복하면 유지비가 과도하게 커진다.
2. samples만 두면 자동 실패 판정이 약하다.
3. 바인딩 테스트는 marshalling, 수명, delegate callback, option mapping 같은
   wrapper 전용 위험만 잡아야 가장 싸고 오래 간다.

권장 디렉터리 방향:

1. `samples/`
   - `Zlink.Samples.sln`
   - `SampleCommon/`
   - `PairRecv/`
   - `PairCallback/`
   - `PubSubRecv/`
   - `PubSubCallback/`
   - `DealerRouterRecv/`
   - `DealerRouterCallback/`
   - `StreamRecv/`
   - `StreamCallback/`
   - `SpotRecv/`
   - `SpotCallback/`
   - 필요 시 `RegistryDiscoveryMonitor/`
2. `tests/`
   - `message` contract
   - `socket` contract
   - `callback-mode` contract
   - `options` contract
   - `monitor` contract
   - `service-wrapper` contract

삭제 기준:

1. core transport matrix를 사실상 다시 검증하는 테스트는 제거
2. reconnect/HWM/protocol 세부 정책을 다시 재현하는 테스트는 제거
3. 바인딩 wrapper가 아니라 core correctness만 보는 테스트는 제거

유지 기준:

1. C API는 맞아도 .NET wrapper에서만 깨질 수 있는 테스트는 유지
2. `IDisposable`, ownership, delegate lifetime, typed option 변환,
   callback attach 배타성, monitor/event marshalling, service wrapper lifecycle은 유지

샘플 구성 원칙:

1. 샘플은 프로젝트 단위로 쪼개고, 각 프로젝트는 하나의 패턴과 하나의 수신 모델만 보여준다.
2. 하나의 샘플 안에 여러 패턴이나 여러 실행 모드를 섞지 않는다.
3. 공통 유틸리티는 `SampleCommon/`에 둔다.
   - endpoint 생성
   - 로그/출력 helper
   - 공통 payload builder
   - 간단한 실행 인자 파서
4. 각 샘플은 "최소 실행 흐름"만 보여준다.
   - 생성
   - bind/connect 또는 bind/set subscription
   - send/publish
   - recv 또는 callback 처리
   - 정리
5. 샘플은 correctness 자동 판정보다 사용 예제 역할을 우선한다.
   자동 검증은 `tests/`가 맡는다.

권장 샘플 산출물:

1. 각 샘플은 독립 `csproj`로 둔다.
2. 루트 `samples/`에는 전체 실행용 README와 스크립트를 둔다.
3. `run_samples.sh`와 `run_samples.ps1`를 제공해 대표 샘플을 순차 실행한다.

권장 초기 샘플 집합:

1. `PairRecv`: 가장 단순한 raw recv 예제
2. `PairCallback`: recv callback attach 예제
3. `PubSubRecv`: subscription + direct subscribe 예제
4. `PubSubCallback`: `SubscribeHandler` 예제
5. `DealerRouterRecv`: routing id와 multipart recv 예제
6. `DealerRouterCallback`: callback + send-ready 조합 예제
7. `StreamRecv`: raw stream recv/send_rid 예제
8. `StreamCallback`: raw stream callback 예제
9. `SpotRecv`: unified `Spot`의 recv mode 예제
10. `SpotCallback`: unified `Spot`의 callback mode 예제
11. `RegistryDiscoveryMonitor`: registry bind, discovery connect,
    service monitor open, snapshot/query를 한 번에 보여주는 운영 예제

샘플별 최소 구현 계약:

1. 각 샘플은 `dotnet run --project ...` 한 번으로 실행 가능해야 한다.
2. 각 샘플 README에는 실행 순서, 프로세스 수, 예상 출력이 있어야 한다.
3. `recv` 샘플은 blocking/polling recv 흐름만 보여준다.
4. `callback` 샘플은 callback attach 이후 direct recv를 하지 않는다.
5. `stream` 샘플은 raw callback만 사용하고 LEN32BE helper를 사용하지 않는다.
6. `spot` 샘플은 unified `Spot`만 사용하고 standalone pub/sub 전제를 두지 않는다.

## Phase 5. 샘플과 contract test 중심 검증 체계로 교체

목표: 대량 포팅 테스트를 걷어내고, `samples + 소수 contract test` 구조로
검증 체계를 단순화한다.

작업:

1. 기존 포팅성 테스트를 분류한다.
   - 삭제 대상: core correctness 재검증 성격 테스트
   - 유지 대상: wrapper 전용 contract test
   - 샘플 전환 대상: 사용자-facing 패턴 테스트
2. `samples/`를 신설하고 패턴별 예제를 추가한다.
   - raw socket 패턴은 `recv` / `callback`을 각각 제공
   - `pair`, `pubsub`, `dealer-router`, `stream`, `spot`을 최소 커버
   - monitor/discovery/registry는 별도 샘플로 소수 제공
   - 공통 helper는 `samples/SampleCommon/`으로 분리
   - 각 샘플은 독립 `csproj`와 짧은 README를 가진다
   - 전체 샘플 실행용 `run_samples.sh` / `run_samples.ps1`를 제공한다
3. `tests/`는 얇은 contract/smoke test만 남긴다.
   - native load / version smoke
   - context option roundtrip
   - multipart send/recv marshalling
   - recv callback attach 후 direct recv가 `EBUSY`
   - send-ready callback attach 후 `POLLOUT` 경로 제약
   - socket option set/get 및 routing id/tls family-specific option
   - socket monitor open/recv/handler/snapshot
   - service monitor/event marshalling
   - discovery metadata/value roundtrip
   - socket attach discovery lifecycle 제약
   - spot publish/subscribe recv mode
   - spot subscribe handler callback mode
   - spot node snapshot/query
   - registry topology snapshot/query
   - callback delegate lifetime / callback exception propagation
4. `Receiver` 기반 테스트는 제거하고 새 service 모델 테스트로 대체한다.
5. test helper 정리
   - 고정 대기/재시도성 로직을 줄이고 deterministic wait helper 사용
   - fail-fast 정책 유지

파일 단위 테스트 정리 계획:

1. 삭제
   - `tests/Zlink.Tests/test_service_discovery.cs`
2. 대체 또는 대폭 수정
   - `tests/Zlink.Tests/test_stream_socket.cs`
   - `tests/Zlink.Tests/test_socket_options.cs`
3. 유지하되 최신 계약에 맞게 수정
   - `tests/Zlink.Tests/test_ctx_options.cs`
   - `tests/Zlink.Tests/test_message.cs`
   - `tests/Zlink.Tests/test_pair_tcp.cs`
   - `tests/Zlink.Tests/test_pubsub.cs`
   - `tests/Zlink.Tests/test_router_multiple_dealers.cs`
   - `tests/Zlink.Tests/test_spot_pubsub_basic.cs`
   - `tests/Zlink.Tests/test_system.cs`
4. 신규 추가 권장
   - `test_callback_contract.cs`
   - `test_monitor_contract.cs`
   - `test_service_monitor_contract.cs`
   - `test_topology_contract.cs`
   - `test_attach_discovery_contract.cs`

테스트 수용 기준:

1. 각 contract test는 "wrapper가 깨졌는가"만 확인해야 한다.
2. 하나의 테스트가 여러 transport/protocol 매트릭스를 재현하면 범위를 줄인다.
3. flakiness를 줄이기 위해 fixed sleep보다 bounded wait helper를 우선한다.
4. callback 예외는 `Runtime.UnhandledCallbackException` 경로로만 검증한다.

권장 검증 순서:

1. `dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj`
2. 샘플 프로젝트 또는 샘플 실행 스크립트로 주요 패턴 smoke 실행
3. Linux x64에서 우선 green 확보
4. packaging output을 통해 각 runtime native 로드 확인

완료 기준:

1. core 포팅성 대량 테스트가 제거됨
2. 사용자-facing 샘플이 패턴별로 정리됨
3. 샘플 디렉터리가 project-per-pattern 구조로 정리됨
4. 바인딩 전용 contract test만 남음
5. 서비스/모니터/멀티파트/콜백 회귀가 자동화됨
6. 대표 hot path 설계 제약이 문서와 코드에 반영됨

## Phase 6. 패키징, 문서, 마이그레이션 공지

목표: 사용자 입장에서 바뀐 점을 설명 가능한 상태로 마감한다.

작업:

1. `Zlink.csproj` 버전과 release note 반영
2. breaking changes 문서 작성
   - 제거된 타입: `Receiver` 등
   - 이름 변경: `SetEndpoints+Start -> Bind`, `ConnectPeerPub -> ConnectPeer`
   - 동작 변경: multipart 기본화, callback ownership, monitor open model
3. 최소 사용 예제 업데이트
   - raw socket
   - spot
   - discovery/registry
   - monitor
4. `[Obsolete]` 유예 없이 breaking change를 문서로 공지한다.

완료 기준:

1. 바뀐 managed 계약을 사용자가 문서만 읽고 따라갈 수 있음
2. 기존 사용자에게 어떤 코드 수정이 필요한지 명확함

## 7. 권장 구현 순서

리스크를 줄이려면 아래 순서를 권장한다.

1. Phase 0: 계약 동결
2. 삭제 작업 선반영
   - `Receiver.cs`
   - `Timers.cs`
   - 구형 P/Invoke 선언
3. Phase 1: interop 전면 교체
4. Phase 2: core managed facade
5. Phase 3: service 계층
6. Phase 4: stream/poller/timer 정리
7. Phase 5: 테스트 정렬
8. `samples/` 추가
9. Phase 6: 패키징/문서

핵심 이유:

1. interop를 먼저 고치지 않으면 그 위의 모든 facade 작업이 흔들린다.
2. service 계층은 core/socket/message/monitor contract 위에 올라가므로 뒤에 두는 것이 안전하다.
3. 테스트는 구형 surface를 강하게 고착하고 있으므로, 구현과 함께 재편해야 한다.

권장 커밋 또는 PR 분할:

1. PR 1: 삭제와 interop 정리
2. PR 2: core facade 재작성
3. PR 3: service facade 재작성
4. PR 4: poller/stream 정리
5. PR 5: tests 재편
6. PR 6: samples + 문서

## 8. 주요 위험과 대응

### 위험 1. callback ownership과 GC 수명 관리

위험:

1. native callback에서 받은 `zlink_msg_t*`를 언제 복사하고 언제 close할지 불명확하면
   double-close 또는 leak가 난다.

대응:

1. callback 전달 모델을 하나로 고정한다.
   - managed copy 후 native close
2. 모든 callback 경로에 공통 helper를 둔다.

### 위험 2. 구형 API 호환성 욕심

위험:

1. `Receiver`, old stream helper, old poller entrypoint를 다 살리려 하면
   복잡도만 늘고 계약 설명이 어려워진다.

대응:

1. 최신 core에 없는 모델은 삭제한다.

### 위험 3. option 타입 불일치

위험:

1. `int`, `long`, `ulong`, `string`, `byte[]` 구분이 틀리면 런타임 오류가 난다.

대응:

1. enum-옵션형 매핑표를 먼저 만들고 테스트로 잠근다.

### 위험 4. attach 후 lifecycle 제약 누락

위험:

1. discovery attach 이후 `close/connect/unbind` 제한을 managed API가 숨기면
   사용자 입장에서 예측 불가능해진다.

대응:

1. 예외 메시지와 XML-doc에 attach lifecycle 제약을 명시한다.

### 위험 5. .NET 편의 API가 hot path 비용을 숨김

위험:

1. `byte[]`/`string`/generic object convenience를 `Socket`에 직접 늘리면
   송수신 loop마다 encode/copy/allocation이 숨어 들어갈 수 있다.
2. `IEnumerable<Message>` 같은 surface를 허용하면 재열거와 임시 컬렉션 생성이
   구현 안쪽에서 발생하기 쉽다.

대응:

1. raw socket public API는 `Message`와 `IReadOnlyList<Message>`로 고정한다.
2. bytes/string convenience는 `Message`에만 두고, `Socket`은 행위만 담당하게 한다.
3. 코드 리뷰에서 숨은 allocation/copy가 없는지 확인한다.

## 9. 버전 정책

이번 변경은 사실상 managed public contract 재정렬이다.

1. NuGet/package 버전은 patch가 아니라 minor 이상으로 올린다.
2. breaking change 문서와 migration note를 같은 변경셋에 포함한다.
3. `Obsolete` 유예보다 명시적 제거를 기본으로 한다.

## 10. 최종 완료 정의

아래가 모두 만족되면 작업 완료로 본다.

1. `.NET` 바인딩이 최신 `libzlink` export와 정합한다.
2. `.NET` public surface가 `doc/guide` 최신 설명과 의미적으로 맞는다.
3. 존재하지 않는 네이티브 심볼에 대한 P/Invoke가 남아 있지 않다.
4. `Receiver` 등 구형 서비스 모델 정리가 끝났다.
5. 서비스/모니터/멀티파트/콜백/옵션 테스트가 green이다.
6. breaking change와 사용 예제가 문서화되었다.

## 11. 실무적으로 가장 먼저 할 일

착수 직후 바로 해야 할 5가지는 아래다.

1. `Receiver.cs`, `Timers.cs`, 관련 P/Invoke와 테스트를 삭제 목록으로 확정
2. `NativeMethods`와 `NativeTypes`를 최신 header/export 기준으로 다시 세운다.
3. `Enums.cs` / `SocketOptions.cs`를 최신 header 기준으로 재생성
4. `Socket`, `Message`, `Monitor`를 최신 multipart/monitor 모델로 재설계
5. `Registry`, `Discovery`, `Spot`, `SpotNode`를 최신 service 모델로 다시 잇기

이 순서로 가야 change amplification이 가장 작다.

## 12. 구현 착수 체크리스트

이 체크리스트가 모두 채워지면 문서는 구현 착수 상태로 본다.

1. 삭제 대상 파일과 API가 확정되었다.
2. callback ownership 정책이 확정되었다.
3. `Receiver`, `Timers`, LEN32BE helper 처리 방향이 확정되었다.
4. `Poller` 1차 지원 범위가 raw socket + fd로 확정되었다.
5. `tests/`를 contract-only로 축소하는 기준이 확정되었다.
6. `samples/` 프로젝트 구조가 확정되었다.
7. 파일 단위 작업 순서가 명시되었다.
8. 버전 정책이 minor 이상 상승으로 확정되었다.

## 13. 착수 후 실행 명령

구현 중 검증은 아래 명령 순서를 기본으로 한다.

1. 라이브러리 빌드
```bash
dotnet build /home/hep7/project/kairos/zlink/bindings/dotnet/Zlink.sln
```

2. contract test 실행
```bash
dotnet test /home/hep7/project/kairos/zlink/bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj
```

3. 대표 샘플 smoke 실행
```bash
dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/PairRecv/PairRecv.csproj
dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/PairCallback/PairCallback.csproj
dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/PubSubRecv/PubSubRecv.csproj
dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/SpotRecv/SpotRecv.csproj
```

실행 규칙:

1. interop 변경 직후에는 반드시 `dotnet build`를 먼저 통과시킨다.
2. service surface 변경 직후에는 `dotnet test`보다 먼저 native load 오류가 없는지 확인한다.
3. samples는 테스트 대체가 아니라 smoke 용도다.
4. 성능 관련 판단은 이번 범위에서 별도 벤치 실행이 아니라 코드 구조와 API shape 검토로 처리한다.

## 14. 파일별 실행 체크리스트

아래 체크리스트는 실제 구현 순서로 사용한다.

### 14.1 삭제

- [x] `src/Zlink/Service/Receiver.cs` 삭제
- [x] `src/Zlink/Timers.cs` 삭제
- [x] `tests/Zlink.Tests/test_service_discovery.cs` 삭제
- [x] `src/Zlink/Native/NativeMethods*.cs`에서 `zlink_receiver_*` 제거
- [x] `src/Zlink/Native/NativeMethods*.cs`에서 `zlink_setsockopt`, `zlink_getsockopt` 제거
- [x] `src/Zlink/Native/NativeMethods*.cs`에서 `zlink_stream_attach_len32be`, `zlink_stream_send`, `zlink_stream_send_msg` 제거
- [x] `src/Zlink/Native/NativeMethods*.cs`에서 `zlink_spot_pub_*`, `zlink_spot_sub_*` 제거
- [x] `src/Zlink/Native/NativeMethods*.cs`에서 `zlink_poller_add_spot_*`, `modify_spot_*`, `remove_spot_*`, `*_receiver` 제거

### 14.2 Native 계층

- [x] `src/Zlink/Native/NativeMethods.Core.cs` 생성 또는 정리
- [x] `src/Zlink/Native/NativeMethods.Socket.cs` 생성 또는 정리
- [x] `src/Zlink/Native/NativeMethods.Monitor.cs` 생성 또는 정리
- [x] `src/Zlink/Native/NativeMethods.Service.cs` 생성 또는 정리
- [x] `src/Zlink/Native/NativeTypes.cs` 업데이트
- [x] `src/Zlink/Native/NativeServiceModels.cs` 추가
- [x] `src/Zlink/Native/NativeLibraryLoader.cs` export 확인 로직 점검

### 14.3 Core facade

- [x] `src/Zlink/Message.cs`를 ownership 계약에 맞게 재작성
- [x] `src/Zlink/Socket.cs`를 `Send` / `Receive` overload 중심으로 재작성
- [x] `src/Zlink/Monitor.cs`를 `SocketMonitor` 중심으로 정리
- [x] `src/Zlink/Enums.cs`를 최신 header 기준으로 정렬
- [x] `src/Zlink/SocketOptions.cs`를 최신 option 분류에 맞게 정리
- [x] `src/Zlink/SocketOptionValidation.cs` 갱신
- [x] `src/Zlink/RoutingIdCodec.cs`를 public string form 기준으로 고정
- [x] `src/Zlink/Context.cs`에 `ZLINK_CTX_OPT_BLOCKY` 반영
- [x] `src/Zlink/Errors.cs`에 예외 규약 반영

### 14.4 Service facade

- [x] `src/Zlink/Service/Registry.cs`를 `Bind` 중심 모델로 재작성
- [x] `src/Zlink/Service/Discovery.cs`를 `serviceType + serviceName` 모델로 재작성
- [x] `src/Zlink/Service/Spot.cs`를 unified `Spot` 중심으로 재작성
- [x] `src/Zlink/Service/ServiceMonitor.cs` 추가
- [x] `src/Zlink/Service/RegistryQueryClient.cs` 추가
- [x] `src/Zlink/Service/TopologyModels.cs` 추가
- [x] `Socket.AttachDiscovery(Discovery)` 구현

### 14.5 부가 API

- [x] `src/Zlink/Poller.cs`를 raw socket + fd 전용으로 축소
- [x] `src/Zlink/Runtime.cs` interop 시그니처 검증
- [x] `src/Zlink/AtomicCounter.cs` interop 시그니처 검증
- [x] `src/Zlink/ZlinkStopwatch.cs` interop 시그니처 검증

### 14.6 Tests

- [x] `tests/Zlink.Tests/test_message.cs` 최신 ownership 계약 기준으로 수정
- [x] `tests/Zlink.Tests/test_socket_options.cs` 최신 option 구조 기준으로 수정
- [x] `tests/Zlink.Tests/test_stream_socket.cs` raw stream callback 기준으로 수정
- [x] `tests/Zlink.Tests/test_ctx_options.cs` 유지 및 갱신
- [x] `tests/Zlink.Tests/test_pair_tcp.cs` 유지 및 갱신
- [x] `tests/Zlink.Tests/test_pubsub.cs` 유지 및 갱신
- [x] `tests/Zlink.Tests/test_router_multiple_dealers.cs` 유지 및 갱신
- [x] `tests/Zlink.Tests/test_system.cs`에 `Runtime`, `AtomicCounter`,
  `ZlinkStopwatch` interop contract 보강
- [x] `tests/Zlink.Tests/test_spot_pubsub_basic.cs` 유지 및 갱신
- [x] `tests/Zlink.Tests/test_system.cs` 유지 및 갱신
- [x] `tests/Zlink.Tests/test_callback_contract.cs` 추가
- [x] `tests/Zlink.Tests/test_monitor_contract.cs` 추가
- [x] `tests/Zlink.Tests/test_service_monitor_contract.cs` 추가
- [x] `tests/Zlink.Tests/test_topology_contract.cs` 추가
- [x] `tests/Zlink.Tests/test_attach_discovery_contract.cs` 추가

### 14.7 Samples

- [x] `samples/Zlink.Samples.sln` 추가
- [x] `samples/SampleCommon/` 추가
- [x] `samples/PairRecv/PairRecv.csproj` 추가
- [x] `samples/PairCallback/PairCallback.csproj` 추가
- [x] `samples/PubSubRecv/PubSubRecv.csproj` 추가
- [x] `samples/PubSubCallback/PubSubCallback.csproj` 추가
- [x] `samples/DealerRouterRecv/DealerRouterRecv.csproj` 추가
- [x] `samples/DealerRouterCallback/DealerRouterCallback.csproj` 추가
- [x] `samples/StreamRecv/StreamRecv.csproj` 추가
- [x] `samples/StreamCallback/StreamCallback.csproj` 추가
- [x] `samples/SpotRecv/SpotRecv.csproj` 추가
- [x] `samples/SpotCallback/SpotCallback.csproj` 추가
- [x] `samples/RegistryDiscoveryMonitor/RegistryDiscoveryMonitor.csproj` 추가
- [x] sample 각 디렉토리에 짧은 `README.md` 추가
- [x] `samples/run_samples.sh` 추가
- [x] `samples/run_samples.ps1` 추가

### 14.8 Solution / project wiring

- [x] `bindings/dotnet/Zlink.sln`에서 제거된 프로젝트 참조 정리
- [x] `bindings/dotnet/Zlink.sln`에는 라이브러리와 테스트만 유지하고, 샘플은 `samples/Zlink.Samples.sln`에만 둔다
- [x] `src/Zlink/Zlink.csproj`에서 제거된 소스 파일 정리
- [x] `src/Zlink/Zlink.csproj`가 신규 `ServiceMonitor.cs`, `RegistryQueryClient.cs`, `TopologyModels.cs`, `NativeServiceModels.cs`를 포함하는지 확인
- [x] `tests/Zlink.Tests/Zlink.Tests.csproj`가 삭제/신규 테스트 파일 상태와 맞는지 확인
- [x] `samples/Zlink.Samples.sln`에서 각 sample project reference 확인
- [x] sample 각 `csproj`에 `../../src/Zlink/Zlink.csproj` project reference 추가

### 14.9 최종 확인

- [x] `dotnet build` green
- [x] `dotnet test` green
- [x] 대표 samples smoke green
- [x] breaking change 문서 반영 완료
- [x] public API에 `IntPtr`/native struct/native enum 이름이 직접 노출되지 않음
- [x] public 메서드 이름이 `.NET` 관례에 맞게 PascalCase와 overload 중심으로 정리됨
- [x] `Socket`은 행위(`Send`/`Receive`) 중심, `Message`는 data convenience 중심으로 정리됨
- [x] raw socket hot path에 `IEnumerable`, LINQ, serializer convenience, per-call delegate allocation이 없음

## 15. API review gate

아래 질문에 모두 `예`라고 답할 수 있어야 public API review를 통과한 것으로 본다.

1. 이 타입은 정말 public이어야 하는가?
2. 이 메서드 이름은 C API 번역체가 아니라 .NET 사용자에게 자연스러운가?
3. 같은 의미를 overload로 표현할 수 있는데 이름을 불필요하게 늘리지는 않았는가?
4. `Socket`은 행위, `Message`는 data convenience라는 경계를 지켰는가?
5. caller가 native 수명 규칙이나 native 메모리 표현을 알 필요가 없는가?
6. caller-allocated buffer 패턴 없이도 같은 기능을 제공하는가?
7. 오류가 .NET 예외 규약으로 일관되게 표현되는가?
8. dispose 이후 접근 규약이 일관적인가?
9. callback이 native ownership 규칙을 직접 다루지 않아도 되는가?
10. 이 API는 최신 `core/include/zlink.h`와 `doc/guide` 계약을 벗어나지 않는가?
11. 이 API shape가 hot path에 숨은 allocation, boxing, 중복 copy를 만들지 않는가?
12. bytes/string convenience가 `Message`에 머물고 `Socket`까지 번지지 않았는가?

하나라도 `아니오`면 public shape를 다시 줄이거나 이름을 다시 다듬는다.
