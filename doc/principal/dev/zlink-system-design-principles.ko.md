# ZLink 시스템 개발 원칙

> [`general-design-principles.ko.md`](./general-design-principles.ko.md)의 범용 설계 원칙을
> ZLink 코어·바인딩, 그리고 ZLink 위에서 만드는 애플리케이션에 적용하는 규칙.
> 범용 원칙과 충돌하지 않는다 — 여기서는 ZLink 고유의 적용 방식, 개념·계약 어휘, 시스템
> 소프트웨어에만 해당하는 규칙만 다룬다.
>
> 공개 계약의 언어 간 정렬(parity) 정책은 `AGENTS.md`의 "Framework public contract parity"가
> 정본이다. 여기서는 그 정책을 설계 원칙 관점에서 어떻게 지키는지만 다룬다 — 정책 자체가
> 바뀌면 `AGENTS.md`를 고친다.

이 문서는 세 부분으로 나뉜다.

1. **원칙의 ZLink 적용** — 범용 원칙(특히 아키텍처 선택)을 ZLink 코어와 ZLink 기반
   애플리케이션 각각에 어떻게 적용하는지.
2. **ZLink 고유 개념과 계약** — RoutingId·handle·public contract처럼 이 코드베이스에서만
   쓰이는 어휘와, 그 어휘가 지켜야 하는 계약.
3. **시스템 소프트웨어 전용 규칙** — 헤더/구현 분리처럼 시스템 소프트웨어에만 해당하는 구체
   규칙.

---

# 1부 — 원칙의 ZLink 적용

## 아키텍처 선택 적용

범용 문서 3부는 헥사고날과 레이어드+public contract/runtime 분리를 "선택지"로 설명한다.
ZLink 생태계에서는 대상에 따라 아래처럼 확정한다.

**ZLink 코어·바인딩 자체 (시스템 소프트웨어) → 레이어드 + public contract/runtime 분리.**
공개 계약은 언어를 넘나들며 오래 안정적으로 유지해야 하고, transport·codec·platform 세부는
자유롭게 바꿀 수 있어야 한다.

```text
+--------------------------------+
| Public Contract                |
| API, ABI, Spec, Bindings       |
+--------------------------------+
              v
+--------------------------------+
| Runtime Boundary                |
| Lifecycle, State, Ownership    |
+--------------------------------+
              v
+--------------------------------+
| Integration Layers             |
| Transport, Codec, Platform     |
+--------------------------------+
```

**ZLink를 사용해 만드는 애플리케이션(샘플, 게임 서버 등) → 헥사고날.** 업무 규칙과 use case가
ZLink·Redis·HTTP 같은 구체 기술보다 오래 살아야 하기 때문이다. 소스 구조에서는 adapter 구현을
application 안에 넣지 않는다.

```text
Server/<role>/
  Domain/
  Application/
  Infrastructure/
    ZLink/
    Redis/
    Http/
```

| 디렉토리 | 책임 |
|---|---|
| `Domain/` | 업무 규칙, 상태 전이, 값 객체, 도메인 이벤트 |
| `Application/` | use case, application service, port 인터페이스 |
| `Infrastructure/` | framework callback, Redis, HTTP, storage 구현, host wiring |

`Application/`은 `Infrastructure/` 타입을 직접 참조하지 않는다. 외부 능력이 필요하면
application이 port 인터페이스를 정의하고, `Infrastructure/`가 그 인터페이스를 구현한다.
ZLink handler·Spot·Actor처럼 framework callback에 붙는 코드는 `Infrastructure/ZLink/`에 둔다.
Redis·파일·HTTP처럼 특정 외부 기술 구현은 `Infrastructure/Redis/`, `Infrastructure/Http/`처럼
기술별 하위 디렉토리로 나눈다. `Adapters/`라는 이름을 쓰는 기존 코드베이스도 의미는 같지만,
새 샘플이나 구조 정리에서는 `Infrastructure/`를 우선 쓴다.

의존 방향은 바깥에서 안쪽이다.

```text
+--------------------------------+
| Infrastructure                 |
| ZLink, HTTP, Queue, DB, API    |
+--------------------------------+
              | port
              v
+--------------------------------+
| Application Use Cases          |
+--------------------------------+
              v
+--------------------------------+
| Domain Model                   |
| Aggregate, Entity, Value       |
+--------------------------------+
```

두 구조 모두 port와 adapter가 요청을 전달만 한다면 얕은 계층이다. 없애거나, 외부 기술의
세부를 숨기고 application이 쓰기 쉬운 깊은 인터페이스로 책임을 키운다.

## 정보 은닉 적용

시스템 소프트웨어에서 public contract에는 ownership·lifecycle·timeout·cancellation·error
contract처럼 호출자가 알아야 하는 의미만 둔다. runtime 자료구조·queue 구현·transport
wiring·codec 세부는 계약에 새지 않게 숨긴다. 레이어드 구조에서도 각 계층은 서로 다른
추상화를 제공해야 한다. public API와 runtime boundary가 같은 이름·같은 동작을 그대로
전달만 한다면, 둘 중 하나는 불필요하거나 책임이 잘못 나뉜 것이다.

---

# 2부 — ZLink 고유 개념과 계약

## 도메인은 시스템 개념이다

엔터프라이즈 소프트웨어의 도메인이 주문·결제·고객 같은 업무 개념이라면, ZLink 코어의 도메인은
context·handle·socket·message·buffer·ownership·lifecycle·timeout·error code처럼 사용자가
정확히 이해해야 하는 시스템 개념이다.

## 시스템 이벤트 스토밍 어휘

ZLink 코어·바인딩에 새 기능을 설계할 때는 업무 사건 대신 상태 전이·계약 사건을 먼저 적는다.
예: `HandleCreated`, `BufferMoved`, `SocketBound`, `PeerDisconnected`, `ReadTimedOut`,
`ResourceClosed`, `MessageMoved`.

command는 API 호출이나 내부 runtime 요청이 된다. 예: `CreateHandle`, `SendMessage`,
`PollReadable`, `CloseResource`. 이 호출이 어떤 사건을 만들고 어떤 오류 계약을 갖는지 적는다.

entity와 aggregate 후보는 business object가 아니라 handle·message buffer·identifier·socket
endpoint·descriptor처럼 public contract에서 생명주기나 소유권을 갖는 개념이다.

## bounded context 적용

runtime·transport·codec·storage·binding은 서로 다른 bounded context가 **될 수 있는** 후보다 —
계층으로 나뉘어 있다는 사실만으로 자동 성립하지는 않는다. 판단 기준은 `timeout`·
`cancellation`·`backpressure`·`ownership` 같은 단어가 그 경계를 넘을 때 **의미가 실제로
달라지는가**다. 예를 들어 transport 계층의 timeout(연결 타임아웃)과 API 계층의 timeout(호출
타임아웃)이 다른 의미라면 경계를 명확히 하고, 같은 의미라면 억지로 나누지 않는다.

## 비즈니스 DDD 용어를 그대로 가져오지 않는다

`ContextAggregate`, `SocketRepository`, `MessageDomainService` 같은 이름은 호출자에게 도움이
안 되면 피한다. 대신 아래를 분명히 한다.

- 어떤 객체가 생명주기를 소유하는가?
- 누가 메모리와 핸들을 해제하는가?
- close·destroy·move 이후 어떤 호출이 가능한가?
- 같은 개념을 가리키는 이름이 모든 공개 API·바인딩·문서에서 같은가?
- 오류 코드는 상태 전이와 호출자 책임을 일관되게 표현하는가?
- timeout·cancellation·backpressure·reconnect의 의미가 계층마다 다르게 해석되지 않는가?

깊은 시스템 API는 이런 결정의 내부 복잡성을 흡수하고, 호출자에게는 단순한 생명주기와 일관된
오류 계약만 노출한다.

## 언어 간 이름 일관성

범용 문서의 "같은 개념 → 같은 이름" 규칙은 ZLink에서 **단일 언어 안**뿐 아니라 **언어
바인딩 전체**에 적용된다. 같은 시스템 개념(RoutingId, Spot, Actor, ownership transfer 등)은
core·cpp·dotnet·java·kotlin·node·rust·python 문서와 API에서 같은 이름을 쓴다. 이름이
언어마다 다르면 그 자체가 정보 누출이다 — 사용자가 언어를 넘나들 때마다 매핑을 다시 배워야
한다.

공개 계약을 언어별로 새로 만들지, 기존 계약을 따를지의 판단 기준(스펙/가이드 문서 근거 여부,
한 언어 구현만으로는 공개 계약 신설 근거가 안 되는 것 등)은 `AGENTS.md`의
"Framework public contract parity"를 따른다.

---

# 3부 — 시스템 소프트웨어 전용 규칙

## 주석 배치: 헤더/선언부 대 구현부

범용 문서는 "인터페이스 계약 주석은 인터페이스가 선언되는 자리에 둔다"고만 말한다. ZLink는
선언과 구현이 파일로 분리된 C/C++ 코어와, 언어별 바인딩을 함께 다루므로 이렇게 구체화한다.

- **공개 API 계약**(시그니처 의미, 소유권, 오류 조건, timeout/cancellation 의미, 단위, null
  의미)은 **헤더(`.h`/`.hpp`) — 또는 해당 언어의 공개 선언부(IDL, `.d.ts`, 인터페이스 파일
  등)**에 둔다. 헤더/선언부만 보는 바인딩 개발자·소비자가 계약을 볼 수 있어야 한다.
- **구현의 이유**(왜 이렇게 짰는지, 비자명한 트레이드오프, 특정 버그의 우회)는 `.c`/`.cpp`
  코드 옆에 둔다. 구현을 고치는 개발자가 맥락을 잃지 않게 하기 위함이다.
- **장기적인 아키텍처 결정**(왜 이 설계를 골랐는지, 어떤 대안을 버렸는지)은 ADR이나 설계
  문서에 남기고, 헤더/구현부에서는 그 문서를 참조한다.
- 하위 단계 설명 주석은 메서드 맨 위가 아니라 각 하위 단계 바로 위에 둔다 (범용 원칙과 동일).

## 테스트 커버리지 기준

ZLink의 기본 목표 커버리지는 **라인 커버리지 80%**다. 이 숫자는 기준선일 뿐, 판단을
대신하지 않는다:

- 커버리지는 공개 계약, 프로토콜 호환성, 생명주기 경계, 오류 경로, timeout/abort 동작,
  backpressure, 샘플 회귀 테스트를 우선해야 한다.
- 커버리지 숫자가 높아도, 사용자가 보는 중요한 동작이 빠져 있으면 품질을 증명하지 못한다.
- 80% 아래로 내려가려면, 생성 코드·플랫폼별 연결 코드·통합/계약 테스트가 더 적합한 코드처럼
  명확한 이유가 있어야 한다 (범용 문서 2부의 예외 판단 기준을 따른다).
- 숫자만 올리는 얕은 테스트를 추가하지 않는다.

모듈이 공개 API나 언어 간 계약을 노출한다면, 구현 세부를 넓게 고정하는 테스트보다 초점이
분명한 계약 테스트를 우선한다. 목표는 모듈의 보장을 지키면서도 구현은 쉽게 리팩토링할 수
있게 두는 것이다.

## ZLink 전용 위험 신호 체크리스트

범용 문서의 17개 체크리스트에 더해 아래를 확인한다.

| # | 위험 신호 | 진단 질문 |
|---|---|---|
| Z1 | **주석이 잘못된 위치** | 공개 계약(소유권, 오류 조건, timeout 의미)이 헤더가 아니라 구현부에만 있는가? |
| Z2 | **언어 간 이름 불일치** | 같은 시스템 개념이 언어 바인딩마다 다른 이름·다른 의미로 쓰이는가? |
| Z3 | **스펙 없는 공개 API 확산** | spec·가이드 문서 근거 없이, 한 언어에만 있던 공개 API·동작이 다른 언어로 그대로 전파됐는가? (`AGENTS.md` 위반 신호) |
| Z4 | **경계 넘어 의미 드리프트** | timeout·cancellation·backpressure·ownership 같은 단어가 transport/codec/storage 경계를 넘을 때 다르게 해석되는가? |
| Z5 | **Infrastructure 안의 도메인 규칙** | ZLink handler·Spot·Actor 콜백 코드(`Infrastructure/ZLink/`) 안에 업무 규칙이 직접 들어가 있는가? |

---

> 이 문서는 범용 원칙의 대체물이 아니라 적용 계층이다. 여기 없는 판단은
> [`general-design-principles.ko.md`](./general-design-principles.ko.md)를 따른다.
