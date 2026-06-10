<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ STREAM Decisions](./stream-open-items.ko.md) | [다음: Draft -- ZLink Framework C++ HTTP Client](./cpp-http-client.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [STREAM](./cpp-stream.ko.md) | [공통 Stream Connector](../../../../doc/spec/draft/streaming-client.ko.md)

# Draft -- ZLink Stream Connector For C++

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, C++용 `ZLink Stream Connector`를 별도 라이브러리와 별도
> 배포 단위로 어떻게 만들지 정리한다.

## 1. 결정 요약

C++ Stream Connector는 `ZLink Framework for C++` 샘플이 아니다. 서버 framework와 같은
저장소에 있을 수는 있지만, public header, CMake target, package는 분리한다.

C++ connector는 엔진별로 core 구현을 복제하지 않는다.
[framework 공통 비동기 정책](../../../../doc/spec/async-execution-policy.ko.md)에 따라
하나의 독립 core connector를 두고, coroutine, 예외 기반 호출, Unreal, Godot, Axmol 같은
환경별 표면은 adapter나 plugin으로 분리한다.

핵심 결정은 아래와 같다.

| 영역 | 결정 |
|------|------|
| core connector | 예외와 coroutine에 의존하지 않는 독립 C++ 라이브러리 |
| server framework | 예외 기반 application API로 정리 |
| coroutine | core에 직접 섞지 않고 선택 adapter로 제공 |
| Unreal | Unreal plugin으로 제공하며 core API를 그대로 노출하지 않음 |
| Godot | GDExtension adapter로 제공 |
| Axmol | C++ native 2D 엔진 adapter로 제공 |
| Cocos Creator | TypeScript connector 사용, 별도 C++ adapter 제공하지 않음 |
| Cocos2d-x | 업데이트 중단 제품으로 보고 지원하지 않음 |

이 결정의 목적은 build option 조합을 사용자에게 떠넘기지 않는 것이다. 예외가 꺼진 client
엔진에서도 core connector를 쓸 수 있어야 하고, 서버나 성능 테스트 client에서는 coroutine과
예외 기반 API를 선택해서 더 짧은 코드를 쓸 수 있어야 한다.

## 2. 위치

권장 배치는 아래와 같다.

```text
framework/languages/cpp/
+-- connector/
|   +-- CMakeLists.txt
|   +-- include/
|   |   +-- zlink/stream_connector/
|   |       +-- contracts/
|   +-- src/
|   |   +-- runtime/
|   +-- adapters/
|   |   +-- coroutine/
|   |   |   +-- include/
|   |   +-- throwing/
|   |       +-- include/
|   +-- packaging/
|   |   +-- cmake/
|   |   +-- vcpkg/
|   |   +-- conan/
|   +-- tests/
|   +-- samples/
+-- unreal-connector/
|   +-- ZLinkStreamConnector.uplugin
|   +-- Source/ZLinkStreamConnector/
|       +-- Public/
|       +-- Private/
+-- godot-connector/
|   +-- extension/
|   +-- include/
|   +-- src/
+-- axmol-connector/
|   +-- CMakeLists.txt
|   +-- include/
|   +-- src/
+-- framework/
+-- http-client/
+-- samples/
+-- CMakeLists.txt
```

`connector/`는 자체 `CMakeLists.txt`와 packaging 파일을 가진다. 상위
`framework/languages/cpp/CMakeLists.txt`는 개발 편의를 위해 이를 포함할 수 있지만,
connector package의 기준은 `connector/` 자체다.

`connector/` 아래에는 Unreal, Godot, Axmol 타입을 넣지 않는다. core connector는 특정 엔진
header 없이 빌드되어야 한다. 엔진 adapter는 core connector를 링크하거나 source로 포함할 수
있지만, core connector가 adapter를 알면 안 된다.

서버 framework package는 connector package를 필요로 하지 않는다. connector package도
서버 framework package를 필요로 하지 않는다. 양쪽은 STREAM header/payload wire 계약만
공유한다.

connector의 public contract와 runtime 구현도 `.NET` Stream Connector의 `Contracts/*`와
`Runtime/*` 분리를 따른다. C++에서는 public header가
`include/zlink/stream_connector/contracts/*`에 있고, 구현은 `src/runtime/*`에 있다.

connector도 framework와 같은 public surface gate를 적용한다. public header는 endpoint,
packet, request/send builder, callback/event, dispatch, codec option만 노출한다. reconnect
state, heartbeat scheduler, pending request table, frame encoder/decoder, compression worker,
Asio socket receive loop는 `src/runtime/*`에 둔다.

## 3. 제품과 지원 범위

지원 범위는 제품 이름이 아니라 현재 유지 상태와 실제 개발 언어를 기준으로 정한다.

| 제품 | 지원 결정 | 이유 |
|------|-----------|------|
| 일반 C++ client/tool | 지원 | core connector의 기본 대상 |
| 서버 성능 테스트 client | 지원 | coroutine adapter를 사용하면 시나리오를 짧게 유지할 수 있음 |
| Unreal Engine | 지원 | Unreal plugin과 delegate/Game Thread 표면이 필요함 |
| Godot | 지원 | GDExtension 표면이 필요함 |
| Axmol Engine | 지원 | 유지되는 C++ Cocos2d-x 계열 엔진 |
| Cocos Creator 3.x | C++ adapter 미지원 | TypeScript 중심 제품이므로 TypeScript connector를 사용 |
| Cocos Creator 2.x | 미지원 | 업데이트 중단 |
| Cocos Creator 3D | 미지원 | Cocos Creator 3.x에 흡수된 제품 |
| Cocos2d-x | 미지원 | 업데이트 중단 |

Cocos 계열 이름은 혼동을 줄이기 위해 문서와 package 이름에서 분명히 나눈다.
`cocos-connector`라는 이름은 쓰지 않는다. C++ native 계열은 `axmol-connector`로 부르고,
Cocos Creator는 TypeScript connector 문서에서 다룬다.

## 4. 패키징

배포 이름은 사용자가 설치하는 단위와 일치시킨다.

| 배포 단위 | 형식 | 내용 |
|-----------|------|------|
| `zlink-stream-connector` | CMake package, vcpkg, Conan | no-exception/no-coroutine core |
| `zlink-stream-connector-coroutine` | CMake component 또는 feature | `co_await` adapter |
| `zlink-stream-connector-throwing` | CMake component 또는 feature | 예외 기반 adapter |
| `zlink-unreal-stream-connector` | Unreal plugin | Unreal delegate와 Game Thread dispatch |
| `zlink-godot-stream-connector` | GDExtension artifact | Godot signal/callback 표면 |
| `zlink-axmol-connector` | CMake package 또는 source package | Axmol scheduler/main thread 표면 |

core connector는 단독 설치가 가능해야 한다. Unreal, Godot, Axmol adapter는 core connector를
내부 `ThirdParty`로 포함하거나 외부 package로 참조할 수 있다. 어느 쪽이든 adapter package
사용자가 서버 framework package를 설치할 필요는 없어야 한다.

기본 CMake target은 아래처럼 나눈다.

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
맡는다. JSON helper는 기본 helper로 항상 포함한다. MessagePack과 Protobuf는 build option이
켜지고 해당 C++ binding codec target이 있을 때만 `zlink::stream_connector_codecs`에
연결된다.

| 기능 | 포함 방식 | 기본값 |
|------|----------|--------|
| raw bytes | 항상 포함 | ON |
| JSON helper | connector package 안에 포함 | ON |
| MessagePack helper | connector package 안의 build feature | OFF |
| Protobuf helper | connector package 안의 optional build feature | OFF |
| LZ4 compression | connector package 안의 build feature, system LZ4 또는 fallback source | ON. packet 압축은 opt-in |

권장 CMake option은 아래와 같다.

```cmake
ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK=OFF
ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF=OFF
ZLINK_STREAM_CONNECTOR_WITH_LZ4=ON
```

JSON helper는 별도 CMake option 없이 기본 포함한다. `ZLINK_STREAM_CONNECTOR_WITH_LZ4=ON`이면
CMake는 먼저 system `lz4.h`와 `liblz4`를 찾는다. 개발 패키지가 없으면 LZ4 source를 받아
`zlink::stream_connector` target 안에 private source로 포함한다. 사용자는 별도 LZ4 target을
직접 링크하지 않는다. 다만 실제 packet을 압축할지는 `send_call_t::compress()`나 Unreal
`FZLinkStreamSendOptions::bCompress`처럼 호출 지점의 option으로 결정한다.

## 5. 기능 기준

C++ Stream Connector는 공통 [ZLink Stream Connector](../../../../doc/spec/draft/streaming-client.ko.md)
초안과 `.NET` `Systems.Zlink.Stream.Connector`의 기능성을 C++20 방식으로 투영한다.

일반 C++ connector의 transport 구현은 Asio를 사용한다. Linux, macOS, Windows에서 같은
receive loop와 timer/reconnect 구조를 유지하기 위해 raw file descriptor나 OS별 socket
API를 connector runtime에 직접 흩어 놓지 않는다. 현재 저장소에서는 C++ binding이 이미
Boost include 경로를 제공하므로 Boost.Asio를 기본 구현 기반으로 사용한다.

기능 기준은 아래와 같고, core와 adapter가 맡는 범위를 분리한다.

| 소유 범위 | 포함 기능 |
|-----------|----------|
| core connector | TCP, TLS, WebSocket, WebSocket over TLS transport |
| core connector | connector 생성, 명시 connect, graceful close |
| core connector | connection state event, reconnect, heartbeat |
| core connector | packet send, typed send helper, typed request/reply helper |
| core connector | callback completion, packet callback receive |
| core connector | request timeout, pending request correlation |
| core connector | manual dispatch mode, immediate dispatch mode |
| core connector | metadata, payload compression flag 처리 |
| core connector | max send payload size, max metadata size |
| core connector | connector instance별 독립 실행 |
| coroutine adapter | no-callback `async()`와 `co_await` 표면 |
| throwing adapter | 성공 값을 바로 반환하고 실패를 예외로 바꾸는 표면 |
| engine adapter | 엔진별 delegate, signal, main thread dispatch 표면 |

공통 Stream Connector 초안과 `.NET` connector가 공개 범위로 둔 TCP, TLS, WebSocket,
WebSocket over TLS transport는 모두 같은 `stream_connection_t` runtime abstraction 아래에서
동작한다. 지원 transport를 바꿔도 상위 packet send/request, heartbeat, dispatch, request
correlation 경로는 같은 frame read/write 의미를 사용한다.

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
    // Decoded notification.
  });
```

codec id는 STREAM header의 `codec` 필드에 기록한다. 압축은 codec이 아니라 header flag다.
수신 처리 순서는 `decompress -> codec decode -> typed payload`이고, 송신 처리 순서는
`typed payload -> codec encode -> optional compress -> header flag 설정`이다.

## 6. Core Connector 계약

core connector는 client 엔진에서 예외와 coroutine이 꺼져 있어도 빌드되어야 한다. 따라서
기본 public contract는 아래 원칙을 따른다.

- 실패는 `result_t<T>`로 표현한다.
- blocking `submit()`은 `result_t<T>`를 반환한다.
- engine client의 주 사용 흐름은 callback/event와 `dispatch()`다.
- callback은 manual dispatch mode에서 game loop와 맞춰 실행할 수 있어야 한다.
- `wait_for`는 sample, CLI, test client 편의 API로 둔다. 엔진 client의 주 push 수신 방식은
  `on<T>(...)` callback이다.
- core public header는 C++ exception이나 coroutine header에 의존하지 않는다.
- core public contract에서 `*_async` 이름은 사용하지 않는다. 이 이름은 awaitable을 반환하는
  coroutine adapter 전용 표면으로 예약한다.

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

template <typename TMessage>
class wait_call_t;

template <typename T>
class result_t;

class connector_factory_t {
public:
    static connector_t create(connector_options_t options);
};

class connector_t {
public:
    result_t<void> connect();
    result_t<void> close();
    codec_registry_t &codecs();

    template <typename TMessage>
    send_call_t send(const TMessage &message);

    template <typename TReply, typename TRequest>
    request_call_t<TReply> request(const TRequest &request);

    template <typename TMessage>
    void on(std::function<void(const TMessage &)> callback);

    result_t<void> dispatch();

    template <typename TMessage>
    wait_call_t<TMessage> wait_for();
};

template <typename TMessage>
class wait_call_t {
public:
    wait_call_t &packet_name(std::string name);
    wait_call_t &timeout(std::chrono::milliseconds timeout);
    wait_call_t &where(std::function<bool(const TMessage &)> predicate);
    result_t<TMessage> submit();
};

} // namespace zlink::stream_connector
```

기본 사용법은 동기 호출과 callback 호출을 분리한다. `connect()`, `dispatch()`,
`send(...).submit()`, `request(...).submit()`, `wait_for<T>().submit()`는 모두 `result_t<T>`를
바로 반환한다. callback completion은 callback을 먼저 등록하고 `start()`로 시작한다.

```cpp
auto connected = connector.connect();

auto login = connector
  .request<login_reply_t>(login_request_t{.user_id = "alice"})
  .submit();

connector
  .request<login_reply_t>(login_request_t{.user_id = "alice"})
  .on_completed([](zlink::stream_connector::result_t<login_reply_t> result) {
      if (!result) {
          return;
      }
  })
  .start();

connector.on<chat_pushed_t>([](const chat_pushed_t &message) {
    // Push notification.
});
connector.dispatch();
```

callback API에는 `submit(callback)` 이름을 쓰지 않는다. C++ connector에서 `*_async`는
`co_await` 가능한 값을 반환하는 coroutine adapter 전용 이름이다. core callback 표면은
`on_completed(...).start()`를 기준으로 둔다. 이 형태가 completion 등록과 operation 시작을
분리해서 보여 주고, Unreal, Godot, Axmol facade로 옮기기도 쉽다.

typed wait에서 조건이 필요하면 `.where(...)`를 사용한다. predicate는 디코딩된 메시지를
받기 때문에 sample code가 packet payload나 codec을 직접 다루지 않는다. 조건에 맞지 않는
packet은 소비하지 않고 이후 wait나 manual dispatch에서 다시 볼 수 있게 queue에 남는다.

기본 대기 시간과 다르게 기다려야 하는 호출에만 `.timeout(...)`을 붙인다.
별도 설정이 없으면 request와 wait는 `connector_options_t::request_timeout` 값을 사용한다.

typed codec은 registry에 등록한다.

```cpp
connector.codecs()
  .add_json<login_request_t>()
  .add_json<login_reply_t>();
```

## 7. Coroutine Adapter

coroutine adapter는 core connector 위의 선택 표면이다. 공통 의미는
[framework 공통 비동기 정책](../../../../doc/spec/async-execution-policy.ko.md)을 따른다.
서버 성능 테스트 client, CLI, tool처럼 C++20 coroutine을 안정적으로 켤 수 있는 환경에서
사용한다.

coroutine adapter에서만 `connect_async()`, `close_async()`, `dispatch_async()`,
`async()`, `wait_for(...).async()` 같은 `*_async` 이름을 제공한다. 이 함수들은 callback을
받지 않고 `task_t<T>`처럼 `co_await` 가능한 값을 반환한다. callback 기반 completion이 필요하면
core connector의 `on_completed(...).start()` 표면을 사용한다.

```cpp
auto auth = co_await client
  .request<auth_res_t>(auth_req_t{"player-1"})
  .async();
```

이 adapter는 core connector package의 필수 dependency가 아니다. 사용자가 adapter header나
component를 선택했을 때만 `task_t<T>`와 no-callback `async()`가 보이게 한다.

## 8. Throwing Adapter

throwing adapter는 서버 framework와 tool code에서 사용할 수 있는 선택 표면이다. core
connector의 `result_t<T>` 실패를 전용 예외로 바꾸고, 성공 경로에서는 값을 바로 반환한다.

```cpp
auto auth = client.request<auth_res_t>(auth_req_t{"player-1"}).submit();
```

이 adapter의 예외는 error code와 message를 보존해야 한다. 예외 기반 표면을 쓰더라도 core
connector에서 알 수 있던 timeout, disconnected, validation failure 같은 정보가 사라지면
안 된다.

서버 framework는 장기적으로 예외 기반 application API로 정리한다. 단, client core connector
자체를 예외 기반으로 바꾸지는 않는다.

## 9. Dispatch Mode

기본 dispatch mode는 manual이다. connector는 수신 packet을 내부 queue에 넣고, 사용자가
`dispatch()`를 호출할 때 등록 callback을 실행한다. 게임 client나 UI runtime에서 frame
loop와 명확히 맞추기 쉽기 때문이다.

callback을 등록하지 않고 특정 packet을 기다려야 하는 sample, CLI, e2e client는
`wait_for<T>().submit()`를 호출한다. 이 API는 `.NET` connector의 `WaitForAsync(...)`와 같은
목적으로 둔다. matching된 packet은 소비되고, 조건에 맞지 않는 packet은 이후 wait나
manual dispatch에서 다시 볼 수 있게 queue에 남는다. timeout 인자를 생략하면
`connector_options_t::request_timeout` 값을 사용한다. timeout을 넘기면 `request_timeout`
오류가 반환되고, 연결이 닫혀 있으면 `disconnected` 오류가 반환된다.

immediate mode도 제공한다. 이 모드에서는 connector가 내부 수신 흐름에서 callback 실행을
예약한다. 사용자는 이 모드에서 callback이 UI thread에서 실행된다고 가정하면 안 된다.

## 10. Unreal Connector

Unreal Connector는 일반 C++ connector와 별도 배포물이다. Unreal 프로젝트에서 바로 쓸 수
있도록 Unreal 전용 함수와 타입을 제공한다. Unreal connector는 core connector API를 그대로
노출하지 않는다. Unreal 사용자는 `std::function`, `result_t<T>`, worker thread callback을
직접 다루지 않는 표면을 기대한다.

Unreal adapter는 아래 원칙을 따른다.

- Unreal plugin/module packaging으로 배포한다.
- `UObject` 또는 subsystem 기반 lifecycle owner를 둔다.
- `FString`, `FName`, `TArray<uint8>`, `TMap<FString, FString>` 기반 packet API를 제공한다.
- Blueprint에서 호출 가능한 connect, close, send, request 함수를 제공한다.
- Unreal delegate나 Blueprint delegate를 completion 표면으로 사용한다.
- callback은 Game Thread에서 실행되도록 dispatch한다.
- Tick 또는 subsystem update에서 manual dispatch를 호출할 수 있어야 한다.
- `UObject` lifetime을 고려해서 completion target이 사라진 경우 호출하지 않는다.
- 예외와 coroutine에 의존하지 않는다.
- core connector runtime type을 Unreal public header에 직접 노출하지 않는다.
- PIE 종료, map unload, game instance shutdown에서 graceful close한다.

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

    UPROPERTY(BlueprintAssignable)
    FZLinkStreamPacketReceived OnPacketReceived;

    UPROPERTY(BlueprintAssignable)
    FZLinkStreamRequestCompleted OnRequestCompleted;
};
```

native C++ 표면은 Unreal delegate와 `Dispatch()` 흐름을 유지한다.

```cpp
Client.Request<FAuthRes>(Req)
  .OnCompleted(AuthCompletedDelegate)
  .Submit();

Client.OnNumberDrawn.AddUObject(this, &UMyObject::HandleNumberDrawn);
Client.Dispatch();
```

Unreal Connector는 callback을 Game Thread에서 실행해야 한다. 내부 network receive나
background thread에서 `UObject`, `AActor`, `UWorld`를 직접 만지지 않는다. 기본 dispatch
mode는 manual이며, `Dispatch()`를 game tick에서 호출하면 그 frame에 쌓인 packet과
lifecycle event를 Game Thread에서 처리한다.

Unreal plugin은 `.uplugin` 단위로 배포한다. 내부적으로 core connector를 `ThirdParty`로
포함할지, 설치된 CMake package를 참조할지는 배포 채널별로 결정한다. Unreal lifecycle 때문에
transport를 Unreal `Sockets`/`Networking` 모듈로 바꾸는 구현도 허용할 수 있지만, public
API와 STREAM wire 의미는 일반 C++ connector와 같아야 한다.

codec은 일반 C++ connector와 같은 배포 정책을 따른다. JSON은 기본 포함한다. MessagePack과
Protobuf는 Unreal plugin 안의 build option으로 포함할 수 있지만, Unreal 사용자가 codec
산출물을 따로 가져오게 만들지 않는다.

## 11. Godot Connector

Godot connector는 GDExtension adapter로 제공한다. core connector의 result/callback 표면을
Godot signal이나 callable 표면으로 바꾼다.

Godot adapter는 아래 원칙을 따른다.

- GDExtension public type으로 노출한다.
- Godot main thread에서 signal을 emit할 수 있게 dispatch 경계를 둔다.
- C++ exception에 의존하지 않는다.
- core connector의 C++ template 표면을 Godot script 사용자에게 직접 노출하지 않는다.

## 12. Axmol Connector

Axmol connector는 유지되는 C++ Cocos2d-x 계열을 대상으로 한다. Cocos2d-x 자체는 지원하지
않는다.

Axmol adapter는 아래 원칙을 따른다.

- Axmol scheduler나 main thread dispatch 방식과 맞춘다.
- 예외에 의존하지 않는다.
- callback/event 중심 표면을 제공한다.
- core connector를 CMake package나 source dependency로 참조한다.

## 13. 테스트

테스트 도구는 서버 framework C++와 같게 둔다.

- GoogleTest
- GoogleMock
- CTest

필수 회귀 항목은 아래와 같다.

- TCP typed request가 request sequence로 response를 정확히 짝짓는다.
- send는 helper header와 payload frame 형식을 그대로 사용한다.
- 여러 packet을 순서대로 dispatch한다.
- 하나의 TCP frame이 여러 read로 나뉘어 도착해도 packet을 복원한다.
- 기본 송신 제한보다 큰 서버 payload도 수신 packet으로 복원한다.
- explicit receive는 callback 없이 서버가 보낸 여러 packet을 순서대로 꺼낸다.
- manual dispatch에서는 callback이 `dispatch()` 호출 경로에서 실행된다.
- immediate dispatch에서는 별도 manual dispatch 없이 callback이 실행된다.
- packet name 기본값은 DTO의 `static constexpr packet_name`을 우선 사용한다. 값이 없을
  때만 C++ type name fallback을 사용한다.
- metadata size limit은 send 전에 적용된다.
- send payload size limit은 transport write 전에 적용된다.
- request timeout은 pending request를 정리한다.
- request callback은 response, timeout, close 상황을 모두 처리한다.
- reconnect 성공은 `reconnecting` 상태를 거쳐 `connected`로 돌아온 뒤 send가 가능해야 한다.
- reconnect 실패 뒤 새 request는 queue에 쌓지 않고 disconnected 계열 오류로 실패한다.
- heartbeat ping/pong과 heartbeat timeout을 검증한다.
- compressed server packet은 typed callback 전에 복원된다.
- core connector test는 예외와 coroutine adapter 없이 통과한다.

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

## 14. 구현 순서

이 초안은 아직 정식 공개 계약이 아니므로 기존 실험 API와의 호환성을 유지하지 않는다.
코드 적용 시에는 `submit(callback)`, core header의 `task_t`, core header의
`connect_async()`, `close_async()`, `dispatch_async()`, `wait_for(...).async()`를 deprecated로
남기지 않고 제거한다. 샘플과 테스트도 새 표면으로 바로 옮긴다.

구현은 아래 순서로 진행한다.

1. `connector/`를 독립 CMake package로 분리한다.
2. core connector public contract에서 `task_t` include와 모든 `*_async` member를 제거한다.
3. `send_call_t`, `request_call_t`, `wait_call_t`에 core callback 표면인
   `on_completed(...).start()`를 추가하고, 기존 `submit(callback)`은 제거한다.
4. codec helper도 core helper와 coroutine helper로 나눈다. core codec helper는
   `submit()`, `on_completed(...).start()`만 노출하고, `async()`는 coroutine helper로
   옮긴다.
5. coroutine adapter header와 package component를 추가한다. 이 adapter에서만
   `connect_async()`, `close_async()`, `dispatch_async()`, `wait_for(...).async()`,
   `async()`를 제공한다.
6. throwing adapter를 core와 분리한다.
7. 샘플과 테스트를 새 core 표면 또는 coroutine adapter 표면 중 하나로 명시적으로 옮긴다.
   게임 client 샘플은 core 표면을 기준으로 작성하고, 서버 성능 테스트 client나 CLI 성격의
   테스트만 coroutine adapter를 include한다.
8. core connector contract test로 core public header가 coroutine header와 exception header에
   의존하지 않는지 고정한다.
9. 서버 framework call 표면을 예외 기반 awaitable API로 정리한다.
10. Unreal plugin을 core connector 위 facade로 정리한다.
11. Godot GDExtension adapter를 설계한다.
12. Axmol adapter를 설계한다.
13. Cocos Creator 문서에는 TypeScript connector 사용을 명시한다.

호환성 shim은 두지 않는다. 각 단계는 해당 단계의 최종 표면으로 빌드하고 테스트해야 한다.
특히 core connector test는 예외와 coroutine adapter 없이 통과해야 한다.
