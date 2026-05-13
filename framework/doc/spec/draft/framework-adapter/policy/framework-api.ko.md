<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework Channel Topology](channel-topology.ko.md) | [다음: ZLink Framework Actor Model](actor-model.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../README.ko.md)

[초안 묶음](./README.ko.md) | [개요](./overview.ko.md) | [use cases](../use-cases/README.ko.md) | [상호작용 모델](./interaction-model.ko.md) | [메시지 모델](./message-model.ko.md) | [channel topology](./channel-topology.ko.md) | [검증](../usecase-validation.ko.md) | [.NET](../bindings/dotnet/README.ko.md) | [Java](../bindings/java/README.ko.md) | [Node.js](../bindings/node/README.ko.md) | [Python](../bindings/python/README.ko.md) | [C++](../bindings/cpp/README.ko.md)

# Draft -- ZLink Framework API

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, 아래 API 이름은 방향 설명을 위한 예시다.

## 1. 목적

같은 `ZLink Framework`라도 `ASP.NET Core`, `Spring Boot`, `NestJS`,
`FastAPI`, `C++ standalone host/runtime` 사용자가 기대하는 표면은 조금씩
다르다. 이 문서는 각 환경에서 "어떤 식으로 보이면 자연스러운가"를 정리한다.

핵심 원칙은 단순하다.

- 프레임워크 사용자가 익숙한 등록 방식에 맞춘다.
- low-level socket 이름을 공용 API 앞면으로 내세우지 않는다.
- request handler, event handler, outbound client를 DI와 함께 설명한다.
- runtime monitoring도 DI와 함께 설명할 수 있어야 한다.
- 서버 간 `send/request`는 HTTP handler mapping과 닮은 경험으로 보이게 한다.
- raw transport header는 handler 인자로 직접 노출하지 않는다.

## 2. 공통 방향

### 2.1 서버 쪽

- handler를 프레임워크 표준 등록 방식으로 붙인다.
- 요청 body는 typed object로 받는다.
- header metadata와 timeout 정보는 context에서 조회한다.
- `send`는 응답 없는 handler, `request`는 응답 있는 handler로 설명할 수 있어야
  한다.
- `stream`은 일반 request handler와 다른 전용 handler 그룹으로 분리할 수
  있어야 한다.
- `stream`은 framework Header 기반 packet session만 우선 지원하고, recv loop는
  기본 application 표면에 올리지 않는다.
- `stream` callback은 write와 peer 식별을 함께 가진 stream 객체를 받고,
  session error는 error kind enum과 native detail을 함께 가진 구조화된 값으로
  받는 편이 자연스럽다.
- stream 직렬성 / callback 실행 규칙의 권위는
  [interaction-model.ko.md §3.4](./interaction-model.ko.md)에 둔다. 이 문서는
  필요한 곳에서 같은 규칙을 따른다고만 적고, 정의는 한 곳에서만 한다.

### 2.2 클라이언트 쪽

- 공용 outbound client를 DI로 주입한다.
- 요청 메서드는 async 중심으로 제공한다.
- codec, timeout, target channel을 설정할 수 있다.
- gateway 주소나 load balancer 주소 대신 `channel name` 기준 호출을 기본으로
  삼는다.
- send는 기본적으로 async submit으로 둔다. backpressure 처리는 호출자가
  `DontWait` 같은 옵션으로 고르지 않고 framework 내부의 nonblocking send와 ready
  notification이 맡는다.
- framework runtime은 등록한 outbound channel마다 별도 outbound runtime을 관리할
  수 있어야 한다.
- 단순 unary request 외에 event publish와 필요하면 aggregate helper를 분리할 수
  있어야 한다.
- 운영 점검이나 관리 API에서는 Registry topology snapshot/query 결과를 읽는
  별도 surface를 둘 수 있어야 한다.
- socket/discovery/registry/spot runtime 변화를 typed event handler로 받을 수
  있는 별도 monitoring surface도 둘 수 있어야 한다.
- 이 outbound client는 framework 전용 메시지 handler 안뿐 아니라, 기존 HTTP
  handler나 controller 안에서도 그대로 쓸 수 있어야 한다.
- caller가 transport 위치값을 직접 넘기는 direct routed 호출은 기본 application
  표면으로 두지 않는다. actor나 spot으로 보내는 public send/request는 resolver가
  target `RoutingId`를 숨기는 형태를 우선한다.
- session server와 play server를 분리하는 구조에서는 `actorId`를 client-facing
  공개 키로 사용한다. session -> actor 방향은 actor create/dispatch helper로,
  actor -> client 방향은 `IZLinkSessionProxy`로 나눈다. actor 개념의 라이프사이클
  과 표면은 [actor-model.ko.md](./actor-model.ko.md)에서, gateway use case의 사용성
  결정은 [session-gateway-usability.ko.md](./session-gateway-usability.ko.md)에서
  본다.

### 2.3 transport 통합 축

framework가 직접 통합할 transport 축은 [overview.ko.md](./overview.ko.md)의
section 2에 정의되어 있다. 이 문서는 channel messaging, `PUB/SUB`, `STREAM`
세 축을 중심으로 보되, 공통 API 원칙과 lifecycle 경계에 직접 영향을 주는
`SPOT` 표면도 함께 다룬다. `SPOT`의 자세한 계약과 샘플은
[../bindings/dotnet/aspnet-core-spot.ko.md](../bindings/dotnet/aspnet-core-spot.ko.md) 등 별도
문서에서 따로 다룬다.

핵심은 transport 축은 명확히 두되, 프레임워크 사용자가 보는 이름은 socket
이름보다 역할 이름이 되게 만드는 것이다.

### 2.4 runtime monitoring

운영 이벤트는 일반 request/send/event handler와 다른 성격을 가진다. 따라서
framework는 monitoring 표면을 별도 축으로 설명하는 편이 맞다.

- event kind는 enum으로 둔다.
- 실제 callback payload는 source 이름과 상세 정보를 함께 가진 구조화된 값으로
  둔다.
- socket/discovery는 하부 monitor를 감싸는 편이 자연스럽다.
- registry/spot는 raw monitor를 가장한 표면보다 snapshot diff 기반 event로
  설명하는 편이 맞다.
- application은 typed runtime event handler를 구현해서 이 이벤트를 받는 모델을
  기본으로 본다.

즉 framework는 모든 source를 같은 raw monitor API로 보이게 하기보다,
source별 구현 차이를 숨긴 typed runtime event surface를 제공하는 편이 더
자연스럽다.

## 3. ASP.NET Core 방향

### 3.1 기대하는 표면

- `AddZLinkFramework(...)`
- `AddZLinkHandlersFromAssemblyContaining<...>()` 또는 그와 비슷한 등록
- outbound client DI
- runtime monitoring 등록
- `SPOT` node / publisher / subscriber의 hosted lifecycle 통합
- stream hosted lifecycle 또는 stream session 등록

### 3.2 예시

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddClientServerChannel("profile", channel =>
    {
        channel.EnableServer();
    });

    options.AddFanoutChannel("profile.events", channel =>
    {
        channel.EnableSubscriber();
    });

    options.AddClientServerChannel("account", channel =>
    {
        channel.EnableClient(client =>
        {
            client.UseManualConnections(peers =>
            {
                peers.Connect("tcp://10.0.20.15:7101");
            });
        });
    });
});

builder.Services.AddZLinkHandlersFromAssemblyContaining<Program>();

public sealed class ProfileHandlers
{
    [ZLinkRequest]
    public ValueTask<ProfileReply> GetAsync(
        GetProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new ProfileReply());
    }
}
```

이 예시에서 중요한 점은 handler가 raw header part를 직접 받지 않는다는 점이다.
필요한 metadata는 `ZLinkRequestContext` 같은 context에서 조회한다.

또한 framework는 startup 시점에 channel별 역할을 등록하고, 필요한 capability만
여는 쪽을 기본 방향으로 본다.

수동 연결을 둘 때는 `channel` 전체가 아니라 `channel + capability` 기준으로
설정해야 한다. 예를 들어 `account.client` 수동 연결과 `account.subscriber`
수동 연결은 별도 집합으로 보는 편이 맞다. 그리고 수동 연결 capability는 startup
설정만이 아니라, 런타임 `Connect`, `Disconnect`, `ListConnections` 같은 제어도
지원해야 한다.

여기서 channel client manual 연결은 remote `RoutingId`를 따로 받지 않는 편이
자연스럽다. 하부 `DEALER(client)`가 connect된 peer 집합으로 요청을 보내는
모델이므로, startup과 런타임 제어 모두 endpoint 집합만 관리하면 된다.

또한 send는 기본 async submit으로 둔다. 구현은 blocking send를 task로 감싸지
않고, 먼저 nonblocking send를 시도한 뒤 temporary backpressure가 발생하면 pending
send queue와 ready notification으로 이어서 처리한다. send 대기 한계는 call
builder가 아니라 channel 또는 socket의 `SendTimeout` 옵션을 따른다.
framework는 core socket 기본값을 직접 사용하지 않고, channel/socket option에
resolved된 `SendTimeout` 값을 async pending deadline으로 사용한다. `200ms` 기본값은
**.NET 바인딩에 한정한 framework 기본값**이며, cross-binding 정책은 "각 binding이 자기
idiom에 맞는 기본값을 정한다"로 둔다. 사용자가 `.NET` option에서 `SendTimeout = null`을
명시한 경우에만 core `-1`과 같은 무한 대기로 본다.
publish도 send와 같은 submit 규칙을 따른다. subscriber 처리 완료를 기다리지 않고,
local publish transport에 메시지를 맡길 수 있을 때까지 비동기로 기다린다.

request도 reply를 기다리는 async 호출로 설명한다. 다만 request packet을 보내는
단계는 send와 같은 async submit 경로를 사용해야 한다. `WithTimeout(...)`은 reply
대기 시간만 정하고, 전송 backpressure는 `SendTimeout` 정책이 처리한다.

고성능 구현에서는 immediate send/publish 성공 path가 allocation 없이 완료되어야
한다. backpressure path는 bounded pending queue를 사용하고, ready notification마다
정해진 batch budget 안에서 queue를 drain한다. 이렇게 해야 thread blocking 없이도
높은 처리량을 유지할 수 있다.

보다 자세한 `.NET` 초안은 [../bindings/dotnet/README.ko.md](../bindings/dotnet/README.ko.md)를 참고한다.

### 3.3 ASP.NET Core의 SPOT 방향

`SPOT`은 일반 channel messaging보다 instance lifecycle과 실행 문맥이 더 먼저
보이는 표면이다. 공통 정책 차원에서는 아래 정도만 고정한다.

- active SPOT channel view는 `AddSpotMesh(channelName, mesh => mesh.UseDiscovery(...))`가
  정한다. `UseSpotDiscovery(...)`와 `AddSpotNode(...)`를 분리하는 방식은 호환 경로로만
  남기고, 새 샘플은 mesh 등록을 기준으로 작성한다.
- `SpotNode`는 router, pub/sub, attach된 외부 호출 capability를 가진다.
- local spot 인스턴스는 등록 이름으로 만들고, lifecycle 안에서 packet, subscribe,
  timer를 등록한다.
- local spot이 없는 외부 노드용 publish 표면은 별도 client로 분리할 수 있다.
- actor/session 모델을 지원하는 binding에서는 actor가 `Spot`에 attach된 뒤의
  actor dispatch를 반드시 해당 `Spot` 실행 문맥에서 처리한다. stream session은
  ingress 역할을 하고, room/stage 같은 domain 상태를 만지는 코드는 `Spot` 실행
  문맥으로 들어가야 한다.
- actor join으로 현재 `Spot`이 바뀌면, join 완료 뒤의 actor dispatch는 새 `Spot`
  실행 문맥에서 처리되어야 한다. framework는 join 상태 갱신과 packet dispatch
  선택 사이의 경합을 막아야 한다.
- actor 코드는 `IZLinkClient`나 `IZLinkSpotClient`를 직접 고르지 않고,
  actor context를 통해 channel request/send와 client stream reply/send를 수행한다.
  context는 join 전에는 일반 channel client 경로를, join 후에는 현재 `Spot`에
  attach된 channel client 경로를 선택한다.
- actor context는 stream 객체를 직접 노출하지 않고, client로 보내는 `Send(...)`와
  request에 응답하는 `Reply(...)` 같은 의도 중심 API를 제공한다.

자세한 contract와 샘플은
[../bindings/dotnet/aspnet-core-spot.ko.md](../bindings/dotnet/aspnet-core-spot.ko.md)
같은 binding 문서를 기준으로 본다.

#### 3.3.1 Actor lifecycle — zlink 라이브러리 위임

zlink 라이브러리에 native Actor API가 추가됨에 따라, framework는 actor lifecycle를
자체 구현 대신 라이브러리의 native API로 위임한다. 이 정책의 핵심은 아래와 같다.

##### Actor 생성 및 입장 흐름

1. `SpotNode.EntrySpot()` — framework가 입장 수신용 `Spot`을 얻는다.
2. `Spot.RecvActorJoin(RecvFlags)` — actor join request를 수신한다.
3. framework가 join 요청 메시지를 ZMP 포맷으로 해석해 등록된 actor join handler를 호출한다.
4. `Spot.ReplyActorJoin(request, accepted, replyMessage)` — join 결과를 응답한다.

##### Actor 생성 (SpotNode 측)

- `SpotNode.CreateActor(string actorId)` — actor node에서 actor를 생성한다.
- `Actor.Join(Spot spot, Message request, TimeSpan timeout, CancellationToken)` — actor가 특정 spot에 join을 요청한다.
- `Actor.Leave(Spot spot, TimeSpan timeout)` — actor가 spot에서 나간다.

##### Actor 메시지 수신

zlink 라이브러리의 `SpotDispatchEvent` 중 두 가지가 actor lifecycle과 관련된다.

| 이벤트 | 값 | 의미 |
| ------ | -- | ---- |
| `ActorJoinReadable` | 6 | 새 actor join 요청이 도착했음 |
| `ActorReadable` | 5 | join된 actor의 STREAM 메시지가 도착했음 |

framework는 이 두 이벤트를 아래와 같이 처리한다.

- `ActorJoinReadable` → `Spot.RecvActorJoin(DontWait)` 루프로 모든 요청을 drain한 뒤 application join handler를 호출하고 `ReplyActorJoin`으로 결과를 반환한다. join handler에는 join 요청의 `TargetActor`(해당 spot에 이미 등록된 로컬 actor)와 요청 메시지를 전달한다.
- `ActorReadable` → 백엔드가 미리 drain한 `ActorPart` 목록을 받아 STREAM 메시지 단위로 묶어서 actor dispatch를 수행한다. 각 메시지는 header part (More=true) + body part (More=false) 구조다.

`OnDispatchEvent` 핸들러는 spot 초기화 시 항상 등록한다. 패킷 handler나 actor join handler가 없는 spot도 런타임에 actor가 join될 수 있으므로 `ActorReadable` 이벤트를 받을 준비가 되어 있어야 한다.

##### 실행 문맥 보장

두 이벤트 모두 spot serial executor를 통해 직렬화된 실행 문맥 안에서 처리되므로, actor join handler와 actor packet handler 사이에 동시성 경합이 없다.

##### framework가 직접 관리하지 않는 것

framework는 `Actor` 객체 자체의 네트워크 수명이 아니라, application actor 객체의 lifecycle과 dispatch routing만 관리한다. native `Actor`의 send/recv 루프는 라이브러리가 담당하며, framework는 dispatch event를 통해 통보를 받는다.

## 4. Spring Boot 방향

### 4.1 기대하는 표면

Spring에서는 annotation 기반 handler가 자연스럽다.
RSocket의 `@MessageMapping`과 비슷한 경험을 주는 방향이 적합하다.
서버 간 `send/request`도 이 annotation 계열에 자연스럽게 올라가야 한다.

### 4.2 예시

```java
@ZLinkController
public final class ProfileController {

    @ZLinkMapping(packetName = "profile.get")
    public Mono<ProfileReply> get(ProfileRequest request, ZLinkContext ctx) {
        return Mono.just(new ProfileReply());
    }
}
```

여기서는 annotation에 packet 이름을 직접 적는 예시를 들었지만, 실제 구현에서는
request 타입 이름을 기본 packet key로 삼고 annotation 값은 explicit override로
쓰는 쪽이 더 자연스럽다.

## 5. NestJS 방향

### 5.1 기대하는 표면

NestJS는 메시지 기반 프로그래밍 모델이 이미 익숙하므로, 가능하면
`@MessagePattern`, `@EventPattern` 같은 기존 감각과 닮게 가는 편이 좋다.
다만 raw header를 message payload에 섞어 넣는 방식은 기본으로 두지 않는다.

### 5.2 예시

```typescript
@Controller()
export class ProfileController {
  @MessagePattern('profile.get')
  getProfile(data: ProfileRequest, ctx: ZLinkContext): Promise<ProfileReply> {
    return Promise.resolve({} as ProfileReply);
  }

  @EventPattern('cache.invalidate')
  invalidate(data: InvalidateEvent, ctx: ZLinkContext): void {
  }
}
```

NestJS 예시도 같은 맥락이다. decorator 값은 packet key 또는 event name override
예시로 보는 편이 맞다.

## 6. FastAPI 방향

### 6.1 기대하는 표면

FastAPI에서는 dependency 주입과 startup/shutdown hook이 핵심이다.
그래서 zlink runtime도 application 수명과 함께 올라가고 내려가는 형태가
자연스럽다.

- `add_zlink_framework(...)`
- `Depends(...)`로 받는 outbound client
- startup 시 local channel / outbound channel 등록
- route handler 안에서 그대로 쓰는 request/send client

### 6.2 예시

```python
app = FastAPI()
add_zlink_framework(
    app,
    channel_name="profile",
    outbound_channels=["account"],
)


@app.post("/profiles/get")
async def get_profile(
    request: GetProfileHttpRequest,
    client: ZLinkClient = Depends(get_zlink_client),
) -> ProfileReply:
    return await client.request(
        "account",
        GetProfileRequest(account_id=request.account_id),
    )
```

FastAPI 방향에서는 framework 내부 dispatch loop를 route 함수로 끌어올리지 않고,
기존 async application 구조 안에 zlink runtime을 붙이는 모양을 기본으로 본다.

## 7. C++ standalone host 방향

### 7.1 기대하는 표면

`C++`는 다른 언어처럼 기존 웹 프레임워크 위 adapter보다, zlink framework가
host/runtime 역할 일부를 직접 제공하는 쪽이 더 자연스럽다.

- application host builder
- local channel 등록
- outbound channel 등록
- request/send handler registry
- poll loop와 lifecycle 통합
- registry/discovery/manual connection 설정

### 7.2 예시

```cpp
using namespace zlink::framework;

int main() {
    app_t app = app_t::build();
    app.set_channel_name("profile")
       .add_outbound_channel("account")
       .add_request_handler("GetProfileRequest", profile_handler)
       .run();
}
```

`C++` 방향에서는 DI container보다 host builder와 registration API가 더 중요하다.
핵심은 raw socket 배선을 application 코드로 퍼뜨리지 않으면서, lifecycle과
dispatch loop를 framework가 직접 관리하는 것이다.

## 8. 결정된 기준

- 공용 annotation 이름을 모든 프레임워크에서 억지로 통일하지 않는다.
  각 호스트 프레임워크의 익숙한 idiom을 우선한다.
- NestJS는 기존 `@MessagePattern`, `@EventPattern`과 닮은 감각을 우선한다.
- ASP.NET Core는 attribute 기반 handler model을 기본으로 보고, endpoint mapping은
  보조 등록 표면으로 다룬다.
- FastAPI는 runtime bootstrap을 helper registration이 맡고, HTTP 쪽은 기존 route
  decorator를 그대로 사용한다.
- `C++` host는 framework lifecycle에 필요한 scheduler/timer만 기본 제공하고,
  범용 application scheduler까지 표준 표면으로 끌어올리지는 않는다.
- pub/sub은 일반 `PUB/SUB` event 모델을 먼저 설명하고, `SPOT` event는 별도 상위
  모델로 분리한다.
- `STREAM` 정책 설명은 framework Header 기반 packet session과 session lifecycle
  축으로 충분하다고 본다.
- scatter-gather 같은 aggregate helper는 adapter 기본 기능이 아니라 별도 확장
  계층으로 둔다.
- context에는 routing, timeout, trace 같은 공통 metadata만 올리고, workflow 엔진
  수준의 metadata는 기본 표면으로 끌어올리지 않는다.

지금 단계에서는 이름보다 "그 프레임워크 사용자가 낯설지 않게 느끼는가"를 더
중요하게 본다.
