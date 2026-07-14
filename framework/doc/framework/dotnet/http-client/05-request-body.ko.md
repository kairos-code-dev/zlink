[← 목차](README.ko.md)

# 5. Request Body

body 소스는 **상호 배타**다. `Body`, `BodyStream`, `Form`, `Multipart` 중 하나만 쓸 수
있고 둘 이상 지정하면 `RequestProtocolError`로 실패한다.

## typed JSON DTO

```csharp
await client.Post("/games")
    .Body(new CreateGameReq("ranked-match-0611"))
    .SubmitAsync<CreateGameRes>();
```

`Body<T>(dto)`는 DTO를 Web 기본값(`JsonSerializerDefaults.Web`)으로 직렬화하고
`content-type: application/json`을 설정한다.

## raw body

```csharp
await client.Post("/raw").Body("plain text payload", "text/plain").SubmitRawAsync();
```

## form-urlencoded

```csharp
await client.Post("/login")
    .Form("user", "aria")
    .Form("password", "secret value")
    .SubmitRawAsync();
// content-type: application/x-www-form-urlencoded, percent-encoding 적용
```

## multipart/form-data

```csharp
await client.Post("/upload")
    .Multipart("title", "patch notes")
    .MultipartFile("file", "notes.txt", fileContent, "text/plain")
    .SubmitRawAsync();
```

## streaming 업로드

`BodyStream(provider, contentType)`는 body를 chunk 단위로 chunked transfer-encoding으로
전송한다. provider는 끝나면 `null`을 돌려준다. streaming 요청은 rewind할 수 없으므로
**자동 retry에서 제외**된다.

```csharp
var chunks = new Queue<byte[]>(/* ... */);
await client.Post("/upload-stream")
    .BodyStream(
        () => chunks.Count > 0 ? chunks.Dequeue() : null,
        "application/octet-stream")
    .SubmitRawAsync();
```

provider는 `Func<byte[]?>`다 — 끝나면 `null`을 돌려준다(참조형이라 `? chunk : null`이
명확하게 동작한다).

[다음: Response 다루기 →](06-handling-responses.ko.md)
