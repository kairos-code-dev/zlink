[← 목차](README.ko.md)

# 1. 개요

## 무엇인가

`Zlink.HttpClient`는 .NET 애플리케이션이 HTTP API를 호출할 때 쓰는 client-side
산출물이다. .NET 표준 라이브러리에는 `System.Net.Http.HttpClient`가 있지만 핸들러
구성·redirect·cookie·압축·재시도 같은 설정이 호출부에 흩어진다. 이 client는 그
복잡성을 fluent builder 뒤로 숨기고 zlink framework의 에러·코덱 모델과 맞춘다.

```csharp
// HttpClient 직접 사용: SocketsHttpHandler, CookieContainer, AutomaticDecompression ...
// Zlink.HttpClient: 아래 한 문장
var profile = await client.Get("/players/7281").Async<PlayerProfile>();
```

JSON 전용 client가 아니다. 일반 HTTP client이며 typed 경로
(`Body(dto)` / `Async<T>()`)는 그 위에 얹은 편의 계층이다.

## 설계 원칙

- **fluent builder.** client 구성과 request 구성 모두 메서드 체인으로 쓴다.
- **공개 표면에 핸들러 없음.** `SocketsHttpHandler`, `HttpClientHandler`,
  `HttpRequestMessage` 같은 타입은 공개 API에 드러나지 않는다. `System.Net.Http`
  의존은 runtime 구현(internal) 안에 갇힌다.
- **네이티브 래핑.** 전송은 `System.Net.Http`에 위임하되, zlink 계약과 의미론이
  다른 부분(cookie jar, redirect 루프, retry, 압축 통제)은 얇은 래퍼에서 직접
  구현한다.

## 산출물 경계

| 역할 | 위치 | 공개 여부 |
|------|------|-----------|
| 공개 contract | `src/Zlink.HttpClient/*.cs`, `Contracts/*` | public |
| runtime 구현 | `src/Zlink.HttpClient/Runtime/*` | internal |
| 회귀 테스트 | `tests/Zlink.HttpClient.UnitTests/*` | private |
| 프로젝트 | `Zlink.HttpClient` | public package |

## 실행 모델

요청 실행은 .NET의 비동기 기본형 위에서 동작한다.

- `AsyncRaw()` / `Async<T>()` / `DownloadAsync(sink)`는 `ValueTask<T>`를
  돌려준다. `await`하는 동안 HTTP I/O는 `SocketsHttpHandler`의 비동기 소켓
  (epoll/IOCP)에서 처리되고 **호출 스레드(handler 스레드)는 점유되지 않는다.**
- 완료 값을 동기로 꺼내는 blocking terminator는 제공하지 않는다.
- standalone client는 `Async`와 callback을 제공한다. DI로 주입받는 server client는
  여기에 `Submit`과 `Yield`를 추가한다.

이 모델의 실용적 결론 하나만 기억하면 된다:

> 일반 요청은 `Async<T>()`로 기다린다. Spot의 다른 처리를 진행해야 하는 외부 I/O만
> DI server client의 `Yield<T>()`로 기다린다.

자세한 규칙은 [7. 비동기](07-async.ko.md)에서 다룬다.

## 기능 한눈에 보기

- 메서드: `GET` `POST` `PUT` `DELETE` `PATCH` `HEAD` `OPTIONS`
- body: typed JSON DTO · raw(임의 content-type) · form-urlencoded ·
  multipart/form-data · chunked streaming 업로드
- 응답: raw · typed JSON · streaming 다운로드
- connection pool(네이티브), redirect 추적, transport retry, cookie jar
- 인증: Basic · Bearer · proxy Basic · mTLS client certificate
- HTTPS/TLS 검증, test certificate trust
- HTTP proxy
- gzip/deflate 응답 투명 해제

범위 밖: caller cancellation 공통 모델(표준 `CancellationToken`은 지원).

[다음: 시작하기 →](02-getting-started.ko.md)
