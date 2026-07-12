[← 목차](README.ko.md)

# 7. 비동기

`SubmitRawAsync()` / `SubmitAsync<T>()` / `DownloadAsync(sink)`는 모두 `ValueTask<T>`를
돌려준다. .NET에서는 `async`/`await`가 코루틴 역할을 한다.

## non-blocking 보장

`SocketsHttpHandler`는 epoll/IOCP 기반 비동기 소켓을 쓴다. 따라서 응답을 기다리는
동안 **어떤 스레드도 park되지 않는다.** 런타임의 비동기 I/O가 이를 제공하므로 별도의
worker scheduler가 필요 없다.

```csharp
public async ValueTask NotifyMatchResultAsync(ZLinkHttpClient client, MatchResult result)
{
    var response = await client.Post($"/matches/{result.MatchId}/result")
        .Body(result)
        .SubmitAsync<AckRes>();

    if (!response.Body.Accepted)
    {
        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.RequestFailed, "match result was not accepted");
    }
}
```

> DNS 해석(`getaddrinfo`)만 OS 레벨에서 blocking이지만 .NET은 이를 threadpool로
> offload하므로 호출/handler 스레드는 막히지 않는다.

## continuation 재개 위치

`Zlink.HttpClient`는 continuation 재개 위치 주입을 제공하지 않는다. 평범한 `Task<T>`를 돌려주는
라이브러리는 호출자의 `await` continuation 재개 위치를 강제할 수 없다(재개는 호출자의
`SynchronizationContext`/awaiter가 결정). 따라서 `Zlink.HttpClient`는 표준 `Task`/
`await` 동작만 제공하며 재개 위치가 필요하면 호출자가 `ConfigureAwait`나 자신의
스케줄러로 제어한다.

## blocking: Fetch&lt;T&gt;()

`Fetch<T>()`는 결과가 올 때까지 호출 스레드를 멈추고 typed body를 돌려주며 실패를
예외로 던진다. 테스트·CLI 전용이다.

```csharp
var board = client.Get("/leaderboard").Fetch<Leaderboard>();   // blocking + 언래핑
```

## 어디서 무엇을 쓰나 — blocking 규칙

> **framework runtime/handler 스레드에서는 blocking 접근(`Fetch<T>()`,
> `.GetAwaiter().GetResult()`)을 쓰지 않는다.** runtime 스레드를 멈추면 같은 스레드에서
> 처리될 다른 작업까지 막힌다.

| 호출 위치 | 권장 |
|-----------|------|
| framework handler / actor / spot 코드 | `await SubmitAsync<T>()` |
| 테스트 코드 | `Fetch<T>()` |
| client 시나리오·CLI·배치 | `Fetch<T>()` |

## streaming callback 위치

`DownloadAsync(sink)`의 sink는 응답 chunk를 읽는 비동기 컨텍스트에서 호출된다. sink
안에서 무거운 동기 작업으로 스레드를 막지 말고 필요하면 thread-safe queue로 넘긴다.

[다음: Streaming →](08-streaming.ko.md)
