<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ STREAM Decisions](./stream-open-items.ko.md) | [다음: Draft -- ZLink Framework C++ STREAM Samples](./stream-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [STREAM](./cpp-stream.ko.md) | [공통 Stream Connector](../../../../doc/spec/draft/streaming-client.ko.md)

# Draft -- ZLink Stream Connector For C++

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, C++용 `ZLink Stream Connector`를 별도 라이브러리와 별도
> 배포 단위로 어떻게 만들지 정리한다.

## 1. 위치

C++ Stream Connector는 `ZLink Framework for C++` 샘플이 아니다. 서버 framework와 같은
저장소에 있을 수는 있지만, public header, CMake target, package는 분리한다.

권장 배치는 아래와 같다.

```text
framework/languages/cpp/
+-- framework/
|   +-- include/zlink/framework/contracts/
|   +-- src/runtime/
+-- connector/
|   +-- include/zlink/stream_connector/
|   |   +-- contracts/
|   +-- src/
|   |   +-- runtime/
|   +-- tests/
|   +-- samples/
|   +-- CMakeLists.txt
+-- unreal-connector/
|   +-- Source/
|   |   +-- ZLinkStreamConnector/
|   |   |   +-- Public/
|   |   |   +-- Private/
|   +-- ZLinkStreamConnector.uplugin
|   +-- Tests/
|   +-- Samples/
+-- CMakeLists.txt
```

서버 framework package는 connector package를 필요로 하지 않는다. connector package도
서버 framework package를 필요로 하지 않는다. 양쪽은 STREAM header/payload wire 계약만
공유한다.

Unreal Connector도 별도 배포 단위다. 일반 C++ connector를 그대로 노출하는 wrapper가
아니라, Unreal 타입과 Unreal thread model에 맞춘 public API를 제공한다. 내부 wire
protocol, codec id, header/payload frame, heartbeat/reconnect 의미는 일반 C++ connector와
같게 유지한다.

connector의 public contract와 runtime 구현도 `.NET` Stream Connector의
`Contracts/*`와 `Runtime/*` 분리를 따른다. C++에서는 public header가
`include/zlink/stream_connector/contracts/*`에 있고, 구현은 `src/runtime/*`에 있다.
Unreal Connector는 Unreal 관례 때문에 `Public/`과 `Private/`를 쓰지만, 의미는 같다.
`Public/`에는 Unreal 전용 타입과 호출 표면만 두고, connection, receive loop, codec,
thread dispatch 구현은 `Private/`에 둔다.

connector도 framework와 같은 public surface gate를 적용한다. public header는 endpoint,
packet, request/send builder, callback/coroutine submit, codec option만 노출한다.
reconnect state, heartbeat scheduler, pending request table, frame encoder/decoder,
compression worker, Asio socket receive loop는 `src/runtime/*`에 둔다. Unreal Connector도
Blueprint/Game Thread 표면만 `Public/`에 두고, 일반 C++ connector runtime class를 그대로
public type으로 노출하지 않는다.

connector의 contract/runtime 분리는 framework보다 약하게 적용하지 않는다. connector는
별도 배포 라이브러리지만, 사용자는 client endpoint와 packet 호출 모델만 알아야 한다.
`connector_t`가 내부 connection state를 가져야 하면 public header에는 opaque state만
두고, reconnect loop, heartbeat timer, request correlation, frame codec, compression
worker는 runtime 구현에 둔다. Unreal Connector도 같은 원칙을 Unreal 방식으로 적용한다.
Unreal `Public/` header에는 `UObject`, Blueprint delegate, Game Thread callback처럼
Unreal 사용자가 직접 보는 표면만 두며, 일반 C++ connector runtime class를 상속하거나
멤버로 노출하지 않는다.

connector 구현도 시작 전에 owner를 아래처럼 나눈다.

| 기능 | C++ connector public owner | C++ connector runtime owner | Unreal public owner | Unreal private owner |
|------|----------------------------|-----------------------------|---------------------|----------------------|
| connector lifecycle | `contracts/zlink_stream_connector.hpp`, `contracts/zlink_stream_connector_factory.hpp` | `src/runtime/connector_lifecycle.*`, `src/runtime/transport/*` with Asio | `Public/ZLinkStreamConnector.h` | `Private/Connection/*` with Unreal Sockets |
| packet send/request | `contracts/calls/zlink_stream_calls.hpp`, `contracts/zlink_stream_models.hpp` | `src/runtime/calls/*`, `src/runtime/connector_runtime.*` | Blueprint callable send/request API | `Private/Messaging/*` with Unreal Sockets |
| callback/coroutine submit | `contracts/task.hpp`, callback overloads on call objects | `contracts/task.hpp` | Blueprint delegate, Game Thread callback | `Private/Dispatch/*` |
| codec option | `contracts/codec_registry.hpp`, `contracts/zlink_stream_enums.hpp` | `src/runtime/protocol/*`, `src/runtime/protocol/compression/*` | Unreal codec option types | `Private/Codecs/*` |
| reconnect/heartbeat | state event contract, options contract | `src/runtime/heartbeat_monitor.*`, `src/runtime/connector_lifecycle.*` | connection state delegate | `Private/Connection/*` |
| compression | packet option contract | `src/runtime/protocol/compression/*` | Unreal packet option | `Private/Compression/*` |

이 표의 public owner는 사용자 호출 shape와 option만 담는다. request correlation table,
receive loop, heartbeat scheduler, frame encoder/decoder, compression worker, Game Thread
queue 구현은 public header에 두지 않는다. 일반 C++ connector만 `task_t`와 `co_await`
기반 coroutine submit을 제공한다. Unreal Connector는 Unreal 사용자가 자연스럽게 쓰는
Blueprint delegate와 native multicast delegate callback만 public 표면으로 제공하며,
별도 coroutine API를 두지 않는다. 현재 C++ runtime 파일 분류는
`calls`, `protocol`, `protocol/compression`, `protocol/framing`, `transport`,
`connector_lifecycle`, `connector_runtime`, `heartbeat_monitor`를 기준으로 고정한다. 받은
packet을 즉시 dispatch할지 queue에 둘지는 `connector_runtime`의 helper가 소유하고, frame을
읽고 쓰는 구현은 `protocol/framing`과 `transport`에 둔다. 일반 C++ connector와 Unreal
Connector는 같은 wire 의미를 공유하지만 public 타입은 서로 독립이다.

## 2. 패키징

패키징 기준은 아래와 같다.

| 항목 | 서버 framework | C++ Stream Connector | Unreal Stream Connector |
|------|----------------|----------------------|-------------------------|
| CMake target | `zlink::framework` | `zlink::stream_connector` | Unreal module/plugin |
| public include | `zlink/framework/...` | `zlink/stream_connector/...` | Unreal plugin public headers |
| umbrella header | `zlink/framework.hpp` | `zlink/stream_connector.hpp` | `ZLinkStreamConnector.h` |
| 배포 단위 | framework server runtime | client connector library | Unreal plugin |
| 주요 사용자 | server application | C++ game/client application | Unreal game/client application |

codec은 connector 배포 단위에 포함하되, 사용하지 않는 codec dependency를 기본 connector
target에 강제로 붙이지 않는다. raw transport와 frame runtime은 `zlink::stream_connector`가
맡고, typed auto codec helper는 같은 배포물 안의 `zlink::stream_connector_codecs` target이
맡는다. MessagePack과 Protobuf는 build option이 켜지고 해당 C++ binding codec target이
있을 때만 `zlink::stream_connector_codecs`에 연결된다.

| 기능 | 포함 방식 | 기본값 |
|------|----------|--------|
| raw bytes | 항상 포함 | ON |
| JSON helper | connector package 안에 포함 | ON |
| MessagePack helper | connector package 안의 build feature | OFF |
| Protobuf helper | connector package 안의 optional build feature | OFF |
| LZ4 compression | connector package 안의 build feature, system LZ4 또는 fallback source | ON |

권장 CMake option은 아래와 같다.

```cmake
ZLINK_STREAM_CONNECTOR_WITH_JSON=ON
ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK=OFF
ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF=OFF
ZLINK_STREAM_CONNECTOR_WITH_LZ4=ON
```

`ZLINK_STREAM_CONNECTOR_WITH_LZ4=ON`이면 CMake는 먼저 system `lz4.h`와 `liblz4`를
찾는다. 개발 패키지가 없으면 LZ4 source를 받아 `zlink::stream_connector` target 안에
private source로 포함한다. 사용자는 별도 LZ4 target을 직접 링크하지 않는다.

## 3. 기능 기준

C++ Stream Connector는 공통 [ZLink Stream Connector](../../../../doc/spec/draft/streaming-client.ko.md)
초안과 `.NET` `Systems.Zlink.Stream.Connector`의 기능성을 C++20 방식으로 투영한다.

일반 C++ connector의 transport 구현은 Asio를 사용한다. Linux, macOS, Windows에서 같은
receive loop와 timer/reconnect 구조를 유지하기 위해 raw file descriptor나 OS별 socket
API를 connector runtime에 직접 흩어 놓지 않는다. 현재 저장소에서는 C++ binding이 이미
Boost include 경로를 제공하므로 Boost.Asio를 기본 구현 기반으로 사용한다.

현재 구현된 포함 기능은 아래와 같다.

- TCP transport
- connector 생성과 명시 connect
- connection state event
- reconnect
- heartbeat
- graceful close
- packet send
- typed send helper
- typed request/reply helper
- callback submit
- coroutine submit
- request timeout
- pending request correlation
- packet callback receive
- manual dispatch mode
- immediate dispatch mode
- metadata
- payload compression flag 처리
- max send payload size
- max metadata size
- connector instance별 독립 실행

TLS, WebSocket, WebSocket over TLS는 `transport_t`에 확장 지점으로 남아 있지만 현재
runtime이 지원하지 않는 transport로 명확히 실패한다. 지원하지 않는 transport를 조용히
TCP처럼 처리하지 않는 이유는 endpoint 보안과 handshake 의미가 달라서, 잘못된 성공이 실제
운영 장애로 이어질 수 있기 때문이다.

request/reply는 pending request table의 sequence로 response frame을 매칭한다. response가
`request_timeout` 안에 도착하지 않으면 pending request를 제거하고 `request_timeout`
error를 반환한다. heartbeat는 별도 background thread를 만들지 않고, manual dispatch 모델과
같은 `dispatch()` 경로에서 처리한다. `dispatch()`는 먼저 도착한 frame을 비워 마지막 inbound
시각을 갱신하고, heartbeat timeout을 넘으면 연결을 `disconnected`로 바꾼다. timeout이
아니고 interval이 지난 경우에는 `$zlink.heartbeat.ping` control frame을 전송한다.
`$zlink.heartbeat.pong` 같은 `$zlink.` control frame은 connector 내부 frame이므로
application packet callback으로 전달하지 않는다. reconnect는 `connect()` 실패 시
`reconnect` option의 시도 횟수와 backoff 값을 따라 재시도하고, 두 번째 시도부터
`reconnecting` state event를 발행한다.

connector는 ActorGateway나 server-side session actor relay를 직접 구현하지 않는다. 그것은
서버 framework의 STREAM/ActorGateway 기능이다. connector는 STREAM 서버가 이해하는
header/payload packet을 만들고 해석하는 client-side library다.

구현 검증은 두 단계로 나눈다. 첫 단계는 local test runtime으로 packet 생성, pending
request 등록, callback dispatch 같은 내부 상태를 검증한다. 이 검증은 transport가 없어도
가능하지만 실제 서버 접속을 증명하지 않는다. 둘째 단계는 framework STREAM endpoint를
실제로 띄우고 connector가 그 endpoint에 연결한 뒤 request reply와 push notification을
주고받는 end-to-end 검증이다. connector를 완료로 볼 수 있는 기준은 둘째 단계까지 통과하는
것이다.

codec 표면은 `message_t` 중심으로 둔다. 사용자가 별도 codec namespace를 찾아
`decode(message)`를 호출하는 방식은 주 표면으로 두지 않는다. connector 내부 typed
send/request/on 경로도 같은 message 중심 codec API를 사용한다.

```cpp
auto message = zlink::message_t::from_json(login_request);
auto reply = reply_message.parse_json<login_reply_t>();
```

`.NET` `Systems.Zlink.Stream.Connector.Codecs`의 auto codec helper에 해당하는 C++ 표면은
`zlink/stream_connector/codecs/auto_codec.hpp`에 둔다. C++에는 attribute reflection이
없으므로 기본은 JSON이고, MessagePack/Protobuf 선택은 사용자가 `codec_traits<T>`를
특수화해서 명시한다.

```cpp
#include <zlink/stream_connector/codecs/auto_codec.hpp>

zlink::stream_connector::codecs::send(connector, login_request)
  .packet_name("login.request")
  .submit();

zlink::stream_connector::codecs::on<login_notify_t>(
  connector,
  [](const login_notify_t &notify) {
    // notify is decoded before this callback runs.
  });
```

codec id는 STREAM header의 `codec` 필드에 기록한다. 압축은 codec이 아니라 header flag다.
수신 처리 순서는 `decompress -> codec decode -> typed payload`이고, 송신 처리 순서는
`typed payload -> codec encode -> optional compress -> header flag 설정`이다.

## 4. C++ API 방향

public namespace는 `zlink::stream_connector`로 둔다. 서버 framework의
`zlink::framework` namespace에 넣지 않는다.

```cpp
namespace zlink::stream_connector {

class connector_t;
class connector_options_t;
class codec_registry_t;
class send_call_t;

template <typename TReply>
class request_call_t;

template <typename T>
class result_t;

template <typename T>
class task_t;

class connector_factory_t {
public:
    static connector_t create(connector_options_t options);
};

class connector_t {
public:
    task_t<void> connect();
    task_t<void> close();
    codec_registry_t &codecs();

    template <typename TMessage>
    send_call_t send(const TMessage &message);

    template <typename TReply, typename TRequest>
    request_call_t<TReply> request(const TRequest &request);

    template <typename TMessage>
    void on(std::string packet_name,
      std::function<void(const TMessage &)> callback);

    task_t<void> dispatch();
};

} // namespace zlink::stream_connector
```

비동기 호출 방식은 서버 framework와 같은 사용성으로 맞춘다.

```cpp
auto reply = co_await connector
  .request<login_reply_t>(login_request_t{.user_id = "alice"})
  .timeout(std::chrono::seconds(2))
  .submit();

connector
  .send(chat_message_t{.text = "hello"})
  .submit([](zlink::stream_connector::result_t<void> result) {
      if (!result) {
          return;
      }
  });
```

typed codec은 registry에 등록한다.

```cpp
connector.codecs()
  .add_json<login_request_t>()
  .add_json<login_reply_t>();
```

## 5. Dispatch Mode

기본 dispatch mode는 manual이다. connector는 수신 packet을 내부 queue에 넣고, 사용자가
`dispatch()`를 호출할 때 등록 callback을 실행한다. 게임 client나 UI runtime에서 frame
loop와 명확히 맞추기 쉽기 때문이다.

immediate mode도 제공한다. 이 모드에서는 connector가 내부 수신 흐름에서 callback 실행을
예약한다. 사용자는 이 모드에서 callback이 UI thread에서 실행된다고 가정하면 안 된다.

## 6. 테스트

테스트 도구는 서버 framework C++와 같게 둔다.

- GoogleTest
- GoogleMock
- CTest

필수 회귀 항목은 아래와 같다.

- TCP typed request가 request sequence로 response를 정확히 짝짓는다.
- send는 helper header와 payload frame 형식을 그대로 사용한다.
- 여러 packet을 순서대로 dispatch한다.
- manual dispatch에서는 callback이 `dispatch()` 호출 경로에서 실행된다.
- immediate dispatch에서는 별도 manual dispatch 없이 callback이 실행된다.
- packet name 기본값은 DTO의 `static constexpr packet_name`을 우선 사용한다. 값이 없을
  때만 C++ type name fallback을 사용한다.
- metadata size limit은 send 전에 적용된다.
- send payload size limit은 transport write 전에 적용된다.
- request timeout은 pending request를 정리한다.
- reconnect 중 새 request는 queue에 쌓지 않고 disconnected 계열 오류로 실패한다.
- heartbeat ping/pong과 heartbeat timeout을 검증한다.
- compressed server packet은 typed callback 전에 복원된다.

## 7. Unreal Connector

Unreal Connector는 일반 C++ connector와 별도 배포물이다. Unreal 프로젝트에서 바로 쓸 수
있도록 Unreal 전용 함수와 타입을 제공한다. Unreal Connector의 transport 구현은 일반 C++
connector의 Asio runtime을 감싸지 않고 Unreal의 `Sockets`/`Networking` 모듈을 사용한다.
그래야 Unreal lifecycle, PIE 종료, map unload, Game Thread callback 규칙과 충돌하지
않는다.

포함해야 할 Unreal 전용 표면은 아래와 같다.

- Unreal plugin/module packaging
- `UObject` 또는 subsystem 기반 lifecycle owner
- `FString`, `FName`, `TArray<uint8>`, `TMap<FString, FString>` 기반 packet API
- Blueprint에서 호출 가능한 connect, close, send, request 함수
- Blueprint assignable connection state event
- Game Thread callback dispatch
- Tick 또는 subsystem update에서 manual dispatch
- Unreal logging category
- Unreal build system module dependency 정리
- PIE 종료, map unload, game instance shutdown에서 graceful close

예시 표면은 아래 방향으로 둔다.

```cpp
UCLASS(BlueprintType)
class UZLinkStreamConnector : public UObject {
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable)
    void Connect(const FString &Endpoint);

    UFUNCTION(BlueprintCallable)
    void Close();

    UFUNCTION(BlueprintCallable)
    void SendJson(FName PacketName, const FString &JsonPayload);

    UFUNCTION(BlueprintCallable)
    void SendJsonWithOptions(
      FName PacketName,
      const FString &JsonPayload,
      const FZLinkStreamSendOptions &Options);

    UFUNCTION(BlueprintCallable)
    void RequestJson(
      FName PacketName,
      const FString &JsonPayload,
      float TimeoutSeconds);

    UFUNCTION(BlueprintCallable)
    void RequestJsonWithOptions(
      FName PacketName,
      const FString &JsonPayload,
      float TimeoutSeconds,
      const FZLinkStreamSendOptions &Options);

    UFUNCTION(BlueprintCallable)
    void Dispatch();

    UPROPERTY(BlueprintAssignable)
    FZLinkStreamPacketReceived OnPacketReceived;

    UPROPERTY(BlueprintAssignable)
    FZLinkStreamRequestCompleted OnRequestCompleted;
};
```

Unreal Connector는 callback을 Game Thread에서 실행해야 한다. 내부 network receive나
background thread에서 `UObject`, `AActor`, `UWorld`를 직접 만지지 않는다. 기본 dispatch
mode는 manual이며, `Dispatch()`를 game tick에서 호출하면 그 frame에 쌓인 packet과
lifecycle event를 Game Thread에서 처리한다.
Unreal Connector에는 coroutine API를 별도로 두지 않는다. 일반 C++ connector의
`task_t`, `submit()`, `co_await` 표면을 Unreal public header로 가져오지 않는다. Unreal
사용자는 Blueprint delegate나 native multicast delegate로 callback을 받고,
`PendingDispatchCount()`는
`Dispatch()` 전에 처리할 완성 frame이 private queue에 몇 개 쌓여 있는지 알려준다.

codec은 일반 C++ connector와 같은 배포 정책을 따른다. JSON은 기본 포함한다. MessagePack과
Protobuf는 Unreal plugin 안의 build option으로 포함할 수 있지만, Unreal 사용자가 codec
산출물을 따로 가져오게 만들지 않는다.

Unreal Connector 테스트는 일반 C++ connector GoogleTest 회귀와 별도로 둔다. Unreal
module compile test, Blueprint-callable API compile check, Game Thread dispatch smoke를
최소 기준으로 둔다.

실제 Unreal 동작 검증은 `ZLink.StreamConnector.Loopback` Automation Test로 한다. 이
테스트는 Unreal `FSocket` loopback server를 열고 `UZLinkStreamConnector`가 실제로
접속하는지, `SendJson`과 `RequestJson`이 STREAM frame 구조로 서버에 도착하는지,
metadata options가 header metadata로 기록되는지, 서버가 보낸 `send` frame의 metadata가
`OnPacketReceivedNative` callback packet으로 돌아오는지, request sequence가 일치하는
`response` frame이 `OnRequestCompletedNative` callback으로 돌아오는지 확인한다. 또한
`FZLinkStreamSendOptions::bCompress`가 켜진 send/request는 `payload_compressed` flag와
LZ4 payload 형식으로 서버에 도착해야 하며, 서버가 보낸 compressed push/response frame은
callback 전에 원래 payload로 복원되어야 한다. 서버가 frame을 보낸 뒤에는
`PendingDispatchCount()`가 0보다 커지고, callback은 `Dispatch()` 호출 전에는 실행되지
않아야 한다.

Unreal Engine이 설치된 머신에서는 아래처럼 headless로 실행한다.

```bash
UnrealEditor-Cmd <TestProject>.uproject \
  -ExecCmds="Automation RunTests ZLink.StreamConnector; Quit" \
  -unattended -nop4 -nosplash -NullRHI
```
