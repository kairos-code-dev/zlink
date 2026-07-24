# .NET configuration 예제

[.NET exact interface 목차](README.ko.md)

## 1. 예제

```csharp
services.AddZLinkFramework(options =>
{
    options.ConfigureNetwork().BindHost = "0.0.0.0"; // 모든 listener의 local bind host 기본값이다.
    options.ConfigureNetwork().AdvertiseHost = podIp; // descriptor에는 remote가 연결할 host를 기록한다.

    var mesh = options.AddRouteMesh("world")
        .Listen() // automatic discovery가 실제 port를 할당하고 advertised endpoint를 게시한다.
        .SetRoutingIdPrefix("game") // lifecycle마다 game-<lowercase canonical UUID v4> RID를 만든다.
        .SetPlacementWeight(100);

    mesh.Objects()
        .Server() // Client capability를 포함하고 등록한 object type을 host한다.
        .AddSpotFactory<StageSpot>(
            "stage",
            placement: null,
            relocation: ZLinkRelocationPolicy<StageSpot>.Disabled);

    mesh.Channel("game")
        .Server() // handler와 weight를 제공하는 target membership이다.
        .AddSendHandler<GameCommandHandler, GameCommand>(); // logical channel handler를 등록한다.
    mesh.Channel("actors").Client(); // outbound route만 등록하고 membership으로 게시하지 않는다.

    mesh.ConfigureRouterSocket().SendHighWaterMark = 1024; // MeshNode ROUTER의 물리 HWM을 설정한다.

    options.AddFanoutChannel("events")
        .EnablePublisher(7400); // classic PUB/SUB는 독립 fanout 기능이다.
});
```
