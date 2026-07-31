<!-- framework-adapter-nav:start -->
[가이드 홈](README.ko.md) | [다음: 2. 시작하기](02-getting-started.ko.md)
<!-- framework-adapter-nav:end -->

# 1. 개요

> 이 문서는 C++ 가이드의 진입점이다. 언어 중립 정의는
> [공통 스펙 목차](../../../common/README.ko.md)가, C++ 표면의 정확한 계약은
> [C++ exact interface 목차](../../../common/spec/server/languages/cpp/interfaces/README.ko.md)가 소유한다.

## 1. 무엇을 만드는가

실시간 메시징이 중요한 서버 시스템을 여러 프로세스로 나눠 만든다. 서버 간 typed
메시징, 상태 단위(Spot)의 직렬 실행, 외부 client 실시간 연결, 무중단 이전을 한 선언
모델 위에서 조합한다.

C++에서는 **framework가 프로세스 자체를 구성한다.** 다른 언어처럼 기존 애플리케이션
프레임워크(Spring Boot · NestJS · ASP.NET Core) 위에 얹는 것이 아니라, DI 컨테이너 ·
설정 바인딩 · HTTP hosting을 framework가 함께 제공한다. 그래서 C++ 가이드에는 다른
언어에 없는 세 장이 더 있다([18](18-di-container.ko.md) ·
[19](19-configuration.ko.md) · [20](20-http-hosting.ko.md)).

## 2. 무엇을 대체하나

| 지금 쓰는 것 | ZLink가 대신하는 부분 |
| --- | --- |
| 서비스 간 gRPC · REST 호출 | host · port · stub 대신 **ChannelName**으로 부른다 |
| 방·세션 상태를 담는 분산 락 | **Spot**의 직렬 실행 — 같은 상태에 두 요청이 겹치지 않는다 |
| 직접 만든 WebSocket 세션 관리 | **STREAM session**과 Actor binding |
| 배포 시 세션 드레이닝 스크립트 | **relocation** — 상태를 다른 node로 옮기고 내린다 |
| 앞단 gateway · 로드밸런서 | location store descriptor 기반 자동 연결과 select-one |

HTTP는 대체하지 않는다. 외부 진입은 framework가 내장한 HTTP hosting이 맡고, ZLink는 그
뒤의 서버 간 통신과 상태 처리를 맡는다. **HTTP 요청을 보내는 쪽**은 별도 산출물
`zlink::http_client`다.

## 3. 산출물

| 항목 | 값 |
| --- | --- |
| CMake target | `zlink::framework` |
| facade header | `#include <zlink/framework.hpp>` |
| 네임스페이스 | `zlink::framework` |
| public 계약 | `zlink/framework/contracts/*` — Boost 같은 구현 의존성을 노출하지 않는다 |
| HTTP 요청 client | `zlink::http_client`([가이드](../http-client/README.ko.md)) |
| codec 확장 | `zlink::framework_codec_protobuf` · `zlink::framework_codec_msgpack` |
| location store | `zlink::framework_locations_redis` — 여러 node를 쓸 때 |

## 4. 등록 진입점

`app_t` 하나를 만들고 `add_zlink_framework`에 구성 람다를 넘긴다. 프로세스 수명은
`run`이 맡는다.

```cpp
#include <zlink/framework.hpp>

int main (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework ([] (zlink::framework::zlink_framework_options_t &options) {
        options.http ()
          .listen ("http://0.0.0.0:8080")
          .map_post<open_conversation_http_handler_t> ("/conversations");
    });
    return app.run (argc, argv);
}
```

**handler는 클래스 하나로 등록한다.** 의존성은 `dependency_types`를 선언하면 생성자로
주입된다([18. DI 컨테이너](18-di-container.ko.md)). Spot packet과 Actor payload handler는
handler class가 아니라 **Spot member 함수**다 — 이 차이는 [6. Spot](06-spot.ko.md)이
다룬다.

## 5. 읽는 순서

이 가이드의 03~17장은 **다섯 언어가 같은 정본에서 생성된다.** 예제는 C++ 코드만 담기며
다른 언어 코드가 섞이지 않는다. 순서는 [C++ 가이드 진입점](README.ko.md)이 제시한다.

먼저 [3. 핵심 개념](03-concepts.ko.md)에서 channel · Spot · Actor · stream ·
relocation 다섯 개념을 잡는다. 나머지 장은 그 조합이다.

**C++은 순서가 조금 다르다.** DI · 설정 · HTTP hosting이 기초에 해당하므로 03장
바로 뒤로 당긴다.

## 6. 도입 순서 고르기

전부 한 번에 쓰지 않는다. 지금 겪는 문제부터 고른다.

| 지금 겪는 문제 | 먼저 볼 장 |
| --- | --- |
| 서비스가 어디 있는지 관리하기 번거롭다 | [5. Channel Messaging](05-channel-messaging.ko.md) |
| 방·세션 상태에 락이 얽힌다 | [6. Spot](06-spot.ko.md) |
| client 실시간 연결을 직접 관리한다 | [9. STREAM](09-stream.ko.md) |
| 배포할 때 세션이 끊긴다 | [10. Location](10-location.ko.md) · [7. Actor와 Spot](07-actor-spot.ko.md) |
| 부하가 몰릴 때 동작을 모르겠다 | [4. Backpressure](04-backpressure.ko.md) |
| 서비스 생성과 의존성 관리를 직접 한다 | [18. DI 컨테이너](18-di-container.ko.md) |

## 7. 관련 문서

- 읽는 순서: [C++ 가이드 진입점](README.ko.md)
- 언어 중립 정의: [공통 스펙 목차](../../../common/README.ko.md)
- C++ 공개 계약: [exact interface 목차](../../../common/spec/server/languages/cpp/interfaces/README.ko.md)
- 다음 장: [2. 시작하기](02-getting-started.ko.md)
