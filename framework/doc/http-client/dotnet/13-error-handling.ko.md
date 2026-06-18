[← 목차](README.ko.md)

# 13. 에러 처리

실패는 `ZLinkFrameworkException`(`Zlink.Framework.Contracts.Errors`)으로 보고된다.
`Kind`(`ZLinkFrameworkErrorKind`)와 `IsRetriable`을 노출한다.

## error kind 매핑

| 상황 | kind |
|------|------|
| 구성/요청 검증 실패(base_url, path, single body source, proxy scheme, 0 timeout 등) | `RequestProtocolError` |
| status ≥ 400 (`SubmitAsync<T>`/`Fetch<T>`) | `RequestFailed` |
| redirect 한도 초과 | `RequestFailed` |
| 응답 JSON 디코드 실패 | `PayloadDecodeFailed` |
| 압축 본문 손상 | `PayloadDecodeFailed` |
| 압축 decoded 크기 초과 / 본문 크기 초과 | `RequestFailed` |
| transport 실패(연결 오류 등) | `RequestFailed` (`IsRetriable = true`) |

## timeout

.NET framework의
`ZLinkFrameworkErrorKind`에는 timeout 전용 kind가 없다. 따라서 timeout은 .NET 관용에
맞춰 **`TimeoutException`**으로 보고된다. retriable 성격이며 `Retry`가 설정돼 있으면
재시도된다(framework 코덱 자체도 timeout을 `TimeoutException`
으로 변환한다).

## retriable

`IsRetriable`이 `true`인 실패(transport 오류, timeout)는 `Retry(attempts)`가 설정돼
있을 때 재시도된다. status 코드 실패(4xx/5xx)는 retriable이 아니다. streaming 요청은
retry에서 제외된다([10장](10-redirects-retries-cookies.ko.md)).

## 예외 경로 정리

```csharp
try
{
    var res = await client.Post("/games").Body(req).SubmitAsync<CreateGameRes>();
}
catch (ZLinkFrameworkException ex) when (ex.Kind == ZLinkFrameworkErrorKind.RequestFailed)
{
    // 4xx/5xx 또는 transport 실패
}
catch (ZLinkFrameworkException ex) when (ex.Kind == ZLinkFrameworkErrorKind.PayloadDecodeFailed)
{
    // 응답 본문 디코드 실패
}
catch (TimeoutException)
{
    // 요청 timeout
}
```

[← 목차](README.ko.md)
