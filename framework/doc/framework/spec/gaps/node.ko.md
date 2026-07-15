# Node.js — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> NestJS host. connector 계약은 TypeScript가 따로 소유한다.

**이 문서는 계약이 아니라 작업 목록이다.** 계약은 spec이 소유한다. 여기서는 **스펙과 코드가 어긋난 자리**와 그것을 닫았는지만 추적한다.

**두 종류를 구분한다** — **미구현**(없다 → 만든다) / **결함**(있는데 계약과 다르게 돈다 → 동작을 바꾼다). 결함이 더 위험하다: 없는 것은 컴파일이 막아 주지만, 있는데 다르게 도는 것은 **부하가 걸릴 때만 드물게 깨진다.**

## 0. 작업 방식 (이 문서를 맡은 사람이 먼저 읽는다)

**이 문서는 위에서부터 훑는 목록이 아니다.** 순서와 게이트가 있다.

### 0.1 왜 순서가 있나 — 이 감사의 핵심 발견

**e2e가 갭을 *못 잡는* 게 아니라, 잡을 수 없게 배치돼 있다.**

그래서 순진하게 framework부터 고치면 **고쳤는지 확인할 방법이 없다.** 게이트는 고치기 전에도
초록, 고친 뒤에도 초록이다. 실제 사례:

- Config 6은 store 장애 갭을 검증하라고 존재하는데 **`docker pause`만 쓴다.** 문서가 요구한
  stop/restart를 안 하므로 **결함이 터지는 조건 자체가 안 생긴다.**
- `MON-A2`(P0)는 monitoring 결함(매 tick 무조건 발행) **덕분에** 통과한다.
- `OBS-A2`는 결함으로 지목된 C++ 전용 토큰(`outcome=`)을 **오히려 못박는다.**

**그래서 규칙 하나**: **구현을 고쳤는데 해당 e2e·샘플이 여전히 통과한다면, 틀린 것은 e2e다.**

### 0.2 작업 단계

```
단계 0  재검증        이 문서의 행이 사실인지 확인한다        (전 항목, 항상 먼저)
단계 1  진짜 버그      게이트와 무관하게 지금 깨져 있다        (바로 고친다)
단계 2  게이트 복구    그 갭을 "실패시킬 수 있게" 만든다       (3단계의 선행 조건)
단계 3  책임 묶음      묶음 단위로 구현 + POSD 리팩토링        (묶음별로 닫는다)
단계 4  전체 감사      LOOP CLEAN 될 때까지 반복
```

#### 단계 0 — 재검증: **이 문서의 행은 증거이지 판결이 아니다**

감사 과정에서 **일곱 건이 검증에 무너졌다** — 갭이 아니었던 것, 수치가 과장된 것, 존재하지 않는
줄을 가리킨 인용, **잘못 기각된 진짜 결함**까지 있었다. 같은 비율이 남아 있다고 가정한다.

갭 하나를 시작할 때 **가장 먼저 하는 일은 구현이 아니라 재검증**이다.

1. **계약 열**의 문서·줄을 열어 그 문구가 정말 그렇게 적혀 있는지 본다.
2. **구현 열**의 코드·줄을 열어 그 코드가 정말 그렇게 도는지 본다.
3. 어긋나면 **행을 고친다.** 갭이 아니면 `~~취소선~~` + 근거를 남기고 닫는다.

**틀린 갭을 구현하는 것이 갭을 놓치는 것보다 나쁘다.**

#### 단계 1 — 진짜 버그: 게이트를 기다리지 않는다

`(**버그**)`로 표시된 항목은 **계약 위반이 아니라 동작이 깨진 것**이다. 사용자가 지금 당하고 있다.
게이트 복구를 기다리지 말고 먼저 고친다. 다만 **고치면서 그 버그를 잡는 단언을 함께 넣는다** —
안 그러면 다음에 또 들어온다.

#### 단계 2 — 게이트 복구: **3단계의 선행 조건**

**손댈 묶음이 정해지면, 그 묶음을 덮는 e2e·샘플 게이트부터 실패시킬 수 있게 만든다.**

1. 그 묶음의 갭을 드러내는 단언을 **먼저** 만들거나 고친다.
2. **실패하는 것을 눈으로 확인한다.** ← 이걸 못 하면 그 갭은 아직 이해하지 못한 것이다.
3. 그 다음에야 구현을 만진다.

**"실패할 수 없는 단언" 11가지 형태**를 체크리스트로 쓴다 — 느슨한 `>=`, 하드코딩 상수 단언,
무조건 PASS 출력, 자기가 던진 오류를 자기가 확인, 실패를 경고로 강등, 계약의 반대를 단언,
재시도·sleep으로 세탁, 경합 창을 스스로 닫음, 설치한 적 없는 것이 비었음을 단언,
존재하지 않는 것의 부재를 단언, 금지된 결과를 통과로 수용.
**새로 쓰는 단언이 그중 하나면 다시 쓴다.**

#### 단계 3 — 책임 묶음: 번호가 아니라 **책임**으로 짠다

이 문서는 감사 라운드 순으로 적혀 있다. **그 순서로 작업하지 마라.**
아래 축으로 이 문서의 ID를 재배열해 **자기 묶음 목록을 먼저 만든다.**

| 묶음 | 무엇을 공유하나 | 이 묶음을 덮는 게이트 |
|------|-----------------|----------------------|
| **A. 근거 없는 공개 표면** | 스펙 근거 없는 타입·메서드, 검증만 되고 안 읽히는 옵션, no-op 설정 | (게이트 없음 — 표면을 지우고 컴파일로 확인) |
| **B. Channel · messaging** | dispatch 키, timeout 분리, retry/dead-letter, session relay, packet registry | Config 1 · 4 |
| **C. Location runtime · store** | owner lease join, generation guard, 페이징, 원자성, auto-connect reconcile | **Config 6** (가장 심하게 깨진 게이트) · Config 1 |
| **D. 관측** | diff 기반 이벤트, polling 주기, source 축, 계기, flow 토큰 | **Config 7 · 11** (둘 다 결함 덕에 통과 중) |
| **E. Stream connector** | 재연결, connect 상태, timeout, heartbeat, dispatch 모드, metadata·압축 wire | Config 2 · 9 |
| **F. Spot · actor lifecycle · 동시성** | close/timer 취소, callback 직렬성, join admission/commit, transfer | Config 2 · 10 |
| **G. 샘플 도메인 · 레이어** | Domain/Application/Infrastructure 경계, framework 타입 누출, owner routing 책임 | 샘플 self-check |
| **H. 샘플 wire 계약** | nullable, enum 표현, 필드 drift, 계약에 없는 메시지 | 샘플 + 교차 언어 대조 — **§0.8 먼저 읽어라** |
| **I. 샘플 릴리스 게이트** | 번호 매긴 client 검증 흐름, push 대기 표면, 단언 강도 | (이것 자체가 게이트다 — 단계 2에 속한다) |
| **J. E2E 구조 · 러너** | 역할 프로젝트 분리, 시나리오 파일 분리, 대기 기준, 설정 전달, 축 변형 | (이것 자체가 게이트다 — 단계 2에 속한다) |

> **I와 J는 다른 묶음의 peer가 아니다.** 게이트 그 자체이므로 **단계 2에서 먼저 닫는다.**
> **H(wire 계약)는 네가 혼자 결정할 수 없다** — §0.8을 읽어라.

**한 묶음을 닫는 순서**

```
근거 재검증 → 공통 원인·의존성 파악 → 설계 대안 2개 이상 검토
  → 게이트 먼저 실패시키기 → 항목 구현 → 테스트 통과
  → 묶음 전체 POSD 리뷰 → 의미 있는 리팩토링 → 테스트·성능 재검증
  → 체크박스 [x] + 근거 한 줄
```

#### 단계 4 — 전체 감사 (아래 0.6)

### 0.3 POSD/DDD 리팩토링은 선택이 아니다 — **묶음 완료의 정의**

**지금까지 작업자들이 갭만 닫고 리팩토링을 건너뛰었다. 그러면 묶음은 닫힌 것이 아니다.**

한 묶음의 갭을 다 구현했는데 POSD/DDD 리팩토링을 안 했으면 그 묶음은 **미완료**다. 체크박스를
`[x]`로 바꾸지 마라. 갭을 닫는 것과 묶음을 닫는 것은 다르다 —

```
갭 하나 닫힘   = 그 계약 위반이 사라짐          (기능 완료)
묶음 하나 닫힘 = 그 위에 POSD/DDD 리뷰+리팩토링이 끝남  (설계 완료)  ← 여기까지 해야 [x]
```

**리팩토링을 "나중에 시간 나면"으로 미루지 않는다.** [POSD 원칙](../../../../../doc/principal/software-design-principles.ko.md)은 개발 시간의 **10–20%를 설계에** 쓰라고 한다 — 그건 권장이 아니라 **이 작업의 배정된 예산**이다.
묶음마다 그 예산을 실제로 쓴다.

**묶음 리팩토링에서 반드시 하는 것 (건너뛰면 묶음 미완료)**

1. 그 묶음이 건드린 코드의 **POSD 위험 신호를 명시적으로 열거**한다(§0.4 목록으로).
2. **DDD 경계를 확인**한다 — Domain이 framework/transport 타입에 의존하는가? 한 aggregate의
   불변식을 다른 계층이 다시 판단하는가? wire DTO가 domain model 자리에 앉아 있는가?
3. 각 위험 신호에 **수정안을 둘 이상** 적고, **인터페이스와 호출자 복잡성을 가장 많이 줄이는 안**을 고른다.
4. 리팩토링을 **수행**한다. 그리고 기능 테스트 + (해당되면) 성능 벤치를 다시 통과시킨다.
5. **재리뷰**에서 의미 있는 위험 신호가 남지 않아야 묶음을 닫는다.

**증거를 남긴다.** 묶음을 닫을 때 "리팩토링함"으로 끝내지 말고 **무엇을 왜 바꿨는지**
(어떤 위험 신호 → 어떤 수정 → 무엇이 줄었는지)를 한두 줄로 남긴다. 근거 없는 "리팩토링 완료"는
안 한 것으로 본다.

---

### 0.3 POSD 리팩토링을 언제 하는가 — 3단계 게이트

**항목마다 구조를 뜯지 않는다. 마지막에 몰아서 하지도 않는다.**
[POSD 원칙](../../../../../doc/principal/software-design-principles.ko.md)은 개발 시간의 **10–20%를 지속적인
설계 개선**에 쓰라고 한다.

| 시점 | 하는 일 |
|------|---------|
| **각 항목 시작 전** | 책임 경계를 어디에 둘지 대안을 **둘 이상** 검토한다 |
| **각 항목 완료 후** | **위험 신호만 짧게 확인**한다. 구조는 건드리지 않는다 |
| **묶음 완료 후** | **실제 POSD 리팩토링을 수행**한다 |
| **문서 전체 완료 후** | 전체 구조 감사 → 수정 → 재리뷰를 **남는 게 없을 때까지 반복** |

**항목마다 전체 리팩토링을 하면 안 되는 이유**: 인접 항목이 같은 코드를 어떻게 바꿀지 아직 모른다.
그때마다 정리하면 다음 항목에서 또 바뀌고, **여러 결함의 공통 원인을 보기도 전에 성급한 추상화**가 생긴다.

**마지막에 몰아서 하면 안 되는 이유**: 초기의 잘못된 책임 경계 위에 후속 구현이 쌓인다.
나중에 고치면 변경 증폭과 테스트 범위가 폭발한다.

### 0.4 항목 완료 시 확인할 위험 신호 (리팩토링이 아니라 **감지**다)

- 새 public surface가 늘었는가?
- 호출자가 더 많은 내부 지식을 알아야 하는가?
- 같은 결정이 여러 파일로 퍼졌는가?
- 인자를 그대로 넘기는 pass-through 계층·메서드가 생겼는가?
- 특정 테스트나 샘플만을 위한 분기가 생겼는가?
- 고쳐야 할 파일 수가 예상보다 많았는가?

하나라도 걸리면 **그 묶음의 리팩토링 목록에 적어 둔다.** 지금 뜯지 않는다.

**다만 아래는 미루지 않는다** — "나중에 할 리팩토링"이 아니라 **지금 방향이 틀렸다는 신호**다.
항목 구현의 일부로 즉시 고친다.

- 샘플 전용 helper가 필요해진다
- 같은 정책을 두 모듈이 알아야 한다
- 내부 transport 정보(node rid, connection id, route channel)가 public API로 새어 나간다
- 호출자가 lifecycle 순서를 알아야 한다
- 같은 option이 여러 호출부에 반복된다

### 0.5 성능 게이트

아래 묶음은 **리팩토링 전 기준값을 먼저 재고**, 같은 runner·같은 runtime으로 후 비교한다.
구조가 좋아졌어도 **allocation · lock contention · Redis 왕복 · dispatch hop이 늘었으면 완료가 아니다.**

- send/request timeout 경로 · pending request 관리 (묶음 B)
- actor/session routing · dispatch 계층 (묶음 B·F)
- Redis lease · 페이징 · 원자성 (묶음 C)
- serialization · nullable 처리 (묶음 H)
- lock 제거 · 전역 상태 제거

### 0.6 문서를 닫는 조건

체크박스를 전부 `[x]`로 만드는 것이 끝이 아니다.

**먼저 확인한다 — 모든 묶음이 §0.3의 POSD/DDD 리팩토링을 거쳤는가?**
갭만 닫고 리팩토링을 건너뛴 묶음이 하나라도 있으면 문서는 닫히지 않는다. 각 묶음의 완료
줄에 "무엇을 왜 바꿨는지"가 남아 있어야 한다(없으면 리팩토링 안 한 것).

그 다음:

1. POSD 위험 신호(0.4) 전수 검색 — 남은 게 있으면 아직 안 끝났다
2. DDD 경계 재확인 — Domain의 framework/transport 의존 0건, wire DTO가 domain model 자리에 없음
3. public contract ↔ 실제 헤더/표면 재대조
4. 안 쓰이는 타입·helper·DI 등록 검색
5. 샘플·E2E에서 내부 타입 사용 여부 검색
6. 이 언어의 전체 테스트 실행
7. 성능 민감 변경 벤치 재실행 (§0.5)
8. 수정 후 **다시** POSD/DDD 리뷰
9. **의미 있는 항목이 남지 않을 때까지 반복** → `LOOP CLEAN`

`LOOP CLEAN`은 "체크박스가 다 [x]"가 아니라 **"한 바퀴 더 돌아도 고칠 게 안 나온다"**는 뜻이다.

**파일 크기나 형식만을 이유로 리팩토링하지 않는다.** 책임 혼합·정보 누출·변경 증폭·호출자 복잡성을
**실제로 줄이는** 변경만 한다.

### 0.7 선행 관계 (뒤집으면 깨진다)

- **join orchestration**(§12.24) → **`yield` terminator**(§12.21) → **샘플의 `yield` 사용처**(SMP-X1)
  — 뒤집으면 `yield` 없이 `async`로 흉내 내게 되어 **샘플이 보여 주려던 대비 자체가 사라진다.**
- **framework의 owner-lease join** → **store의 lease script·페이징** — store부터 고치면 아무도 안 쓴다.
- **wire 계약(묶음 H)은 한 언어만 고치면 오히려 깨진다.** 결정은 §0.8의 2단계 프로토콜을 따른다.
- 어떤 갭이 다른 갭 **덕분에** 가려져 있는 경우가 있다(`MON-A2` ← monitoring 결함). **그런 쌍은 함께 연다.**

### 0.8 이 문서의 경계 — **네가 혼자 결정하면 안 되는 것**

**너는 한 언어를 맡았다. 그런데 어떤 갭은 계약을 바꿔야 닫힌다.**
계약은 다섯 언어가 공유한다. **개별 에이전트가 계약을 바꾸면 다른 넷이 즉시 깨진다.**

그래서 이 리포는 **결정과 구현을 분리**한다. 이미 있는 규약이다 —
「교차 언어 결함 — 이 언어에서 무엇을 고치나」 절이 그것을 명시한다:
**갭 인덱스가 "왜"(계약과 결정)를 소유하고, 이 문서의 표가 "무엇을"(이 언어의 작업)을 소유한다.**

#### 2단계 프로토콜

```
1단계  계약 결정   ← 네 소관이 아니다. 멈추고 올린다.
2단계  언어 구현   ← 네 소관이다. 결정이 내려온 뒤에 한다.
```

**1단계에 해당하는 것 (멈춰라)**

- 공유 wire 메시지의 **필드 추가·삭제·이름 변경·타입 변경**
  (예: `QuestProgress`에 `Version`을 넣을 것인가, `DeliveryStatus`를 문자열로 할 것인가)
- **`SMP-X*` · `IMP-X*`** 로 표시된 항목 — 정의상 전 언어 공통이다
- 공통 spec·공통 sample·공통 e2e 문서(`common/**`, `spec/**`)의 **계약 변경**
- 새 public API 추가 — 다른 언어에 있다는 것은 근거가 아니다([AGENTS.md](../../../../../AGENTS.md))

**멈춘 뒤 할 일**

1. **구현하지 마라.** 우회 helper·내부 API·프레임 직접 조작으로 메우지도 마라.
2. 이 문서의 해당 행에 **무엇을 결정해야 하는지, 선택지가 무엇인지** 적는다.
3. [갭 인덱스](../90-implementation-gap.ko.md)에 올린다. **결정은 거기서 한 번만 내린다.**
4. 결정이 내려오면 **그때 네 언어에만** 구현한다.

**2단계에 해당하는 것 (네가 한다)**

- 이미 계약이 정해져 있는데 **네 언어만 안 지키는 것** — 대부분의 갭이 여기다
- 네 언어의 e2e·샘플·러너·내부 구조
- 갭 인덱스가 **이미 결정을 내려 둔** `IMP-X*`의 "이 언어의 작업" 열

#### 판별법

> **"내가 이걸 고치면 다른 언어의 테스트가 깨지는가?"**
> — 깨지면 1단계다. 안 깨지면 2단계다.

교차 언어 wire 호환은 **이 감사가 실제로 파손을 확인한 자리**다
(`DeliveryStatus`가 `.NET`만 정수, `QuestProgress.Version`이 `.NET`에만 없음).
**혼자 고치면 파손을 다른 방향으로 옮길 뿐이다.**

## 1. 진행 체크리스트

**전체 20건. 완료 0건.**

### 구현 감사에서 발굴 (2026-07-14, 스펙↔코드 직접 대조)

- [x] **IMP-ND-01** (결함) — 54 §4·§5
  - 근거: drain 시작 전에 상태·location handoff를 순서화하고 host가 그 책임을 소유하게 정리했다. 기존 순서 단언이 실패하던 `drain-control` 게이트가 통과한다. 커밋 `c151e3de7`.
- [x] **IMP-ND-02** (미구현) — 54 §3.1·§4-2
  - 근거: actor·spot admission을 공통 drain 상태에 연결해 drain 중 신규 진입을 거부하고 중복 판단을 runtime 경계로 모았다. drain admission 회귀 게이트가 실패에서 통과로 바뀌었다. 커밋 `26144697a`.
- [x] **IMP-ND-03** (결함) — 03 §5.3
  - 근거: channel reply가 public error kind를 보존하도록 envelope 변환 책임을 한곳에 두었다. kind가 유실되던 `channel-envelope-error` 게이트가 통과한다. 커밋 `b1cd22745`.
- [x] **IMP-ND-04** (미구현) — 54 §7.1
  - 근거: managed stream에 heartbeat·liveness 판정을 구현하고 session runtime이 같은 정책을 사용하게 했다. timeout을 검출하지 못하던 `stream-session-runtime` 게이트가 통과한다. 커밋 `a76571fd6`.
- [x] **IMP-ND-05** (결함) — 54 §3.3-4
  - 근거: drain 완료가 location owner 정리 완료까지 기다리도록 lifecycle 책임을 연결했다. 조기 완료를 잡는 `drain-control`·`location-runtime` 게이트가 통과한다. 커밋 `0535310bb`.
- [x] **IMP-ND-06** (결함) — 54 §3.4
  - 근거: drain marker 기록 실패를 host 내부에서 재시도해 호출자에게 순서·재시도 정책을 노출하지 않았다. 일시 실패 뒤 marker가 빠지던 `drain-control` 게이트가 통과한다. 커밋 `9feb195b2`.
- [x] **IMP-ND-07** (결함) — 05 §2.6·22 §2
  - 근거: handler filter scope를 dispatch scope와 함께 만들고 Nest adapter의 중복 scope 판단을 분리했다. request-scoped filter가 다른 context를 보던 channel/Nest 게이트가 통과한다. 커밋 `ab29fd6b5`.
- [x] **IMP-ND-08** (미구현) — 22 §6·§6.1
  - 근거: actor resolver가 store의 원격 actor location을 실제 `ActorRef`로 해석하게 하고 배치 판단을 resolver에 가뒀다. 원격 actor를 찾지 못하던 actor/location 게이트가 통과한다. 커밋 `bff714aa4`.
- [x] **IMP-ND-09** (미구현) — 51·05 §2.4.3
  - 근거: mailbox·channel·spot·stream의 runtime metric 기록을 공통 계측 경계에 연결해 호출부별 계측 누락을 없앴다. metric 부재로 실패하던 `runtime-metrics` 게이트가 통과한다. 커밋 `f345d5668`.
- [x] **IMP-ND-10** (결함) — 30 §7.2
  - 근거: 같은 stream node 등록을 builder 단계에서 거부해 뒤늦은 runtime 충돌을 없앴다. 중복 등록이 통과하던 Nest module 게이트가 startup 오류를 확인하며 통과한다. 커밋 `135d27edb`.

### 교차 언어 결함 (여러 구현에 같은 문제)

- [ ] **IMP-X2** — location event source(`location-peer/spot/actor/route`, `StoreFailure`/`StoreRecovered`)가 없다
- [ ] **IMP-X3** — startup validation이 스펙의 설정 오류를 통과시킨다

### 언어별 표면 차이 (기준선 대조)

- [ ] **§12.5** — spot 메시징 표면 누락 (Node)
- [ ] **§12.6** — session handler registry 키 (Node)
- [ ] **§12.11** — location event kind 이름 (Node)

### 전 언어 공통 계약 갭 (모든 언어가 함께 닫는다)

- [ ] **§12.20** (결함) — 응답에 packet name을 싣는다
- [ ] **§12.21** (결함+미구현) — `yield` terminator 부재 + `async`가 자동으로 turn을 반납
- [ ] **§12.22** (결함+미구현) — HTTP client가 framework 계약 밖에 있다
- [ ] **§12.23** (미구현) — worker 축 분리와 `yield` 부재
- [ ] **§12.24** (결함) — actor join의 orchestration이 뒤집혀 있다

본문은 [갭 인덱스](../90-implementation-gap.ko.md)가 소유한다. **§12.21과 §12.24는 한 묶음이다** — join orchestration을 먼저 바로잡지 않고 자동 turn dispatch만 걷어내면 user Spot → user Spot join이 즉시 막힌다.

## 2. 구현 감사 상세

| ID | 종류 | 계약 | 구현이 하는 일 |
|----|------|------|----------------|
| **IMP-ND-01** | 결함 | [54 §4·§5](../server/54-graceful-drain-handoff.ko.md): 순서는 marker → 신규 수용 차단 → **handoff** → in-flight 대기 → owner 정리. 기존 세션은 actor handoff를 거친다 | `runtime/host/index.ts:485-487` — `notifyServerDrain()`이 **handoff 앞에** 있다. 모든 세션을 먼저 끊는다. ⇒ SIGTERM 한 번에 **모든 클라이언트가 먼저 끊기고**, bound actor는 세션을 잃은 뒤에야 handoff된다. DRAIN-003·DRAIN-013이 통과할 수 없다 |
| **IMP-ND-02** | 미구현 | [54 §3.1·§4-2](../server/54-graceful-drain-handoff.ko.md): drain 중 신규 수용을 거부한다(`RequestRejected`/`ActorCreateRejected`) | **admission gate가 없다.** `ready` 플래그를 읽는 곳이 `isReady()` 하나뿐이고, spot create·actor create·신규 STREAM 연결·draining 노드로의 join 어디에도 검사가 없다. `RequestRejected`는 **런타임 코드에서 한 번도 생성되지 않는다** |
| **IMP-ND-03** | 결함 | [03 §5.3](../03-message-model.ko.md): `Error`는 **비어 있지 않은 `error-code`**를 갖고, 실패를 `RequestFailed`로 뭉개지 않는다 | `runtime/channels/channel-envelope.ts:134-150` — 오류 reply 인코더가 `message: string`만 받아 **kind를 버리고** `errorCode:'ZLinkRouteHandlerError'`를 **하드코딩**한다. 디코더(:157-159)는 `errorCode`를 **읽지도 않고** `ZLinkConfigurationException`을 던진다. ⇒ 호출자에게 `kind`도 `isRetriable`도 없다. **오류 분류가 wire에서 통째로 소실**된다 |
| **IMP-ND-04** | 미구현 | [54 §7.1](../server/54-graceful-drain-handoff.ko.md): 서버는 1초 ping / 5초 pong timeout / 30초 idle timeout | **liveness 루프가 없다.** ping도 pong 추적도 idle 타이머도 없다. `protocol.ts:82-97`은 reason 바이트를 **`4`(server_drain)로 하드코딩**한다. ⇒ 전원이 뽑힌 클라이언트(half-open TCP)를 **영원히 감지 못 한다.** 세션·bound actor·binding·location row가 전부 남는다 |
| **IMP-ND-05** | 결함 | [54 §3.3-4](../server/54-graceful-drain-handoff.ko.md): row/lease 정리가 **성공한 뒤에만** `Drained`로 전이한다 | `runtime/locations/runtime.ts:156-173` — 정리 실패를 **삼킨다.** 그래서 Redis가 죽어 있어도 drain이 `Drained`를 반환한다. `OwnerCleanupFailed`는 **생성되는 곳이 없다.** ⇒ 죽은 노드의 lease와 row가 TTL까지 남아 peer들이 계속 dial한다 |
| **IMP-ND-06** | 결함 | [54 §3.4](../server/54-graceful-drain-handoff.ko.md): 마커 게시는 **deadline까지 재시도**한다 | `runtime/host/index.ts:476-484` — **한 번만** 게시하고 실패하면 즉시 force-stop. 전파 대기도 없다. ⇒ SIGTERM 순간의 일시적 store 오류 하나가 **모든 방과 actor를 강제 종료**시킨다 |
| **IMP-ND-07** | 결함 | [05 §2.6](../05-framework-api.ko.md)·[22 §2](../server/22-actor-model.ko.md): **filter는 그 dispatch의 DI scope에서 resolve한다. handler와 같은 scope다** | `runtime/channels/channel-dispatch-services.ts:67-81` — filter를 **싱글턴으로 한 번** resolve해 프로세스 수명 내내 재사용한다. handler는 dispatch마다 새 scope다. ⇒ request-scoped filter가 **resolve되지 않고**, 필드에 상태를 두는 filter는 동시 dispatch 간에 **조용히 공유**된다 |
| **IMP-ND-08** | 미구현 | [22 §6·§6.1](../server/22-actor-model.ko.md): 호출자는 resolver나 actor manager로 **remote `ActorRef`**를 얻는다 | `runtime/actors/index.ts:114-121` — `find()`가 **로컬 in-process map만** 본다. `ensure()`는 placement 인자를 무시하고 로컬 생성으로 간다. ⇒ **다른 노드의 actor에 메시지를 보낼 public 경로가 Node에 없다** |
| **IMP-ND-09** | 미구현 | [51](../server/51-runtime-metrics.ko.md)·[05 §2.4.3](../05-framework-api.ko.md): trace가 off여도 **metric/counter는 남는다** | `zlink.channel.messages.dropped` 등 **7개 계기가 아예 없다.** ⇒ trace를 끄면 drop이 **완전히 보이지 않는다** |
| **IMP-ND-10** | 결함 | [30 §7.2](../server/30-stream-session.ko.md): stream node 이름 빈 값·중복은 설정 오류 | `RegistrationBuilders.ts:198-201` — `streamNodes[name] ??= {}`. 두 모듈이 같은 이름을 등록하면 **조용히 덮어쓴다** |

## 3. 언어별 표면 차이 상세

### §12.5 spot 메시징 표면 누락 (Node)

**미충족(Node).** 두 항목이다.

- route client에 `sendToSpot` / `requestToSpot`가 없다. spot node가 아닌 외부 client가 spot handle로
  spot에 메시지를 보낼 수 없다([20 §6](../server/20-spot-messaging.ko.md), [24 §3](../server/24-spot-address-messaging.ko.md)).
- spot 전송이 handle을 한 번 resolve한 뒤 그대로 보내고 끝난다. [24 §4](../server/24-spot-address-messaging.ko.md)가
  요구하는 **stale 실패 감지 → handle 갱신 → request 1회 재전송**이 없다.

### §12.6 session handler registry 키 (Node)

**미충족(Node).** [31 §10.2](../server/31-session-actor-dispatch.ko.md)의 session handler registry는 packet
name을 키로 dispatch해야 한다. Node 구현은 **handler 클래스 이름**을 키로 저장하므로 wire의 packet
name과 우연히 일치하지 않으면 영구 미매치가 된다. 중복 등록 검출과 `Configure()` 등록 창 강제도
없다.

### §12.11 location event kind 이름 (Node)

**미충족(Node).** [40 §9](../server/40-location-runtime.ko.md)와 [50 §3.1](../server/50-runtime-monitoring.ko.md)이 고정한
location runtime event kind의 닫힌 집합은 `StatusChanged`, `TopologyChanged`,
`ServiceSummaryChanged`, **`StoreFailure`**, `StoreRecovered`다. `.NET`, Java, C++은 이 이름을
쓰는데 Node 구현만 `StoreUnavailable`을 쓴다. 닫힌 enum의 멤버 이름은 관측 데이터의 안정 키이므로
언어마다 다를 수 없다.

## 라운드 2 (2026-07-14) — 관측 · channel topology · TypeScript connector

### 체크리스트

- [x] **IMP-ND-11** (결함) — `flow_id`를 **홉마다 새로 만든다.** wire에 실린 id와 로그의 id가 **다르다**
  - 근거: 생성된 flow를 async context에 보존해 후속 hop·wire·로그가 같은 id를 재사용하고 flow 생성 책임을 context에 모았다. hop마다 id가 달라 실패하던 message-flow continuation 게이트가 통과한다. 커밋 `196559482`.
- [x] **IMP-ND-12** (결함) — tracing이 `off`인데도 **wire에 flow id를 생성한다**
  - 근거: 새 flow 생성은 diagnostics mode가 활성일 때만 수행하고 inbound ambient flow 보존과 분리했다. off host가 flow field를 만들던 message-flow 게이트가 통과한다. 커밋 `afc26ae07`.
- [x] **IMP-ND-13** (결함) — 모니터링 dispatcher가 예외 시 `continue`가 아니라 **`return`**한다
  - 근거: runtime event publisher가 handler 예외를 해당 handler에 격리하고 다음 handler dispatch를 계속한다. 첫 예외 뒤 전달이 중단되어 실패하던 monitoring-runtime 게이트가 통과한다. 커밋 `fcdc9a98c`.
- [x] **IMP-ND-14** (결함) — 샘플링이 **flow 단위가 아니라 이벤트 단위**다
  - 근거: sampling 결정을 flow id에서 한 번 계산해 같은 flow의 모든 event가 함께 유지되거나 제외되게 했다. 한 flow가 섞여 기록되던 message-flow sampling 게이트가 통과한다. 커밋 `1d2bcd789`.
- [x] **IMP-ND-15** (미구현) — Entry Spot이 `spot.count`/`created`/`closed`에 **잡히지 않는다**
  - 근거: Entry Spot activation·close를 공통 `spot-lifecycle-metrics` 경계로 연결해 일반 Spot과 같은 count/counter 정책을 사용한다. Entry metric 부재로 실패하던 runtime-metrics 게이트가 통과하고 중복 계측 책임도 제거했다. 커밋 `ec5002070`, `8751f0e19`.
- [x] **IMP-ND-16** (결함) — handler 없는 `server`/`subscriber` 역할이 startup을 통과하고 **소켓을 아예 bind하지 않는다**
  - 근거: receiver 역할의 handler 존재 검증을 registration validator로 모아 bind 없는 성공을 startup에서 거부한다. handlerless server/subscriber 게이트가 실패에서 통과로 바뀌었다. 커밋 `664d45650`.
- [x] **IMP-ND-17** (결함) — channel 종류가 **배타적이지 않고**, 같은 이름을 두 번 등록하면 **조용히 병합**된다
  - 근거: channel 이름별 kind 배타성과 중복 등록 거부를 builder가 일관되게 적용한다. 조용히 병합되던 Nest registration 게이트가 명시적 오류를 확인하며 통과한다. 커밋 `cc380c128`.
- [x] **IMP-ND-18** (결함) — 수동 endpoint가 그 역할의 자동 연결 reconcile을 **끄지 않는다**
  - 근거: 수동 endpoint 역할은 lookup만 유지하고 auto-connect reconcile 대상에서 제외하도록 lifecycle 정책을 한곳에 뒀다. 이중 연결을 잡는 location auto-connect/host 게이트가 통과한다. 커밋 `7b715d30f`.
- [x] **IMP-ND-19** (결함) — SPOT timer 등록 검증이 **startup이 아니라 spot 활성화 시점**
  - 근거: timer 계약 검증을 별도 registration validator로 옮겨 잘못된 주기를 startup에서 거부한다. 활성화 때까지 실패가 미뤄지던 startup-validation 게이트가 통과한다. 커밋 `b67ceb5e0`.
- [x] **IMP-ND-20** (결함) — `fanout.received`가 등록되지 않은 topic까지 라벨로 단다(`.NET` IMP-DN-08과 동형)
  - 근거: fanout receive metric에서 동적 topic label을 제거하고 닫힌 label 집합만 계측 경계가 만든다. 미등록 topic이 label로 노출돼 실패하던 runtime-metrics 게이트가 통과한다. 커밋 `e1b834eb9`.
- [ ] **IMP-TS-01** (결함) — **TypeScript connector**: 안 읽은 backlog가 쌓이면 `FrameTooLarge`로 **세션을 끊는다**
- [ ] **IMP-TS-02** (결함) — **TypeScript connector**: handler 없는 수신 메시지를 **버려서** `waitFor`가 이미 도착한 메시지를 못 받는다

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-ND-11** | [53 §2.1·§6](../server/53-flow-correlation.ko.md): **진입점에서 한 번만 생성**한다. 홉마다 재생성하지 않는다 | `flow-context.ts:12-14` — ambient flow가 없으면 **매번 새 id를 만들고 저장하지 않는다.** 그래서 `channel-envelope.ts:69`이 header에 **A**를 싣고, `message-flow.ts:127`은 `sent` 로그에 **B**를 찍고, `reply_received`는 **C**를 찍는다. ⇒ 서버는 `flow=A`로 남기는데 Node 클라이언트는 `flow=B`로 남긴다. **`grep flow=A`가 호출자 자신의 로그를 못 찾는다** — 이게 `53`이 존재하는 이유인데 |
| **IMP-ND-12** | [53 §2.2·§6](../server/53-flow-correlation.ko.md): tracing이 `Off`면 **새 id를 만들지 않는다** | `channel-envelope.ts:69,94`·`stream-frame-factory.ts:74` — 모드 검사 **없이** 항상 생성한다. `.NET`·C++은 게이트한다 |
| **IMP-ND-13** | [50 §3.2](../server/50-runtime-monitoring.ko.md): **한 handler가 예외를 던져도 다음 handler를 계속 실행한다.** monitoring loop를 깨지 않는다 | `runtime/diagnostics/index.ts:63-71` — `catch { console.error(...); return; }`. 게다가 handler를 **타입 구분 없이 한 리스트**에 등록해 모든 이벤트를 모든 handler에 보낸다. ⇒ 예상 못 한 이벤트 타입에 **처음 터지는 handler 하나가 그 뒤 전부를 영구히 굶긴다** |
| **IMP-ND-14** | [53 §5](../server/53-flow-correlation.ko.md): **flow 단위 일관 샘플링** — `flow_id` 해시로 결정해 한 흐름은 전부 남거나 전부 빠진다 | `message-flow.ts:172-183` — `sampleCounter % stride`. ⇒ `sample_rate=0.1`이면 한 흐름의 **10%만 무작위로** 남아 **어떤 흐름도 끝까지 추적되지 않는다.** `53 §5`가 막으려던 바로 그 실패다 |
| **IMP-ND-15** | [51 §4.2](../server/51-runtime-metrics.ko.md): `kind` 라벨은 `entry`/`user`로 나뉜다 | `spot-activation-registry.ts:104-105,132-133` — `{kind:'user'}` **하드코딩**. Entry Spot emit 지점이 없다. ⇒ `spot.count{kind="entry"}`가 **존재하지 않아** Entry Spot 큐 적체(매치메이킹 병목 신호)를 볼 수 없다 |
| **IMP-ND-16** | [11 §4](../server/11-channel-messaging.ko.md): server에 request/send handler 없음, subscriber에 publish handler 없음은 **설정 오류**. **모든 설정 오류는 host 시작 전에 실패한다** | `RegistrationValidators.ts:205-216` — **반대 방향만** 검사한다(handler ⇒ capability). 그리고 `channel-runtime-lifecycle.ts:174-181`이 handler 0개면 `continue`해서 **ROUTER를 아예 만들지도 bind하지도 않는다.** ⇒ handler를 다른 이름으로 묶는 오타 하나에 host가 **healthy로 기동하고 `:5001`에 아무것도 안 붙는다.** 로그도 metric도 없다. **갭 문서 §4.13이 이 행을 해소했다고 적고 있는데, 아니다** |
| **IMP-ND-17** | [10 §4](../server/10-channel-topology.ko.md): channel 종류는 **배타적**이다. 같은 이름을 두 번 등록하는 것도 **설정 오류** | `RegistrationBuilders.ts:154-162,192-195` — `channels[name] ??= {}`. 두 번 등록하면 **병합**되고, client/server + fanout을 같은 이름에 걸면 **네 역할이 다 켜진 채** 검증을 통과한다. `addSpotMesh`는 중복을 거부하므로 **패턴은 이미 알고 있었다** |
| **IMP-ND-18** | [10 §5.2](../server/10-channel-topology.ko.md) | `channel-autoconnect.ts:95-165` — 수동 endpoint가 있어도 reconcile 루프를 만든다. SPOT 역할은 이 규칙을 **지키므로**(`spot-node-autoconnect.ts:109-150`) **두 표면이 서로 어긋난다.** Java(IMP-JV-16)와 같은 결함 |
| **IMP-ND-19** | [25 §4.1](../server/25-stage-wrapper-on-spot.ko.md) | `spot-timer.ts:299-320` — 검증이 활성화 시점의 `add()`에만 있다. ⇒ `periodMs: 0`이 healthy로 기동하고 **모든 방 생성이 실패**한다 |
| **IMP-ND-20** | [51 §5](../server/51-runtime-metrics.ko.md) | `channel-dispatchers.ts:239` — handler 조회(:249) **앞에서** topic 라벨을 붙인다. `.NET` IMP-DN-08과 동형 |
| **IMP-TS-01** | [32 §4.7·§10.1](../stream-connector/32-stream-connector.ko.md): 한도는 **payload 바이트에만**. 큐가 가득 차면 새 메시지를 **버리고 `ReceivedMessageDropped`를 보고**한다(한도 = 1024 **메시지**) | `BrowserWebSocketConnection.ts:119-130` — **누적 미읽음 바이트**를 per-payload 한도와 비교해 `FrameTooLarge`를 던지고 **WebSocket을 끊는다.** ⇒ 기본 `Manual` 모드에서 게임 루프가 pump 사이에 있는 동안 서버가 2KB 프레임 40개를 밀면 backlog 80KB > 64KB → **연결이 끊긴다** |
| **IMP-TS-02** | [32 §10.1](../stream-connector/32-stream-connector.ko.md): `Send` packet은 handler나 **대기 표면(`waitFor`)으로 넘어가기 전까지 수신 큐에 머문다** | `packages/stream-connector/src/Runtime/ZlinkStreamReceivedMessages.ts:38-44,58-64` — `enqueue`가 큐에 넣고 곧바로 `scheduleDrain()`하며, `drain()`은 `const handlers = this.handlers.get(message.name); if (handlers === undefined) continue;`로 **버린다.** `waitFor`는 호출 시점에야 handler를 등록한다. ⇒ 세션 bind 직후 서버가 민 `GameStarted`를 client가 `waitFor`로 기다리면 **이미 버려져서 5초 뒤 timeout**된다. `.NET`은 독립 unread history를 유지해 만족한다. **이 drop이 SMP-ND-05(TicTacToe의 self-join negative 단언)를 구조적으로 무의미하게 만드는 원인이기도 하다** |

## 라운드 3 (2026-07-14) — 근거 없는 표면 · 조용한 no-op · 경합

**export 집합 자체는 깨끗하다** — framework 268/268, nestjs 66/66이 카탈로그와 일치한다.
**문제는 다른 데 있다.**

### 체크리스트

- [x] **IMP-ND-21** (결함) — connector 패키지가 **raw header bytes API를 root export**한다
  - 근거: raw frame/header codec을 connector public root에서 제거하고 내부 protocol helper로만 사용해 wire 결정을 숨겼다. root export가 남으면 실패하는 contract-surface 게이트가 통과한다. 커밋 `e38e811fd`.
- [x] **IMP-ND-22** (결함) — nestjs의 배포된 `.d.ts`가 **선언되지 않은 subpath를 import**해 내부 등록 레코드를 앱 타입 그래프로 끌고 온다
  - 근거: Nest 공개 계약 타입을 패키지 내부의 안정된 contracts 경계로 분리해 선언되지 않은 framework subpath 의존을 제거했다. package 선언 검사가 실패하던 build/type gate가 통과한다. 커밋 `c09ccdf3d`.
- [x] **IMP-ND-23** (결함) — payload decode 실패를 **조용히 문자열로 바꾼다.** actor 경로에서 `PayloadDecodeFailed`가 **도달 불가**
  - 근거: actor payload codec이 잘못된 JSON을 fallback 문자열로 바꾸지 않고 `PayloadDecodeFailed`로 분류하며 dispatcher는 handler를 호출하지 않는다. malformed payload 게이트가 통과한다. 커밋 `6bf0df118`.
- [x] **IMP-ND-24** (결함) — `ZLinkWorkerOptions.minThreads`/`idleTimeoutMs`가 **조용한 no-op**
  - 근거: Node runtime이 적용하지 않는 두 option을 public surface와 validator에서 제거해 효과 없는 설정을 없앴다. inert option 노출을 금지하는 contract-surface gate가 통과한다. 커밋 `79e8b1681`.
- [x] **IMP-ND-25** (결함) — `includeNativeDiagnostics`를 **읽는 곳이 없다**
  - 근거: 효과 없는 public option을 제거하고 진단 정책을 실제 message-flow 설정만 소유하게 해 얕은 표면을 줄였다. option 존재를 금지하는 contract/message-flow 게이트가 통과한다. 커밋 `43d9e7029`.
- [x] **IMP-ND-26** (결함) — **actor가 든 spot을 닫을 수 있다** (`.NET` IMP-DN-17과 동형)
  - 근거: spot close admission과 actor membership 변경을 activation registry에서 직렬화해 검사·종료 경합을 없앴다. actor가 남은 spot close 경합 게이트가 통과한다. 커밋 `67aeaf1ba`.
- [x] **IMP-ND-27** (결함) — 중복 `destroyActor`가 **파괴되기 전에 성공을 반환**하고, 실패한 destroy는 **영구히 재시도 불가**
  - 근거: 동시 destroy 호출이 하나의 teardown completion을 공유하고 실패한 cleanup은 재시도 가능하게 actor runtime state에 캡슐화했다. 조기 성공·영구 실패 게이트가 통과한다. 커밋 `1bf9b83b5`.
- [x] **IMP-ND-28** (결함) — 첫 `GetOrCreate` 호출자의 취소가 **다른 호출자 전부를 실패**시킨다
  - 근거: 공유 actor 생성 promise와 개별 호출자의 abort 대기를 분리해 한 호출자의 취소가 생성 작업으로 전파되지 않게 했다. 동시 `GetOrCreate` 취소 게이트가 통과한다. 커밋 `f2de94a4c`.
- [x] **IMP-ND-29** (결함) — 서버가 `correlation_id`를 `request_seq`로 **날조한다**
  - 근거: correlation이 없는 stream 요청은 없는 값으로 유지하고 transport sequence를 대체값으로 노출하지 않는다. absent correlation 회귀 게이트가 실패에서 통과로 바뀌었다. 커밋 `deae68548`.
- [x] **IMP-ND-30** (결함) — `listPageSize`를 **읽는 곳이 없다.** 내부 기본값이 **무한**이다
  - 근거: location runtime이 호출자가 page size를 생략했을 때 등록된 `listPageSize`를 적용하도록 paging 결정을 한곳에 모았다. 무한 기본값을 잡는 location-runtime 게이트가 통과한다. 커밋 `cbc18227e`.
- [x] **IMP-ND-31** (미구현) — `storeFailureGrace`를 **읽는 곳이 없다**
  - 근거: auto-connect reconciler가 마지막 정상 target을 grace 안에서만 재사용하도록 option을 실제 정책에 연결했다. grace 안/밖 동작이 같아 실패하던 location-autoconnect 게이트가 통과한다. 커밋 `c3ce36a31`.

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-ND-21** | [32 §5·§4.2](../stream-connector/32-stream-connector.ko.md): **임의 header bytes를 다루는 API를 공개 표면에 두지 않는다.** application code는 이 header를 직접 만들거나 수정하지 않는다 | `stream-connector/src/index.ts:2-3`이 `ZlinkStreamFrameCodec`·`ZlinkStreamHeaderCodec`을 export한다. `ZlinkStreamFrameCodec.ts:6`의 `encode(header, payload, maxPayloadSize = 64*1024)`는 한도가 **호출자가 덮어쓸 수 있는 기본 인자**라, 이 경로는 connector에 설정된 `maxSendPayloadSize`를 **통째로 우회한다.** 앱이 `kind=Response`·`requestSeq`를 **위조**하거나 압축하지 않고 압축 플래그만 켤 수 있다 |
| **IMP-ND-22** | [00 §5](../00-public-contract-governance.ko.md)·[node 01](../server/languages/node/01-system-structure.ko.md): 내부 등록 레코드는 공개 표면이 아니다 | `nestjs/dist/contracts.d.ts:2`(외 `.d.ts` 10개)가 `'@zlink-systems/framework/nest-integration'`에서 `ZLinkFrameworkRegistrationOptions` 등을 import하는데, `framework/package.json`의 `exports`는 **`"."` 하나뿐**이다. 그 이름들은 268개 카탈로그에 **없다.** `framework-loader.ts:7-13`이 `createRequire`+`path.join`으로 **exports map을 의도적으로 우회**하고, `nestjs/package.json`엔 `exports`도 `files`도 없어 `dist/` 전체가 deep-import 가능하다. ⇒ `node16`/`bundler` 해석에선 소비자가 **타입 체크조차 못 하고**, 레거시 해석에선 **framework 내부 선언을 조용히 흡수**한다. **갭 문서 §4.2가 이 항목을 "해소"로 적고 있는데, 아니다** |
| **IMP-ND-23** | [05 §2.4.3](../05-framework-api.ko.md): decode 실패는 `PayloadDecodeFailed` + drop + metric | `runtime/messaging/payload-codec.ts:61-66` — `try { JSON.parse(text) } catch { return text as T; }`. serializer registry가 비어 있는 **기본 경로**가 이거다. `spot-actor-packet-dispatch.ts:211-227`이 `PayloadDecodeFailed`를 보고하려고 `try/catch`로 감싸는데, **호출된 쪽이 이미 파싱 오류를 삼켰으므로 그 catch는 영영 안 튄다.** channel/route 경로는 제대로 던진다(`channel-envelope.ts:199-207`) — **두 dispatch 표면이 서로 어긋난다.** ⇒ 손상되거나 codec이 틀린 actor packet이 **JS `string`을 DTO 타입으로 캐스팅한 채 handler에 넘어간다** |
| **IMP-ND-24** | — | `RegistrationTypes.ts:64-69`에 선언, `RegistrationValidators.ts:59,61`에서 검증, `runtime/workers/index.ts:20,22`에서 resolve — 그런데 스케줄러는 `maxQueueLength`와 `maxThreads`만 읽는다 |
| **IMP-ND-25** | — | 쓰는 곳 3, 읽는 곳 0. **갭 문서 §4.1이 이 행을 닫힌 것으로 적고 있는데, 아니다** |
| **IMP-ND-26** | [21 §close](../server/21-spot-node.ko.md) | `spot-activation-registry.ts:118-121`이 `canClose()`를 확인하고 close를 등록하는데, **close 본문은 spot의 직렬 큐에 post**된다(`spot-activation.ts:244-255`). 그 줄에 **이미 큐잉된 join이 먼저 실행**되어 `joinedActors.size === 1`이 되고, 뒤이어 도는 close 작업은 **`canClose()`를 다시 확인하지 않는다.** ⇒ timer·native spot·location row를 **무조건 정리한다** |
| **IMP-ND-27** | [22 §destroy](../server/22-actor-model.ko.md): 같은 actor instance에 대한 **중복 destroy는 성공으로 끝난다** — 파괴가 **끝난 뒤** 성공이지, 그 전이 아니다 | `runtime/actors/index.ts:255-258` — `beginDestroy()`가 `undefined`를 반환하면(=이미 destroying) **즉시 성공으로 resolve**한다. ⇒ A가 native destroy를 await하는 동안 B가 destroy를 부르면 **B는 곧바로 성공**을 받는다. B의 호출자가 "없어졌구나" 하고 같은 id로 `createActor`를 하면 **같은 state 객체를 돌려받고**, 뒤늦게 끝난 A가 `states.delete()`를 실행해 **방금 만든 actor의 상태를 지운다.** 게다가 `releaseActor`가 던지면 `resetDestroying()`만 하고 `nativeActorRef`는 **남겨 두어** 재시도가 이미 파괴된 native ref로 또 부른다 → **destroy가 영영 성공할 수 없고** location row가 lease 만료까지 샌다. `.NET`은 두 번째 호출자가 **공유 teardown에 합류해 실제 완료를 기다린다** |
| **IMP-ND-28** | [21](../server/21-spot-node.ko.md)·[54 §6](../server/54-graceful-drain-handoff.ko.md) | `spot-activation-registry.ts:183-207` — 소유자의 `create` 클로저가 **첫 호출자의 `signal`**을 캡처하고, 대기자들은 `pending.ready`의 거부를 물려받는다. `.NET` IMP-DN-18과 **같은 경합** |
| **IMP-ND-29** | [52 §9](../server/52-message-flow-tracing.ko.md) | `stream-session-runtime.ts:200,236,281` — `?? decodedHeader.requestSeq?.toString()` |
| **IMP-ND-30** | [40 §3·§8.2](../server/40-location-runtime.ko.md) | `contracts/Locations/Options.ts:5,13` — 읽는 곳 0. `framework-locations-redis/src/store.ts:369-373`이 `pageSize <= 0`이면 `SMEMBERS`로 **전체**를 읽는다 |
| **IMP-ND-31** | [40 §6.1](../server/40-location-runtime.ko.md) | `contracts/Locations/Options.ts:6,14` — **어느 패키지에도** 읽는 곳이 없다 |

## 교차 언어 결함 — 이 언어에서 무엇을 고치나

**교차 언어 결함이라도 고치는 일은 이 언어에서 한다.** [갭 인덱스](../90-implementation-gap.ko.md) §15.3이
**왜**(계약과 결정)를 소유하고, 아래 표가 **무엇을**(이 언어의 작업)을 소유한다.

| 교차 결함 | 무엇이 깨지나 | 이 언어의 작업 |
|---|---|---|
| **IMP-X2** | location event source 결측 | §12.11 |
| **IMP-X3** | startup validation이 설정 오류를 통과 | IMP-ND-10 · IMP-ND-16 · IMP-ND-17 · IMP-ND-19 |
| **IMP-X5** | message-flow 관측자가 로그 모드에 묶여 침묵 | **이 언어 전용 ID 없음** — `runtime/diagnostics/message-flow.ts:116-142`. `flowIfEnabled()`(:93-98) 때문에 호출부가 **이벤트를 만들지도 않는다**. [52 §3](../server/52-message-flow-tracing.ko.md)대로 관측자는 모드와 무관하게 발화해야 한다 |
| **IMP-X6** | `origin=lifecycle`을 생성하지 않는다 | **이 언어 전용 ID 없음** — `flow-context.ts:12`의 `currentOrCreateFlow()`가 항상 `Application`으로 만든다. enum은 `contracts/Eventing/Contracts.ts:52`에 있고 디코더만 쓴다 |
| **IMP-X8** | 수동 endpoint가 auto-reconcile을 끄지 않는다 | IMP-ND-18 |
| **IMP-X10** | SPOT timer 등록 검증이 startup이 아니다 | IMP-ND-19 |
| **IMP-X11** | `fanout.received` 미등록 topic 라벨 | IMP-ND-20 |
| **IMP-X12** | actor가 든 spot을 닫을 수 있다 (경합) | IMP-ND-26 |
| **IMP-X13** | `correlation_id` 날조 | IMP-ND-29 |
| **IMP-X14** | `listPageSize`가 죽어 있다 | IMP-ND-30 |
| **IMP-X15** | `storeFailureGrace`가 죽어 있다 | IMP-ND-31 |
| **IMP-X16** | `includeNativeDiagnostics`가 죽어 있다 | IMP-ND-25 |
| **IMP-X17** | `GetOrCreate` 취소가 다른 호출자를 실패시킨다 | IMP-ND-28 |
| **IMP-X18** | Redis fixture 불일치 | 빈 컬렉션을 `{}`/`[]`로 낸다 |

## 이전 기록 — 기준선 대조 (2026-07-13 이전)

> **이 절은 과거 기록이다.** 당시 계약 기준으로 확인한 내용이며, 그 뒤 계약이 바뀐 항목이 있다
> (특히 실행 terminator — [갭 인덱스 §12.21](../90-implementation-gap.ko.md) 참조).
> **현재 작업 목록은 이 문서 위쪽의 체크리스트다.**

### 4.1 dispatch options

언어별 스펙은 dispatch 최적화 전략을 runtime 내부에 두고 message kind별 unhandled
policy, diagnostics와 message-flow observer만 정의한다. 현재 `ZLinkDispatchOptions`는
단일 `mode`, 단일 `unhandled.action`과 제한된 diagnostics를 제공한다.

2026-07-13 구현에서 다음 항목을 정식 계약에 맞췄다.

```text
public dispatch mode 제거 완료
request/send/publish별 unhandled policy
ReplyError
LogAndDrop
Drop
includeNativeDiagnostics
localRid
peerRid
socketRole
```

현재 계약과 구현 위치는
`packages/framework/src/contracts/Dispatch/ZLinkDispatchOptions.ts`다. Config 8
`AutomaticTurnDispatch`의 전체 Node.js runner도 통과했다 — **구 계약 기준 기록**이며, 그 config는
[config-8 실행 turn과 terminator](../../common/e2e/config-8-execution-turn.ko.md)(`TD-*`)로 대체됐다.

### 4.2 public export 경계

2026-07-13 구현에서 package root와 공개 `contracts/Configuration` export가 framework
내부 등록 record, normalize/validate helper와 default builder를 더 이상 내보내지 않도록
정리했다. 다음 종류의 이름은 package root에서 제거했다.

```text
createFrameworkRegistration
createFrameworkOptions
RouteChannelInternalState
MutableCodecRegistryOptions
DefaultDispatchOptionsBuilder
내부 registration record
내부 normalize/validate helper
```

공개 options, builder와 사용자가 구현하는 extension point만 package root에 남겼다. NestJS adapter는
framework package 내부의 integration bridge를 빌드 시점에 사용하지만, 이 bridge는 package export에
등록된 public subpath가 아니다. 따라서 application public surface에는 내부 등록 record와 구현 타입이
나타나지 않는다. source export test와 실제 `.tgz` consumer test가 이 경계를 검증한다.

### 4.3 typed session handler

typed payload handler와 serializer registry 연결을 구현했다. application handler에서 raw
`ZLinkMessage`를 받는 escape hatch는 제거했으며, bound session도 packet 타입으로 routing한다.

### 4.4 one-way actor와 bound session

actor와 bound session을 포함한 one-way submit을 `void submit()`으로 통일했다. 취소 신호는
actor 이동이나 session bind처럼 완료를 기다리는 장기 작업에만 남겼다.

### 4.5 interface catalog와 export 목록

언어별 interface catalog는 application public 타입의 목표 시그니처를 모두 고정한다.
location interface의 `I` prefix를 제거했다. package root의 내부 registration 타입도 제거했고,
companion NestJS package의 참조는 application export와 분리된 integration subpath로 옮겼다.

### 4.6 Actor membership와 join 결과

`isJoined`와 중복 join call을 제거하고 `spotRid`를 membership 상태 기준으로 고정했다. join
결과는 `status` discriminated union이며 승인 variant만 필수 actor ref를 가진다.

### 4.7 관측과 종료

OpenTelemetry meter `zlink.framework`, UUIDv7 flow correlation, typed graceful drain과
`session-closing` 제어 프레임을 구현했다. Node.js Config 11 `ObservabilityOps` runner는
OBS-A1~C5 evidence와 함께 통과했다. `Bingo.Ts`도 flow, metrics, drain 설정을 사용하는
sample smoke를 통과했다.

### 4.8 typed packet identity와 최종 상태

channel, route, Spot과 fanout packet identity는 `@ZLinkPacket`이 해당 class에 직접 기록한
metadata를 우선 사용하고, metadata가 없으면 생성자 이름을 사용한다. payload의
`packetName()` method와 call builder의 packet name override는 제거했다. decorator가 없는
subclass는 부모 class의 metadata를 상속하지 않는다. Stream Connector frame의 명시적 packet
name은 별도 connector 계약이므로 이 규칙의 제거 대상이 아니다.

### 4.9 stream disconnect routing id

SupportChat의 즉시 재연결 검증에서 기존 연결의 disconnect 처리와 새 actor binding이 겹치는
경합을 발견했다. Node.js framework는 같은 actor의 disconnect와 새 binding을 직렬화하고, 이전
binding token이 새 binding을 지우지 못하도록 수정했다. Stream Connector도 `close()`가 TCP 종료를
완료한 뒤 반환하도록 수정했다.

**충족.** core STREAM session은 disconnect monitor event에 peer routing id를 기록한다. Node addon은
이 값을 public `MonitorEvent.routingId`로 전달하고, framework adapter는 같은 값을 session runtime에
넘긴다. 따라서 같은 endpoint에 여러 session이 있어도 종료된 session 하나만 선택해 disconnect
callback과 binding 정리를 실행한다. routing id가 없는 이전 event를 endpoint만으로 추측하지 않는
방어 동작은 유지한다.

검증은 실제 STREAM peer를 연결·종료해 addon event의 routing id가 비어 있지 않은지 확인하고,
framework의 다중 session 회귀 검사에서 지정된 session만 종료되는지 확인했다. sample 재검토는
별도 G5 gate에서 계속 추적한다.

### 4.10 Stream Connector browser-only package와 검증

`@zlink-systems/stream-connector` package root를 플랫폼 `WebSocket` 기반 browser ESM으로 교체했다.
Node TCP/TLS, 직접 WebSocket 구현, Node flow context와 `/browser` subpath를 제거했다. public
transport는 `WebSocket`과 `WebSocketSecure`만 남으며 `tcp://`와 `tls://`는 connector를 만들 때
`ConfigurationError`로 거부한다.

브라우저 비동기 flow는 [flow correlation §4.4](../server/53-flow-correlation.ko.md)의 명시적 계약을 따른다.
connector instance에는 현재 inbound flow를 저장하지 않는다. 관련 outbound는 call builder의
`flowFrom(message)`로 flow 쌍을 전달하고, 표시하지 않은 outbound는 새 application flow를 만든다.
fake WebSocket contract test에서 관련 outbound의 보존과 관련 없는 callback의 격리를 확인했다.

MessagePack과 Protobuf package root도 browser-safe payload codec만 내보내고 server serializer 등록은
`./framework` subpath로 분리했다. `stream-wire`는 같은 source의 ESM/CommonJS 산출물을 제공한다.
Bingo는 생성된 정적 encode/decode와 결정성 검사를 사용하며 runtime filesystem lookup과 `protoPath`
option을 사용하지 않는다.

실제 Chromium은 `ws`와 `wss` request/reply·push, 명시적 flow 전달과 관련 없는 callback 격리,
reconnect, drain, close reason을 검증한다. 브라우저 기본 신뢰 설정에서는 자체 서명 인증서를
거부하며, 테스트가 이를 우회하는 connector option은 없다. `close()`는 WebSocket의 실제 close
event가 올 때까지 완료되지 않는지 fake WebSocket 회귀 검사에서도 확인한다.

Node ambient type 없는 browser declaration/build, browser bundle의 Node module 부재, codec graph
분리, Bingo 생성 codec 결정성, npm tarball browser/CommonJS consumer도 통과했다. 다섯 STREAM
sample client와 네 framework E2E client를 Chromium으로 실행했고, Browser TypeScript connector에서
`.NET`과 C++ STREAM server로 보내는 cross-language smoke도 통과했다. 따라서 이 항목에 남은
public contract gap은 없다.

### 4.11 dispatch 실패 수준과 `FailCaller`

2026-07-13 재대조에서 두 가지 구현 차이를 추가로 확인하고 해소했다.

첫째, channel dispatch error reporter가 원인과 message kind에 관계없이 모든 실패를 Error로
기록했다. publish handler가 없으면 unhandled policy가 Warning을 한 번 더 기록해 중복 로그도
남았다. reporter가 handler 예외는 Error, handler 없음·decode 실패·invalid frame은 send는
Warning, publish는 Debug로 내부 결정하도록 수정했다. 공개 `ZLinkUnhandledDispatchOptions`에서
호출자가 이 계약을 바꿀 수 있던 `sendLogLevel`과 `publishLogLevel`도 제거했다.

둘째, 공통 framework API가 요구하는 `FailCaller`가 Node.js enum과 local dispatch 경로에
없었다. local Spot request와 같은 reply frame 없는 호출은 이제 caller의 Promise를 실패시키고
observer event에 `FailCaller`를 기록한다. transport reply frame을 만들 수 있는 request는
기존처럼 `ReplyError`를 사용한다.

두 항목은 contract test에서 로그 호출 횟수와 수준, local caller의 Promise 실패 및 observer
event를 함께 검증한다.

### 4.12 actor 소유권 변경 중 session relay

2026-07-13 sample 반복 검증에서 actor가 다른 Spot node로 이동하는 동안 session binding의
`ActorRef`를 갱신하는 짧은 구간에 다음 client request가 들어오면 `ActorSessionNotBound`로
실패하는 경합을 확인했다. binding 갱신은 actor별 lifecycle coordinator를 사용했지만 session
relay는 같은 직렬화 경로에 참여하지 않아, 이전 route를 제거한 뒤 새 route를 등록하기 전의
중간 상태를 관찰할 수 있었다.

session relay도 같은 actor별 lifecycle coordinator에서 실행하도록 수정했다. 이제 소유권 갱신
중 들어온 relay는 갱신 완료 뒤 새 `ActorRef`와 binding route를 사용한다. contract test는 binding
갱신을 의도적으로 중단한 동안 relay가 실패하거나 먼저 실행되지 않는지 검증한다. Bingo sample은
서로 다른 play node 사이 actor 이동 직후 client request를 반복 실행해 이 경합의 실제 경로도
검증한다.

### 4.13 startup validation 누락 (해소)

2026-07-13에 [channel 메시징 §4](../server/11-channel-messaging.ko.md)와
[SPOT 메시징 §8](../server/20-spot-messaging.ko.md)의 각 행을 Node.js registration validator에 직접
대입해 다음 누락을 확인했고, 같은 날 구현과 회귀 검사를 추가해 모두 해소했다.

- server에 request/send handler가 하나도 없어도 startup이 성공한다.
- subscriber에 publish handler가 하나도 없어도 startup이 성공한다.
- router와 pub/sub 역할을 모두 사용하지 않는 SpotNode가 허용된다.
- actor factory를 등록한 SpotNode에 router 역할이 없어도 허용된다.
- router 또는 pub/sub 역할을 사용하면서 bind endpoint를 지정하지 않아도 허용된다.
- location store의 자동 연결과 같은 SPOT 수신 역할의 수동 peer endpoint를 함께 지정하면
  역할별 연결 정책이 필요하다.

해소한 항목은 설정 오류를 첫 message 호출이나 연결 timeout까지 늦추므로 application 개발자가
runtime 내부 연결 조건과 구동 순서를 알아야 하는 문제로 이어진다. Node.js는 registration과
NestJS handler discovery가 끝난 뒤, socket을 만들기 전에 위 구성을
`ZLinkConfigurationException`으로 거부한다. 회귀 검사는 잘못된 구성이 startup 전에 실패하는지
검증한다.

마지막 항목은 공통 channel topology §5.2의 역할별 manual 연결 규칙을 runtime에 적용해 해소했다.
router에 manual peer가 있으면 router auto reconcile만 수행하지 않고, pub/sub에 manual endpoint가
있으면 pub/sub auto reconcile만 수행하지 않는다. location store와 actor 위치 조회는 그대로
유지한다. 따라서 TicTacToe는 sample 전용 wrapper 없이 수동 SPOT peer와 원격 actor 위치 조회를
함께 사용할 수 있다.

## 라운드 4 (2026-07-14) — 샘플 · E2E

**여기서 "가짜 통과"가 나왔다.** 실패할 수 없는 검증이다.

### 체크리스트

- [ ] **E2E-ND-01** (**가짜 통과**) — Config 11에 **e2e 앱이 없고 시나리오를 `echo`로 통과시킨다**
- [x] **E2E-ND-02** (**가짜 통과**) — probe 서버가 **클라이언트가 검사할 값을 리터럴로 만들어 낸다**
  - 근거: probe가 합성하던 topology 값을 제거하고 실제 consumer app role의 public query와 resilience client가 같은 관측 경로를 사용하게 했다. 합성 probe 없이는 실패하던 topology/RL-A1 게이트가 통과한다. 커밋 `cc857d12a`, `31f56b068`.
- [x] **SMP-ND-01** (미구현) — Bingo의 정본 `yield` 왕복이 **계약·서버·클라이언트 게이트 어디에도 없다**
  - 근거: Bingo record 흐름에 `yield` 왕복과 client release marker를 추가하고 room domain과 handler 책임을 분리했다. 왕복이 없으면 실패하는 yield record 게이트가 통과한다. 커밋 `675dc2ff4`.
- [x] **SMP-ND-02** (결함) — **6개 샘플 전부가 framework session handler registry를 우회**한다
  - 근거: sample session packet dispatch를 framework handler registry로 옮겨 packet-name switch와 샘플별 우회를 제거했다. registry를 거치지 않으면 실패하는 sample session 게이트가 통과한다. 커밋 `a355f8d86`.
- [x] **SMP-ND-03** (결함) — DeliveryDispatch가 **문서가 명시적으로 금지한** route-mesh + node rid로 offer를 보낸다
  - 근거: courier offer를 location resolver가 제공하는 Spot handle 경로로 바꾸고 node rid·route mesh 지식을 샘플 업무 코드에서 제거했다. 금지 경로를 검출하는 DeliveryDispatch gate가 통과한다. 커밋 `203d28ac7`.
- [x] **SMP-ND-04** (결함) — TicTacToe가 **자체 Redis room-route 스키마**를 들고 있다
  - 근거: 샘플 전용 room-route store와 병렬 schema를 제거하고 framework location store를 유일한 routing 책임으로 사용한다. 자체 schema가 남으면 실패하는 TicTacToe location-store 게이트가 통과한다. 커밋 `53bf76b30`.
- [x] **SMP-ND-05** (결함) — TicTacToe의 "self-join notify 없음" 검사가 **25ms 창**이다
  - 근거: 짧은 무발생 sleep 대신 후속 관측 구간과 명시적 notify 수를 사용해 self-join 비오염을 검증한다. 25ms timing oracle을 금지하는 self-join 게이트가 통과한다. 커밋 `921b146a0`.
- [x] **E2E-ND-03** (결함) — Config 9·10에 **`Client/Scenarios/`가 없다**
  - 근거: Config 9의 7개와 Config 10의 20개 ID를 각각 `Client/Scenarios/` 한 파일로 분리하고 공통 HTTP·assertion·설정은 `Client/Support/`가 소유하게 해 두 거대 `main.ts`의 책임 혼합을 제거했다. 디렉터리와 27개 파일이 없어 실패하던 layout gate가 통과하고 두 client build·ToActor 7/7·SpotActorTransfer 전체 시나리오가 통과한다. 커밋 `92bdfbf4d`.
- [x] **E2E-ND-04** (결함) — `§2.1` settle 상수가 **어느 runner에도 없고** readiness가 최대 **60초**
  - 근거: 11개 runner의 readiness·settle budget을 runner-local 상수로 고정해 숨은 장기 대기를 제거했다. 60초 대기와 상수 누락을 잡는 local-wait gate가 실패에서 통과로 바뀌고 TA-A1·ST-A1도 통과했다. 커밋 `a22a2169f`.
- [x] **E2E-ND-05** (결함) — Redis 격리에 **탈출구**가 있다(`ZLINK_REDIS_E2E_ENDPOINT`)
  - 근거: 외부 Redis endpoint override를 제거하고 runner가 실행별 격리 instance를 소유하게 했다. 환경변수 탈출구가 남으면 실패하는 Redis isolation gate가 통과한다. 커밋 `ffc2009dc`.
- [x] **E2E-ND-06** (결함) — e2e 앱 코드가 **환경변수를 읽고 쓴다**(`§2.6`: 0개)
  - 근거: role 설정에서 읽은 rid를 `process.env`에 다시 쓰는 경로와 공유 evidence 모듈 위치를 환경 변수로 받는 경로를 제거했다. `EvidenceStore`도 숨은 환경 변수 fallback과 생성자 중복을 없애고 rid를 필수 입력으로 고정했다. 7개 위반 파일로 실패하던 config-file-only 게이트가 0건으로 통과하고 관련 6개 role 빌드와 실제 `SM-F6`·`MON-A1`이 통과했다. 커밋 `9057be562`.
- [ ] **E2E-ND-07** (결함) — e2e 클라이언트가 **HTTP client wrapper를 안 쓴다** — 전부 raw `fetch`
- [x] **E2E-ND-08** (결함) — 시나리오 파일 **138개 중 0개**에 머리말 주석이 없다
  - 근거: 모든 기존 scenario 파일 첫머리에 검증 의도를 적고 common 문서 heading과 대응시키는 단일 header gate를 추가해 설명 지식의 중복 drift를 막았다. 139개 누락으로 실패하던 gate가 139개 전부를 확인하며 통과했다. 커밋 `5bf207ab5`.
- [x] **E2E-ND-09** (결함) — 낡은 디렉토리 이름과 죽은 `dist/`
  - 근거: Git에 추적되지 않는 로컬 `dist/`, 현재 정본과 일치하는 `RegistryMessaging`·`AutomaticTurnDispatch` 이름, §2.2가 이름 일치까지 요구하지 않는 `DiscoveryRegistryHa`는 갭에서 제외했다. 실제 계약을 어긴 10개 runner의 `logs/`를 실행별 `log/`로 통일하고, 각 config에 흩어진 ignore 정책은 E2E 루트로 모았다. 10개 runner에서 실패하던 log-directory 게이트가 통과하고 실제 `TA-A1`도 새 경로에서 통과했다. 커밋 `faede5ca0`.
- [x] **E2E-ND-10** (결함) — `START_ORDER` 축이 config 2개에만 있다
  - 근거: Config 1·2·9 runner가 forward/reverse/fixed-seed shuffle을 같은 인자로 받고 기본 전체 sweep이 변형을 실제 실행한다. 누락 config·미실행 축으로 실패하던 start-order gate가 통과한다. 커밋 `8e34f6bb2`.
- [x] **E2E-ND-11** (결함) — SpotService `all`이 **문서에 없는 `SM-Q9`를 기본 게이트에 넣는다**
  - 근거: 문서 밖 보조 시나리오 `SM-Q9`를 기본 `all` 목록에서 분리해 정식 scenario suite가 계약 목록만 소유하게 했다. 기본 목록에 Q9가 있으면 실패하는 gate가 통과한다. 커밋 `6d2f74ab5`.
- [x] **E2E-ND-18** (결함) — `RM-B2`가 scale-in 동안 트래픽을 끊고 **남은 provider를 직접 호출한다**
  - 근거: 살아 있는 consumer 경로에서 scale-in 전후 연속 트래픽을 보내고 provider 선택은 앱 topology가 담당하게 했다. 직접 호출·트래픽 공백을 잡는 scale-in gate가 통과한다. 커밋 `fac1af0c6`.
- [x] **E2E-ND-19** (결함) — Config 1 negative가 **public error kind를 전혀 분류하지 않는다**
  - 근거: missing packet·잘못된 payload를 public framework error kind로 분류하고 channel pipeline의 변환을 공통 경계로 모았다. kind 단언이 없으면 실패하는 public-errors gate와 channel client gate가 통과한다. 커밋 `36f437217`.
- [x] **E2E-ND-20** (미구현) — `RC-A6`가 세 startup-invalid 축 중 **duplicate 하나만** 검증한다
  - 근거: duplicate·invalid role·invalid timer 세 startup 축을 별도 invalid host 설정으로 실행한다. 두 축이 빠져 실패하던 registration-codec invalid-axes gate가 통과한다. 커밋 `6eb18ed0d`.
- [x] **E2E-ND-21** (결함) — `RL-D1`은 fanout이 아니라 **평범한 request 120개**다
  - 근거: RL-D1을 실제 fanout publish/subscribe 부하와 consumer evidence로 바꾸고 load 관측 책임을 evidence store에 모았다. request만 보내면 실패하는 high-fanout gate가 통과한다. 커밋 `967d7d544`.
- [x] **E2E-ND-22** (미구현) — `RL-D4`가 `Error=5`와 `errorCode`/`errorMessage` wire를 검증하지 않는다
  - 근거: missing handler 응답의 wire kind `Error=5`와 code/message를 모두 단언하고 envelope encoding을 공통 구현에서 고쳤다. 필드 하나라도 없으면 실패하는 track-D gate가 통과한다. 커밋 `fab02f2df`.
- [x] **E2E-ND-23** (결함) — `RL-D5`가 수 분 soak가 아니라 **단발 Promise burst**다
  - 근거: mixed workload를 지속 시간 동안 pace하고 동적으로 바뀌는 provider evidence를 관측하도록 soak driver를 분리했다. 단발 burst·고정 provider로 실패하던 track-D gate와 RL-D5 실행이 통과한다. 커밋 `6dbf73bf9`, `c272ce908`, `9ea78fecb`.
- [x] **E2E-ND-24** (**가짜 통과**) — `SF-B2`가 신규 outbound connect를 만들지 않아 **죽은 `storeFailureGrace`를 잡지 못한다**
  - 근거: store 장애 중 새 peer를 게시해 신규 outbound connect가 grace 정책으로 차단되는 실제 조건을 만들었다. 새 dial을 시도하지 않던 gate가 실패한 뒤 SF-B2와 contract gate가 통과했다. 커밋 `16b06351a`.
- [x] **E2E-ND-25** (결함) — `SF-D1`·`SF-D2`가 store stop/restart 대신 **pause/unpause**만 한다
  - 근거: runner가 Redis를 실제 stop/restart하고 빈 store 재등록·auto-connect recovery를 framework 경계에서 처리한다. pause만으로는 통과하지 않는 restart-recovery/location gates와 SF-D1·D2가 통과한다. 커밋 `0a9c1f084`.
- [ ] **E2E-ND-26** (미구현) — `SF-C2`가 draining marker·drain deadline·정상 종료를 검증하지 않는다
- [x] **E2E-ND-27** (미구현) — `MON-A1`이 socket event의 **RemoteAddr·RoutingId를 단언하지 않는다**
  - 근거: MON-A1이 실제 socket event의 remote address와 routing id를 필수로 단언한다. identity 필드가 비어도 통과하던 monitoring socket gate가 실패에서 통과로 바뀌었다. 커밋 `e21abc645`.
- [x] **E2E-ND-28** (**가짜 통과**) — `MON-A2`가 provider 추가·종료를 일으키지 않고 **기존 startup event만 기다린다**
  - 근거: client가 managed service를 추가·종료해 topology 변화를 직접 만들고 service evidence와 before/after를 대조한다. startup event 재사용으로 실패하던 monitoring topology gate가 통과한다. 커밋 `5922af9aa`.
- [x] **E2E-ND-29** (미구현) — `MON-A4`가 failover 절반을 실행하지 않고 topology **payload 변화도 대조하지 않는다**
  - 근거: MON-A4가 기존 `svc-b`를 종료하고 같은 rid·다른 channel/Spot/HTTP endpoint의 replacement role을 시작해 old `disconnected`, replacement `connected`·`connectionReady`, `TopologyChanged` payload의 endpoint 교체를 대조한 뒤 drain/restore까지 검증한다. replacement 구조가 없어 실패하던 gate와 실제 `./run_e2e.sh MON-A4`가 통과한다. 커밋 `95bc98500`.
- ~~**E2E-ND-30** (미구현) — Config 2·9의 P0에 **route-mesh-absent × separated-deployment** 조합이 없다~~ — **갭 아님**
  - 근거: Config 2의 `SM-F6`은 multi-node 역할을 별도 프로세스로 실행하면서 `--spot-only true`로 RouteMesh를 등록하지 않고, Config 9는 Actor·Session·Caller 역할을 별도 프로세스로 실행하면서 SpotMesh만 등록한다. 누락으로 지목된 두 조합을 현재 runner에서 다시 실행한 결과 `./run_e2e.sh SM-F6`와 `./run_e2e.sh TA-A1`이 모두 통과했다. 최초 구현 커밋 `0caeb0ba8`, 재검증 커밋 `dfeb0e9f6`.
- [x] **E2E-ND-31** (**가짜 통과**) — actor ref의 `generation > 0`을 **어느 config도 단언하지 않는다**
  - 근거: SpotService와 ToActorMessaging이 실제 응답의 actor ref에서 양수 generation을 단언한다. 합성·0 generation으로 실패하던 concrete-actor-ref gate가 통과한다. 커밋 `7598155b8`.
- [x] **E2E-ND-32** (미구현) — `TA-A1`~`A3`가 bound-session snapshot과 **no-bind 비오염 negative**를 검증하지 않는다
  - 근거: TA-A1~A3가 bind 전후 actor snapshot과 no-bind 비오염을 server evidence로 대조한다. snapshot 단언이 없으면 실패하는 binding gate와 실제 TA 시나리오가 통과한다. 커밋 `3e7e37698`.
- [x] **E2E-ND-33** (미구현) — `TA-A4`가 actor destroy 뒤 `ActorRouteNotFound` 절반을 실행하지 않는다
  - 근거: TA-A4가 actor destroy 완료 뒤 같은 ref로 request해 public `ActorRouteNotFound`를 확인한다. destroy 후 요청이 없으면 실패하는 gate와 실제 TA-A4가 통과한다. 커밋 `82f52000d`.
- [ ] **E2E-ND-34** (결함) — `TA-B2`·`TA-B3`가 실제 owner 교체·route 단절 대신 **ActorRef 필드를 위조한다**
- [x] **E2E-ND-35** (결함) — Config 10이 spot 생성마다 500ms sleep해 **수렴 직후 첫 요청 결함을 가린다**
  - 근거: spot 생성 뒤 고정 500ms sleep을 제거하고 준비 상태 직후 첫 요청을 보낸다. sleep이 남으면 실패하는 first-request gate와 SpotActorTransfer 시나리오가 통과한다. 커밋 `c92360970`.

### 가장 무거운 둘

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-ND-01** | [e2e §2·§2.2·§2.4·§2.5](../../common/e2e/README.ko.md): 역할 서버 + 시나리오 ID당 클라이언트 파일 하나. **test-runner로 대체하지 않는다**. §5: e2e는 in-process contract test가 **아니다** | `e2e/ObservabilityOps/`에 **`run_e2e.sh`와 `feature-map.ko.md` 둘뿐이다.** `Server/`도 `Client/`도 `Shared/`도 없다. runner는 **폐기된 config-8(ATD)을 재실행**해 로그를 grep하고, **in-process contract test**를 돌린 뒤, 이렇게 통과시킨다 — `for scenario in OBS-A1 … ; do echo "$scenario … PASS"; done`. **`echo`가 검증이다.** 그리고 feature-map은 13개를 전부 "구현"으로 적는다. **[gaps §4.7]이 "OBS-A1~C5 evidence와 함께 통과했다"고 기록하고 있는데 — 거짓이다** |
| **E2E-ND-02** | [config-1 RM-A1(**P0**)](../../common/e2e/config-1-location-messaging.ko.md): live-owner peer row와 **두 provider로의 연결 상태**를 확인한다 | `RegistryMessaging/Server/LocationProbe/Endpoints/location-probe-endpoints.ts:24-28` — 모든 row를 `serviceRole: Router`, `state: Ready` **리터럴로** 매핑한다. 클라이언트는 `serviceRole === Router && state === Ready`를 단언한다. ⇒ **절대 실패할 수 없는 단언이다.** 살아 있는 검증은 `rows >= 2` 하나뿐. `ResilienceLifecycle/Server/TopologyProbe`도 `state: Ready`를 하드코딩한다. 게다가 이 probe 서버들은 **application 역할이 없어** §2.4가 금지하는 형태다 |
| **E2E-ND-11** | [e2e README §2.7:371-375](../../common/e2e/README.ko.md): 기본 실행은 **해당 config가 정의한 구현 시나리오**를 순차 실행한다. [config-2 §5:654-658](../../common/e2e/config-2-spot-service.ko.md): Track A~G가 계약 범위다 | `SpotService/Client/main.ts:110,112`가 문서에 없는 `SM-Q9`를 등록하고, `run_e2e.sh:158-166`이 `all`의 child group으로 **항상 실행**한다. `config-2` 문서 전체에 `SM-Q9`는 0건이다 |
| **E2E-ND-18** | [config-1 RM-B2:151-159](../../common/e2e/config-1-location-messaging.ko.md): 지속 request 중 B를 정상 종료하고, in-flight가 reply/public error로 끝나며 consumer가 A로 수렴해야 한다 | `RegistryMessaging/Client/Scenarios/rm-b2-scale-in-scenario.ts:16-21,34-48` — scale-in 전후 요청을 consumer가 아니라 **provider A HTTP endpoint에 직접** 보낸다. B 종료 뒤 1초 sleep 동안 요청은 0개다. 따라서 stale endpoint 반복 timeout과 pending 정리를 관측할 수 없다 |
| **E2E-ND-19** | [config-1 RM-C5:204-212](../../common/e2e/config-1-location-messaging.ko.md): request error reply와 observer `HandlerMissing`/`ReplyError`, send의 `HandlerMissing`/`Drop`을 구분한다. [RM-C8:230-238](../../common/e2e/config-1-location-messaging.ko.md): 한도 초과를 정해진 public error로 분류한다 | `RegistryMessaging/Client/Scenarios/rm-c5-missing-packet-scenario.ts:6-14`은 `failed` bool과 packet-name 포함만 보고 reason/action을 검사하지 않는다. `rm-c8-payload-round-trip-scenario.ts:23-27`도 oversized 결과의 `failed === true`만 본다. timeout·decode 오류·앱이 만든 bool도 모두 같은 성공이다 |
| **E2E-ND-20** | [config-4 RC-A6:103-111](../../common/e2e/config-4-registration-codec.ko.md): duplicate kind+packet, 잘못된 handler group, 미지원 channel kind 조합을 각각 startup에서 거부한다 | `RegistrationCodec/Client/Scenarios/InvalidRegistrationScenario.ts:5-13`과 `feature-map.ko.md:10`은 **duplicate registration 하나만** 만들고 검사한 뒤 RC-A6 전체를 `구현`으로 표시한다 |
| **E2E-ND-21** | [config-5 RL-D1:214-222](../../common/e2e/config-5-resilience-lifecycle.ko.md): 많은 subscriber/consumer에 높은 **fanout** 부하를 주고 누락·붕괴를 본다 | `ResilienceLifecycle/Client/Scenarios/rl-d1-high-fanout-scenario.ts:7-17`은 한 consumer HTTP endpoint에 channel request 120개를 병렬 전송할 뿐 publish/subscriber/fanout이 0건이다. evidence도 `:20-26`에서 두 provider 중 하나의 marker 하나만 요구한다 |
| **E2E-ND-22** | [config-5 RL-D4:244-252](../../common/e2e/config-5-resilience-lifecycle.ko.md): wire `message-kind=Error(5)`와 camelCase `errorCode`/`errorMessage`를 확인하고 성공 `Response(2)`와 구분한다 | `ResilienceLifecycle/Client/Scenarios/rl-d4-missing-request-handler-scenario.ts:7-19`은 앱 DTO의 `failed` bool과 server marker의 packet name만 본다. raw/error header·message kind·error code/message 검사가 모두 없다 |
| **E2E-ND-23** | [config-5 RL-D5:254-262](../../common/e2e/config-5-resilience-lifecycle.ko.md): 동시 N client가 request/send를 **수 분간 지속**하고 latency drift와 종료 후 pending/정리를 관측한다 | `ResilienceLifecycle/Client/Scenarios/rl-d5-mixed-burst-scenario.ts:7-32`은 request 60개와 send 60개를 한 번 `Promise.all`하고 marker 하나씩만 기다린다. 지속 시간·latency·drift·pending·resource 검사가 없다 |
| **E2E-ND-24** | [config-6 SF-B2:109-117](../../common/e2e/config-6-store-failure-recovery.ko.md): grace 초과 중 기존 연결은 유지하되, **장애 중 재시작한 provider 같은 새 outbound connect는 중단**돼야 한다 | `DiscoveryRegistryHa/run_e2e.sh:214-219`은 topology를 전부 띄운 뒤 Redis만 제거한다. `Client/Scenarios/SfB2StoreFailureGraceScenario.ts:12-24`도 이미 연결된 A/B로 request를 반복할 뿐 새 provider를 시작하지 않는다. 따라서 `IMP-ND-31`처럼 `storeFailureGrace`를 아예 읽지 않아도 결과가 같다 |
| **E2E-ND-25** | [config-6 SF-D1:150-158](../../common/e2e/config-6-store-failure-recovery.ko.md)·[SF-D2:160-168](../../common/e2e/config-6-store-failure-recovery.ko.md): store를 정지했다 **재기동**하고 재등록→heartbeat 유예→빠진 target disconnect 순서를 검증한다 | `DiscoveryRegistryHa/run_e2e.sh:236-264`은 D1·D2 모두 같은 Redis container를 `docker pause`/`unpause`할 뿐 stop/restart하지 않는다. D2 client는 `SfD2LongOutageRecoveryScenario.ts:35-58`에서 실패를 삼키고 성공 간격을 **6초까지 허용**한다. 빈 store 재시작과 전 구간 성공·연결 보존을 검증하지 못한다. 별도 D3만 `run_e2e.sh:267-279`에서 container를 재생성한다 |
| **E2E-ND-26** | [config-6 SF-C2:131-146](../../common/e2e/config-6-store-failure-recovery.ko.md): `Draining=true` 게시, 신규 배정 제외, 30초 deadline 안 정상 종료, terminal 직후 row 제거를 crash/lease 만료와 구분한다 | `DiscoveryRegistryHa/run_e2e.sh:229-233`은 `/shutdown` 호출 직후 client를 실행하고, `Client/Scenarios/SfC2GracefulShutdownScenario.ts:11-23`은 row 부재와 A reply만 본다. draining row·신규 배정·process exit/deadline·강제 종료 여부를 한 번도 읽지 않는다 |
| **E2E-ND-27** | [config-7 MON-A1:57-65](../../common/e2e/config-7-monitoring.ko.md): 연결/해제 kind뿐 아니라 source name과 payload `RemoteAddr`, 있으면 `RoutingId`까지 확인한다 | `RuntimeMonitoring/Client/Scenarios/mon-a1-socket-events-scenario.ts:12-29`은 source와 connected/disconnected kind만 찾는다. `RemoteAddr`와 `RoutingId` 문자열은 시나리오 전체에 0건이다 |
| **E2E-ND-28** | [config-7 MON-A2:67-75](../../common/e2e/config-7-monitoring.ko.md): `svc-b`를 **추가/종료**해 peer row를 바꾸고, 살아 있는 `svc-a`의 projection payload가 실제 diff를 반영하는지 본다 | `RuntimeMonitoring/Client/Scenarios/mon-a2-location-runtime-events-scenario.ts:6-26`은 상태 변경 동작 없이 기존 evidence에서 `TopologyChanged`/`ServiceSummaryChanged`와 count nonzero를 기다린다. startup 때 event 하나만 있어도 통과하며 add/stop diff·before/after payload가 없다 |
| **E2E-ND-29** | [config-7 MON-A4:87-95](../../common/e2e/config-7-monitoring.ko.md): (a) 같은 rid·다른 endpoint failover와 (b) drain/restore를 모두 일으키고 socket/location projection 전이를 확인한다 | `RuntimeMonitoring/Client/Scenarios/mon-a4-availability-transition-scenario.ts:7-44`은 drain/restore만 실행한다. failover는 0건이고, topology 단언도 `TopologyChanged` line 수가 `>= 2`인지 볼 뿐 각 line의 peer endpoint/상태가 전후 동작과 일치하는지 비교하지 않는다 |
| **E2E-ND-30** | [e2e README §3.1:487-497,546-547](../../common/e2e/README.ko.md): Config 2·9 P0은 **route mesh 없음 × session/spot 분리 배치**를 우선 적용한다 | **갭 아님.** Config 2의 `SM-F6`은 multi-node 역할을 별도 프로세스로 실행하고 `--spot-only true`로 SpotMesh만 등록한다. Config 9도 Actor·Session·Caller 역할을 별도 프로세스로 실행하고 각 역할이 SpotMesh만 등록한다. 현재 runner에서 `SM-F6`와 `TA-A1`을 각각 재실행해 통과를 확인했다 |
| **E2E-ND-31** | [e2e README §3.1:514-519](../../common/e2e/README.ko.md): 응답 actor ref는 node rid가 비어 있지 않고 **generation > 0**인 concrete snapshot이어야 한다 | Node E2E client의 generation 비교는 값 보존 또는 존재만 본다. 대표적으로 `SpotService/Client/Scenarios/sm-d15-scenario.ts:30-55`는 `generation !== undefined`, `ToActorMessaging/Client/main.ts:143-160`은 bind reply와 입력의 동등성만 확인한다. `generation > 0` 단언은 전체 `e2e/*/Client`에 0건이라 양쪽이 0이어도 통과한다 |
| **E2E-ND-32** | [config-9 TA-A1:69-77](../../common/e2e/config-9-to-actor-messaging.ko.md)·[TA-A2:79-87](../../common/e2e/config-9-to-actor-messaging.ko.md)·[TA-A3:89-97](../../common/e2e/config-9-to-actor-messaging.ko.md): no-bind 전후 bound-session snapshot과 session gateway/client의 bind·push **부재 evidence**까지 대조한다 | `ToActorMessaging/Client/main.ts:54-101`은 send/request/push positive와 actor handler evidence만 본다. bound-session snapshot을 조회하는 호출이 없고, A2의 session/client negative와 A3의 bind 전 snapshot·no-bind-created marker 부재도 단언하지 않는다 |
| **E2E-ND-33** | [config-9 TA-A4:99-107](../../common/e2e/config-9-to-actor-messaging.ko.md): disconnect 뒤 생존 actor 성공에 이어 actor를 destroy하고 같은 ref가 `ActorRouteNotFound`, 새 handler evidence 0인지 확인한다 | `ToActorMessaging/Client/main.ts:104-113`은 connector close 뒤 send/request 성공까지만 실행하고 끝난다. destroy 호출·destroy 뒤 request·부재 error·negative evidence가 모두 없다 |
| **E2E-ND-34** | [config-9 TA-B2:121-129](../../common/e2e/config-9-to-actor-messaging.ko.md): owner 교체/generation 변경 뒤 old ref 실패와 새 live ref 성공을 본다. [TA-B3:131-139](../../common/e2e/config-9-to-actor-messaging.ko.md): 실제 route 단절 뒤 `RouteNotConnected`, 복구 뒤 같은 ref 성공을 본다 | `ToActorMessaging/Client/main.ts:122-140`은 owner를 교체하지 않고 generation에 `+1`, route를 끊지 않고 `nodeRid='to-actor-missing-route'`로 **입력 DTO를 위조**한다. 새 live ref 성공과 route 복구 follow-up도 없다 |
| **E2E-ND-35** | [e2e README §3.1:527-529](../../common/e2e/README.ko.md): location 발견·dial 수렴 직후 settle delay 없이 첫 요청을 보내며 retry/sleep으로 가리지 않는다 | `SpotActorTransfer/Client/main.ts:685-690`의 공용 `createSpot()`은 모든 spot 생성 뒤 **무조건 500ms sleep**하고, 그 뒤 remote join/request를 보낸다. 첫 요청 수렴 race가 재현될 창을 스스로 닫는다 |

### 샘플 — 정본 흐름 누락과 우회

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-ND-01** (미구현) | [bingo §7.1:459-464](../../common/sample/bingo/README.ko.md): player의 **전적은 Api 서버가 소유한다.** room Spot의 `OnJoinedActor`는 `GetPlayerRecordReq`를, `OnLeaveActor`는 `ReportBingoResultReq`를 **`yield`**로 왕복한다. [메시지 계약:739-760](../../common/sample/bingo/README.ko.md)이 두 packet을, [state:838-847](../../common/sample/bingo/README.ko.md)이 `BingoPlayerState.Wins`/`Losses`를 고정하고, [client 5단계:570-573](../../common/sample/bingo/README.ko.md)이 join push의 그 값들을, [lifecycle gate:1120-1122](../../common/sample/bingo/README.ko.md)가 leave의 `ReportBingoResultReq`를 server evidence로 확인한다 | **세 층 전부에 없다.** `Server/Api/bingo-api-module.ts:36-38`의 `api` handler group은 authenticate·match 둘뿐 — record store도 두 packet도 없다. `Shared/Contracts/bingo-messages.generated.ts:109-121`의 `BingoPlayerState`엔 `wins`/`losses` 필드가 **아예 없고**, `bingo-room-spot.ts:132-157`의 `onJoinedActor`는 Api를 부르지 않으며 `onLeaveActor`(`:160-163`)는 **로그 한 줄이 전부**다. 샘플 트리 전체에서 `yield` grep **0건**, `GetPlayerRecord`/`ReportBingoResult` grep **0건**, `wins`/`losses` grep **0건**. ⇒ config-8이 폐기된 지금 **`yield` 계약이 실제로 도는 것을 보여 주는 자리는 Bingo가 유일한데**, Node엔 그 왕복이 계약(`Shared`)·서버(`Api`/`Play`)·client 게이트 **어디에도 존재하지 않는다** |
| **SMP-ND-02** (결함) | [31 §10.2:482-488](../server/31-session-actor-dispatch.ko.md): **session context는 packet handler registry를 갖는다.** session은 `Configure()` 안에서 handler를 등록하고(같은 packet name 중복 등록은 startup 오류), dispatch callback이 그 registry의 `TryHandle`로 위임한다. [§22:1049](../server/31-session-actor-dispatch.ko.md)는 **`packet name switch`를 명시적 anti-pattern**으로 올려 두고 "실행 문맥별 handler registry와 message type metadata로 분리한다"고 못박는다 | framework는 registry를 **이미 갖고 있다** — `contracts/Streams/IZLinkSession.ts:31`의 `context.handlers`, `contracts/Streams/IZLinkSessionPacketHandler.ts:8-11`의 `addHandler`/`tryHandle`. **그런데 session 클래스 6개 전부가 이걸 쓰지 않고 `onDispatch`에서 packet name을 손으로 분기한다** — `Bingo.Ts/.../bingo-session.ts:24-47`, `SupportChat.Ts/.../supportchat-session.ts:43-56,153-156`, `DeliveryDispatch.Ts/Server/Session/customer-session.ts:33-35`, `.../CourierSession/courier-session.ts:30-40`, `GameQuest.Ts/.../game-api-session.ts:34-35,87-93`, `TicTacToe.Ts/.../play-session.ts:55,145-146`. 샘플 트리에서 `context.handlers`를 부르는 곳은 **Spot 4줄뿐**이다(`play-entry-spot.ts:52-53`, `tictactoe-game-spot.ts:44-45`) — **같은 트리 안에서 Spot은 registry, session은 switch**로 갈린다. ⇒ 중복 등록 검출도 등록 창 강제도 없고, packet을 늘릴 때마다 `if` 사슬이 자란다(§12.6의 registry **키** 결함과 별개다 — 샘플은 registry에 **도달조차 하지 않는다**). *체크리스트의 "6개 샘플 전부"는 부정확하다 — session 서버가 있는 샘플은 **5개**(ShoppingMall엔 session이 없다)이고 session 클래스가 6개다. 결론은 같다: **session이 있는 샘플은 하나도 registry를 쓰지 않는다*** |
| **SMP-ND-03** (결함) | [deliverydispatch:231-236](../../common/sample/deliverydispatch/README.ko.md): DispatchWorker는 배치 정책으로 담당 노드를 정한 뒤 **spot handle resolver**로 그 노드의 `CourierEntrySpot` handle을 얻어 offer를 보낸다. "전송 대상 인자는 **불투명한 `SpotHandle` 하나**이며, application이 **route mesh channel에 node rid를 찍어 보내는 표면은 이 샘플에서 쓰지 않는다**"(`:250`도 같은 말) | `Server/DispatchCenter/dispatch-worker.ts:80-84` — `this.routes.sendToNode(SampleNames.courierActorNodeRouteChannel, courierActorNodeRid(courierId), offerDelivery(...))`. **문서가 이름 붙여 배제한 바로 그 표면이다**: route mesh channel + 샘플이 문자열로 만든 node rid(`Shared/Configuration/sample-names.ts:20-22`). actor 존재 확인도 같다(`:128-135`의 `requestToNode`). `SpotHandle`을 얻는 resolver 호출은 **샘플 전체에 0건**이고, `sample-names.ts:3-4`는 같은 이름 `delivery-couriers`를 route channel과 spot mesh **양쪽에 걸어 둔 채** route 쪽만 쓴다. ⇒ 배치 정책이 정한 노드로 **주소를 직접 찍어 보내므로**, courier actor가 다른 노드로 옮겨가면 offer는 **빈 노드로 간다.** `SpotHandle`이 존재하는 이유가 정확히 그것이다 |
| **SMP-ND-04** (결함) | [tictactoe:20-21,33,129](../../common/sample/tictactoe/README.ko.md): Redis 기반 위치 저장소를 **framework의 public spot remote address resolver 계약 뒤에 숨긴다.** actor가 `JoinSpot(roomId)`를 쓰면 **Redis-backed resolver가** owner SpotNode route를 돌려주고, remote join은 거기서 얻은 `SpotHandle`로 간다. [`:263-266`](../../common/sample/tictactoe/README.ko.md): "framework의 internal runtime 객체나 **sample-local route helper로 remote join 경로를 우회하면 안 된다**" | `Server/Configuration/redis-room-route-store.ts:9-57,87-89` — 샘플이 **자체 `redis` client**로 `<prefix>tictactoe:rooms:<roomId>` 해시에 `RouteChannelId`·`OwnerNodeRid`·`SpotRid`·`SpotKind`를 쓴다. framework spot location row의 **평행 복제 스키마**다. 그런데 **이 스키마를 읽는 resolver가 없다.** 유일한 소비자가 `tictactoe-game-room-provisioner.ts:27-39`인데, `save()` 직후 `load()`로 **방금 자기가 쓴 값을 도로 읽어** 같은지 비교하고 `room-route=verified`를 찍는다 — **실패할 수 없는 단언**이고 이 marker를 읽는 게이트도 없다(트리 grep이 출력 지점 1건뿐). 진짜 remote join은 `play-entry-spot.ts:68`의 `actor.context.joinSpot(roomId, request)`가 framework location store로 처리한다. ⇒ 문서가 요구한 resolver 계약은 **한 번도 타지 않으면서**, 아무도 안 읽는 Redis 스키마를 하나 더 들고 다닌다 |
| **SMP-ND-05** (결함) | [tictactoe:537,544](../../common/sample/tictactoe/README.ko.md)·[§13:814-816](../../common/sample/tictactoe/README.ko.md)·[`:1013`](../../common/sample/tictactoe/README.ko.md): 첫 actor join에는 self-join notify를 보내지 않는다. **host도 guest도 자기 join notify를 받지 않아야 하며 client가 그것을 확인한다** | `Client/tictactoe-client-scenario.ts:101-106,122-127`이 `expectNoMessage(...)`로 확인하는데, 그 helper(`:225-243`)는 `waitFor(...).timeout(25)`가 **timeout으로 죽으면 성공**으로 친다. **부재 증명의 창이 25ms**다. 게다가 stream connector는 handler가 등록되기 **전에** 도착한 메시지를 drain 루프에서 **버린다**(`stream-connector/src/Runtime/ZlinkStreamReceivedMessages.ts:38-44,58-64` — IMP-TS-02). `expectNoMessage`는 join reply를 `await`한 **뒤에야** waiter를 등록하므로, reply와 함께 도착했을 self-notify는 **이미 버려진 상태**다. ⇒ 서버가 self-notify를 보내도 이 검사는 **구조적으로 거의 항상 통과한다** — 25ms 창은 그 위에 얹힌 두 번째 안전망일 뿐이다 |

### 하네스 규약 위반 — 구조 · 대기 · 설정

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-ND-03** (결함) | [e2e README §2.2:236-238](../../common/e2e/README.ko.md): `Client/Scenarios/<ScenarioId><Name>Scenario.*` — **시나리오 ID 하나마다 파일 하나**를 두고, 공유 보조 코드는 `Client/Support/`에 둔다. [§2.5:310,328-332](../../common/e2e/README.ko.md): "여러 시나리오를 하나의 `AllScenario`, `ScenarioSet`, `DriverScenario` 파일로 묶지 않는다… config 문서의 시나리오 ID 하나는 client scenario 파일 하나와 대응해야 한다" | **Config 9(`ToActorMessaging`)와 Config 10(`SpotActorTransfer`)에는 `Client/Scenarios/`도 `Client/Support/`도 없다.** 두 config의 client는 `main.ts` **한 파일**이다 — `ToActorMessaging/Client/main.ts:54-140`에 `runTaA1`~`runTaB3` **7개**가, `SpotActorTransfer/Client/main.ts`(**805줄**)에 시나리오 함수 **21개**가 들어 있다. 나머지 8개 config는 `Client/Scenarios/`를 갖고 있으므로 **한 트리 안에서 두 구조가 공존한다.** ⇒ §2.5가 이름까지 붙여 금지한 형태이며, 시나리오별로 무엇을 단언하는지 파일 경계로 드러나지 않아 E2E-ND-31~35 같은 누락이 **리뷰에서 보이지 않는다** |
| **E2E-ND-04** (결함) | [§2.1:150-169](../../common/e2e/README.ko.md): readiness **3초** / poll 0.1초 / **route settle 5초** / **scenario settle 3초** / HTTP probe 3초를 각 `run_e2e.sh` 상단의 **명시적 config 상수**로 둔다. "이 값 안에 준비되지 않는 로컬 e2e는 **대기 시간을 늘려서 통과시키지 않는다**"(`:166`) | **settle 상수는 11개 runner 어디에도 없다** — 트리 전체 `SETTLE` grep **0건**. 대신 맨 `sleep`이 흩어져 있다(`SpotActorTransfer/run_e2e.sh:141`의 `sleep 2` 등). readiness는 **기준을 크게 넘긴다** — `SpotService/run_e2e.sh:13-16`이 `LOCAL_READINESS_TIMEOUT_SECONDS=3`을 **선언해 놓고 쓰지 않고**, 실제 루프(`:461-469`)는 `LOCAL_READINESS_ATTEMPTS=600` × poll `0.1` = **60초**다(기준의 **20배**). `AutomaticTurnDispatch/run_e2e.sh:26`은 150(=15초). `SpotActorTransfer/run_e2e.sh:23,33`과 `ToActorMessaging/run_e2e.sh:20,24`는 **상수 없이** `seq 1 160`×`sleep 0.25`(=40초), `seq 1 120`×`0.25`(=30초)를 인라인으로 박았다. `ObservabilityOps/run_e2e.sh`엔 대기 상수가 **하나도 없다**. ⇒ §2.1이 막으려던 것 — "느리면 늘려서 통과" — 이 그대로 굳어 있다 |
| **E2E-ND-05** (결함) | [§2.7:359-363](../../common/e2e/README.ko.md): "Redis가 필요한 각 E2E 실행은 그 실행만 사용하는 **전용 Docker Redis container를 새로 만들어야 한다.** 이미 실행 중인 container, host Redis, 다른 E2E나 sample이 만든 Redis endpoint를 **공유하거나 fallback으로 사용하면 안 된다.** key prefix만 다르게 지정하는 것도 인스턴스 공유를 허용하지 않는다"(`:386-391`도 같은 규칙) | `SpotService/run_e2e.sh:192-199` — `REDIS_ENDPOINT="${ZLINK_REDIS_E2E_ENDPOINT:-}"`. 이 환경변수가 있으면 **container 생성을 통째로 건너뛰고 외부 Redis를 그대로 쓴다.** 격리는 `REDIS_KEY_PREFIX`(`:201`) 하나로 대신하는데, **§2.7이 "그것으론 부족하다"고 이름 붙여 배제한 방식**이다. pause/stop/flush를 쓰는 config(Config 6)가 같은 인스턴스를 잡으면 서로를 깬다. 게다가 `ToActorMessaging/feature-map.ko.md:17`은 이 탈출구를 **공식 실행 모드로 안내하는데**("Docker를 쓸 수 없으면 `ZLINK_REDIS_E2E_ENDPOINT=host:port`로 외부 Redis를 지정한다"), 정작 `ToActorMessaging/run_e2e.sh`엔 그 변수를 **읽는 코드가 없다** — 계약이 금지한 기능을, 그것도 **없는 기능을** 문서가 권하고 있다 |
| **E2E-ND-06** (결함) | [§2.6:336-344](../../common/e2e/README.ko.md): runner가 실행별 role 설정 파일을 만들고 framework host에는 **설정 파일 경로만** 넘긴다. "Endpoint, Redis, routing id, timeout, 로그와 evidence 경로를 환경 변수…로 전달하지 않으며, **server와 client 애플리케이션 코드에서 직접 사용할 수 있는 환경 변수는 0개다.**" 어긋나면 **feature-map에 configuration migration gap을 기록**한다(`:352-354`) | 닫힘. rid와 evidence 경로는 role 설정 객체에서 직접 전달하고, 공유 evidence 모듈은 빌드 산출물의 고정 상대 위치에서 불러온다. E2E 애플리케이션 소스의 `process.env` 사용은 0건이며 `EvidenceStore`도 명시적인 rid만 받는다 |
| **E2E-ND-07** (결함) | [§2.5:314-316](../../common/e2e/README.ko.md): "client는 server app endpoint를 **언어별 HTTP client wrapper**로 호출한다." Node의 그 wrapper는 [http-client 12 §1](../http-client/12-http-client.ko.md)이 계약을 소유하는 `@zlink-systems/http-client`다 | `e2e/` 트리 전체에서 `@zlink-systems/http-client` import **0건**이다(샘플은 쓴다 — `samples/ShoppingMall.Ts/Client/main.ts:1`). 대신 **7개 config가 `Client/Support/http-client.ts`를 각자 복사해** 전역 `fetch`를 감싼다(`RegistryMessaging/Client/Support/http-client.ts:1-19` — GET/POST 두 함수, 상태코드 검사만). 그 7개 중 **timeout이 있는 건 `DiscoveryRegistryHa` 하나**뿐이라, 나머지는 §2.1의 3초 HTTP probe 기준을 **강제할 수단 자체가 없다.** Config 9·10은 `e2e/browser-client-runtime.ts:40-124`의 `BrowserE2eHttpClientFactory`(같은 fluent 표면을 e2e가 **자체 복제**한 것, 기본 timeout **30초**)를 쓰고 `SpotActorTransfer/Client/main.ts:43-44`는 그것을 **40초**로 올린다. ⇒ framework가 정본 계약으로 소유한 client를 **e2e가 한 번도 실행하지 않아**, 오류 kind 매핑·timeout·retry 계약이 전부 검증 밖에 있다 |
| **E2E-ND-08** (결함) | [§2.5:311](../../common/e2e/README.ko.md)·[§2.9:450-451](../../common/e2e/README.ko.md): "각 scenario 파일 **첫머리**에 해당 시나리오가 무엇을 검증하는지 설명한다. 독자가 파일을 열었을 때 '이 시나리오가 왜 필요한가'를 바로 알 수 있어야 한다" | `e2e/*/Client/Scenarios/*.ts` **138개 중 머리말 주석이 있는 파일은 0개다.** 전부 `import`로 시작한다(예: `SpotService/Client/Scenarios/sm-d15-scenario.ts:1`). ⇒ 시나리오 ID가 **무엇을 단언해야 하는지**가 파일 안에 없어 config 문서를 열어야만 알 수 있다. **E2E-ND-12~17·E2E-ND-19~35의 "실패할 수 없는 단언"이 리뷰를 통과한 이유가 여기 있다** — 의도가 코드 옆에 적혀 있지 않으면 코드가 그 의도를 만족하는지 대조할 기준이 없다 |
| **E2E-ND-09** (결함) | [§2.2:218-238](../../common/e2e/README.ko.md): config는 `.NET`과 같은 **독립 실행 배포 묶음**으로 옮기고 역할마다 `Server/<Role>/` 하나를 둔다. [§6.1:577-582](../../common/e2e/README.ko.md): 로그는 실행별 **`log/` 폴더**에 남기고 VCS에서 제외한다 | 닫힘. 소스 없이 남은 `dist/`는 Git에 추적되지 않는 로컬 빌드 산출물이다. `RegistryMessaging`·`AutomaticTurnDispatch`는 현재 정본 이름과 일치하고, §2.2는 `DiscoveryRegistryHa`처럼 언어별 config 디렉터리 이름까지 같아야 한다고 정하지 않으므로 이름 주장은 제외했다. 실제 위반이던 10개 runner의 `logs/`를 `log/`로 통일하고 상위 `.gitignore` 한 곳이 로그 제외 정책을 소유하게 했다 |
| **E2E-ND-10** (결함) | [§3.1:493](../../common/e2e/README.ko.md): **기동 순서 축** — 서버 역할의 기동 순서를 뒤바꾼 변형을 **Config 1·2·9**에 적용한다. [`:504-506`](../../common/e2e/README.ko.md): "config 러너는 서버 역할 기동 순서를 인자(예: `E2E_START_ORDER=reverse\|shuffle:<seed>`)로 받는다… 축 변형은 **역방향 전체 1회 + 고정 seed shuffle 1회**를 최소로 돌린다" | 축을 구현한 runner는 `SpotService`(Config 2)와 `ToActorMessaging`(Config 9) **둘뿐이다**(`SpotService/run_e2e.sh:12,103-126`, `ToActorMessaging/run_e2e.sh:10,101,171`). **Config 1(`RegistryMessaging`)엔 없다** — §3.1이 rid 방향·peer 수 축과 함께 Config 1을 지목한 바로 그 자리다. 그리고 그 둘조차 기본값이 `forward`인데, `run_e2e_all.sh:83`은 `./run_e2e.sh "${scenario}"`만 부른다 — **reverse도 shuffle도 기본 스윕에서 한 번도 돌지 않는다.** 인자 이름도 갈렸다(`E2E_START_ORDER` 환경변수 vs 위치 인자 `$2`). ⇒ §3.1이 "**순서가 고정된 러너에서는 영원히 재현되지 않는다**"고 적은 결함군이, 축을 "구현한" 두 config에서도 **실행되지 않는다** |

## 라운드 5 — 샘플 · e2e 심층

**Node에는 좋은 소식이 하나 있다.** **Bingo가 C++ 버그 전부에 대해 깨끗하다** — 시작 notify를
제외 필터 없이 **전원에게** 보내고, 카드 재제출을 거부하고, 관전 종료에 진짜 멤버십 가드가 있고,
방을 **실제로 닫고 타이머를 취소한다.** 그리고 **클라이언트 게이트가 두 player 모두** 시작 notify를
기다리고 둘 다 `Running`을 단언한다 — **C++ 버그를 가렸던 약한 게이트가 여기엔 없다.**
`DeliveryDispatch`는 배송 상태 **도착 순서를 진짜로 단언한다**(`.NET`·C++은 못 한다).

### 체크리스트

- [x] **SMP-ND-06** (**버그**) — SupportChat open 응답이 실제 conversation state를 버린다
  - 근거: allocate 응답부터 Entry Spot까지 도메인이 만든 8필드 conversation state를 그대로 전달하고 중복 조립을 계약 mapper로 모았다. 하드코딩 `WaitingForAgent` 때문에 실패하던 open-state gate가 통과한다. 커밋 `c72ff6c1a`.
- [x] **SMP-ND-07** (**버그**) — 닫힌 SupportChat·TicTacToe Spot의 timer가 계속 실행된다
  - 근거: SupportChat은 close-grace 종료 때, TicTacToe는 terminal room의 마지막 actor leave 때 `context.close()`를 호출해 Spot과 timer 수명을 framework lifecycle이 함께 정리한다. close 호출이 없어 실패하던 두 sample lifecycle gate가 DI 대역을 명시한 현재 checkout에서도 통과한다. 구현 커밋 `16613716f`, 게이트 복구 커밋 `345b6fc77`.
- [x] **SMP-ND-08** (**버그**) — SupportChat 상담원 재연결이 `WaitingForClose`를 `Active`로 되돌린다
  - 근거: 상담원 rejoin은 `WaitingForClose` 상태와 기존 close deadline을 보존하고 새 message만 재활성화하도록 domain transition을 고쳤다. rejoin으로 상태가 되돌아가 실패하던 SupportChat domain gate가 통과한다. 커밋 `1ab5ce152`.
- [x] **SMP-ND-09** (**버그**) — TicTacToe timeout을 승리로 기록한다
  - 근거: turn timeout을 승리와 분리한 terminal outcome으로 유지하고 수를 두지 않은 player/cell을 마지막 수처럼 만들지 않게 domain 규칙을 고쳤다. timeout 승리·milestone 오염으로 실패하던 match test가 통과한다. 커밋 `2366f8908`.
- [x] **SMP-ND-10** (**버그**) — TicTacToe Entry Spot handler가 framework lifecycle 밖 객체를 사용한다
  - 근거: milestone 관측을 lifecycle이 만든 Entry Spot handler로 라우팅하고 session factory의 고아 `new PlayEntrySpot`·packet switch를 제거했다. context 접근에서 실패하던 TicTacToe session-dispatch gate가 통과한다. 커밋 `e2212e9e2`.
- [x] **SMP-ND-11** (결함) — 샘플 wire 응답이 inline object와 흩어진 packet 문자열로 만들어진다
  - 근거: SupportChat·DeliveryDispatch·GameQuest 응답을 이름 있는 wire contract로 옮겨 호출부가 payload 모양을 다시 결정하지 않게 했다. inline 응답을 검출하는 named-wire gate가 통과한다. 커밋 `0d98a2384`.
- [x] **SMP-ND-12** (결함) — Bingo·TicTacToe inbound observer marker를 release gate가 확인하지 않는다
  - 근거: 두 client가 observer marker와 필수 필드를 수집·단언하고 runner 성공 조건에 포함한다. marker 출력만으로 통과하던 inbound-observer release gate가 실패에서 통과로 바뀌었다. 커밋 `a984b609b`.
- [x] **SMP-ND-13** (미구현) — Bingo·TicTacToe lifecycle server evidence gate가 없다
  - 근거: Bingo room leave·Entry Spot destroy·destroy 뒤 callback 부재를 server evidence로 기록하고 공용 runner가 검증한다. lifecycle 연결을 끊어도 통과하던 Bingo lifecycle gate가 실패에서 통과로 바뀌었다. 커밋 `c718beed1`.
- [x] **SMP-ND-14** (결함) — ShoppingMall 두 주문을 순차 시작해 scale-out 동시성을 검증하지 못한다
  - 근거: 주문 A/B 시작을 `Promise.all`로 실제 동시 실행하고 이후 같은 projection을 확인한다. 순차 시작이면 실패하는 ShoppingMall scale-out gate가 통과한다. 커밋 `3c2b82602`.
- [x] **SMP-ND-15** (결함) — GameQuest가 서로 다른 owner에서 처리됐는지 확인하지 않는다
  - 근거: 두 player 처리의 owner identity를 server evidence에 기록하고 client가 서로 다름을 단언해 owner 판단을 store 경계에 모았다. 같은 owner로도 통과하던 GameQuest scale-out gate가 통과한다. 커밋 `45fab0587`.
- ~~**SMP-ND-16** (결함) — DeliveryDispatch status request의 고객 식별자 계약이 어긋난다~~ — **갭 아님**
  - 근거: 공통 결정 D3와 갱신된 DeliveryDispatch 계약은 `DeliveryStatusChangedReq.CustomerId`를 필수로 정의하고, Node 계약·Tracking 경로가 그 값을 사용한다. “CustomerId가 다음 hop에만 있다”는 이전 감사 전제가 재검증에서 무너졌으므로 구현 변경 없이 닫는다.
- [x] **SMP-ND-17** (미구현) — TicTacToe 내부 join reply 전용 계약이 없다
  - 근거: client-facing `JoinGameRes`와 별도 `TicTacToeGameJoinRes`를 정의하고 Entry/Game Spot 내부 join 경로가 전용 타입을 사용한다. 타입을 재사용하면 실패하는 internal-join contract gate가 통과한다. 커밋 `c8a7c77fb`.
- [x] **SMP-ND-18** (**wire 파손**) — TicTacToe terminal state의 `nextTurn`이 `null`이다
  - 근거: terminal state도 non-null `nextTurn` 계약을 유지하도록 domain snapshot과 client 단언을 맞췄다. `null` wire를 허용하면 실패하는 next-turn gate가 통과한다. 커밋 `933b9eb28`.
- [ ] **SMP-ND-19** (**wire 파손**) — ShoppingMall decimal 금액을 JavaScript `number`로 표현한다
- [x] **SMP-ND-20** (결함) — SupportChat client가 대화별 message 의미 값을 충분히 검증하지 않는다
  - 근거: participant state와 conversation id·sender·text·sequence를 두 대화 각각 대조해 잘못된 방 payload를 거부한다. sequence만 맞으면 통과하던 message-semantics gate가 통과한다. 커밋 `98f26ff9c`.
- [x] **E2E-ND-12** (**가짜 통과**) — PubSub 측정 구간이 warm-up과 겹치고 순서를 검사하지 않는다
  - 근거: 측정 run/sequence/value를 warm-up과 분리하고 전체 순서·개수를 정확히 단언하도록 evidence 판정기를 캡슐화했다. 측정 publish를 빼면 실패하는 pubsub-evidence gate가 통과한다. 커밋 `5805e5663`.
- ~~**E2E-ND-13** (**가짜 통과**) — RM-C9 submit 결과가 하드코딩 문자열이다~~ — **갭 아님**
  - 근거: 갱신된 Config 1 계약은 one-way send에 public 완료 객체나 bounded-failure oracle이 없음을 명시한다. `'Submitted'`는 HTTP app의 제출 확인이고, RM-C9는 backlog 해소 뒤 follow-up request와 provider evidence 회복을 별도로 단언하므로 하드코딩 성공 판정이라는 감사 전제가 성립하지 않는다.
- [x] **E2E-ND-14** (미구현) — PubSub이 Redis location store를 사용하지 않는다
  - 근거: publisher/subscriber 모두 실행별 Redis location store를 사용하고 runner가 store lifecycle을 소유하며 하드와이어 endpoint를 제거했다. in-memory store가 남으면 실패하는 PubSub Redis gate와 실제 PS-A1이 통과한다. 커밋 `9665b1f21`.
- [x] **E2E-ND-15** (**가짜 통과**) — TA-B1이 stale ref를 transport에 보내지 않는다
  - 근거: TA-B1이 형식은 유효하지만 stale한 `ActorRef`를 실제 actor client에 전달하고 public route error를 분류한다. local undefined 검사로 우회하면 실패하는 ToActor gate가 통과한다. 커밋 `e82d138b5`.
- [x] **E2E-ND-16** (**가짜 통과**) — ST-F1·F3 순서 단언이 kind만 비교한다
  - 근거: transfer evidence가 `P1→P2→P3` value 순서와 source cleanup marker를 모두 단언하고 runtime이 cleanup 증거를 낸다. 순열·marker 누락으로 실패하던 transfer-order gate와 ST-F1·F3가 통과한다. 커밋 `c05fd501c`.
- [x] **E2E-ND-17** (**가짜 통과**) — RM-A4가 살아 있는 consumer의 handover를 검증하지 않는다
  - 근거: 기존 consumer를 유지한 채 같은 rid의 replacement를 띄우고 같은 app endpoint에서 v1→v2 handover와 v1 부재를 대조한다. replacement 직접 호출로는 통과하지 않는 failover gate와 RM-A4가 통과한다. 커밋 `a69409cbc`.

### 진짜 버그

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-ND-06** (**버그**) | [supportchat §11](../../common/sample/supportchat/README.ko.md): `OpenConversationRes.State`는 8필드 `ConversationState`다 | `support-entry-handlers.ts:58-67` — **채널 응답에서 `conversationId`만 가져오고 나머지를 손으로 지어낸다**: `status: WaitingForAgent`(하드코딩), `subject`는 **요청자가 보낸 값을 에코**, `lastMessageSeq: 0`. 도메인이 낸 진짜 status는 **버린다** — 그 시점엔 이미 **상담원이 배정돼 `Active`**인데. ⇒ 상담원이 붙었는데 고객은 **"대기 중"이라고 듣는다.** 그리고 클라이언트 단언 **5개가 하드코딩된 리터럴을 검사한다 — 실패할 수 없다** |
| **SMP-ND-07** (**버그**) | [25 §8](../server/25-stage-wrapper-on-spot.ko.md): spot 종료 뒤 추가 callback을 만들지 않는다 | SupportChat이 대화마다 **50ms 타이머**를 걸고 `context.close()`를 **한 번도 부르지 않는다**(grep 0건). 대화가 `Closed`가 돼도 타이머는 **초당 20회 영원히 돈다.** ⇒ 대화 1,000건을 처리한 서버가 **초당 2만 번 헛돌고** 죽은 Spot 1,000개를 프로세스 수명 내내 붙든다. TicTacToe도 같다(1초 주기) |
| **SMP-ND-08** (**버그**) | [supportchat §13](../../common/sample/supportchat/README.ko.md): `WaitingForClose → Active`로 가는 **유일한 입력은 새 `SendChatMessageReq`**다 | `conversation.ts:74-81` — **상담원이 재접속해 re-join하면** 상태 가드 없이 `Active`로 되돌리고 **close 기한을 지운다.** ⇒ 대화가 idle로 넘어가 양쪽이 알림을 받은 뒤 상담원 스트림이 끊겼다 붙으면, 방이 **조용히 되살아나고 `ConversationClosedNotify`가 영영 안 온다.** 고객은 "곧 종료됩니다"에 **영원히 갇힌다** |
| **SMP-ND-09** (**버그**) | [tictactoe §9](../../common/sample/tictactoe/README.ko.md): 승리는 **라인 완성**이다. timeout은 승리 조건이 아니다 | `tictactoe-match.ts:99-103` — turn timeout 시 **상대를 승자로 만들고**, `lastMoveActorId`를 **수를 두지 않은 쪽**으로, `lastMoveCell`을 `null`로 세팅한다. 게다가 `publishWinMilestone`은 `status === Won`일 때만 도는데 **`TurnTimedOut`은 거기 도달할 수 없다.** ⇒ 99승인 host가 **timeout으로 100승을 채우면 milestone이 영영 발행되지 않고**, 두 클라이언트는 **수를 두지도 않은 player가 `null` 칸에 뒀다**고 렌더한다 |
| **SMP-ND-10** (**버그**) | [tictactoe §7](../../common/sample/tictactoe/README.ko.md): `PlayActorObserveMilestoneHandler`를 `EntrySpot/Handlers/`에 둔다 | **그 파일이 없다.** 대신 `play-session-factory.ts:30`이 **`new PlayEntrySpot(...)`** — framework lifecycle 밖에서 만든 Spot이라 `context`가 **영영 할당되지 않는다.** 세션이 **packet-name switch**로 그 고아 객체의 메서드를 부른다. ⇒ `observeMilestone`에 `this.context.*`를 **한 줄만 추가해도** 모든 `ObserveMilestoneReq`가 **TypeError로 죽는다** |
| **SMP-ND-11** (결함) | [공통 샘플 §공통 작성 원칙:313-325](../../common/sample/README.ko.md): 모든 wire payload는 **이름 있는 계약**으로 두고, 호출 지점의 inline object literal과 흩어진 packet-name 문자열을 금지한다 | 세 샘플이 호출 지점에서 응답 객체를 직접 만든다 — `SupportChat/.../supportchat-session.ts:95-100`, `DeliveryDispatch/.../customer-session.ts:62`, `GameQuest/.../game-api-session.ts:46`. packet 이름도 `SupportChat/.../conversation-actor-handlers.ts:28`과 `TicTacToe/.../play-actor-{join-game,leave-game,place-mark}-handler.ts:19-20`에 문자열로 흩어져 있다. 타입 검사는 일부 `satisfies`에만 걸리고 **wire 이름과 payload 계약을 한 선언에서 고정하지 못한다** |
| **SMP-ND-12** (결함) | [bingo client 12단계:588-594](../../common/sample/bingo/README.ko.md)·[tictactoe client 12단계:558-564](../../common/sample/tictactoe/README.ko.md): 세 client의 inbound observer marker와 필수 필드를 **release gate가 확인**한다 | Bingo는 `Client/main.ts:35-40`, TicTacToe는 `tictactoe-client-scenario.ts:261-267`에서 marker를 **출력만** 한다. 각 scenario는 각각 `bingo-client-scenario.ts:228-234`, `tictactoe-client-scenario.ts:205-211`에서 끝나며 marker 존재·필드 단언이 없다. 공용 runner도 browser 성공만 기다린다(`run-sample.mjs:139-146,227-231`) ⇒ observer를 제거하거나 필드를 비워도 샘플은 통과한다 |
| **SMP-ND-13** (미구현) | [bingo lifecycle gate:1116-1131](../../common/sample/bingo/README.ko.md)·[tictactoe lifecycle gate:945-957](../../common/sample/tictactoe/README.ko.md): room leave·Entry Spot destroy·추가 lifecycle callback 부재를 **server-side evidence로 검증**한다 | 공용 runner의 Bingo 경로(`run-sample.mjs:107-146`)와 TicTacToe 경로(`:177-231`)는 서버를 띄우고 browser client만 실행한다. `destroyActor`·room `onLeaveActor`·추가 callback 부재를 읽는 단언이 0건이다. actor destroy 연결을 끊어도 client가 `LeaveGameReq`를 submit한 직후 성공 종료하므로 release gate는 계속 초록이다 |
| **SMP-ND-14** (결함) | [shoppingmall scale-out:981-982](../../common/sample/event/shoppingmall.ko.md): 주문 A/B를 서로 다른 owner에서 **동시에 처리**하고 어느 API에서도 같은 조회 모델을 확인한다 | `shoppingmall-client-scenario.ts:124-129`이 A 시작 응답을 **await한 뒤** B 시작을 보낸다. `Promise.all`은 이미 시작이 끝난 두 주문의 상태 조회에만 쓴다(`:130-133`). owner 직렬화나 전역 락으로 두 주문을 순차 처리해도 통과한다 |
| **SMP-ND-15** (결함) | [gamequest scale-out:604-608](../../common/sample/event/gamequest.ko.md): 2노드에서 PlayerA/B가 **서로 다른 owner**에서 동시에 처리되는지 확인한다 | `gamequest-client-scenario.ts:73-90`은 두 stream request를 동시에 보내지만 owner identity를 읽거나 비교하지 않은 채 `gamequest-concurrent-owners=completed`를 출력한다. 최종 server assertion도 reward/source event와 marker 존재만 검사하고 owner 상이성은 보지 않는다(`quest-progress-store.ts:195-203`). 두 player가 같은 owner에 배치돼도 통과한다 |
| **SMP-ND-16** (결함) | [deliverydispatch 메시지 계약:324-328](../../common/sample/deliverydispatch/README.ko.md): `DeliveryStatusChangedReq`는 `DeliveryId`·`Status`·`CourierId`·`OccurredAt` 네 필드다. 고객 식별자는 다음 hop의 `DeliveryStatusUpdatedMsg`에만 있다 | `Shared/Contracts/messages.ts:127-134`가 `DeliveryStatusChangedReq`에 **`customerId`를 추가**하고, Tracking handler가 그 비계약 필드로 actor를 resolve한다(`tracking-handlers.ts:24-37`). 같은 packet을 계약대로 쓰는 peer는 고객 actor를 찾을 수 없고, Node가 보낸 payload에는 문서에 없는 필드가 실린다 |
| **SMP-ND-17** (미구현) | [tictactoe 내부 join 계약:664-675](../../common/sample/tictactoe/README.ko.md): room Spot join reply는 별도 `TicTacToeGameJoinRes { State }`다 | `Shared/Contracts/messages.ts:118-125`에는 client-facing `JoinGameRes`와 `TicTacToeGameJoinReq`만 있고 **`TicTacToeGameJoinRes`가 없다**. room Spot도 내부 join reply를 `JoinGameRes`로 반환한다(`tictactoe-game-spot.ts:182-203`). `.NET`은 정식 타입으로 encode/decode하므로(`dotnet/samples/TicTacToe/Shared/Contracts/Messages.cs:60`) packet 계약을 이름으로 맞추는 교차 언어 흐름이 갈라진다 |
| **SMP-ND-18** (**wire 파손**) | [tictactoe `GameState`:713-726](../../common/sample/tictactoe/README.ko.md): `NextTurn`은 non-null `string`이고 terminal nullable 필드 집합에 포함되지 않는다 | `Shared/Contracts/messages.ts:170-180`은 `nextTurn: string | null`, `tictactoe-match.ts:47-51,130-143`은 terminal state에서 실제로 `null`을 wire에 싣는다. `.NET`·C++ 계약 타입은 non-null string(`dotnet/.../Messages.cs:104`, `cpp/.../messages.hpp:146`)이라 terminal state의 표현이 Node에서만 갈라진다 |
| **SMP-ND-19** (**wire 파손**) | [shoppingmall 메시지 계약:735-763,794-797](../../common/sample/event/shoppingmall.ko.md): 주문·결제의 `Amount`와 조회 상태의 `Amount`는 **decimal**이다 | `Shared/Contracts/messages.ts:26-36,65-75`가 둘 다 JavaScript `number`로 선언한다. `number`는 이진 부동소수라 decimal 금액을 보존하지 못하며, 큰 값이나 소수 금액은 `.NET decimal` peer가 보낸 값을 decode→encode하는 순간 달라질 수 있다 |
| **SMP-ND-20** (결함) | [supportchat client 8~17단계:1051-1061](../../common/sample/supportchat/README.ko.md): participant join과 두 방의 message response/push를 **상태와 conversation별 의미 값으로 검증**한다 | `supportchat-client-scenario.ts:57-72`는 첫 `ParticipantJoinedNotify`에서 actor id만, chat response/push에서 `MessageSeq`만 본다. `ConversationId`·sender·text와 join push의 `Active` state를 단언하지 않는다. 두 번째 방도 `:82-91`에서 conversation id 또는 sequence 하나만 본다. 잘못된 방의 payload나 요청 text를 에코하지 않는 push도 같은 sequence만 맞으면 통과한다 |

### 실패할 수 없는 e2e 게이트

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-ND-12** (**가짜 통과**) | [config-3 PS-A1(**P0**)](../../common/e2e/config-3-pubsub.ko.md): fanout 전달을 **순서대로** 확인한다 | warm-up이 **`seq 1..120`을 같은 `runId`·같은 topic**으로 발행한 뒤, 측정 구간이 **`seq 100..111`** — **warm-up 범위 안에 통째로 들어 있다.** 판정기는 `runId`·topic·seq 범위로만 거르고 **`value`를 읽지 않는다**(`warmup-100`과 `measure-100`을 구분하는 유일한 필드다). ⇒ **측정 발행을 전부 지워도, 그 시점에 fanout이 완전히 깨져도 통과한다.** 게다가 12개 중 `>= 3`이고 **순서 검사가 없다** |
| **E2E-ND-13** (**가짜 통과**) | [config-1 RM-C9](../../common/e2e/config-1-location-messaging.ko.md): 송신 큐를 **HWM까지 채운다** | `consumer-endpoints.ts:107-112` — `submitProfileUnderPressure`가 `.submit()`을 **await하지도 확인하지도 않고** `return 'Submitted'` 한다. 클라이언트는 `outcomes.every(o => o === 'Submitted')`를 단언한다. ⇒ **문자열 리터럴이 성공 판정기다. 전송이 전부 실패해도 통과한다** |
| **E2E-ND-14** (**미구현**) | [config-3 §2](../../common/e2e/config-3-pubsub.ko.md): **모든 노드에 Redis location store**를 두고 peer row를 framework가 관리한다 | `publisher-host-factory.ts:35`·`subscriber-host-factory.ts:48` — **`useInMemoryLocationStores()`**다. subscriber는 `--publisher-endpoint`로 **하드와이어**돼 있고 `run_e2e.sh`에 **"redis"가 0건**이다. ⇒ 이 config의 존재 이유인 **store 기반 fanout이 한 번도 실행되지 않는다.** feature-map에 기록 없음 |
| **E2E-ND-15** (**가짜 통과**) | [config-9 TA-B1(**P0**)](../../common/e2e/config-9-to-actor-messaging.ko.md): **형식은 맞지만 stale한 ref**를 넘긴다 | 클라이언트가 `actor`를 **아예 안 넘기고**, caller의 `requireActorRef`가 `request.actor === undefined`에 **자기가 예외를 던진다** — `sendToActor`에 **도달하지 않는다.** ⇒ framework의 actor-route 분류를 **통째로 지워도 통과한다** |
| **E2E-ND-16** (**가짜 통과**) | [config-10 ST-F1·ST-F3(**둘 다 P0**)](../../common/e2e/config-10-spot-actor-transfer.ko.md): `P1 → P2 → P3` 순서를 단언한다 | `assertOrder`가 **`entry.kind`만 비교하고 `entry.value`를 안 읽는다.** ST-F1은 `['packet_handler','packet_handler','packet_handler']` — **같은 kind 셋**이라 **어떤 순열이든 통과한다.** `P1/P2/P3`는 `value`에 있다. ⇒ **"3개가 도착했다"로 퇴화한다.** 필수 marker `source_cleanup`은 **트리 전체에 0건** |
| **E2E-ND-17** (**가짜 통과**) | [config-1 RM-A4(**P0**)](../../common/e2e/config-1-location-messaging.ko.md): consumer **재시작 없이** peer handover를 확인한다 | 교체 후 요청을 **replacement 프로세스 자신의 HTTP**로 보낸다. p1을 resolve했던 클라이언트가 **하나도 살아남지 않아** handover 경로가 **구조적으로 관측 불가능**하다. `v1Count === 0` 단언도 죽은 프로세스의 연결 실패를 삼키고 `[]`를 반환해 **항상 참**이다 |

**Config 8이 Node에 없다** — `TD-*` grep 0건. 대신 폐기된 `ATD-*` 앱이 **기본 스윕에 남아 있고**,
**Config 11의 P0 증거가 그 폐기된 앱과 in-process contract test에서 나온다**(e2e README §5가 e2e가
아니라고 명시한 것이다).

> **감사자 자신의 한계 보고:** samples는 수렴했으나 **e2e는 수렴하지 않았다.** config-2(SpotService,
> 시나리오 51개)와 config-11을 C++ 수준 깊이로 보지 못했다. **SpotService가 가장 크고 거의 확실히
> 더 있다.**
