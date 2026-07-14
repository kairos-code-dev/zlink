[← 목차](README.ko.md)

# 2. 시작하기

## 프로젝트 참조

`Zlink.HttpClient`는 `Zlink.Framework`를 참조하는 라이브러리 프로젝트다. 소비 프로젝트
에서는 프로젝트/패키지 참조를 추가한다.

```xml
<ItemGroup>
  <ProjectReference Include="../Zlink.HttpClient/Zlink.HttpClient.csproj" />
</ItemGroup>
```

```csharp
using Zlink.HttpClient;
```

## 첫 요청

```csharp
using var client = ZLinkHttpClient.Create("http://127.0.0.1:18080")
    .Build();

var player = await client.Get("/players/7281").SubmitAsync<PlayerProfile>();
Console.WriteLine(player.Body.Name);
```

- `Create(baseUrl)`로 builder를 시작하고 `.Build()`로 client를 만든다.
- client는 재사용 가능하고 thread-safe하다. 보통 한 번 만들어 오래 쓴다.
- `using`으로 수명을 관리하면 내부 `HttpClient`/핸들러가 정리된다.

## 한 줄 요청

단발 요청은 `Build()`를 생략하고 builder에서 바로 메서드를 호출할 수 있다. 내부적으로
client를 만들어 요청을 수행한다.

```csharp
var res = await ZLinkHttpClient.Create("https://game-api.example.internal")
    .Post("/games")
    .Body(new CreateGameReq("ranked-match-0611"))
    .SubmitAsync<CreateGameRes>();
```

반복 호출한다면 client를 한 번 만들어 재사용하는 편이 connection pool 재사용 측면에서
유리하다.

## blocking 한 줄(테스트/CLI)

```csharp
var board = ZLinkHttpClient.Create("http://127.0.0.1:18080")
    .Get("/leaderboard")
    .Fetch<Leaderboard>();
```

`Fetch<T>()`는 결과를 기다려 typed body를 돌려주고 실패를 예외로 던진다. handler
스레드에서는 쓰지 않는다([7장](07-async.ko.md)).

[다음: Client 구성 →](03-client-configuration.ko.md)
