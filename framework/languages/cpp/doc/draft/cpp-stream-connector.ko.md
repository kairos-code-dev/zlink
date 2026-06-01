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
+-- include/zlink/framework/
+-- src/
+-- tests/
+-- samples/
+-- connector/
|   +-- include/zlink/stream_connector/
|   +-- src/
|   +-- tests/
|   +-- samples/
|   +-- CMakeLists.txt
+-- unreal-connector/
|   +-- Source/
|   |   +-- ZLinkStreamConnector/
|   |   +-- ZLinkStreamConnector.uplugin
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

## 2. 패키징

패키징 기준은 아래와 같다.

| 항목 | 서버 framework | C++ Stream Connector | Unreal Stream Connector |
|------|----------------|----------------------|-------------------------|
| CMake target | `zlink::framework` | `zlink::stream_connector` | Unreal module/plugin |
| public include | `zlink/framework/...` | `zlink/stream_connector/...` | Unreal plugin public headers |
| umbrella header | `zlink/framework.hpp` | `zlink/stream_connector.hpp` | `ZLinkStreamConnector.h` |
| 배포 단위 | framework server runtime | client connector library | Unreal plugin |
| 주요 사용자 | server application | C++ game/client application | Unreal game/client application |

codec은 connector 배포 단위에 포함한다. `.NET`처럼 codec별 package를 기본 배포 단위로
쪼개지 않는다. 사용자는 하나의 `zlink::stream_connector` target을 링크하고, codec
지원은 build option과 runtime codec registry로 선택한다.

| 기능 | 포함 방식 | 기본값 |
|------|----------|--------|
| raw bytes | 항상 포함 | ON |
| JSON helper | connector package 안에 포함 | ON |
| MessagePack helper | connector package 안의 build feature | OFF |
| Protobuf helper | connector package 안의 optional build feature | OFF |
| LZ4 compression | connector package 안의 build feature | OFF |

권장 CMake option은 아래와 같다.

```cmake
ZLINK_STREAM_CONNECTOR_WITH_JSON=ON
ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK=OFF
ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF=OFF
ZLINK_STREAM_CONNECTOR_WITH_LZ4=OFF
```

## 3. 기능 기준

C++ Stream Connector는 공통 [ZLink Stream Connector](../../../../doc/spec/draft/streaming-client.ko.md)
초안과 `.NET` `Systems.Zlink.Stream.Connector`의 기능성을 C++20 방식으로 투영한다.

포함 기능은 아래와 같다.

- TCP, TLS, WebSocket, WebSocket over TLS transport
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

connector는 ActorGateway나 server-side session actor relay를 직접 구현하지 않는다. 그것은
서버 framework의 STREAM/ActorGateway 기능이다. connector는 STREAM 서버가 이해하는
header/payload packet을 만들고 해석하는 client-side library다.

codec 표면은 `message_t` 중심으로 둔다. 사용자가 별도 codec namespace를 찾아
`decode(message)`를 호출하는 방식은 주 표면으로 두지 않는다. connector 내부 typed
send/request/on 경로도 같은 message 중심 codec API를 사용한다.

```cpp
auto message = zlink::message_t::from_json(login_request);
auto reply = reply_message.parse_json<login_reply_t>();
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
- packet name 기본값은 payload 타입 이름을 사용한다.
- metadata size limit은 send 전에 적용된다.
- send payload size limit은 transport write 전에 적용된다.
- request timeout은 pending request를 정리한다.
- reconnect 중 새 request는 queue에 쌓지 않고 disconnected 계열 오류로 실패한다.
- heartbeat ping/pong과 heartbeat timeout을 검증한다.
- compressed server packet은 typed callback 전에 복원된다.

## 7. Unreal Connector

Unreal Connector는 일반 C++ connector와 별도 배포물이다. Unreal 프로젝트에서 바로 쓸 수
있도록 Unreal 전용 함수와 타입을 제공한다.

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
    void RequestJson(
      FName PacketName,
      const FString &JsonPayload,
      float TimeoutSeconds);

    UFUNCTION(BlueprintCallable)
    void Dispatch();
};
```

Unreal Connector는 callback을 Game Thread에서 실행해야 한다. 내부 network receive나
background thread에서 `UObject`, `AActor`, `UWorld`를 직접 만지지 않는다. 기본 dispatch
mode는 manual이며, `Dispatch()`를 game tick에서 호출하면 그 frame에 쌓인 packet과
lifecycle event를 Game Thread에서 처리한다.

codec은 일반 C++ connector와 같은 배포 정책을 따른다. JSON은 기본 포함한다. MessagePack과
Protobuf는 Unreal plugin 안의 build option으로 포함할 수 있지만, Unreal 사용자가 codec
산출물을 따로 가져오게 만들지 않는다.

Unreal Connector 테스트는 일반 C++ connector GoogleTest 회귀와 별도로 둔다. Unreal
module compile test, Blueprint-callable API compile check, Game Thread dispatch smoke를
최소 기준으로 둔다.
