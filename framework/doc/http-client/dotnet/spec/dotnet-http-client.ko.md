# Spec -- ZLink HTTP Client For .NET

> 사용법 중심 문서는 [사용자 가이드](../README.ko.md)를 본다.
> 이 문서는 `Zlink.HttpClient` 산출물의 공개 계약을 정리한다.
> 실제 계약의 단일 기준은 `src/Zlink.HttpClient/**` 공개 타입과
> `Zlink.HttpClient.UnitTests` 회귀 테스트다.

## 1. 목적

`Zlink.HttpClient`는 .NET에서 HTTP request를 보내기 위한 별도 client-side 산출물이다.
JSON 전용 client가 아니라 일반 HTTP client이며, zlink fluent builder 스타일로
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
- `ZLinkHttpClientBuilder` — `BaseUrl`, `Json`, `Timeout`, `DefaultHeader`,
  `BasicAuth`, `BearerToken`, `MaxResponseBodySize`, `TrustCertificateFile`,
  `ClientCertificateFile`, `FollowRedirects`, `Retry`, `Cookies`, `Proxy`,
  `ProxyBasicAuth`, `Compression`, `Build`, 그리고 단발 verb shortcut.
- `ZLinkHttpRequestBuilder` — `Header`, `Query`, `Timeout`, `Body<T>`, `Body(content,
  contentType)`, `BodyStream`, `Form`, `Multipart`, `MultipartFile`, `SubmitRawAsync`,
  `DownloadAsync`, `SubmitAsync<T>`, `Fetch<T>`.
- `RawHttpResponse` { `Status`, `Headers`, `Body` }.
- `HttpResponse<T>` { `Status`, `Headers`, `Body`, `RawBody` }.
- `ZLinkHttpMethod` enum.

## 4. 실행 모델

- 모든 제출은 `ValueTask<T>`를 돌려준다. `SocketsHttpHandler`의 비동기 I/O로
  네트워크 대기 중 호출 스레드는 점유되지 않는다.
- C++의 execute/resume scheduler 주입은 .NET에서 제공하지 않는다. 평범한 `Task`를
  돌려주는 라이브러리는 `await` continuation 재개 위치를 강제할 수 없기 때문이다.
  표준 `Task`/`await` 동작만 제공한다.
- `Fetch<T>()`는 blocking 접근으로 테스트·CLI 전용이다.

## 5. 전송 의미론(C++ 계약과 동일)

- **redirect**: `301/302/303/307/308` + `Location`. `303`/(`301`·`302`+`POST`)→`GET`,
  본문 제거. **same-origin `Authorization` 보존, cross-origin 제거.** `max` 초과 시
  `RequestFailed`. 네이티브 auto-redirect는 끄고 래퍼 루프로 구현.
- **retry**: retriable transport 실패만, 고정 50ms, streaming 제외.
- **cookie jar**: host 정확 매칭, 기본 `Path=/`, `Path`/`Secure`/`Max-Age`만, host당
  128개. 네이티브 cookie container는 끄고 래퍼 jar로 구현.
- **compression**: gzip+deflate 해제, `Content-Encoding` 제거, decoded 크기 한도,
  streaming 비해제. 네이티브 auto-decompression은 끄고 래퍼로 구현.
- **body 소스 상호 배타**: `Body`/`BodyStream`/`Form`/`Multipart` 중 하나.

## 6. 에러 매핑

| 상황 | kind |
|------|------|
| 구성/요청 검증 | `RequestProtocolError` |
| status ≥ 400 (typed) / redirect 한도 / 크기 초과 / transport | `RequestFailed` |
| JSON·압축 디코드 | `PayloadDecodeFailed` |
| timeout | `TimeoutException`(언어 차이) |

## 7. 회귀 테스트 축

`Zlink.HttpClient.UnitTests`의 `HttpClientContractTests`가 C++ `test_cpp_http_client`
시나리오를 미러링한다. chunked 업로드는 managed Linux `HttpListener`가 chunked 요청
본문을 못 받으므로 raw-socket 서버(`RawCaptureServer`)로 검증한다.
