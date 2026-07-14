# Spec -- ZLink HTTP Client For .NET

> 사용법 중심 문서는 [사용자 가이드](../../../../../http-client/dotnet/README.ko.md)를 본다.
> **언어 중립 공통 계약은 [공통 spec](../../README.ko.md)이 정본**이며,
> 이 문서는 공통 계약에 대한 .NET 고유 편차와 구현 매핑만 기술한다.
> 실제 계약의 단일 기준은 공통 spec + `src/Zlink.HttpClient/**` 공개 타입과
> `Zlink.HttpClient.UnitTests` 회귀 테스트다.

## 1. 목적

`Zlink.HttpClient`는 .NET에서 HTTP request를 보내기 위한 별도 client-side 산출물이다.
JSON 전용 client가 아니라 일반 HTTP client이며 zlink fluent builder 스타일로
`System.Net.Http`의 낮은 수준 설정을 흡수한다. typed JSON 경로
(`Body(dto)`/`SubmitAsync<T>()`/`Fetch<T>()`)는 그 위에 얹은 편의 계층이다.

이 client는 `Zlink.Framework`의 에러 모델(`ZLinkFrameworkException`)에 의존하지만
framework core의 기본 의존성은 아니다(단방향 의존).

## 2. 산출물 경계

| 역할 | 위치 | 공개 여부 |
|------|------|-----------|
| 공개 contract | `src/Zlink.HttpClient/*.cs`, `Contracts/*` | public |
| runtime 구현 | `src/Zlink.HttpClient/Runtime/*` | internal |
| 회귀 테스트 | `tests/Zlink.HttpClient.UnitTests/*` | private |
| 프로젝트 | `Zlink.HttpClient` | public package |

공개 표면에는 `SocketsHttpHandler`, `HttpClientHandler`, `HttpRequestMessage`,
`HttpResponseMessage` 같은 `System.Net.Http` 타입을 노출하지 않는다.

## 3. 공개 타입

- `ZLinkHttpClient` — `Create()` / `Create(baseUrl)`, 메서드 `Get/Post/Put/Delete/
  Patch/Head/Options`, `IDisposable`.
- `ZLinkHttpClientBuilder` — `BaseUrl`, `Codecs`, `Timeout`, `DefaultHeader`,
  `BasicAuth`, `BearerToken`, `MaxResponseBodySize`, `TrustCertificateFile`,
  `ClientCertificateFile`, `FollowRedirects`, `Retry`, `Cookies`, `Proxy`,
  `ProxyBasicAuth`, `Compression`, `Build`, 그리고 단발 verb shortcut.
  (`Codecs`는 framework codec extension 등록 — .NET 고유 확장점,
  [공통 spec 2.3장](../../02-client-builder.ko.md) 언어 편차)
- `ZLinkHttpRequestBuilder` — `Header`, `Query`, `Timeout`, `Body<T>`, `Body(content,
  contentType)`, `BodyStream`, `Form`, `Multipart`, `MultipartFile`, `SubmitRawAsync`,
  `DownloadAsync`, `SubmitAsync<T>`, `Fetch<T>`.
- `RawHttpResponse` { `Status`, `Headers`, `Body` }.
- `HttpResponse<T>` { `Status`, `Headers`, `Body`, `RawBody` }.
- `ZLinkHttpMethod` enum.

## 4. 실행 모델

- 모든 제출은 `ValueTask<T>`를 돌려준다. `SocketsHttpHandler`의 비동기 I/O로
  네트워크 대기 중 호출 스레드는 점유되지 않는다.
- .NET은 continuation 재개 위치 주입을 제공하지 않는다. 평범한 `Task`를
  돌려주는 라이브러리는 `await` continuation 재개 위치를 강제할 수 없기 때문이다.
  표준 `Task`/`await` 동작만 제공한다.
- `Fetch<T>()`는 blocking 접근으로 테스트·CLI 전용이다.

## 5. 전송 의미론

기본값·redirect·retry·cookie·압축·인증 스크럽·body 소스 배타 의미론은
[공통 spec 2~8장](../../README.ko.md)을 따른다. .NET 구현 매핑:

- 네이티브 자동 기능 비활성: `SocketsHttpHandler`에서 `AllowAutoRedirect=false`,
  `AutomaticDecompression=None`, `UseCookies=false` — 의미론은 래퍼가 구현.
- per-attempt timeout은 `HttpClient.Timeout` 대신 linked
  `CancellationTokenSource.CancelAfter`로 강제(호출자 취소와 timeout을 구분).
- TLS: `SslClientAuthenticationOptions`(trust 추가 + mTLS). proxy: `WebProxy`.
- 압축 해제: 래퍼가 `System.IO.Compression`으로 수행.

## 6. 에러 매핑

[공통 spec 9장](../../09-error-model.ko.md)을 따른다. .NET 표기는
`ZLinkFrameworkErrorKind`(PascalCase: `RequestProtocolError`/`RequestFailed`/
`PayloadDecodeFailed`) + `IsRetriable`.

- timeout은 `RequestFailed`(`IsRetriable=true`) + inner `TimeoutException`으로
  보고한다(2026-07-12에 `TimeoutException` 직접 노출에서 회수). 호출자 취소는
  `OperationCanceledException` 그대로 전파된다.

## 7. 회귀 테스트 축

`Zlink.HttpClient.UnitTests`의 `HttpClientContractTests`가 전송 계약 시나리오를 검증한다. chunked 업로드는 managed Linux `HttpListener`가 chunked 요청
본문을 못 받으므로 raw-socket 서버(`RawCaptureServer`)로 검증한다.
