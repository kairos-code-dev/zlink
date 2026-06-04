[English](./README.md) | [한국어](./README.ko.md)

[문서 목록](../README.ko.md)

# zlink 가이드

zlink는 [libzmq](https://github.com/zeromq/libzmq) 기반의 현대적 메시징
라이브러리다. 이 가이드는 **개념(왜·언제)**과 **C API 사용법**을 언어 중립적으로
다룬다. 특정 언어에서 쓰는 법은 [언어별 바인딩 가이드](./bindings/README.ko.md)를
본다.

## 어떻게 읽나 — 역할별 입구

| 나는… | 여기서 시작 |
|-------|------------|
| **빨리 써 보고 싶다** | [개요 & 빠른 시작](#part-0--시작하기)에서 첫 메시지를 띄우고, 필요한 패턴 문서로 |
| **메시징을 배우려 한다** | Part 1 개념 → Part 2 패턴을 차례로 |
| **특정 언어로 쓴다** | [바인딩 가이드](./bindings/README.ko.md)로 바로 가서, 막히는 개념만 이 가이드로 |
| **상태형 실시간 서버를 만든다** | Part 4 서비스 계층([서비스 개요](./07-0-services.ko.md))부터 |
| **운영·튜닝한다** | Part 5 운영 |
| **API 멤버를 찾는다** | Part 6 레퍼런스 |

---

## Part 0 · 시작하기

- [01 개요 & 빠른 시작](./01-overview.ko.md) — zlink란, 아키텍처, 첫 메시지

## Part 1 · 개념 (왜·무엇)

- [03-0 소켓 패턴 선택 가이드](./03-0-socket-patterns.ko.md) — 어떤 패턴을 언제
- [신뢰성·전달 보장](./reliability.ko.md) — 무엇이 보장되고 무엇이 안 되나
- [설계 근거](./design-rationale.ko.md) — 왜 이렇게 설계했나(zero-copy/lock-free/Proactor)
- [09 메시지 API & 소유권](./09-message-api.ko.md) — 메시지 수명·zero-copy
- [11 스레드 안전성](./11-thread-safety.ko.md) — 무엇을 공유해도 되나

## Part 2 · 메시징 패턴 (how-to)

- [08 Routing ID](./08-routing-id.ko.md) — 피어 식별 (ROUTER·STREAM의 전제)
- [03-1 PAIR](./03-1-pair.ko.md) · [03-2 PUB/SUB](./03-2-pubsub.ko.md) ·
  [03-3 DEALER](./03-3-dealer.ko.md) · [03-4 ROUTER](./03-4-router.ko.md)
- [03-5 STREAM](./03-5-stream.ko.md) — 외부 raw TCP 피어
- [03-6 Proxy](./03-6-proxy.ko.md) — 프론트/백엔드 중계

## Part 3 · 전송 & 보안 (how-to)

- [04 트랜스포트](./04-transports.ko.md) — `tcp`/`ipc`/`inproc`/`ws`/`tls`
- [05 TLS/보안](./05-tls-security.ko.md)

## Part 4 · 서비스 계층 (how-to)

상태형 실시간 서비스(게임 룸·채팅·존)와 동적 토폴로지를 위한 상위 기능.
**언제·왜 쓰는지**는 [서비스 개요 §멘탈 모델](./07-0-services.ko.md#12-멘탈-모델--어느-층을-언제-쓰나)이 먼저.

- [07-0 서비스 개요](./07-0-services.ko.md) — raw 소켓 / Discovery / SPOT / Actor 멘탈 모델
- [07-1 Discovery](./07-1-discovery.ko.md) — 이름 기반 발견·자동 연결
- [07-3 SPOT](./07-3-spot.ko.md) — 동적 상태 단위 + 직렬 실행
- [07-4 Actor](./07-4-actor.ko.md) — 세션↔엔티티 binding
- [07-4 Registry](./07-4-registry.ko.md) — 중앙 서비스 디렉토리

## Part 5 · 운영 (how-to)

- [06 모니터링](./06-monitoring.ko.md) — 연결 수명 이벤트
- [10 성능 튜닝](./10-performance.ko.md) — HWM·I/O 스레드·측정

## Part 6 · 레퍼런스 (찾아보기)

- [02 Core C API](./02-core-api.ko.md) — context·socket·message·timer 함수
- [09 Message API](./09-message-api.ko.md) — 메시지 타입·수명 상세
- [12 소켓 옵션](./12-socket-options.ko.md) — 전체 옵션 목록
- [ZMP 프로토콜 레퍼런스](./zmp-protocol.ko.md) — wire 형식(새 바인딩·interop용)
- [용어집](./glossary.ko.md) — 용어 한 줄 정의 + 링크

## 보조 (작성자·시나리오)

- [공유 시나리오 매트릭스](./scenarios.ko.md) — 코어·전 언어가 재사용하는 정규 예제
- [가이드 작성 스타일 규약](./STYLE.ko.md) — 황금률·챕터 템플릿·링크 규칙
- [예제 코드 관리 규약](./EXAMPLES.ko.md) — 샘플 단일 출처·드리프트 방지

---

> **언어별 사용 가이드**: [.NET](./bindings/dotnet/index.ko.md) 등은
> [바인딩 가이드 목록](./bindings/README.ko.md)에서 고른다. 개념은 이 코어 가이드가
> 한 번만 소유하며, 언어 가이드는 그 언어의 사용법·타입 매핑만 다룬다.
