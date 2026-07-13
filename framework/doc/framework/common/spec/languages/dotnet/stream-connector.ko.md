<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ASP.NET Core STREAM](system-structure.ko.md)
<!-- framework-adapter-nav:end -->

[.NET spec 목차](README.ko.md)

# .NET Stream Connector 공개 계약

> 이 문서는 [Stream Connector 공통 스펙](../../stream-connector.ko.md)의 **`.NET` 투영**이다.
> transport·wire·생명주기·오류 의미는 공통 스펙이 소유하고, 이 문서는 그 의미가 `.NET`에서 갖는
> **정확한 public 표면**을 고정한다.

## 1. 목적과 package 경계

Stream Connector는 ZLink STREAM 서버에 연결하는 client 전용 모듈이다. 서버 framework의
session packet과 같은 header, codec, compression 및 종료 사유를 사용하지만 ASP.NET Core host,
Spot, actor와 location runtime에는 의존하지 않는다.

공개 package는 `Systems.Zlink.Stream.Connector`다. 정확한 public 타입과 시그니처 및 배포 archive는
다음 고정 snapshot이 소유한다.

- [API snapshot](../../../../../../languages/dotnet/contract/api/Systems.Zlink.Stream.Connector.api.txt)
- [package snapshot](../../../../../../languages/dotnet/contract/packages/Systems.Zlink.Stream.Connector.package.txt)

이 문서는 snapshot의 각 member를 반복해서 나열하지 않고, 사용자가 관찰하는 동작과 옵션 의미를
고정한다. connector 생성은 `ZlinkStreamConnectorFactory.Create(options)`를 사용하고 반환 타입은
`IZlinkStreamConnector`다.

### 1.1 대상 실행 환경

**엔진 × 빌드 타깃별 담당 connector는 [공통 스펙 §2](../../stream-connector.ko.md)가 소유한다.**
그 배정에 따라 `.NET` connector가 담당하는 것은 **네이티브 빌드**(데스크톱·서버 애플리케이션,
Unity, Godot C#)이며, 웹(브라우저·WASM) 빌드는 담당하지 않는다.

이 배정이 `.NET` 표면에 남기는 결과는 하나다. 게임 엔진 객체를 main thread 밖에서 다룰 수
없으므로 **dispatch mode 기본값이 `Manual`** 이고(§3), 사용자가 main thread에서
`Dispatch.Async()`로 펌프한다. 엔진별 사용법은
[.NET Stream Connector 가이드](../../../../../stream-connector/dotnet/guide/INDEX.ko.md)가 다룬다.

## 2. lifecycle과 완료 의미

`Connect`, `Close`, `Dispatch`는 `IZlinkStreamLifecycleCall`을 반환한다. 호출자는
`Async(CancellationToken)`으로 해당 작업의 완료를 기다린다. `DisposeAsync()`는 connector가 소유한
transport, callback 작업과 observer 작업의 최종 정리를 기다린다.

- `Connect.Async(...)`는 연결과 receive loop 준비가 끝나면 완료된다.
- callback 밖에서 호출한 `Close.Async(...)`는 연결 종료와 terminal callback 정리가 끝나면 완료된다.
- callback 안의 `Close.Async(...)`는 순환 대기를 피하기 위해 종료를 시작한 뒤 즉시 반환한다. 이후
  callback 밖에서 호출한 `Close.Async(...)` 또는 `DisposeAsync()`가 공유 terminal 결과를 기다린다.
- 반복된 `Close`와 `DisposeAsync()` 호출은 같은 terminal 결과 또는 실패를 공유한다.
- callback 안에서 `Close`를 요청할 수 있지만, callback 안에서 `DisposeAsync()`로 자기 callback의
  종료를 기다리는 순환 대기는 허용하지 않고 즉시 오류로 처리한다.
- lifecycle waiter의 `CancellationToken`은 그 waiter만 취소하며 이미 시작된 공유 종료 작업을
  취소하지 않는다.

`ErrorReceived`, `Disconnected`, `ConnectionStateChanged`는 비동기 event다. handler는 등록 순서대로
호출되며 handler 실패는 connector runtime을 종료하지 않고 `UserCallbackFailed` 오류로 보고한다.

## 3. dispatch mode와 bounded admission

기본 dispatch mode는 `Manual`이다. 이 모드에서는 수신 callback, request callback과 lifecycle event가
`Dispatch.Async(...)`를 호출한 실행 문맥에서 처리된다. `Immediate`에서는 connector가 소유한
background dispatch 작업에서 처리한다.

`MaxPendingDispatchCallbacks`의 기본값은 1024다. 이 제한에는 수신 handler뿐 아니라 이미 수락된
request의 완료 callback을 보존하기 위한 예약 슬롯도 포함된다.

outbound frame은 dispatch callback 제한과 별개인 순서 보존 전송 queue를 사용한다. one-way
`Submit()`은 이 queue가 최대 4096개의 전송을 보관하는 범위에서만 수락되며, 더 수락할 수 없으면
즉시 오류를 발생시킨다. 같은 connector에서 먼저 수락한 send는 뒤에 시작한 request보다 먼저
전송된다. request는 자기 frame의 실제 write가 끝난 뒤 response를 기다린다. 전송을 background
thread의 callback 실행으로 우회하지 않는다.

`MaxReceivedMessages`의 기본값도 1024다. `WaitFor(...)`가 사용할 unread 수신 기록이 이 값을 넘으면
가장 오래된 unread 기록부터 제거한다. 이 history 제한은 response와 heartbeat 같은 control frame의
처리를 막지 않는다. `.NET` 계약에는 history 제거를 별도 `ZlinkStreamErrorCode`로 보고하는 member가
없다.

`PendingDispatchCount`는 아직 dispatch되지 않은 callback 수를, `ReceivedCount(name)`은 bounded unread
history에 남아 있는 같은 packet 이름의 수를 제공한다. wait가 message를 소비하면 이 값도 감소한다.
진단과 scenario assertion을 위한 값이며 application flow control에 사용하지 않는다.

## 4. send, request, wait와 handler

raw 경로는 `ZlinkStreamEncodedPayload`를 받는다. packet name과 metadata는 payload 객체가 아니라
operation builder가 소유한다.

```csharp
connector.Send(payload)
    .PacketName("profile.changed") // wire packet identity를 이 operation에 지정한다.
    .Metadata("tenant", "alpha")  // 전송 시점에 불변 snapshot으로 복사된다.
    .Compress()
    .Submit();                      // local bounded queue 수락만 확인한다.

var reply = await connector.Request(payload)
    .PacketName("profile.get")     // response를 연결할 request identity다.
    .Timeout(TimeSpan.FromSeconds(3))
    .Async(cancellationToken);      // response 또는 명시적 실패까지 기다린다.
```

request 기본 timeout은 30초다. `Timeout(...)`은 해당 request에만 적용한다. callback 기반 request
`Submit(...)`도 같은 결과를 전달하며 `Manual` mode에서는 callback이 `Dispatch.Async(...)`에서만
실행된다.

`On(name, handler)`은 장기 push handler를 등록하고 반환된 `IDisposable`로 해제한다.
`WaitFor(name)`은 다음 unread matching message 하나를 소비한다. 기본 wait timeout은 5초이며
`Timeout(...)`과 `Where(...)`로 해당 wait의 제한과 predicate를 정한다. production의 지속적인 push
처리는 `On(...)`을 사용하고 sample, CLI와 E2E의 한 번성 대기는 `WaitFor(...)`을 사용한다.

## 5. typed payload, codec과 packet identity

typed extension은 `Send<TPayload>`, `Request<TPayload>`, `On<TPayload>`와 `WaitFor<TPayload>`를 제공한다.
기본 payload codec은 JSON이다. `PayloadCodec`을 지정하면 typed encode/decode 전체가 그 codec을
사용한다.

typed packet identity는 configured `IZlinkStreamPacketNameResolver`가 결정한다. 기본 resolver는
`ZlinkStreamPacketNameAttribute`를 우선하고 attribute가 없으면 타입 이름을 사용한다. 사용자 resolver는
자체 정책을 적용할 수 있다. `.NET` connector는 이미 encode한 raw payload와 외부 protocol interop을
위해 operation별 `PacketName(...)`을 허용한다. 이는 server framework의 typed registration descriptor
계약과 역할이 다르며, server handler call site에 packet 이름을 다시 노출하는 근거가 아니다.

metadata는 전송 전에 불변 snapshot으로 복사된다. typed decode 이후에도 connector 내부 buffer나
mutable transport header를 공개하지 않는다.

## 6. transport와 TLS

endpoint URI scheme이 실제 transport를 결정한다.

| scheme | transport |
|--------|-----------|
| `tcp://` | `Tcp` |
| `tls://` | `Tls` |
| `ws://` | `WebSocket` |
| `wss://` | `WebSocketSecure` |

nullable `Transport` option은 별도 선택 경로가 아니라 URI scheme과 설정이 일치하는지 확인하는
보조 값이다. 둘이 다르면 `ConfigurationError`로 실패한다. 기본 connect timeout은 5초다.

TLS와 WSS는 기본적으로 인증서 chain과 host name을 검증한다.
`SkipServerCertificateValidation`의 기본값은 `false`이며 테스트의 자체 서명 인증서에만 사용한다.

## 7. heartbeat, reconnect와 종료 사유

heartbeat와 reconnect의 **기본값은 [공통 스펙 §6.1](../../stream-connector.ko.md)이 소유한다.**
reconnect는 같은 endpoint에만 수행하며 서버가 대체 endpoint를 전달하는 계약은 없다.

종료 사유의 **값 집합과 의미는 [공통 스펙 §6.2](../../stream-connector.ko.md)가 소유한다.**
`.NET`은 이 값을 `ZlinkStreamCloseReason` enum으로 표현하고
**`Disconnected` event의 인자 `ZlinkStreamDisconnected.CloseReason`으로 노출한다.**

`session-closing` frame의 wire 값은 1~6이고 `.NET` enum의 내부 ordinal은 0~5다. codec이 둘을
명시적으로 변환하므로 **enum을 정수로 cast해 wire 값으로 사용하지 않는다.**

## 8. compression과 payload 제한

압축 기본값과 `None`의 의미는 [공통 스펙 §8](../../stream-connector.ko.md)이 소유한다.
`.NET`은 `CompressionCodec`을 지정하면 built-in codec 대신 해당 구현을 사용하고, `.Compress()`가
압축을 명시적으로 요청하는 call 표면이다.

payload 한도는 [공통 스펙 §4.7](../../stream-connector.ko.md)이 소유한다. `.NET`에서 수신 frame이
제한을 넘으면 application handler나 request completion으로 전달하지 않고 `FrameTooLarge` 오류로
현재 연결을 종료한다.
disconnect 사유는 `TransportError`이며 reconnect가 활성화되어 있으면 같은 endpoint로 재연결을
시도한다.

## 9. inbound observer

`ObserveInbound(...)`은 연결 시작 전에만 등록한다. observer는 message kind, packet name, codec,
request sequence, metadata, payload 길이, compression 여부, 수신 시각과 설정된 길이의 preview를
불변 snapshot으로 받는다. preview 기본 길이는 0이다.

- observer는 frame을 drop, 변환하거나 reply할 수 없다.
- observer callback에서는 connector의 send, request, wait와 dispatch를 호출하지 않는다.
- callback은 receive path에서 직접 실행되지 않는다.
- callback 실패는 `ObserverFailed`, bounded queue overflow는 `ObserverDropped`로 보고한다.
- 이 오류는 원래 frame의 request completion이나 message dispatch를 막지 않는다.
- observer queue 기본 크기는 1024다.
- dispose는 cancellation을 무시하고 실행 중인 observer도 종료할 때까지 기다린다.

## 10. flow와 wire header

connector outbound operation은 별도 public 옵션 없이 UUIDv7 `flow_id`를 한 번 생성한다. callback
안에서 시작한 후속 operation은 현재 inbound flow를 재사용하고 callback이 끝나면 ambient flow를
정리한다.

wire header는 필수 `0xF2` marker와 versioned flags를 사용한다. 알 수 없는 mandatory flag,
유효하지 않은 flow 값과 control frame의 flow field는 protocol 오류다. raw header 객체는 connector
public API에 노출하지 않는다.

## 11. metric

connector metric은 [runtime metric 공통 계약](../../runtime-metrics.ko.md)의 STREAM catalog와 닫힌
label을 따른다. connector는 reconnect 시도 횟수, handshake 시간과 실패 횟수, inbound/outbound wire
byte 수를 기록한다. metric listener 실패는 send/request 결과나 연결 상태를 바꾸지 않는다.

## 12. 기본값과 검증

| option | 기본값 | 검증 |
|--------|--------|------|
| `ConnectTimeout` | 5초 | 양수 |
| `RequestTimeout` | 30초 | 양수 |
| `WaitTimeout` | 5초 | 양수 |
| `MaxSendPayloadSize` | 64 KiB | 양수 |
| `MaxReceivePayloadSize` | 64 KiB | 양수 |
| `MaxReceivedMessages` | 1024 | 양수 |
| `MaxPendingDispatchCallbacks` | 1024 | 양수 |
| `MaxInboundObserverNotifications` | 1024 | 양수 |
| `MaxInboundObserverPayloadPreviewBytes` | 0 | 음수 불가 |
| `DispatchMode` | `Manual` | public 지원 값은 `Manual`, `Immediate` |
| `Compression` | `Lz4` | public 지원 값은 `None`, `Lz4` |

endpoint가 없으면 `ArgumentException`으로 실패한다. 지원하지 않는 scheme과 URI scheme/`Transport`
불일치는 연결을 시작하기 전에 `ZlinkStreamException`의 `ConfigurationError`로 실패한다. 유효하지
않은 timeout, queue 크기와 heartbeat/reconnect 조합은 `ValidationFailed`로 실패한다.

## 13. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `StreamConnectorTests.ConnectorImplementationIsHiddenBehindPublicInterface` | 구현 타입은 숨기고 factory가 public interface를 반환한다. |
| `StreamConnectorTests.ConnectorCallInterfacesMatchTheFrozenSurface` | lifecycle, send, request와 wait call의 정확한 member를 고정한다. |
| `StreamConnectorTests.ConnectorOptionsMatchTheFrozenDefaults` | connector option의 기본값을 고정한다. |
| `StreamConnectorTests.ManualDispatchRunsHandlerOnDispatchCaller` | Manual callback은 dispatch caller에서 실행된다. |
| `StreamConnectorTests.ImmediateDispatchRunsHandlerWithoutManualDispatch` | Immediate callback은 별도 manual dispatch 없이 실행된다. |
| `StreamConnectorTests.ManualRequestCallbackAdmission_Is_Bounded_And_Never_Falls_Back_To_A_Background_Thread` | request callback admission은 bounded이며 background 우회를 허용하지 않는다. |
| `StreamConnectorTests.RequestTimeoutRemovesPendingRequest` | timeout 뒤 pending request를 제거한다. |
| `StreamConnectorTests.TcpTypedRequestCorrelatesResponse` | typed request와 response correlation을 유지한다. |
| `StreamConnectorTests.TypedConnectorUsesJsonByDefaultAndDecodeReply` | typed 기본 codec은 JSON이다. |
| `StreamConnectorTests.PacketNameAttributeIsUsedByDefault` | packet name attribute를 기본 identity로 사용한다. |
| `StreamConnectorTests.DisconnectEventCarriesTheFrozenCloseReasonContract` | disconnect event의 닫힌 종료 사유를 고정한다. |
| `StreamConnectorTests.SessionClosingPublishesServerDrainReasonAfterDisconnectedState` | session-closing frame을 `ServerDrain` 사유로 변환한다. |
| `StreamConnectorTests.SharedCloseFaultIsObservedByRepeatedCloseAndDispose` | 반복 close와 dispose가 같은 실패를 관찰한다. |
| `StreamConnectorTests.OneWaySubmit_Accepts_Into_A_Bounded_Queue_And_Rejects_Full_Synchronously` | one-way submit은 bounded queue 수락 또는 동기 거부로 끝난다. |
| `StreamConnectorTests.RequestQueueWaitsForEarlierAcceptedOneWaySend` | 먼저 수락된 one-way send와 뒤 request의 wire 전송 순서를 보존한다. |
| `StreamConnectorTests.CallerCancellationDoesNotInterruptAnInProgressFrameWrite` | frame write가 시작된 뒤에는 caller cancellation이 partial frame을 만들지 않는다. |
| `StreamConnectorTests.InboundObserverRegistrationIsRejectedAfterConnectAndStopsAfterDispose` | observer 등록 시점과 해제 의미를 고정한다. |
| `StreamConnectorTests.Dispose_Waits_For_Cancellation_Ignoring_Inbound_Observer` | dispose는 cancellation을 무시하는 observer 종료를 기다린다. |
| `StreamConnectorTests.InboundObserverFailureReportsObserverFailedAndMessageStillDispatches` | observer 실패를 보고하면서 원래 message를 계속 처리한다. |
| `StreamConnectorTests.InboundObserverOverflowReportsObserverDroppedAndRequestStillCompletes` | observer overflow가 request 완료를 막지 않는다. |
| `StreamConnectorTests.OutboundFrameCreatesFlowOnceAndCodecRemainsDeterministic` | outbound flow를 한 번 생성하고 header codec 결과를 고정한다. |

G1과 G7에서는 `scripts/verify_packaged_contract.sh`로 source assembly, 위 API snapshot, 실제 NuGet
package와 clean consumer가 모두 같은 계약인지 검증한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md)
<!-- framework-adapter-nav:bottom:end -->
