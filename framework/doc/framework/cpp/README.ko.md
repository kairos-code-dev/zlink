# ZLink Framework C++ — 문서

`zlink::framework` C++ 산출물의 문서 허브다.

| 문서 | 범위 |
|------|------|
| [01 시스템 구조](../common/spec/languages/cpp/01-system-structure.ko.md) | 패키지·빌드 타깃, application host, **DI 컨테이너**, configuration, **logging**, HTTP scope·middleware 순서, 기능 등록 |
| [02 framework 인터페이스](../common/spec/languages/cpp/02-framework-interfaces.ko.md) | 전체 public 타입·시그니처 카탈로그 — App/Host, DI, builder, handler registry, messaging, SPOT, actor, STREAM, dispatch 오류, health, location store |
| [60 HTTP hosting](../common/spec/languages/cpp/60-http-hosting.ko.md) | HTTP 호스팅 계약 |
| [61 내장 HTTP 서버](../common/spec/languages/cpp/61-embedded-http-server.ko.md) | 내장 서버 |

**기능의 의미와 동작 규칙은 [공통 스펙](../common/spec/README.ko.md)이 소유한다.** C++ 문서는 그
의미가 C++에서 갖는 **정확한 public 표면**을 고정한다.

**C++가 다른 언어보다 문서가 많은 이유** — `.NET`은 ASP.NET Core를, Node는 NestJS를, Java는
Spring Boot를 빌려 쓰지만 **C++에는 빌릴 host가 없어 framework가 host·DI·configuration·logging·
HTTP를 직접 제공한다.**

## 별도 산출물 문서

| 산출물 | 문서 |
|--------|------|
| HTTP client (`zlink::http_client`) | [가이드](../../http-client/cpp/README.ko.md) · [spec](../../http-client/cpp/spec/cpp-http-client.ko.md) |
| Stream connector (`zlink::stream_connector`) | [사용자 가이드](../../stream-connector/cpp/guide/INDEX.ko.md) |

## internals 목록

[runtime architecture](internals/runtime-architecture.ko.md) ·
[backend dependency policy](internals/backend-dependency-policy.ko.md) ·
[regression test matrix](internals/regression-test-matrix.ko.md)

공통 6종의 역할, DTO와 검증 기준은 [공통 샘플](../common/sample/README.ko.md)에서 확인한다.

상위 framework 공통 문서는 [framework/doc](../../README.ko.md)을 본다.
