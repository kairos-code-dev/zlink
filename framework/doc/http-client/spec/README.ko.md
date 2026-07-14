# Spec — ZLink HTTP Client 공통 계약 (언어 중립 정본)

> 이 문서 세트는 zlink HTTP client의 **언어 중립 공통 계약 정본**이다.
> 5개 언어(cpp / dotnet / java / kotlin / node) 구현과 언어별 spec
> (`../<lang>/spec/<lang>-http-client.ko.md`)은 이 계약을 따르며,
> 언어별 spec은 **이 계약에 대한 언어 고유 편차와 구현 매핑만** 기술한다.
> 계약과 구현이 어긋나면 계약이 기준이고 구현이 고쳐진다
> (단, 개정 후보로 합의된 항목은 [10장](10-revision-candidates.ko.md) 참조).

## 목차

| 장 | 문서 | 내용 |
| --- | --- | --- |
| 1 | [범위와 아키텍처](01-scope-and-architecture.ko.md) | 정체성, 산출물 경계, framework와의 관계 |
| 2 | [Client builder 계약](02-client-builder.ko.md) | builder 옵션 전체와 **기본값 표** |
| 3 | [Request 계약](03-request-builder.ko.md) | HTTP 메서드, 헤더/query, body 소스 5종과 배타 규칙 |
| 4 | [Response 계약](04-response-model.ko.md) | raw/typed/download/fetch, status ≥ 400 정책 |
| 5 | [실행 모델](05-execution-model.ko.md) | 비동기 계약, blocking 금지 규칙, client 수명 |
| 6 | [Redirect · Retry · Cookie](06-redirect-retry-cookie.ko.md) | rewrite 규칙 표, 재시도 계약, cookie 부분집합 |
| 7 | [인증 · TLS · Proxy](07-auth-tls-proxy.ko.md) | Basic/Bearer, PEM 신뢰/mTLS, CONNECT tunnel |
| 8 | [압축](08-compression.ko.md) | gzip/deflate 투명 해제 의미론 |
| 9 | [에러 모델](09-error-model.ko.md) | error kind 공통 집합, retriable, 언어별 매핑과 구현 갭 |
| 10 | [개정 후보](10-revision-candidates.ko.md) | **비계약** — 승격 전 검토 항목(R1~R14) |
| 11 | [회귀 테스트 계약](11-regression-tests.ko.md) | 공통 계약 케이스 매트릭스, 게이트, 커버리지 갭 |
| — | [언어별 인터페이스 정의](language-interfaces.ko.md) | 5개 언어 공개 API 표면의 정확한 이름 대응표 |
| — | [성능 측정과 비교](../perf/README.ko.md) | perf 시나리오 매트릭스·지표·회귀 판정 정본 |

## 계약 변경 절차

1. 새 동작/공개 API는 먼저 [10장 개정 후보](10-revision-candidates.ko.md)에
   R-항목으로 등재한다. 개정 후보는 계약이 아니며 구현 근거가 되지 않는다.
2. 승격이 결정되면 해당 장의 계약 본문으로 옮기고, 5개 언어 구현·계약 테스트·
   언어별 spec을 함께 갱신한다. 한 언어만 먼저 구현하는 것은 허용하지 않는다
   (저장소 public contract parity 정책).
3. 언어 고유 편차(키워드 회피 `delete_`, kotlin DSL 등)는 이 정본의 각 장에
   "언어 편차" 절로 명시된 것만 인정한다.

## 관련 문서

- 사용자 가이드: `../<lang>/README.ko.md` (언어별 13장)
- codec extension 공유 규약: `../../framework/common/spec/05-framework-api.ko.md`
- e2e에서 raw HTTP client 직접 사용 금지 규약: `../../framework/common/e2e/README.ko.md`
