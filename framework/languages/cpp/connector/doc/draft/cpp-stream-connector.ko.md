<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ STREAM Decisions](../../../doc/internals/stream-open-items.ko.md) | [다음: Draft -- ZLink Framework C++ HTTP Client](../../../http-client/doc/draft/cpp-http-client.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../../../doc/spec/README.ko.md)

[C++ 묶음](../../../doc/README.ko.md) | [C++ 정책](../../../doc/internals/cpp-framework-policy.ko.md) | [STREAM](../../../doc/spec/cpp-stream.ko.md) | [공통 Connector 초안](../../../../../../doc/spec/draft/connector/README.ko.md)

# Draft -- ZLink Stream Connector For C++

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, C++용 `ZLink Stream Connector`를 별도 라이브러리와 별도
> 배포 단위로 어떻게 만들지 정리한다.

## 1. 결정 요약

C++ Stream Connector는 `ZLink Framework for C++` 샘플이 아니다. 서버 framework와 같은
저장소에 있을 수는 있지만, public header, CMake target, package는 분리한다.

C++ connector는 엔진별로 core 구현을 복제하지 않는다.
[framework 공통 비동기 정책](../../../../../doc/spec/async-execution-policy.ko.md)에 따라
하나의 독립 기본 connector를 두고, coroutine, 예외 기반 호출, Unreal, Godot, Axmol 같은
환경별 표면은 adapter나 plugin으로 분리한다.

핵심 결정은 아래와 같다.

| 영역 | 결정 |
|------|------|
| 기본 connector | 예외와 coroutine에 의존하지 않는 독립 C++ 라이브러리 |
| server framework | 예외 기반 application API로 정리 |
| e2e client | 서버 검증용 scenario client로 제공하고 내부에서 coroutine을 사용할 수 있음 |
| Unreal | Unreal plugin으로 제공하며 core API를 그대로 노출하지 않음 |
| Godot | GDExtension adapter로 제공 |
| Axmol | C++ native 2D 엔진 adapter로 제공 |
| Cocos Creator | TypeScript connector 사용, 별도 C++ adapter 제공하지 않음 |
| Cocos2d-x | 업데이트 중단 제품으로 보고 지원하지 않음 |

이 결정의 목적은 build option 조합을 사용자에게 떠넘기지 않는 것이다. 예외가 꺼진 client
엔진에서도 기본 connector를 쓸 수 있어야 한다. 서버 e2e, smoke, perf client에서는
`e2e-client` package를 선택해서 coroutine 기반 scenario code를 짧게 유지할 수 있어야 한다.

## 2. 위치

권장 배치는 아래와 같다.

```text
framework/languages/cpp/
+-- connector/
|   +-- CMakeLists.txt
|   +-- core/
|   |   +-- include/
|   |   |   +-- zlink/stream_connector/
|   |   |       +-- contracts/
|   |   +-- src/
|   |   |   +-- runtime/
|   |   +-- packaging/
|   |   |   +-- cmake/
|   |   |   +-- vcpkg/
|   |   |   +-- conan/
|   |   +-- tests/
|   |   +-- samples/
|   +-- e2e-client/
|   |   +-- include/
|   |   |   +-- zlink/stream_e2e_client/
|   |   +-- src/
|   |   +-- packaging/
|   |   |   +-- cmake/
|   |   |   +-- vcpkg/
|   |   |   +-- conan/
|   |   +-- tests/
|   +-- engines/
|       +-- unreal/
|       |   +-- ZLinkStreamConnector.uplugin
|       |   +-- Source/ZLinkStreamConnector/
|       |       +-- Public/
|       |       +-- Private/
|       |   +-- Source/ZLinkStreamConnectorTests/
|       |       +-- Private/
|       +-- godot/
|       |   +-- extension/
|       |   +-- include/
|       |   +-- src/
|       +-- axmol/
|           +-- CMakeLists.txt
|           +-- include/
|           +-- src/
+-- framework/
+-- http-client/
+-- samples/
+-- CMakeLists.txt
```

`connector/`는 C++ connector 제품군의 루트다. 이 아래에서 배포 방식에 따라 `core/`,
`e2e-client/`, `engines/`를 분리한다. 상위 `framework/languages/cpp/CMakeLists.txt`는 개발
편의를 위해 이 하위 산출물을 포함할 수 있지만, package의 기준은 각 산출물 루트다.

`connector/core/`는 공통 runtime과 public contract를 소유한다. 이 core에는 Unreal, Godot,
Axmol 타입을 넣지 않는다. 기본 connector는 특정 엔진 header 없이 빌드되어야 한다.

`connector/e2e-client/`는 서버 framework e2e, smoke, perf scenario client를 짧게 작성하기
위한 test/tool adapter다. 구현은 C++20 coroutine을 쓸 수 있지만, 제품 이름을
`coroutine connector`로 두지 않는다. coroutine은 구현 기술이고, 사용 목적은 서버 검증용
stream scenario client다.

`connector/engines/`는 엔진별 source 배포물을 둔다. Unreal, Godot, Axmol wrapper는 모두
공통 core를 사용하지만, 엔진별 build system과 lifecycle 때문에 vcpkg/Conan binary package의
주 대상이 아니다. 이 디렉터리의 adapter는 source package나 plugin source 형태로 배포한다.
engine adapter가 기본 connector를 링크하거나 source로 포함할 수는 있지만, 기본 connector가
engine adapter를 알면 안 된다.

서버 framework package는 connector package를 필요로 하지 않는다. connector package도
서버 framework package를 필요로 하지 않는다. 양쪽은 STREAM header/payload wire 계약만
공유한다.

connector core의 public contract와 runtime 구현도 `.NET` Stream Connector의 `Contracts/*`와
`Runtime/*` 분리를 따른다. C++에서는 public header가
`connector/core/include/zlink/stream_connector/contracts/*`에 있고, 구현은
`connector/core/src/runtime/*`에 있다.

connector core도 framework와 같은 public surface gate를 적용한다. public header는 endpoint,
packet, request/send builder, callback/event, dispatch, codec option만 노출한다. reconnect
state, heartbeat scheduler, pending request table, frame encoder/decoder, compression worker,
Asio socket receive loop는 `connector/core/src/runtime/*`에 둔다.

### 2.1 현재 트리에서 적용해야 하는 정리

이 문서를 구현할 때는 기존 실험 코드를 호환 계층으로 남기지 않고 아래 기준으로 옮긴다.

| 현재 위치나 이름 | 최종 위치나 이름 | 처리 |
|------------------|------------------|------|
| `connector/core/include/zlink/stream_connector/coroutine.hpp` | `connector/e2e-client/include/zlink/stream_e2e_client/coroutine.hpp` | core public header에서 제거 |
| `connector/core/include/zlink/stream_connector/coroutine/task.hpp` | `connector/e2e-client/include/zlink/stream_e2e_client/task.hpp` | e2e client 전용 awaitable로 이동 |
| `connector/core/include/zlink/stream_connector/codecs/coroutine_auto_codec.hpp` | `connector/e2e-client/include/zlink/stream_e2e_client/codecs/auto_codec.hpp` | coroutine codec helper를 e2e client로 이동 |
| `connector/core/src/runtime/protocol/*` | 유지 | frame, header, metadata, compression 처리는 core runtime 소유 |
| `connector/engines/unreal/Source/ZLinkStreamConnector/Private/*` | 유지 | core public API만 private으로 호출 |
| `connector/engines/unreal/Source/ZLinkStreamConnector/Public/*` | 유지 | Unreal 타입만 public으로 노출 |

`connector/core`의 public header는 `<coroutine>`을 include하지 않는다. 예외 기반 helper도 core
umbrella header에서 보이지 않아야 한다. core 내부 구현이 외부 라이브러리 예외를 잡아
`result_t<T>`로 바꾸는 것은 허용하지만, public 호출자가 예외를 처리해야만 정상 흐름을 사용할
수 있는 계약은 두지 않는다.

### 2.2 산출물별 완료 조건

구현자가 각 산출물을 끝냈다고 판단하려면 아래 조건을 모두 만족해야 한다.

| 산출물 | 완료 조건 |
|--------|-----------|
| `connector/core` | `zlink::stream_connector` target이 wrapper와 분리되어 빌드되고, public header가 coroutine/engine header 없이 컴파일된다. |
| `connector/e2e-client` | `zlink::stream_e2e_client` target이 core를 public 또는 private dependency로 사용하고, `task_t`와 `async()` 표면은 이 target을 include할 때만 보인다. |
| `connector/engines/unreal` | Unreal public header에 core type이 노출되지 않고, private 구현은 core target을 `PRIVATE`로 링크한다. |
| `connector/engines/godot` | GDExtension public type이 core template type을 노출하지 않고, signal은 Godot main thread 경계 뒤에서 emit된다. |
| `connector/engines/axmol` | Axmol public header가 core type을 노출하지 않고, callback은 Axmol thread로 전달된 뒤 실행된다. |
| packaging | core와 e2e client는 CMake package, vcpkg, Conan 기준을 갖고, engine adapter는 source package 기준을 갖는다. |

## 3. 제품과 지원 범위

지원 범위는 제품 이름이 아니라 현재 유지 상태와 실제 개발 언어를 기준으로 정한다.

| 제품 | 지원 결정 | 이유 |
|------|-----------|------|
| 일반 C++ client/tool | 지원 | 기본 connector의 기본 대상 |
| 서버 e2e/smoke/perf client | 지원 | e2e client를 사용하면 coroutine 기반 시나리오를 짧게 유지할 수 있음 |
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
| `zlink-stream-e2e-client` | CMake package, vcpkg, Conan | 서버 e2e/smoke/perf scenario client. 내부에서 coroutine을 사용할 수 있음 |
| `zlink-stream-connector-throwing` | CMake component 또는 feature | 예외 기반 tool helper. core의 필수 dependency가 아님 |
| `zlink-unreal-stream-connector` | source plugin package | Unreal delegate와 Game Thread dispatch |
| `zlink-godot-stream-connector` | source GDExtension package | Godot signal/callback 표면 |
| `zlink-axmol-connector` | source package | Axmol scheduler/main thread 표면 |

vcpkg/Conan 배포 대상은 공통 core와 서버 검증용 e2e client다. Unreal, Godot, Axmol adapter는
각 engine build system과 project layout에 묶이므로 source package로 배포한다. engine adapter는
기본 connector를 내부 `ThirdParty`로 포함하거나 외부 package로 참조할 수 있다. 어느 쪽이든
adapter package 사용자가 서버 framework package를 설치할 필요는 없어야 한다.

기본 CMake target은 아래처럼 나눈다.

| 항목 | 서버 framework | C++ Stream Connector core | Stream e2e client | Unreal Stream Connector |
|------|----------------|---------------------------|-------------------|-------------------------|
| CMake target | `zlink::framework` | `zlink::stream_connector` | `zlink::stream_e2e_client` | Unreal module/plugin |
| public include | `zlink/framework/...` | `zlink/stream_connector/...` | `zlink/stream_e2e_client/...` | Unreal plugin public headers |
| umbrella header | `zlink/framework.hpp` | `zlink/stream_connector.hpp` | `zlink/stream_e2e_client.hpp` | `ZLinkStreamConnector.h` |
| 배포 단위 | framework server runtime | vcpkg/Conan core library | vcpkg/Conan test client helper | source plugin package |
| 주요 사용자 | server application | C++ game/client application | server e2e/smoke/perf tests | Unreal game/client application |

compiled CMake target과 package export는 `framework/languages/cpp/CMakeLists.txt`가 만든다.
`connector/CMakeLists.txt`는 connector family만 source로 가져간 consumer가 include layout을
확인할 수 있는 개발 편의용 entry point다. 이 파일은 core runtime library를 다시 정의하지 않는다.
target 이름과 include directory는 아래 규칙을 지킨다.

| target | public include directory | public dependency | private dependency |
|--------|--------------------------|-------------------|--------------------|
| `zlink_stream_connector` / `zlink::stream_connector` | `connector/core/include` | C++ standard library, zlink C++ binding public header | Boost.Asio/Beast, OpenSSL, LZ4 fallback source |
| `zlink_stream_connector_codecs` / `zlink::stream_connector_codecs` | `connector/core/include` | `zlink::stream_connector` | optional MessagePack/Protobuf codec target |
| `zlink_stream_e2e_client` / `zlink::stream_e2e_client` | `connector/e2e-client/include` | `zlink::stream_connector` | coroutine helper implementation detail |
| `zlink_unreal_stream_connector` | Unreal module public include | Unreal module API only | `zlink::stream_connector`, `zlink::stream_connector_codecs` |

core target은 `zlink::framework`, HTTP client, server e2e executable에 의존하지 않는다.
반대로 server framework target도 connector target을 필수 의존성으로 갖지 않는다. sample이나
test가 양쪽을 함께 쓸 수는 있지만, package dependency graph는 양방향이 되면 안 된다.

권장 CMake option은 기능이 켜졌을 때 public contract가 바뀌지 않는 이름으로 둔다.

```cmake
ZLINK_STREAM_CONNECTOR_BUILD_E2E_CLIENT=ON
ZLINK_STREAM_CONNECTOR_BUILD_UNREAL=OFF
ZLINK_STREAM_CONNECTOR_BUILD_GODOT=OFF
ZLINK_STREAM_CONNECTOR_BUILD_AXMOL=OFF
ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK=OFF
ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF=OFF
ZLINK_STREAM_CONNECTOR_WITH_LZ4=ON
ZLINK_STREAM_CONNECTOR_WITH_TLS=ON
ZLINK_STREAM_CONNECTOR_WITH_WEBSOCKET=ON
```

`BUILD_*` option은 산출물을 추가하거나 빼는 용도다. `WITH_*` option은 같은 public API 안에서
transport나 codec 구현을 켜는 용도다. option이 꺼진 기능을 호출하면 compile error가 아니라
`result_t<T>`의 `unsupported_feature` 계열 오류를 반환하는 쪽을 기본으로 한다. 단, optional
typed codec처럼 header 자체가 없으면 사용자가 include 단계에서 빌드 설정을 알아차릴 수 있게
별도 header와 target으로 분리한다.

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

JSON helper는 별도 CMake option 없이 기본 포함한다. `ZLINK_STREAM_CONNECTOR_WITH_LZ4=ON`이면
CMake는 먼저 system `lz4.h`와 `liblz4`를 찾는다. 개발 패키지가 없으면 LZ4 source를 받아
`zlink::stream_connector` target 안에 private source로 포함한다. 사용자는 별도 LZ4 target을
직접 링크하지 않는다. 다만 실제 packet을 압축할지는 `send_call_t::compress()`나 Unreal
`FZLinkStreamSendOptions::bCompress`처럼 호출 지점의 option으로 결정한다.

vcpkg port와 Conan recipe는 같은 기능 이름을 사용한다. 기능 option은
`zlink-stream-connector` core package가 소유한다. `zlink-stream-e2e-client`는 header-only
scenario client package이며 core package를 다시 빌드하거나 다시 설치하지 않는다. vcpkg에서는
e2e package의 feature가 core package feature 의존성으로 전달된다. Conan에서는 e2e package에
별도 build option을 두지 않고, consumer가 필요한 core package option을 선택한다.

| 기능 | vcpkg feature | Conan option |
|------|---------------|--------------|
| MessagePack codec | `messagepack` | `with_messagepack=True` |
| Protobuf codec | `protobuf` | `with_protobuf=True` |
| LZ4 compression | `lz4` | `with_lz4=True` |
| TLS transport | `tls` | `with_tls=True` |
| WebSocket transport | `websocket` | `with_websocket=True` |

engine adapter는 vcpkg/Conan의 주 산출물에 넣지 않는다. Unreal, Godot, Axmol project는 engine
version, editor layout, generated files, build scripts의 영향을 크게 받기 때문이다. engine
adapter package는 source archive, git subtree, engine plugin 형태를 기준으로 문서화한다.

## 5. 기능 기준

C++ Stream Connector는 공통 [Connector 초안](../../../../../../doc/spec/draft/connector/README.ko.md)
초안과 `.NET` `Systems.Zlink.Stream.Connector`의 기능성을 C++20 방식으로 투영한다.

일반 C++ connector의 transport 구현은 Asio를 사용한다. Linux, macOS, Windows에서 같은
receive loop와 timer/reconnect 구조를 유지하기 위해 raw file descriptor나 OS별 socket
API를 connector runtime에 직접 흩어 놓지 않는다. 현재 저장소에서는 C++ binding이 이미
Boost include 경로를 제공하므로 Boost.Asio를 기본 구현 기반으로 사용한다.

기능 기준은 아래와 같고, core와 adapter가 맡는 범위를 분리한다.

| 소유 범위 | 포함 기능 |
|-----------|----------|
| 기본 connector | TCP, TLS, WebSocket, WebSocket over TLS transport |
| 기본 connector | connector 생성, 명시 connect, graceful close |
| 기본 connector | connection state event, reconnect, heartbeat |
| 기본 connector | packet send, typed send helper, typed request/reply helper |
| 기본 connector | callback completion, packet callback receive |
| 기본 connector | request timeout, pending request correlation |
| 기본 connector | manual dispatch mode, immediate dispatch mode |
| 기본 connector | metadata, payload compression flag 처리 |
| 기본 connector | max send payload size, max metadata size |
| 기본 connector | connector instance별 독립 실행 |
| e2e client | no-callback `async()`와 `co_await` 기반 서버 검증 시나리오 표면 |
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

## 6. 기본 Connector 계약

기본 connector는 client 엔진에서 예외와 coroutine이 꺼져 있어도 빌드되어야 한다. 따라서
기본 public contract는 아래 원칙을 따른다.

- 실패는 `result_t<T>`로 표현한다.
- blocking `submit()`은 `result_t<T>`를 반환한다.
- engine client의 주 사용 흐름은 callback/event와 `dispatch()`다.
- callback은 manual dispatch mode에서 game loop와 맞춰 실행할 수 있어야 한다.
- `wait_for`는 sample, CLI, test client 편의 API로 둔다. 엔진 client의 주 push 수신 방식은
  `on<T>(...)` callback이다.
- core public header는 C++ exception이나 coroutine header에 의존하지 않는다.
- core public contract에서 `*_async` 이름은 사용하지 않는다. awaitable을 반환하는 표면은
  e2e client helper 안에서만 제공한다.

public namespace는 `zlink::stream_connector`로 둔다. 서버 framework의
`zlink::framework` namespace에 넣지 않는다.

```cpp
namespace zlink::stream_connector {

class connector_t;
class connector_options_t;
class codec_registry_t;
class send_call_t;
class packet_t;
class message_t;

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

core public header의 최소 파일 경계는 아래와 같이 둔다.

| header | 역할 |
|--------|------|
| `zlink/stream_connector.hpp` | core umbrella header. coroutine, throwing adapter, engine adapter를 include하지 않는다. |
| `zlink/stream_connector/contracts/result.hpp` | `result_t<T>`, `error_t`, `error_code_t` |
| `zlink/stream_connector/contracts/connector.hpp` | `connector_t`, call object, dispatch, callback registration |
| `zlink/stream_connector/contracts/codec_registry.hpp` | typed codec 등록과 message 변환 |
| `zlink/stream_connector/contracts/stream_payload.hpp` | `packet_t`, `message_t`, metadata, payload bytes |
| `zlink/stream_connector/codecs/auto_codec.hpp` | core typed codec helper. `submit()`과 `submit(callback)`만 제공 |

`result_t<T>`는 오류를 값으로 보존한다. `bool(result)`는 성공 여부만 뜻하고, 오류 정보는
`result.error().code()`와 `result.error().message()`로 읽는다. 구현은 내부 예외나
외부 라이브러리 오류를 잡아서 아래 error code 중 하나로 변환한다.

| error code | 의미 | 대표 발생 위치 |
|------------|------|----------------|
| `invalid_argument` | endpoint, packet name, metadata, timeout 같은 호출 인자가 잘못됨 | send/request/connect 시작 전 |
| `unsupported_feature` | 빌드에서 꺼진 transport, TLS, compression, codec을 사용함 | connect 또는 send 준비 단계 |
| `connect_failed` | 첫 연결 시도가 실패함 | `connect()` |
| `disconnected` | 연결이 닫힌 상태에서 send/request/wait/dispatch를 호출함 | 모든 operation |
| `request_timeout` | reply나 wait 대상 packet이 timeout 안에 도착하지 않음 | request/wait |
| `payload_too_large` | 송신 payload가 설정 한도를 넘음 | send/request |
| `metadata_too_large` | metadata encoding 결과가 설정 한도를 넘음 | send/request |
| `codec_error` | typed encode/decode에 실패함 | typed send/request/on/wait |
| `compression_error` | compress/decompress에 실패함 | send 또는 receive |
| `protocol_error` | frame, header, metadata 형식이 STREAM 계약과 맞지 않음 | receive loop |
| `closed` | 사용자가 `close()`를 호출해 operation이 종료됨 | pending request/callback |

error code 이름은 구현 중 기존 enum과 맞춰 조정할 수 있지만, 오류 분류는 유지한다. 특히
timeout과 disconnected를 같은 오류로 합치지 않는다. test와 engine adapter가 사용자에게
다른 대응을 제공해야 하기 때문이다.

기본 사용법은 동기 호출과 callback 호출을 분리한다. `connect()`, `dispatch()`,
`send(...).submit()`, `request(...).submit()`, `wait_for<T>().submit()`는 모두 `result_t<T>`를
바로 반환한다. callback completion은 `submit(callback)`으로 시작한다.

```cpp
auto connected = connector.connect();

auto login = connector
  .request<login_reply_t>(login_request_t{.user_id = "alice"})
  .submit();

connector
  .request<login_reply_t>(login_request_t{.user_id = "alice"})
  .submit([](zlink::stream_connector::result_t<login_reply_t> result) {
      if (!result) {
          return;
      }
  });

connector.on<chat_pushed_t>([](const chat_pushed_t &message) {
    // Push notification.
});
connector.dispatch();
```

callback API는 `submit(callback)`으로 시작한다. `async()`는 callback을 받지 않고
`co_await` 가능한 값을 반환하는 coroutine 표면에만 사용한다. 이 구분은 호출자가 callback
방식과 coroutine 방식을 이름만 보고 구분할 수 있게 하기 위한 규칙이다.

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

callback 소유권은 connector가 단순 복사 가능한 callable을 저장하는 방식으로 정의한다.
callback 제거 API는 registration token을 반환하는 형태로 둔다. token이 파기되거나
`unsubscribe()`가 호출되면 이후 packet에는 callback을 호출하지 않는다. callback 실행 중
사용자가 같은 connector에 send/request를 호출할 수 있어야 하므로, runtime lock을 잡은 채
사용자 callback을 실행하지 않는다.

```cpp
auto subscription = connector.on<chat_pushed_t>(
  [](const chat_pushed_t &message) {
      // User code runs outside connector internal locks.
  });

subscription.unsubscribe();
```

transport endpoint scheme은 connector option에서 해석한다.

| scheme | transport | 필수 build feature |
|--------|-----------|--------------------|
| `tcp://host:port` | TCP | core |
| `tls://host:port` | TLS over TCP | `WITH_TLS` |
| `ws://host:port/path` | WebSocket | `WITH_WEBSOCKET` |
| `wss://host:port/path` | WebSocket over TLS | `WITH_WEBSOCKET`, `WITH_TLS` |

endpoint parser는 scheme, host, port, path를 한 곳에서 검증한다. adapter가 endpoint string을
직접 쪼개서 transport를 선택하지 않는다. Unreal, Godot, Axmol adapter는 사용자가 입력한
endpoint를 core `connector_options_t`로 전달하고, 오류 표시만 engine 표면에 맞게 바꾼다.

## 7. Stream E2E Client

Stream e2e client는 기본 connector 위의 선택 표면이다. 공통 의미는
[framework 공통 비동기 정책](../../../../../doc/spec/async-execution-policy.ko.md)을 따른다.
서버 framework e2e, smoke, perf test처럼 C++20 coroutine을 안정적으로 켤 수 있는 환경에서
사용한다. 엔진 client나 일반 app의 기본 표면이 아니다.

이 산출물은 구현 기술 이름을 제품명으로 노출하지 않는다. 제품과 folder 이름은 `e2e-client`
또는 `stream_e2e_client`로 둔다.
내부 구현이 coroutine helper를 사용하더라도 public 목적은 서버 검증용 scenario client다.

e2e client의 coroutine 표면은 operation builder의 `async()` terminator로 제공한다. lifecycle
함수나 operation 시작 함수에 `*_async` 이름을 따로 붙이지 않는다. callback 기반 completion이
필요하면 같은 builder에서 `submit(callback)`을 사용한다.

```cpp
auto scenario_client = zlink::stream_e2e_client::use(client);
auto auth = co_await scenario_client
  .request<auth_res_t>(auth_req_t{"player-1"})
  .async();
```

이 helper는 기본 connector package의 필수 dependency가 아니다. 사용자가 e2e client package를
선택했을 때만 `task_t<T>`와 no-callback `async()`가 보이게 한다. core `connector_t`의 call
object는 `submit()`과 `submit(callback)`만 노출한다. auto codec helper도 같은 규칙을 따른다.
core helper는
`zlink/stream_connector/codecs/auto_codec.hpp`에 두고, coroutine helper는
`zlink/stream_e2e_client/codecs/auto_codec.hpp`에서만 `async()`를 노출한다.

## 8. Throwing Adapter

throwing adapter는 서버 framework와 tool code에서 사용할 수 있는 선택 표면이다. core
connector의 `result_t<T>` 실패를 전용 예외로 바꾸고, 성공 경로에서는 값을 바로 반환한다.

```cpp
auto auth = client.request<auth_res_t>(auth_req_t{"player-1"}).submit();
```

이 adapter의 예외는 error code와 message를 보존해야 한다. 예외 기반 표면을 쓰더라도 core
connector에서 알 수 있던 timeout, disconnected, validation failure 같은 정보가 사라지면
안 된다.

서버 framework는 장기적으로 예외 기반 application API로 정리한다. 단, client 기본 connector
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
있도록 Unreal 전용 함수와 타입을 제공한다. Unreal connector는 기본 connector API를 그대로
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
- 기본 connector runtime type을 Unreal public header에 직접 노출하지 않는다.
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

Unreal plugin은 `.uplugin` 단위로 배포한다. 내부적으로 기본 connector를 `ThirdParty`로
포함할지, 설치된 CMake package를 참조할지는 배포 채널별로 결정한다. Unreal lifecycle 때문에
Unreal `Sockets`/`Networking` 모듈이 필요할 수는 있지만, STREAM frame 처리, reconnect,
heartbeat, pending request correlation 같은 core connector 동작을 Unreal adapter 안에 다시
구현하지 않는다. Unreal adapter는 기본 connector를 private 구현으로 소유하고, Unreal public
API와 Game Thread dispatch만 책임진다.

codec은 일반 C++ connector와 같은 배포 정책을 따른다. JSON은 기본 포함한다. MessagePack과
Protobuf는 Unreal plugin 안의 build option으로 포함할 수 있지만, Unreal 사용자가 codec
산출물을 따로 가져오게 만들지 않는다.

### 10.1 Unreal adapter 구현 계약

Unreal adapter는 기본 connector의 wrapper가 맞다. 다만 wrapper라는 말은 public header에
`zlink::stream_connector` 타입을 그대로 노출한다는 뜻이 아니다. public header는 Unreal 타입만
보여 주고, private 구현이 기본 connector를 소유해서 호출을 위임한다.

소유권과 include 경계는 아래처럼 둔다.

| 위치 | 허용 | 금지 |
|------|------|------|
| `Source/ZLinkStreamConnector/Public/*` | `UObject`, `FString`, `FName`, `TArray<uint8>`, `TMap<FString, FString>`, Unreal delegate | `zlink/stream_connector...` include, `result_t`, `packet_t`, `connector_t`, coroutine type |
| `Source/ZLinkStreamConnector/Private/*` | `zlink/stream_connector.hpp`, `zlink/stream_connector/codecs/auto_codec.hpp`, Unreal Game Thread dispatch helper | `connector/core/src/runtime/*` private header include, STREAM frame encoder/decoder 재구현 |
| CMake/Build.cs | 기본 connector를 private dependency나 bundled ThirdParty로 연결 | 기본 connector dependency를 Unreal public API dependency로 전파 |

Unreal module layout은 Unreal 문서의 module 구조를 따른다. module root에는
`ZLinkStreamConnector.Build.cs`를 두고, C++ 코드는 `Public`과 `Private`로 나눈다. Unreal
public header가 다른 module에 노출되므로 기본 connector header는 `Private`에서만 include한다.

repo CMake test target에서는 `zlink_unreal_stream_connector`가 `zlink::stream_connector`와
`zlink::stream_connector_codecs`를 `PRIVATE`로 링크한다. 실제 Unreal plugin 배포에서는 같은
의존성을 설치된 CMake package로 참조하거나 `ThirdParty` source/library로 포함할 수 있다. 두
방식 모두 Unreal 사용자가 `zlink::stream_connector` 타입을 직접 include해야만 plugin을 쓸 수
있는 형태가 되면 안 된다.

Unreal public method와 기본 connector 호출은 아래처럼 대응한다.

| Unreal API | 기본 connector 호출 | 세부 규칙 |
|------------|--------------------|-----------|
| `Connect(Endpoint)` | `connector_factory_t::create(options)`, `connector.connect()` | endpoint scheme으로 `tcp`, `tls`, `ws`, `wss` transport를 정한다. 기본 `dispatch_mode`는 manual이다. |
| `Close()` | `connector.close()` | pending request와 received queue 정리는 기본 connector 의미를 따른다. |
| `SendJson(PacketName, JsonPayload)` | `connector.send(packet).codec(json).submit(callback)` | payload는 UTF-8 bytes로 넣고, completion은 Unreal delegate로 변환한다. |
| `SendJsonWithOptions(...)` | `send(...).metadata(...).compress().submit(callback)` | `Metadata`는 connector `metadata_t`로 복사하고 `bCompress`가 true일 때만 `compress()`를 호출한다. |
| `RequestJson(PacketName, JsonPayload, TimeoutSeconds)` | `connector.request<zlink::message_t>(packet).codec(json).timeout(...).submit(callback)` | response correlation과 timeout 처리는 기본 connector에 맡긴다. |
| `Dispatch()` | `connector.dispatch()` 반복 또는 adapter queue flush | delegate broadcast는 Game Thread에서만 실행한다. |
| `Tick(DeltaSeconds)` | `Dispatch()` 호출 또는 subsystem update 연결 | Tick 자체가 network protocol을 구현하지 않는다. |
| lifecycle shutdown | `Close()` | PIE 종료, map unload, game instance shutdown은 모두 graceful close로 모은다. |

callback thread 규칙은 adapter가 책임진다. 기본 connector callback이 worker thread나 receive
경로에서 들어올 수 있는 모드라면, Unreal adapter는 callback payload를 adapter 내부 queue에
넣고 `Dispatch()` 또는 Unreal Game Thread 예약 경로에서 delegate를 broadcast한다. `UObject`,
`AActor`, `UWorld`는 background thread에서 직접 만지지 않는다.

Unreal adapter는 별도 pending request table, heartbeat scheduler, reconnect state machine,
frame encoder/decoder, LZ4 frame codec을 만들지 않는다. 이런 동작은 기본 connector의
runtime 구현에 둔다. Unreal adapter가 소유하는 정보는 Unreal 객체 lifetime, delegate binding,
Game Thread 전달 queue, public option을 기본 connector option으로 바꾸는 mapping에 한정한다.

### 10.2 Unreal adapter 보강에 필요한 테스트

CTest에서 Unreal Engine 없이 빌드하는 smoke target은 아래를 확인한다.

- `Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h`가 기본 connector header를 include하지
  않는다.
- `zlink_unreal_stream_connector` target이 `zlink::stream_connector`와
  `zlink::stream_connector_codecs`를 `PRIVATE`로 링크한다.
- Unreal private source가 `connector/core/src/runtime/*` private header를 include하지 않는다.
- Unreal private source가 자체 STREAM frame enum, header codec, metadata codec, LZ4 frame
  codec을 정의하지 않는다.
- `Connect`, `SendJson`, `RequestJson`, `Dispatch`, lifecycle shutdown이 기본 connector를
  호출하는 adapter boundary test를 가진다.

기존 test gate가 Unreal private 구현에서 `zlink/stream_connector` include나
`zlink::stream_connector` link를 금지하고 있다면 그 gate는 제거한다. 새 gate는 public header
노출을 금지하고 private dependency를 요구해야 한다. 즉, 금지 대상은 public surface leakage와
`connector/core/src/runtime/*` private header include이지, private 구현의 기본 connector public API
사용이 아니다.

Unreal Engine이 설치된 환경의 Automation Test는 별도 `ZLinkStreamConnectorTests` module에 둔다.
이 module은 Rider의 Unreal test explorer와 Unreal Editor Automation 창에서 발견될 수 있어야
한다. Runtime module인 `ZLinkStreamConnector`에는 Automation Test나 editor-only helper를 넣지
않는다. 테스트는 engine lifecycle과 thread 경계만 확인한다. loopback server를 직접 열어 frame
byte를 검증하는 테스트는 기본 connector 회귀 테스트의 책임이다. Unreal Automation Test는
`UZLinkStreamConnector`가 Game Thread에서 delegate를 broadcast하는지, `UObject` target이 사라진
completion을 호출하지 않는지, PIE 종료와 map unload에서 `Close()`가 호출되는지를 검증한다.

### 10.3 Unreal에서 non-Unreal core를 감쌀 때 주의할 점

Unreal adapter는 기본 connector를 private 구현으로 사용하지만, 기본 connector 자체는 Unreal
API를 쓰지 않는다. 이 구조는 core를 재사용하기 좋지만 Unreal runtime과 맞지 않는 기본 C++
가정을 adapter가 흡수해야 한다. 아래 항목은 Unreal adapter의 구현 계약으로 고정한다.

| 주의 영역 | 규칙 |
|-----------|------|
| thread | core callback, reconnect event, receive event가 어느 thread에서 오든 Unreal object는 직접 만지지 않는다. adapter queue에 넣고 Game Thread에서 delegate를 broadcast한다. |
| lifetime | core callback은 `UObject` raw pointer를 캡처하지 않는다. `TWeakObjectPtr`이나 adapter-owned token으로 target 생존을 확인한 뒤 호출한다. |
| shutdown | PIE 종료, map unload, game instance shutdown, module unload가 모두 `Close()`와 callback 취소로 모인다. shutdown 이후 core callback이 도착해도 Unreal delegate를 호출하지 않는다. |
| allocator/ownership | core object는 Unreal GC 대상이 아니다. `UObject` public wrapper가 private runtime을 소유하고, private runtime이 core connector를 RAII로 소유한다. |
| exception | Unreal public API 경계 밖으로 C++ exception이 나가면 안 된다. core나 third-party 예외는 private 구현에서 `result_t` 또는 Unreal error delegate로 변환한다. |
| RTTI/build flag | Unreal module build flag와 core library build flag가 충돌하지 않게 한다. core public header가 RTTI, exception, coroutine을 요구하지 않아야 한다. |
| strings/encoding | Unreal public API는 `FString`/`FName`을 받고, core에는 명시적으로 UTF-8 `std::string`으로 변환해서 넘긴다. 변환 실패는 validation error로 처리한다. |
| bytes | `TArray<uint8>`와 `std::vector<std::uint8_t>` 사이를 복사하거나 명시 소유권으로 이동한다. core buffer가 Unreal container memory를 비동기로 참조하지 않는다. |
| logging | core logging을 Unreal log category에 직접 묶지 않는다. adapter가 core event/error를 받아 Unreal log로 변환한다. |
| build/link | Unreal public dependency에 core include path나 target을 노출하지 않는다. ThirdParty로 묶더라도 public header include 없이 plugin을 사용할 수 있어야 한다. |
| editor/runtime | editor-only API는 runtime module public API에서 쓰지 않는다. Automation Test나 editor helper는 Rider와 Editor에서 발견되는 별도 `ZLinkStreamConnectorTests` module로 분리한다. |
| hot reload | hot reload나 module unload 중에는 pending callback을 버리고 core runtime을 닫는다. unload 이후 static callback이나 background thread가 module code를 호출하지 않게 한다. |

Unreal adapter는 core의 `manual dispatch`를 기본으로 사용한다. immediate dispatch를 지원하더라도
Unreal delegate는 즉시 broadcast하지 않고 adapter queue를 거쳐 Game Thread에서 실행한다.
이 규칙을 지키면 core가 Unreal을 모르는 상태에서도 Unreal object lifetime과 thread 규칙을
깨지 않는다.

adapter boundary test는 fake core runtime을 주입할 수 있어야 한다. 이 fake는 protocol byte를
검증하지 않고, adapter가 core에 넘긴 endpoint, timeout, metadata, compression option과
callback 취소 여부만 기록한다. 이렇게 해야 Unreal test가 core protocol test를 반복하지 않고
Unreal 경계만 검증할 수 있다.

### 10.4 외부 엔진 기준

Unreal module 구조와 Automation Test 실행은 Epic의
[Unreal Engine automated test 문서](https://dev.epicgames.com/documentation/unreal-engine/run-automation-tests-in-unreal-engine?lang=en-US)에
맞춘다. module root에는 `Build.cs`, `Public`/`Private` 폴더, public/private dependency
구분을 둔다.

Godot adapter는 Godot 4
[GDExtension C++ binding 모델](https://docs.godotengine.org/en/stable/tutorials/scripting/cpp/gdextension_cpp_example.html)을
따른다. Godot 문서는 GDExtension이 C API 위의 C++ binding으로 Godot class를 확장하는
방식이며, 대상 Godot 버전에 맞는 `godot-cpp` branch를 써야 한다고 설명한다. command line
test는 Godot의
[command line 실행 문서](https://docs.godotengine.org/en/stable/tutorials/editor/command_line_tutorial.html)를
따른다. thread 사용 시에는 Godot의 thread-safe API 여부를 확인해야 하므로 signal emission은
adapter의 main-thread dispatch 경계 뒤에 둔다.

Axmol adapter는 Axmol `Scheduler::runOnAxmolThread`를 main thread dispatch 경계로 사용한다.
Axmol [Director 문서](https://axmol.dev/manual/latest/d4/d72/classax_1_1_director.html)는
background task가 끝난 뒤 completion이 `Scheduler::runOnAxmolThread()`를 통해 Axmol thread에서
실행되는 흐름을 설명한다. adapter도 같은 thread 경계를 따른다.

## 11. Godot Connector

Godot connector는 GDExtension adapter로 제공한다. 기본 connector의 result/callback 표면을
Godot signal이나 callable 표면으로 바꾼다.

Godot adapter는 아래 원칙을 따른다.

- GDExtension public type으로 노출한다.
- Godot main thread에서 signal을 emit할 수 있게 dispatch 경계를 둔다.
- C++ exception에 의존하지 않는다.
- 기본 connector의 C++ template 표면을 Godot script 사용자에게 직접 노출하지 않는다.
- private 구현은 기본 connector를 소유하고, Godot public type은 기본 connector 타입을 노출하지
  않는다.
- Godot object나 signal은 thread-safe API 여부가 확인된 경로에서만 다룬다. 기본은 received
  event를 adapter queue에 넣고 Godot main thread update에서 signal을 emit하는 방식이다.

## 12. Axmol Connector

Axmol connector는 유지되는 C++ Cocos2d-x 계열을 대상으로 한다. Cocos2d-x 자체는 지원하지
않는다.

Axmol adapter는 아래 원칙을 따른다.

- Axmol scheduler나 main thread dispatch 방식과 맞춘다.
- 예외에 의존하지 않는다.
- callback/event 중심 표면을 제공한다.
- 기본 connector를 CMake package나 source dependency로 참조한다.
- private 구현은 기본 connector를 소유한다. Axmol public header에는 `zlink::stream_connector`
  타입을 노출하지 않는다.
- background callback은 `Scheduler::runOnAxmolThread`를 통해 Axmol thread로 전달한 뒤 사용자
  callback을 실행한다.

## 13. 테스트

테스트 도구는 서버 framework C++와 같게 둔다.

- GoogleTest
- GoogleMock
- CTest

core connector 필수 회귀 항목은 아래와 같다.

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
- 기본 connector test는 예외와 e2e client helper 없이 통과한다.

engine adapter 테스트는 core connector GoogleTest 회귀와 별도로 둔다. engine adapter는 STREAM
frame byte, request sequence, metadata encoding, compression payload 형식을 다시 검증하지
않는다. 그런 검증을 engine test에서 반복하면 adapter가 core 구현을 복제하게 된다. engine
test는 engine lifecycle, public API 노출, main thread dispatch, object lifetime, build/package
경계를 검증한다.

### 13.1 공통 engine adapter test matrix

모든 engine adapter는 아래 matrix를 가진다.

| 테스트 축 | 검증 내용 | core test와의 경계 |
|-----------|-----------|--------------------|
| public header boundary | engine public header가 `zlink/stream_connector...`를 include하지 않음 | core public API 자체는 core contract test가 검증 |
| private dependency | adapter private 구현만 `zlink::stream_connector`와 codec target을 사용함 | STREAM runtime 동작은 core test가 검증 |
| lifecycle | engine 생성, connect, close, shutdown hook이 core lifecycle로 모임 | frame close byte는 core test가 검증 |
| main thread dispatch | delegate, signal, callback이 engine main thread에서 실행됨 | packet queue 순서는 core dispatch test가 검증 |
| destroyed target | object가 사라진 뒤 completion을 호출하지 않음 | pending request cleanup은 core test가 검증 |
| options mapping | endpoint, timeout, metadata, compression option이 core option/call로 변환됨 | option별 protocol 결과는 core test가 검증 |
| package smoke | engine project가 source package만으로 build됨 | vcpkg/Conan package smoke는 core/e2e client가 검증 |

엔진이 설치되지 않은 CI에서도 최소한의 static test는 실행한다. 이 static test는 header include
금지, CMake/Build script dependency, private source 금지 패턴, adapter boundary 호출을 검사한다.
엔진이 설치된 CI나 개발 머신에서는 engine runtime test를 추가로 실행한다.

### 13.2 Unreal 테스트

Unreal Connector 테스트는 일반 C++ connector GoogleTest 회귀와 별도로 둔다. Unreal runtime
module compile test, Blueprint-callable API compile check, Game Thread dispatch smoke를 최소
기준으로 둔다. Automation Test는 plugin의 `ZLinkStreamConnectorTests` module에 둔다. 이 module은
Rider에서 Unreal test로 선택 실행할 수 있고, command line에서는 Unreal Automation runner로
실행할 수 있어야 한다.

실제 Unreal 동작 검증은 `ZLink.StreamConnector.Loopback` Automation Test로 한다. 이
테스트는 Unreal test project에서 기본 connector 기반 `UZLinkStreamConnector`가 실제 server에
접속하고, Game Thread delegate와 lifecycle shutdown을 지키는지 확인한다. STREAM frame 구조,
metadata encoding, request sequence, compression payload 형식은 기본 connector GoogleTest가
검증한다. Unreal Automation Test가 같은 byte-level protocol test를 다시 구현하면 engine
adapter가 core connector 구현을 복제하게 되므로 금지한다.

Unreal Engine이 설치된 머신에서는 아래처럼 headless로 실행한다.

```bash
UnrealEditor-Cmd <TestProject>.uproject \
  -ExecCmds="Automation RunTests ZLink.StreamConnector; Quit" \
  -unattended -nop4 -nosplash -NullRHI
```

Unreal plugin에는 아래 module 구분을 둔다.

| module | type | 책임 |
|--------|------|------|
| `ZLinkStreamConnector` | Runtime | `UZLinkStreamConnector` public API와 core connector adapter 구현 |
| `ZLinkStreamConnectorTests` | DeveloperTool | Rider/Editor/command line에서 실행되는 Automation Test |

`ZLinkStreamConnectorTests.Build.cs`는 `ZLinkStreamConnector`를 private dependency로 참조한다.
반대로 runtime module은 test module이나 editor-only module을 참조하지 않는다.

Rider에서는 Unreal project를 열고 Unreal test explorer에서 `ZLink.StreamConnector` group을
선택해 실행한다. command line과 Rider 실행 결과가 같은 테스트 이름을 공유해야 CI와 로컬
검증이 어긋나지 않는다.

Unreal test module에는 아래 Automation Test를 둔다.

| 테스트 이름 | 목적 |
|-------------|------|
| `ZLink.StreamConnector.ModuleLoads` | plugin module이 editor commandlet에서 load되는지 확인 |
| `ZLink.StreamConnector.BlueprintApi` | `UZLinkStreamConnector`의 Blueprint callable 함수와 delegate가 reflection에 등록되는지 확인 |
| `ZLink.StreamConnector.GameThreadDispatch` | background completion을 adapter queue에 넣고 `Dispatch()`에서 Game Thread delegate로 broadcast하는지 확인 |
| `ZLink.StreamConnector.TargetLifetime` | delegate target `UObject`가 destroy된 뒤 completion을 호출하지 않는지 확인 |
| `ZLink.StreamConnector.LifecycleShutdown` | PIE 종료, map unload, game instance shutdown이 모두 `Close()` 의미로 모이는지 확인 |
| `ZLink.StreamConnector.OptionsMapping` | endpoint, timeout, metadata, compression option이 core call로 전달되는지 fake runtime으로 확인 |

Unreal Engine이 없는 CI에서는 repo CTest가 아래 static gate를 실행한다.

- `Source/ZLinkStreamConnector/Public/*`가 core header를 include하지 않는다.
- `ZLinkStreamConnector.Build.cs`의 public dependency에 core connector가 드러나지 않는다.
- repo CMake smoke target에서 core connector와 codec target은 `PRIVATE` dependency다.
- private source가 `connector/core/src/runtime/*` header를 include하지 않는다.
- private source가 frame/header/metadata/compression codec을 새로 정의하지 않는다.

### 13.3 Godot 테스트

Godot adapter는 GDExtension package와 Godot runtime test를 나눈다.

엔진이 없는 CI의 static gate는 아래를 확인한다.

- GDExtension public header가 core connector header나 template type을 노출하지 않는다.
- adapter private source만 `zlink::stream_connector` public header를 include한다.
- `godot-cpp` binding은 adapter boundary 뒤에만 보이고 core target dependency로 전파되지 않는다.
- signal emission code가 adapter queue나 main-thread dispatch helper를 통하지 않고 직접 호출되지 않는다.
- build script가 대상 Godot version과 `godot-cpp` branch를 명시한다.

Godot이 설치된 환경의 runtime test는 headless Godot으로 실행한다.

```bash
godot --headless --path <TestProject> \
  --script res://tests/zlink_stream_connector_tests.gd
```

Godot runtime test project에는 아래 항목을 둔다.

| 테스트 이름 | 목적 |
|-------------|------|
| `extension_loads` | GDExtension이 project에서 load되고 connector class가 등록되는지 확인 |
| `signal_on_main_thread` | received/request completion signal이 Godot main thread에서 emit되는지 확인 |
| `callable_lifetime` | target node가 queue_free된 뒤 completion을 호출하지 않는지 확인 |
| `process_dispatch` | `_process`나 명시 dispatch 호출에서 adapter queue가 비워지는지 확인 |
| `options_mapping` | endpoint, timeout, metadata, compression option이 core call로 전달되는지 fake runtime으로 확인 |
| `loopback_smoke` | 실제 STREAM test server에 접속해 connect, send, request, close 흐름이 통과하는지 확인 |

Godot runtime test의 loopback smoke는 adapter wiring만 검증한다. frame byte, compression format,
request sequence 세부 검증은 core GoogleTest에 남긴다.

### 13.4 Axmol 테스트

Axmol adapter도 static gate와 engine runtime test를 나눈다.

엔진이 없는 CI의 static gate는 아래를 확인한다.

- Axmol public header가 core connector header나 `zlink::stream_connector` type을 노출하지 않는다.
- adapter private source만 core public header를 include한다.
- user callback 실행은 `Scheduler::runOnAxmolThread`나 동일한 main-thread dispatch helper 뒤에 있다.
- adapter가 자체 frame/header/metadata/compression codec을 정의하지 않는다.
- CMake package가 Axmol project에서 source dependency로 추가될 수 있는 layout을 가진다.

Axmol이 설치된 환경의 runtime test는 최소 test app을 headless 또는 offscreen 가능한 runner로
실행한다. runner가 없는 플랫폼에서는 CI artifact로 test app을 빌드하고, 개발 머신에서 수동
실행할 수 있는 command를 문서화한다.

```bash
<AxmolTestApp> --zlink-test=stream-connector --headless
```

Axmol runtime test app에는 아래 항목을 둔다.

| 테스트 이름 | 목적 |
|-------------|------|
| `component_lifecycle` | scene/layer/component 생성과 destroy가 connector close로 모이는지 확인 |
| `callback_on_axmol_thread` | background completion이 Axmol thread에서 user callback으로 전달되는지 확인 |
| `destroyed_node_callback` | node가 제거된 뒤 completion을 호출하지 않는지 확인 |
| `scheduler_dispatch` | `Scheduler::runOnAxmolThread` 경로가 adapter queue를 flush하는지 확인 |
| `options_mapping` | endpoint, timeout, metadata, compression option이 core call로 전달되는지 fake runtime으로 확인 |
| `loopback_smoke` | 실제 STREAM test server에 접속해 connect, send, request, close 흐름이 통과하는지 확인 |

Axmol loopback smoke도 adapter wiring을 보는 테스트다. STREAM protocol의 byte-level 검증은 core
GoogleTest가 담당한다.

## 14. 구현 순서

이 초안은 아직 정식 공개 계약이 아니므로 기존 실험 API와의 호환성을 유지하지 않는다.
코드 적용 시에는 callback을 받는 과거 async submit 표면, core header의 불필요한 `task_t`
노출, lifecycle에 붙어 있던 과거 `*_async` member를 deprecated로 남기지 않고 제거한다.
샘플과 테스트도 새 표면으로 바로 옮긴다.

구현은 아래 순서로 진행한다.

1. `connector/` 아래에 core, e2e client, engine adapter source layout을 분리하고,
   compiled target과 CMake package export는 상위 `framework/languages/cpp/CMakeLists.txt`에
   둔다.
2. 기본 connector public contract에서 `task_t` include와 모든 `*_async` member를 제거한다.
3. core의 `send_call_t`, `request_call_t`, `wait_call_t`는 `submit()`과
   `submit(callback)`만 노출한다.
4. codec helper도 core helper와 coroutine helper로 나눈다. core codec helper는
   `submit()`, `submit(callback)`을 노출하고, coroutine helper는 `async()` terminator를
   노출한다.
5. `e2e-client` package를 추가한다. 이 helper는 builder terminator `async()`만 추가하고
   lifecycle `*_async` member는 추가하지 않는다.
6. throwing adapter를 core와 분리한다.
7. 샘플과 테스트를 새 core 표면 또는 e2e client 표면 중 하나로 명시적으로 옮긴다.
   게임 client 샘플은 core 표면을 기준으로 작성하고, 서버 성능 테스트 client나 CLI 성격의
   테스트만 e2e client helper를 include한다.
8. 기본 connector contract test로 core public header가 coroutine header와 exception header에
   의존하지 않는지 고정한다.
9. 서버 framework call 표면을 예외 기반 awaitable API로 정리한다.
10. Unreal plugin을 기본 connector 위 facade로 정리한다. 기존 자체 frame/socket/runtime 구현은
    제거하고, public Unreal API와 Game Thread dispatch만 adapter에 남긴다.
11. Godot GDExtension adapter를 구현하고 static gate와 Godot runtime test project를 추가한다.
12. Axmol adapter를 구현하고 static gate와 Axmol runtime test app을 추가한다.
13. Cocos Creator 문서에는 TypeScript connector 사용을 명시한다.

호환성 shim은 두지 않는다. 각 단계는 해당 단계의 최종 표면으로 빌드하고 테스트해야 한다.
특히 기본 connector test는 예외와 e2e client helper 없이 통과해야 한다.

## 15. 구현 착수 전 체크리스트

아래 항목이 모두 문서와 코드 계획에 반영되어야 구현을 시작할 수 있다.

| 항목 | 완료 기준 |
|------|-----------|
| 폴더 구조 | `connector/core`, `connector/e2e-client`, `connector/engines/{unreal,godot,axmol}`로 산출물이 분리되어 있다. |
| core public API | `zlink/stream_connector.hpp`에서 coroutine, throwing adapter, engine header가 보이지 않는다. |
| coroutine 표면 | `task_t`와 `async()`는 `zlink::stream_e2e_client` include와 target을 선택했을 때만 보인다. |
| 오류 계약 | timeout, disconnected, unsupported feature, codec, compression, protocol 오류가 `result_t<T>`로 구분된다. |
| package graph | server framework와 connector package가 서로 필수 의존성이 아니다. |
| codec graph | raw/json은 기본이고 MessagePack/Protobuf는 별도 feature와 target으로 분리된다. |
| transport graph | TCP/TLS/WebSocket/WSS는 같은 frame read/write 의미를 공유한다. |
| engine wrapper | Unreal/Godot/Axmol public header는 engine type만 노출하고 core type을 숨긴다. |
| engine tests | 각 engine은 static gate와 engine runtime test 계획을 가진다. |
| 회귀 테스트 | core protocol test와 engine lifecycle/thread test가 서로 중복되지 않는다. |
| 배포 | core/e2e client는 vcpkg/Conan 기준을 갖고, engine adapter는 source/plugin package 기준을 갖는다. |
| 제거 대상 | 기존 `unreal-connector` 위치, core 안의 coroutine helper, adapter 자체 protocol 구현을 남기지 않는다. |

## 16. Goal 실행 추적표

이 문서를 구현 goal로 실행할 때는 아래 ID를 작업 단위로 사용한다. 각 ID는 코드 변경,
테스트, 문서 보강을 함께 끝내야 완료로 본다. 중간에 일부 engine SDK가 없는 환경이면 static
gate와 build script까지 완료하고, runtime gate는 `engine-required`로 남긴 뒤 실행 방법과
필요 SDK 버전을 문서에 적는다.

| ID | 작업 단위 | 산출물 | 완료 gate |
|----|-----------|--------|-----------|
| G20-01 | 기존 layout 정리 | `connector/core`, `connector/e2e-client`, `connector/engines/*`, 기존 `unreal-connector` 제거 | layout contract, `find` 기반 경로 검사 |
| G20-02 | core CMake target 분리 | `zlink::stream_connector`, `zlink::stream_connector_codecs` | `cmake --build`, package export smoke |
| G20-03 | core public API 정리 | `zlink/stream_connector.hpp`, contracts headers | public header compile test, coroutine/engine include 금지 test |
| G20-04 | `result_t` 오류 계약 | `result_t`, `error_t`, `error_code_t` | timeout/disconnected/unsupported/codec/compression/protocol error unit test |
| G20-05 | transport runtime | TCP, TLS, WebSocket, WSS transport factory | transport integration test, unsupported feature test |
| G20-06 | protocol runtime | frame/header/metadata/compression runtime | frame split, metadata limit, payload limit, compression test |
| G20-07 | request/dispatch runtime | pending request, callback queue, wait queue, heartbeat, reconnect | request correlation, timeout cleanup, manual/immediate dispatch, heartbeat/reconnect test |
| G20-08 | codec helper | JSON 기본 helper, optional MessagePack/Protobuf feature | typed send/request/on/wait test, optional feature compile test |
| G20-09 | e2e client 분리 | `zlink::stream_e2e_client`, `task_t`, `async()` terminator | e2e client compile test, core no-coroutine compile test |
| G20-10 | throwing adapter 분리 | throwing component/header | exception mapping unit test, core umbrella 비노출 test |
| G20-11 | samples/e2e migration | core sample, e2e scenario client sample | sample smoke, e2e smoke |
| G20-12 | packaging | CMake config, vcpkg port, Conan recipe | package install/import smoke |
| G20-13 | Unreal adapter | Unreal source plugin, fake core boundary, Automation Test plan | static gate, repo CTest, Unreal runtime gate |
| G20-14 | Godot adapter | GDExtension source package, Godot test project | static gate, Godot headless runtime gate |
| G20-15 | Axmol adapter | Axmol source package, Axmol test app | static gate, Axmol runtime gate 또는 documented manual gate |
| G20-16 | unsupported Cocos 정리 | Cocos Creator는 TypeScript connector로 안내, Cocos2d-x 미지원 명시 | doc grep, package name grep |
| G20-17 | final audit | 전체 문서, label, package graph, dependency graph | `git diff --check`, connector labels, full relevant CTest |

### 16.1 실행 순서와 의존성

아래 순서로 실행한다. 뒤 단계가 앞 단계의 public contract를 전제로 삼기 때문에 순서를
바꾸지 않는다.

```text
G20-01
  -> G20-02
  -> G20-03
  -> G20-04
  -> G20-05
  -> G20-06
  -> G20-07
  -> G20-08
  -> G20-09
  -> G20-10
  -> G20-11
  -> G20-12
  -> G20-13
  -> G20-14
  -> G20-15
  -> G20-16
  -> G20-17
```

G20-13, G20-14, G20-15는 core protocol을 다시 만들지 않는다. 이 세 단계는 engine public
API, main thread dispatch, object lifetime, package layout만 검증한다. core protocol에 결함이
보이면 G20-05, G20-06, G20-07로 되돌려 고친다.

### 16.2 필수 산출물 체크리스트

각 파일이나 디렉터리는 해당 ID가 끝날 때 존재해야 한다. 아직 구현이 비어 있더라도 placeholder
대신 최소 compile 가능한 source나 명시적인 test fixture를 둔다.

| ID | 필수 산출물 |
|----|-------------|
| G20-01 | `connector/CMakeLists.txt`, `connector/core`, `connector/e2e-client`, `connector/engines/unreal`, `connector/engines/godot`, `connector/engines/axmol` |
| G20-02 | `connector/core/include/zlink/stream_connector.hpp`, `connector/core/src/runtime`, exported `zlink::stream_connector` target |
| G20-03 | `contracts/result.hpp`, `contracts/connector.hpp`, `contracts/codec_registry.hpp`, `contracts/stream_payload.hpp` |
| G20-04 | error enum, error message mapping, operation result tests |
| G20-05 | endpoint parser, transport factory, TCP/TLS/WebSocket/WSS adapters |
| G20-06 | frame codec, header codec, metadata codec, compression codec |
| G20-07 | pending request table, dispatch queue, heartbeat monitor, reconnect policy |
| G20-08 | `codecs/auto_codec.hpp`, JSON helper, optional codec feature headers |
| G20-09 | `connector/e2e-client/include/zlink/stream_e2e_client.hpp`, `task.hpp`, coroutine codec helper |
| G20-10 | throwing adapter header/source outside core umbrella |
| G20-11 | core client sample, e2e scenario sample, migrated tests |
| G20-12 | CMake config template, vcpkg port files, Conan recipe, package smoke project |
| G20-13 | `ZLinkStreamConnector.uplugin`, `Source/ZLinkStreamConnector/Public`, `Private`, `Source/ZLinkStreamConnectorTests/Private`, Automation Test module |
| G20-14 | `.gdextension` template, GDExtension C++ source, Godot test project |
| G20-15 | Axmol CMake source package, test app source |
| G20-16 | doc update that names Cocos Creator TypeScript connector and Cocos2d-x unsupported status |
| G20-17 | final validation log in implementation plan or PR description |

### 16.3 금지 항목 체크리스트

아래 항목이 하나라도 남으면 goal은 완료가 아니다.

| 금지 항목 | 확인 방법 |
|-----------|-----------|
| `framework/languages/cpp/unreal-connector`가 남아 있음 | `test ! -e framework/languages/cpp/unreal-connector` |
| core umbrella가 `<coroutine>` 또는 engine header를 include함 | public header grep/compile test |
| `task_t`나 `async()`가 `zlink/stream_connector.hpp`만 include해도 보임 | negative compile test |
| engine public header가 `zlink/stream_connector` type을 노출함 | header grep/ABI compile test |
| engine adapter가 frame/header/metadata/compression codec을 자체 정의함 | private source grep/static test |
| Unreal adapter가 background thread에서 `UObject`, `AActor`, `UWorld`를 직접 만짐 | static grep과 Automation Test |
| Godot adapter가 main thread dispatch 없이 signal을 emit함 | static grep과 headless runtime test |
| Axmol adapter가 Axmol thread 경계 없이 user callback을 실행함 | static grep과 runtime test |
| server framework target이 connector target을 필수 dependency로 가짐 | CMake graph test |
| connector target이 server framework target을 필수 dependency로 가짐 | CMake graph test |
| compatibility shim이 남아 있음 | old API grep과 compile failure test |

### 16.4 검증 명령

기본 검증은 아래 명령으로 시작한다. label 이름은 구현 중 실제 CTest label과 맞춰 유지한다.

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L connector-contract --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-protocol --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-transport --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-typed --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-package --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-unreal-contract --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-unreal-compile --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-unreal-smoke --output-on-failure
git diff --check
```

엔진 SDK가 설치된 환경에서는 아래 runtime gate도 실행한다.

```bash
UnrealEditor-Cmd <TestProject>.uproject \
  -ExecCmds="Automation RunTests ZLink.StreamConnector; Quit" \
  -unattended -nop4 -nosplash -NullRHI

godot --headless --path <TestProject> \
  --script res://tests/zlink_stream_connector_tests.gd

<AxmolTestApp> --zlink-test=stream-connector --headless
```

SDK가 없는 환경에서는 runtime gate를 통과로 표시하지 않는다. 대신 어떤 SDK가 없어서
실행하지 못했는지, static gate가 어디까지 통과했는지 기록한다.

## 17. 현재 구현 검증 기록

이 절은 이 draft를 goal로 실행할 때 빠진 항목이 없도록 남기는 진행 기록이다. 정식 spec으로
승격할 때는 실제 PR 설명이나 구현 계획 문서의 검증 기록으로 옮긴다.

2026-06-11 현재 확인한 항목은 아래와 같다.

| ID | 현재 상태 | 증거 |
|----|-----------|------|
| G20-01 | 완료 | 기존 `unreal-connector` 경로 제거, `connector/core`, `connector/e2e-client`, `connector/engines/{unreal,godot,axmol}` 존재 |
| G20-02 | 완료 | `zlink::stream_connector`, `zlink::stream_connector_codecs` build 및 install export 확인 |
| G20-03 | 완료 | core umbrella가 coroutine, throwing adapter, engine header를 include하지 않도록 layout contract로 고정 |
| G20-04 | 완료 | connector error/result 회귀는 `test_cpp_stream_connector`의 connector label에서 검증 |
| G20-05 | 완료 | TCP/TLS/WebSocket/WSS transport factory는 core connector test와 package build에서 검증 |
| G20-06 | 완료 | frame/header/metadata/compression runtime은 core connector protocol test에서 검증 |
| G20-07 | 완료 | request correlation, timeout, dispatch, heartbeat, reconnect는 core connector test에서 검증 |
| G20-08 | 완료 | JSON helper와 optional MessagePack/Protobuf feature wiring은 connector typed/package gate에서 검증 |
| G20-09 | 완료 | `zlink::stream_e2e_client`, `task_t`, `async()`는 e2e client include와 target에서만 노출 |
| G20-10 | 완료 | `zlink::stream_connector_throwing`은 별도 header/target이며 core umbrella에 포함하지 않음 |
| G20-11 | 완료 | core client sample과 e2e client sample을 CTest `connector-e2e` label로 실행 |
| G20-12 | 완료 | CMake install/import smoke, vcpkg manifest/portfile, Conan `create` 검증 완료. connector package install은 framework/http-client 산출물을 설치하지 않음 |
| G20-13 | static 완료, runtime engine-required | Unreal source plugin은 core public API를 private으로 사용하고 자체 STREAM frame/protocol 구현을 하지 않음. Rider/Editor에서 발견되는 `ZLinkStreamConnectorTests` module을 분리하고, runtime owner는 `TWeakObjectPtr`와 detach 경계로 확인함 |
| G20-14 | static 완료, runtime artifact-required | Godot source package, `.gdextension`, headless test script 추가. request completion은 main thread dispatcher 경계를 거친다. Godot 4.6.3 headless 실행은 확인했으나 `godot-cpp` build artifact가 없으면 full GDExtension runtime gate를 통과 처리하지 않음 |
| G20-15 | static 완료, runtime engine-required | Axmol source package와 test app source 추가. request completion은 Axmol thread dispatcher 경계를 거친다. Axmol SDK/app runner가 없는 환경에서는 runtime gate를 통과 처리하지 않음 |
| G20-16 | 완료 | Cocos Creator는 TypeScript connector 사용, Cocos2d-x 미지원, `cocos-connector` package name 금지 |
| G20-17 | 완료 | 아래 검증 명령으로 layout, label, connector package, Unreal static smoke를 확인 |

실행한 검증 명령은 아래와 같다.

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build -j2
ctest --test-dir framework/languages/cpp/build -L 'connector-contract|connector-protocol|connector-transport|connector-typed|connector-e2e|connector-package|connector-unreal-contract|connector-unreal-compile|connector-unreal-smoke' --output-on-failure
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_layout_contract|test_cpp_framework_label_contract' --output-on-failure
.tools/godot/godot --headless --path framework/languages/cpp/connector/engines/godot/tests --script res://zlink_stream_connector_tests.gd
.tools/conan-venv/bin/conan create framework/languages/cpp/connector/core/packaging/conan --build=missing -s build_type=Release
.tools/conan-venv/bin/conan create framework/languages/cpp/connector/e2e-client/packaging/conan --build=missing -s build_type=Release
.tools/vcpkg-src/bootstrap-vcpkg.sh -disableMetrics
.tools/vcpkg-src/vcpkg version
git diff --check
```

package 분리 검증은 아래 설정으로 수행한다.

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build-core-package-check \
  -DZLINK_FRAMEWORK_CPP_BUILD_TESTS=OFF \
  -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=OFF \
  -DZLINK_FRAMEWORK_CPP_INSTALL_FRAMEWORK=OFF \
  -DZLINK_STREAM_CONNECTOR_BUILD_E2E_CLIENT=OFF \
  -DZLINK_STREAM_CONNECTOR_BUILD_UNREAL=OFF \
  -DZLINK_STREAM_CONNECTOR_BUILD_GODOT=OFF \
  -DZLINK_STREAM_CONNECTOR_BUILD_AXMOL=OFF
cmake --build framework/languages/cpp/build-core-package-check --target zlink_stream_connector -j2
cmake --install framework/languages/cpp/build-core-package-check --prefix framework/languages/cpp/build-core-package-check/install

cmake -S framework/languages/cpp -B framework/languages/cpp/build-e2e-package-check \
  -DZLINK_FRAMEWORK_CPP_BUILD_TESTS=OFF \
  -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=OFF \
  -DZLINK_FRAMEWORK_CPP_INSTALL_FRAMEWORK=OFF \
  -DZLINK_STREAM_CONNECTOR_BUILD_E2E_CLIENT=ON \
  -DZLINK_STREAM_CONNECTOR_BUILD_UNREAL=OFF \
  -DZLINK_STREAM_CONNECTOR_BUILD_GODOT=OFF \
  -DZLINK_STREAM_CONNECTOR_BUILD_AXMOL=OFF
cmake --build framework/languages/cpp/build-e2e-package-check --target zlink_stream_connector -j2
cmake --install framework/languages/cpp/build-e2e-package-check --prefix framework/languages/cpp/build-e2e-package-check/install
```

feature option 검증은 아래 설정으로 수행한다.

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build-feature-off-check \
  -DZLINK_FRAMEWORK_CPP_BUILD_TESTS=OFF \
  -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=OFF \
  -DZLINK_FRAMEWORK_CPP_INSTALL_FRAMEWORK=OFF \
  -DZLINK_STREAM_CONNECTOR_BUILD_E2E_CLIENT=OFF \
  -DZLINK_STREAM_CONNECTOR_BUILD_UNREAL=OFF \
  -DZLINK_STREAM_CONNECTOR_BUILD_GODOT=OFF \
  -DZLINK_STREAM_CONNECTOR_BUILD_AXMOL=OFF \
  -DZLINK_STREAM_CONNECTOR_WITH_TLS=OFF \
  -DZLINK_STREAM_CONNECTOR_WITH_WEBSOCKET=OFF
cmake --build framework/languages/cpp/build-feature-off-check --target zlink_stream_connector -j2
```

engine runtime gate는 현재 환경에서 아래 수준까지 확인했다.

| gate | 상태 | 필요한 환경 |
|------|------|-------------|
| Unreal Automation Test | engine-required | `UnrealEditor-Cmd`와 Unreal test project |
| Godot headless test | partial | Godot 4.6.3 executable로 test project script 실행 확인. Full GDExtension runtime gate에는 `godot-cpp` build artifacts가 필요 |
| Axmol test app | engine-required | Axmol SDK와 app runner |

로컬 도구는 아래 위치에 준비했다.

| tool | 위치 | 확인 |
|------|------|------|
| Godot | `.tools/godot/godot` | `4.6.3.stable.official.7d41c59c4` |
| Conan | `.tools/conan-venv/bin/conan` | `Conan version 2.29.0` |
| vcpkg | `.tools/vcpkg-src/vcpkg` | `2026-05-27-d5b6777d666efc1a7f491babfcdab37794c1ae3e` |
