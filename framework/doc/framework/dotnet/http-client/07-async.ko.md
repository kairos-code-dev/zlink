[← 목차](README.ko.md)

# 7. 비동기

`AsyncRaw()` / `Async<T>()` / `DownloadAsync(sink)`는 `ValueTask<T>`를 돌려준다.
완료를 기다리지 않는 server 호출에는 `Submit()`을 사용한다. HTTP request builder에는 Spot turn을
반납하는 `Yield<T>()`가 없다.

## non-blocking 보장

`SocketsHttpHandler`는 epoll/IOCP 기반 비동기 소켓을 쓴다. 따라서 응답을 기다리는
동안 **어떤 스레드도 park되지 않는다.** 런타임의 비동기 I/O가 이를 제공하므로 별도의
worker scheduler가 필요 없다.

```csharp
public async ValueTask NotifyMatchResultAsync(ZLinkHttpClient client, MatchResult result)
{
    var response = await client.Post($"/matches/{result.MatchId}/result")
        .Body(result)
        .Async<AckRes>();

    if (!response.Body.Accepted)
    {
        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.RequestFailed, "match result was not accepted");
    }
}
```

> DNS 해석(`getaddrinfo`)만 OS 레벨에서 blocking이지만 .NET은 이를 threadpool로
> offload하므로 호출/handler 스레드는 막히지 않는다.

## Spot turn 선택

`Async<T>()`는 현재 Spot turn을 유지한다. 응답을 기다리는 동안 같은 Spot의 다음 callback이
시작하지 않으므로, 요청 전후의 상태 불변식을 유지해야 할 때 사용한다.

외부 HTTP 응답을 기다리는 동안 shared Spot gate를 반납하려면 `RunIoWorker(...)` 안에서
`Async<T>()`를 실행하고 worker call의 `Yield()`로 기다린다. Gate를 다시 얻은 continuation에서는 다른
callback이 Spot 상태를 바꿨을 수 있으므로 상태를 다시 확인한다.

```csharp
public async ValueTask<PlayerProfile> LoadProfileAsync(
    IZLinkSpotContext context,
    ZLinkHttpServerClient client,
    string playerId)
{
    var response = await context
        .RunIoWorker(async workerCancellation =>
            await client.Get($"/players/{playerId}")
                .Async<PlayerProfile>(workerCancellation))
        .Yield(); // Gate 반납은 HTTP client가 아니라 worker call이 수행한다.
    return response.Body;
}
```

완료 값을 동기로 꺼내는 public terminator는 없다. `.GetAwaiter().GetResult()` 같은 blocking
언래핑도 framework handler에서 사용하지 않는다.

| 호출 위치 | 권장 |
|-----------|------|
| framework handler의 상태 보존 요청 | `await Async<T>()` |
| framework handler의 독립된 외부 I/O와 gate 반납 | `RunIoWorker(...).Yield()` 안에서 `await Async<T>()` |
| 테스트·client 시나리오·CLI·배치 | `await Async<T>()` |

## callback 완료

callback overload는 awaitable을 돌려주지 않는다. Spot handler에서 호출하면 현재 실행 줄을
점유하지 않고 반환하며, 완료 callback은 같은 실행 줄의 새 turn으로 처리된다.

```csharp
client.Get("/health").Async<HealthRes>((error, response) =>
{
    if (error is not null) return; // 전송·status·decode 실패를 확인한다.
    RecordHealth(response!.Body);  // 이 callback은 별도 Spot turn에서 실행된다.
});
```

## streaming callback 위치

`DownloadAsync(sink)`의 sink는 응답 chunk를 읽는 비동기 컨텍스트에서 호출된다. sink
안에서 무거운 동기 작업으로 스레드를 막지 말고 필요하면 thread-safe queue로 넘긴다.

[다음: Streaming →](08-streaming.ko.md)
