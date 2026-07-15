[← 목차](README.ko.md)

# 3. Client 구성

builder는 client 전역 설정을 모은다. 옵션은 네이티브 `SocketsHttpHandler` 설정으로
매핑되거나, 의미론이 다른 경우 래퍼에서 직접 처리된다.

## builder 옵션

기본값은 [공통 spec 2장](../../spec/http-client/02-client-builder.ko.md)이 정본이다.

| 옵션 | 효과 | 기본값 | 구현 |
| --- | --- | --- | --- |
| `BaseUrl(url)` | 모든 요청의 기준 URL | 없음(필수) | 래퍼 |
| `Timeout(span)` | 시도당 timeout(요청별 override 가능) | **3000ms** | `CancellationToken` |
| `DefaultHeader(n, v)` | 모든 요청에 붙는 기본 헤더 | 없음 | 래퍼 |
| `BasicAuth(u, p)` / `BearerToken(t)` | `Authorization` 헤더 | off | 래퍼 |
| `MaxResponseBodySize(bytes)` | 응답 본문 상한(decoded 기준) | **16 MiB** | 래퍼 |
| `TrustCertificateFile(path)` | 신뢰 인증서 추가 | 시스템 root | `SslClientAuthenticationOptions` |
| `ClientCertificateFile(cert, key)` | mTLS client 인증서 | off | `SslClientAuthenticationOptions` |
| `FollowRedirects(max)` | redirect 추적(무인자 시 **5회**) | off | **래퍼 redirect 루프** |
| `Retry(attempts)` | transport 실패 재시도(총 1+n회 시도) | off | **래퍼 retry 루프** |
| `Cookies()` | cookie jar 활성화 | off | **래퍼 cookie jar** |
| `Proxy(url)` / `ProxyBasicAuth(u, p)` | HTTP proxy | off | `WebProxy` |
| `Compression()` | gzip/deflate 투명 해제 | off | **래퍼 해제** |
| `Codecs(configure)` | framework codec extension 등록(.NET 고유) | JSON | 래퍼 |

## framework 서버에 등록

Spot handler에서 사용하는 client는 이름을 붙여 DI에 등록한다. 서버 등록은 connection pool을
재사용하고 `Yield`와 callback 완료를 현재 Spot 실행 줄에 연결한다.

```csharp
services.AddZLinkHttpClient("player-api", http => http
    .BaseUrl("https://player-api.internal") // 이 이름으로 주입되는 client의 기준 URL을 고정한다.
    .Timeout(TimeSpan.FromSeconds(3)));      // 외부 API의 시도당 timeout을 한곳에서 관리한다.
```

handler는 같은 이름의 `ZLinkHttpServerClient`를 주입받는다. 정적 팩토리로 만든
`ZLinkHttpClient`는 client-side 코드용이므로 `Submit`과 `Yield`를 제공하지 않는다.

## 네이티브 위임 vs 래퍼 구현

`SocketsHttpHandler`에서 **auto-redirect, auto-decompression, cookie container는 끈다.**
이는 네이티브 기본 동작이 zlink 계약과 의미론이 다르기 때문이다(예: .NET auto-redirect는 same-origin에서도
`Authorization`을 보존하지 않고 네이티브 cookie container는 RFC 6265 전체를 구현해
default path/domain/만료가 다르다). 대신 래퍼가 redirect 루프·cookie jar·압축 해제를
직접 수행해 의미론을 zlink 계약대로 맞춘다. connection pool·proxy·TLS는 네이티브에
위임한다.

## client 재사용과 connection pool

client는 내부 `HttpClient` 하나를 감싸며 connection pool을 공유한다. 요청마다 client를
새로 만들지 말고 **한 번 만들어 재사용**한다. 단발 한 줄 요청(§2)은 편의를 위해 내부
client를 만들지만 반복 호출에는 재사용이 낫다.

## 요청별 timeout override

```csharp
await client.Get("/slow-report").Timeout(TimeSpan.FromSeconds(30)).AsyncRaw();
```

[다음: Request 만들기 →](04-making-requests.ko.md)
