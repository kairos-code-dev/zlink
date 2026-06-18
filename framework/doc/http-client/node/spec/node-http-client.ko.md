# Spec -- ZLink HTTP Client For Node

> 사용법 중심 문서는 [사용자 가이드](../README.ko.md)를 본다.
> 이 문서는 `@zlink-systems/http-client` 산출물의 공개 계약을 정리한다.
> 실제 계약의 단일 기준은 `packages/http-client/src/**` 공개 타입과
> `test/contract/http-client.test.js` 회귀 테스트다.

## 1. 목적

`@zlink-systems/http-client`는 Node에서 HTTP request를 보내기 위한 별도 client-side
산출물이다. JSON 전용 client가 아니라 일반 HTTP client이며 zlink fluent builder 스타일로
undici의 낮은 수준 설정을 흡수한다. typed JSON 경로(`body(dto)`/`submit<T>()`)는 그 위에
얹은 편의 계층이다.

`@zlink-systems/framework`의 에러 모델(`ZLinkFrameworkException`)에 의존하지만 framework
core의 기본 의존성은 아니다(단방향 의존).

## 2. 산출물 경계

| 역할 | 위치 | 공개 여부 |
|------|------|-----------|
| 공개 contract | `packages/http-client/src/{index,client,request-builder,types}.ts` | public |
| runtime 구현 | `packages/http-client/src/runtime/*` | internal |
| 회귀 테스트 | `test/contract/http-client.test.js` | private |
| 패키지 | `@zlink-systems/http-client` | public package |

공개 표면에는 undici `Dispatcher`/`Agent`/`request` 타입을 노출하지 않는다.

## 3. 공개 타입

- `ZLinkHttpClient` — `create()` / `create(baseUrl)`, 메서드 `get/post/put/delete/
  patch/head/options`, `close()`.
- `ZLinkHttpClientBuilder` — `baseUrl`, `json`, `timeout`, `defaultHeader`, `basicAuth`,
  `bearerToken`, `maxResponseBodySize`, `trustCertificateFile`, `clientCertificateFile`,
  `followRedirects`, `retry`, `cookies`, `proxy`, `proxyBasicAuth`, `compression`,
  `build`, 그리고 단발 verb shortcut.
- `ZLinkHttpRequestBuilder` — `header`, `query`, `timeout`, `body`(JSON/raw 오버로드),
  `bodyStream`, `form`, `multipart`, `multipartFile`, `submitRaw`, `download`, `submit<T>`.
- `RawHttpResponse` { `status`, `headers`, `body` }.
- `HttpResponse<T>` { `status`, `headers`, `body`, `rawBody` }.
- `ZLinkHttpMethod`, `BodyChunkProvider`(`() => Uint8Array | null`), `DownloadSink`.

## 4. 실행 모델

- 모든 제출은 `Promise`를 돌려준다. undici의 libuv 비동기 I/O로 네트워크 대기 중 event
  loop는 점유되지 않는다.
- 단일 event loop인 Node에는 continuation 재개 위치 주입이 없다(`.coroutines()` 없음).
- Node에는 동기 blocking HTTP 접근이 없다.

## 5. 전송 의미론

- **백엔드**: undici 저수준 `request`(`fetch` 아님). auto-redirect/decompress/cookie 없음.
- **redirect**: `301/302/303/307/308` + `Location`. `303`/(`301`·`302`+`POST`)→`GET`,
  본문 제거. same-origin `Authorization` 보존, cross-origin 제거. `max` 초과 시
  `requestFailed`. 래퍼 루프로 구현.
- **retry**: retriable transport 실패만, 고정 50ms, streaming 제외.
- **cookie jar**: host 정확 매칭, 기본 `Path=/`, `Path`/`Secure`/`Max-Age`만, host당 128개.
- **compression**: gzip+deflate 해제, `content-encoding` 제거, decoded 크기 한도,
  streaming 비해제.
- **TLS**: `trustCertificateFile`→`Agent.connect.ca`, mTLS→`connect.cert/key`.
- **proxy**: `ProxyAgent`(인증은 `token`).
- **body 소스 상호 배타**: `body`/`bodyStream`/`form`/`multipart` 중 하나.

## 6. 에러 매핑

| 상황 | kind |
|------|------|
| 구성/요청 검증 | `requestProtocolError` |
| status ≥ 400 (typed) / redirect 한도 / 크기 초과 / transport / timeout | `requestFailed` |
| JSON·압축 디코드 | `payloadDecodeFailed` |

timeout은 `requestFailed`(`isRetriable=true`)로 보고한다(framework enum에 timeout kind
없음).

## 7. 회귀 테스트 / 등록

- 회귀 테스트: `test/contract/http-client.test.js`(node:test). chunked 업로드·retry는
  raw `net` 서버, TLS/mTLS는 `node:https` + `test/fixtures/tls/` 인증서로 검증.
- 등록: workspace `package.json`(undici 런타임 의존), 루트 `package-lock.json`,
  `tsconfig.base.json` paths, `tsconfig.build.json` references, ESLint flat config(자동 scope).
- 커버리지: node 내장 coverage 게이트(`packages/*/dist`) 기준 80% 초과.
