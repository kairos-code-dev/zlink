[← 목차](README.ko.md)

# 6. Response 다루기

## raw 응답

`AsyncRaw()`는 `RawHttpResponse`를 돌려준다.

```csharp
RawHttpResponse response = await client.Get("/players/7281").AsyncRaw();
int status = response.Status;
string body = response.Body;
string contentType = response.Headers["content-type"];
```

`Headers`는 대소문자 무시 조회를 지원한다.

## typed JSON 응답

`Async<T>()`는 응답을 JSON으로 디코드해 `HttpResponse<T>`를 돌려준다.

```csharp
HttpResponse<PlayerProfile> response = await client.Get("/players/7281").Async<PlayerProfile>();
PlayerProfile profile = response.Body;     // 디코드된 DTO
string raw = response.RawBody;             // 원본 응답 텍스트
```

- status가 **400 이상**이면 `ZLinkFrameworkException(RequestFailed)`를 던진다.
- 본문 JSON 디코드 실패는 `ZLinkFrameworkException(PayloadDecodeFailed)`로 보고된다.

## status 처리 정리

| 경로 | 4xx/5xx |
|------|---------|
| `AsyncRaw()` | status를 그대로 돌려준다(예외 없음) |
| `Async<T>()` / typed callback | `RequestFailed` 오류 |

[다음: 비동기 →](07-async.ko.md)
