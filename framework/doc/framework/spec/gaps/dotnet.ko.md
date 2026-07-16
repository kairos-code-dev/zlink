# `.NET` — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> **기준선이다.** 다른 언어가 여기에 맞춘다. 그래서 여기 남은 갭은 **다른 언어로 전파된다.**

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

**전체 체크리스트 62건. 완료 62건. 미완료 0건.**

### 2026-07-15 독립 재검토에서 추가로 닫은 항목

- [x] **IMP-DN-19** (결함) — actor straggler relay queue 포화가 Spot 실행 줄을 blocking한다
  — bounded queue admission을 즉시 판정하고 포화 시 `ActorLocationStale`로 fail-fast한다. 호출자는
  내부 queue 해제를 기다리지 않고 actor 위치를 다시 조회할 수 있다. capacity 1 회귀 테스트가
  포화 호출이 대기하지 않는지 검증한다.
- [x] **IMP-DN-20** (계약 밖 공개 표면) — `SendLogLevel`·`PublishLogLevel`이 공통 spec의 고정 로그
  수준을 application이 덮어쓰게 한다
  — 두 공개 option을 제거하고 send `Warning`, publish `Debug`, handler 예외 `Error` 결정을 dispatch
  내부에 고정했다. public API snapshot과 dispatch/logging 테스트가 이를 고정한다.
- [x] **IMP-DN-21** (결함) — connector test helper가 상위 예외 안의 transport 실패를 분류하지 못한다
  — exception 원인 체인에서 `HttpRequestException`·`IOException`을 `Disconnected`로 분류하고, 알 수
  없는 programming exception은 그대로 재전파한다.
- [x] **SMP-DN-15** (결함) — GameQuest join 응답에 binding identity가 없어 client self-check를 할 수 없다
  — `JoinSessionRes.PlayerId`를 계약에 추가하고 Alice·Bob 요청값과 응답값을 각각 대조한다. actor의
  routing id나 generation은 client에 노출하지 않는다.
- [x] **SMP-DN-16** (결함) — async sample/E2E client가 HTTP 완료를 `GetResult()`로 동기화한다
  — client 시나리오의 동기 unwrap을 모두 `await`로 바꾸고 Client 소스 전체 회귀 검사를 추가했다.
- [x] **E2E-DN-22** (결함) — LocationMessaging의 동적 provider 역할 설정이 필수 `MaxMessageSize`를 누락한다
  — 정적 provider와 같은 2 MiB 한도를 동적 역할 설정에 기록한다. RM-A4 독립 실행이 통과한다.
- [x] **E2E-DN-23** (결함) — PubSub 일반 subscriber에서 선택 설정 `HandlerDelayMs`가 필수로 바뀐다
  — 기본값 0을 typed 설정에 명시하고 느린 subscriber만 3000 ms를 설정한다. PubSub 7개 시나리오가
  통과한다.
- [x] **E2E-DN-24** (결함) — LocationMessaging consumer·workflow가 route 수렴 실패를 30초
  application retry로 가린다
  — 역할 endpoint는 첫 framework request 결과를 그대로 반환한다. LocationMessaging 역할 소스 전체에
  retry helper가 없음을 regression이 고정한다.
- [x] **E2E-DN-25** (가짜 통과) — ResilienceLifecycle·StoreFailure consumer가 profile request 실패를
  최대 30초 동안 다시 보내 첫 요청의 오류를 성공으로 바꾼다
  — profile endpoint와 새 client-host 경로는 framework request를 한 번만 보낸다. RL-B6는 gray provider
  실패와 정상 provider 성공을 모두 관찰하고, StoreFailure 전체 10개 시나리오도 통과한다.
- [x] **E2E-DN-26** (규약 위반) — SpotService·AutomaticTurnDispatch의 routed request helper가
  `Timeout`·`RouteNotConnected`·`RequestTargetNotFound` 등을 5~20초 동안 반복한다
  — location handle을 resolve한 뒤 단일 request/send를 수행하도록 바꿨다. Config 2 전체 runner와
  Config 8의 27개 시나리오·shutdown·recovery가 재시도 없이 통과한다.
- [x] **IMP-DN-22** (결함) — Entry Spot actor의 미등록 request가 remote bound-session 오류 reply를
  보내기 전에 예외로 빠져나가 client timeout이 된다
  — actor dispatch router가 local·remote 경로에 공통으로 쓰이는 `ZLinkActorReply` 오류 frame을 반환한다.
  bound-session 회귀 테스트와 실제 SM-B5가 `RemoteError`를 확인한다.
- [x] **IMP-DN-23** (결함) — 같은 actor에 새 session이 bind된 직후 이전 session이 disconnect되면,
  이전 session의 `NotifyDisconnectedAsync()`가 binding identity를 잃고 최신 session을 disconnect한다
  — session actor가 보유한 binding token을 runtime까지 전달하고 현재 token과 일치할 때만 disconnect를
  처리한다. 오래된 disconnect가 교체 binding을 유지하는 회귀 테스트와 실제 ZoneWorld ZW-B2가 통과한다.
- [x] **E2E-DN-27** (결함) — SM-C1의 정상 후속 request timeout이 앞선 1.5초 slow handler의 남은 실행
  시간보다 짧은 1초로 고정되어, 재시도를 제거하면 처리 후에도 호출자가 먼저 만료된다
  — 후속 request는 한 번만 보내되 5초 deadline으로 같은 Spot mailbox의 순차 완료를 기다린다. 의도한
  100ms timeout request와 후속 상태 증가를 모두 검증하는 SM-C1 독립·전체 runner가 통과한다.
- [x] **E2E-DN-28** (가짜 실패·검증 순서 결함) — ResilienceLifecycle의 restart·flapping·crash·shutdown
  시나리오가 peer row 삭제를 consumer connection 제외 수렴으로 간주하고, 직후 여러 요청의 무조건
  성공을 요구한다
  — down window의 허용 public error는 typed attempt 결과로 분류한다. stale provider 제외가 성공 기준인
  시나리오는 고유 marker traffic으로 연속 surviving-provider 수렴을 먼저 증명하고, 그 직후 첫 요청을
  한 번만 보낸다. RL-A1·A5·B2·B3·C3 독립 실행과 ResilienceLifecycle 전체 20개가 통과한다.
- [x] **E2E-DN-29** (검증 시간 예산 결함) — StoreFailure SF-D2가 약 3초인 disconnect 수렴 창 안에서
  각 probe에 같은 3초 timeout을 사용해, 죽은 transport가 한 번 선택되면 첫 probe 하나가 전체 판정
  시간을 소진한다
  — probe timeout을 location polling interval로 제한해 여러 routing 관측이 하나의 수렴 창 안에서
  끝나도록 했다. 수렴을 확인한 뒤의 여덟 요청은 재시도 없이 각각 한 번만 보내며, 회귀 검사와 실제
  SF-D2 runner가 통과한다.
- [x] **E2E-DN-30** (설정·검증 불일치) — ObservabilityOps OBS-B3가 11초 Redis 정지로 lease 갱신
  지연을 검사하면서 Workflow 역할은 기본 10초 heartbeat를 사용해, 장애 주입 시점에 따라 0.5초
  이상의 lateness가 발생하지 않는다
  — Play와 Workflow가 같은 typed location 설정(heartbeat 1초, lease TTL 3초)을 configuration 경계에서
  받도록 통일했다. 역할 코드의 시간 상수를 제거했고 설정 회귀 검사와 실제 OBS-B3가 통과한다.
- [x] **SMP-DN-17** (결함) — ZoneWorld의 500ms bot timer가 완료를 관찰할 수 없는 one-way 이동
  명령을 계속 적재해, 원격 actor transfer와 대상 노드 중단이 겹치면 같은 actor에 수십 개의 이동이
  쌓인다
  — bot step을 request/response로 바꾸고 timer turn에서 `Yield`로 한 step의 완료를 기다린다. transfer가
  지연되면 이후 period는 timer의 `SkipLateTicks` 정책에 따라 건너뛰므로 stale 이동을 재생하지 않는다.
  노드 중단 중인 join 자체의 timeout은 그대로 보고하며 retry나 sleep을 추가하지 않았다. 정적 회귀
  검사와 브라우저 노드 중단을 포함한 ZoneWorld 전체에서 B1·B4·D1-spots와 모든 시나리오가 통과했다.

### 구현 감사에서 발굴 (2026-07-14, 스펙↔코드 직접 대조)

- [x] **IMP-DN-01** (결함) — 20 §5
  — SPOT route send도 공통 dispatch decode 경계를 사용해 잘못된 payload를 `Drop`으로 끝낸다. 잘못된 payload 다음의 정상 메시지가 처리되는 회귀 게이트와 인접 dispatch 테스트 21건이 통과했다.
- [x] **IMP-DN-02** (결함) — 22 §5·20 §8
  — backend 생성 전 user Spot의 구성 단계와 스캔 descriptor를 공통 packet registry로 검증한다. 명시 handler 중복이 host 시작에서 설정 오류로 실패하는 게이트, unit 627건, sample regression 39건이 통과했다.
- [x] **IMP-DN-03** (결함) — 31 §15
  — binding의 bound-session send-ready 알림과 framework의 상한 있는 pending queue를 연결했다. 호출자는 일시적인 HWM 포화를 재시도하지 않으며, queue가 찼을 때만 `Backpressure`로 분류한다. 단위 테스트와 실제 SpotActorTransfer 전체 runner가 통과했다.
- [x] **IMP-DN-04** (결함) — 51
  — `CreateAsync`가 activation을 catalog에 commit한 직후 user Spot 생성 계측을 기록해 close와 gauge가 균형을 이룬다. metric/lifecycle 테스트 16건, 전체 unit 630건, sample regression 39건이 통과했다.
- [x] **IMP-DN-05** (결함) — 52 §4
  — actor payload codec 경계가 decode 예외를 handler 예외와 구분하고 user Spot·Entry Spot의 send/request 정책이 각각 `Drop`·`ReplyError`로 `PayloadDecodeFailed`를 보고한다. 실패 게이트 2건과 Entry Spot actor 테스트를 포함한 관련 테스트 62건, 전체 unit 634건, sample regression 39건이 통과했다.
- [x] **IMP-DN-06** (결함) — 40 §3·§8.2
  — store resolver가 등록된 `ZLinkLocationOptions.ListPageSize`를 모든 Spot 목록 page request에 사용한다. location 관련 테스트 37건, 전체 unit 632건, sample regression 39건이 통과했다.
- [x] **IMP-DN-07** (결함) — 20 §8
  — registration validator가 노드 전체의 Entry Spot 타입을 추적해 같은 타입의 중복을 `AddZLinkFramework` 단계에서 거부한다. registration 테스트 40건과 전체 unit 628건이 통과했다.

### 교차 언어 결함 (여러 구현에 같은 문제)

- [x] **IMP-X3** — startup validation이 스펙의 설정 오류를 통과시킨다
  — 명시·attribute handler 중복, timer 규칙, Entry Spot 타입 중복을 backend 생성 전에 공통 startup validator가 거부한다. 설정 오류 단위 테스트와 전체 unit 646건이 통과했다.
- [x] **IMP-X4** — location store read에 5초 취소 상한이 없다
  — store read 경계를 `StoreReadTimeout`으로 한 곳에서 감싸고 drain 계산도 같은 상한을 사용한다. location·drain 회귀 테스트가 통과했다.

### 언어별 표면 차이 (기준선 대조)

- [x] **§12.7** — metric drop reason 라벨 도달 불가 (`.NET`)
  — `no_handler`·`decode_error`·`backpressure`·`stale_route`가 각각 실제 실패 경계에서 기록된다. 닫힌 라벨 집합과 도달 가능성을 metric 테스트가 고정한다.

### 전 언어 공통 계약 갭 (모든 언어가 함께 닫는다)

- [x] **§12.20** (결함) — 응답에 packet name을 싣는다
  — response envelope과 reply writer에서 packet name을 제거하고 request correlation으로만 응답을 맞춘다. route codec 회귀 테스트가 wire 형식을 고정한다.
- [x] **§12.21** (결함+미구현) — `yield` terminator 부재 + `async`가 자동으로 turn을 반납
  — request·actor join·worker의 `Async`는 turn을 유지하고 `Yield`만 turn을 반납한 뒤 재진입한다. 실행 turn 테스트와 Bingo·ZoneWorld runner가 통과했다.
- [x] **§12.22** (결함+미구현) — HTTP client가 framework 계약 밖에 있다
  — framework 계약 타입과 DI 구성을 HTTP client 구현에 연결하고 proxy·timeout·오류 표면을 같은 계약으로 검증한다. HTTP client 전체 56건이 통과했다.
- [x] **§12.23** (미구현) — worker 축 분리와 `yield` 부재
  — CPU worker는 bounded pool, I/O worker는 비차단 async 경계로 분리하고 둘 모두 `Async`·`Yield`를 제공한다. queue 포화와 turn 유지·반납 테스트가 통과했다.
- [x] **§12.24** (결함) — actor join의 orchestration이 뒤집혀 있다
  — join call이 owner·lease·handoff orchestration을 내부에서 소유하고 호출자는 대상과 요청만 지정한다. SpotActorTransfer 전체 시나리오와 ZoneWorld 이동 시나리오가 통과했다.

본문은 [갭 인덱스](../90-implementation-gap.ko.md)가 소유한다. **§12.21과 §12.24는 한 묶음이다** — join orchestration을 먼저 바로잡지 않고 자동 turn dispatch만 걷어내면 user Spot → user Spot join이 즉시 막힌다.

## 2. 구현 감사 상세

아래 표의 구현 설명은 갭을 발견했을 때의 상태를 보존한 감사 기록이다. 현재 해소 상태와 검증 근거는
앞의 완료 체크리스트와 「2026-07-15 완료 재검증」을 기준으로 판단한다.

| ID | 종류 | 계약 | 구현이 하는 일 |
|----|------|------|----------------|
| **IMP-DN-01** | 결함 | [20 §5](../server/20-spot-messaging.ko.md): SPOT route one-way의 decode 실패는 **`Drop`** + 경고 + metric | `Runtime/Spots/ZLinkSpotRouteDispatcher.cs:105-108` — `Request` 분기는 `TryDecode`로 감싸는데 **one-way 분기만 `DecodeBody`를 무방비 호출**한다. 예외가 dispatcher를 뚫고 나가 **drain 루프까지 중단**시키고, 뒤에 큐잉된 route 메시지가 처리되지 않는다 |
| **IMP-DN-02** | 결함 | [22 §5](../server/22-actor-model.ko.md)·[20 §8](../server/20-spot-messaging.ko.md): handler 중복 등록은 **startup 오류** | 중복 검사가 spot **활성화 시점**의 `Bind()`에만 있다(`ZLinkSpotPacketRegistry.cs:25-38`). host는 정상 기동하고 **첫 방 생성에서** 터지며, 그것도 `SpotCreateFailed`로 감싸여 설정 오류로 보이지 않는다. Entry Spot 중복은 startup에서 잡히므로 **두 표면이 비대칭** |
| **IMP-DN-03** | 결함 | [31 §15](../server/31-session-actor-dispatch.ko.md): send는 nonblocking 시도 → **pending queue + ready 알림** | `Runtime/Streams/ZLinkBoundSessionService.cs:82-90` — bound-session push만 `SendFlags.DontWait` 실패 시 **즉시 `RouteNotConnected` 예외**. 클라이언트 소켓 하나가 잠깐 차면 브로드캐스트 타이머 턴이 죽는다 |
| **IMP-DN-04** | 결함 | [51](../server/51-runtime-metrics.ko.md): `zlink.spot.count`는 현재 유지 중인 SPOT 수 | `ZLinkSpotNodeCatalog.cs` — `RecordSpotCreated`가 `GetOrCreateAsync`(:354)에만 있고 **`CreateAsync`에는 없다.** 종료는 양쪽 다 기록(:548, :581). `CreateAsync`만 쓰는 앱은 5회 만들고 닫으면 게이지가 **-5** |
| **IMP-DN-05** | 결함 | [52 §4](../server/52-message-flow-tracing.ko.md#4-event-fields): decode 실패 reason은 `decode_error`다 | 재검증 결과 user Spot의 `ZLinkSpotActorPacketDispatcher`뿐 아니라 같은 invoker를 사용하는 Entry Spot actor 경로도 decode 예외를 handler 예외로 분류했다. actor payload codec이 decode 실패를 명시적으로 구분하고 두 표면의 send/request 경계가 `PayloadDecodeFailed`로 처리한다 |
| **IMP-DN-06** | 결함 | [40 §3·§8.2](../server/40-location-runtime.ko.md): 목록 조회는 `list page size` option을 따른다 | `ZLinkStoreLocationResolvers.cs:90-98` — `new ZLinkPageRequest(1000, …)` **하드코딩**. drain 대상 탐색 경로라 option이 무시된다 |
| **IMP-DN-07** | 결함 | [20 §8](../server/20-spot-messaging.ko.md): **같은 Entry Spot 타입 중복**은 설정 오류 | `ZLinkSpotRegistrationValidator.cs:55-63` — spot factory는 노드 간 중복을 검사하는데 **Entry Spot 타입은 안 한다.** 두 SpotNode가 같은 Entry Spot 타입을 등록해도 기동된다 |

## 3. 언어별 표면 차이 상세

### §12.7 metric drop reason 라벨 도달 불가 (`.NET`)

**수정 전 상태.** [51 §4.4](../server/51-runtime-metrics.ko.md)의
`zlink.channel.messages.dropped`는 `no_handler`, `decode_error`, `backpressure`, `stale_route` 네
라벨을 규정하지만, 감사 당시 `.NET` 런타임에서 실제로 방출되는 값은 `no_handler`뿐이었다.
현재는 decode 실패, backpressure, stale route 경로가 각각 정식 라벨을 기록하며 metric 계약
테스트로 네 사유의 도달 가능성을 고정한다.

## 라운드 2 (2026-07-14) — 관측 · Stage · companion 패키지

라운드 1이 대조하지 않은 축(`00`·`10`·`11`·`25`·`50~53`·`12`·`32`)을 스펙과 코드로 직접 대조했다.

### 체크리스트

- [x] **IMP-DN-08** (결함) — `zlink.fanout.received`가 **등록되지 않은 topic까지 라벨로 단다**
  — subscription registry lookup 결과로 metric 라벨을 정해 미등록 topic은 합계만 기록한다. 관측 관련 테스트 31건, 전체 unit 630건, sample regression 39건이 통과했다.
- [x] **IMP-DN-09** (결함) — STREAM ingress가 client가 안 보낸 `correlation_id`를 `request_seq`로 **날조한다**
  — session ingress·reply와 actor/Entry/Spot dispatch가 wire의 correlation만 전파한다. correlation 없는 request 회귀 게이트, 관련 flow 테스트 8건, 전체 unit 631건, sample regression 39건이 통과했다.
- [x] **IMP-DN-10** (결함) — attribute로 선언한 SPOT timer 검증이 **startup이 아니라 spot 활성화 시점**
  — 등록된 Spot 타입의 스캔 timer를 host 시작에서 검증하고 runtime 생성과 같은 이름·주기·overrun 규칙을 사용한다. 관련 테스트 47건, 전체 unit 629건, sample regression 39건이 통과했다.
- [x] **IMP-DN-11** (결함) — connector가 **짝 없는 `Response`/`Error`를 수신 큐에 적재**한다
  — pending 매칭이 끝난 뒤 짝 없는 `Response`는 폐기하고 짝 없는 `Error`는 `RemoteError`로 전달한다. 두 실패 게이트, connector 전체 130건, framework unit 634건, sample regression 39건이 통과했다.
- [x] **IMP-DN-12** (결함) — HTTP client가 **proxy 자격증명을 대상 서버로 흘리고**, CONNECT는 인증 없이 나간다
  — proxy user/password는 transport options에 보존해 `WebProxy.Credentials`로만 설정하고 origin request header에는 추가하지 않는다. 누출·인증 실패 게이트 2건, HTTP client 전체 56건, framework unit 636건, sample regression 39건이 통과했다.
- [x] **IMP-DN-13** (결함) — connector send payload 한도를 **압축 전** payload에 적용한다
  — frame sender가 압축 여부와 관계없이 실제 wire payload가 확정된 뒤 송신 한도를 검사한다. 압축 축소·확대 실패 게이트 2건, connector 전체 132건, framework unit 634건, sample regression 39건이 통과했다.

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-DN-08** | [51 §5·§4.4b](../server/51-runtime-metrics.ko.md): 라벨은 **등록 시점의 닫힌 집합**만 붙인다. 앱이 room id를 인코딩한 동적 topic은 **금지 대상**이며, 미등록 topic은 라벨을 생략하고 합계만 기록한다 | `ZLinkSpotSubscriptionRegistry.cs:143` — `RecordFanoutReceived(message.Topic)`이 `_descriptorsByTopic.TryGetValue`(:152) **앞에** 있다. ⇒ 소켓에 도달하는 **모든 topic 값이 수집기에 새 시계열을 만든다.** ZoneWorld가 실제로 `zone.border.<from>.<to>` 같은 topic을 쓴다. 발행 측(`ZLinkSpotPublishCalls.cs:43,105`)은 올바르게 `null`을 넘긴다 |
| **IMP-DN-09** | [52 §9](../server/52-message-flow-tracing.ko.md): `correlation_id`는 **보내는 client가 생성**하고 **server는 echo만** 한다. **서버는 ingress에서 생성하지 않는다** | 재검증에서 `ZLinkStreamSessionRuntime` 2곳뿐 아니라 session context와 actor/Entry/Spot dispatch 6곳도 `CorrelationId ?? RequestSeq`를 사용한다는 사실을 확인했다. `request_seq`는 연결별 값이라 서로 다른 세션을 같은 `corr`로 합친다. 여덟 fallback을 모두 제거해 wire correlation이 없으면 downstream 관측에도 correlation이 없다 |
| **IMP-DN-10** | [25 §4.1](../server/25-stage-wrapper-on-spot.ko.md): 빈 이름·`period ≤ 0`·`catch-up ≤ 0`은 **host 시작 또는 등록 시점**의 설정 오류 | `ZLinkScannedSpotHandlers.cs:86-90` — scanner가 `[ZLinkSpotTimerHandler(name, 0)]`을 **검증 없이** descriptor로 만든다. 검사는 `ZLinkSpotTimerRegistry.cs:48-60`(**활성화 시점**)에만 있다. ⇒ host는 healthy로 기동하고 **첫 방 생성부터 전부 실패**한다. Java는 startup scan에서 잡는다 |
| **IMP-DN-11** | [32 §10.1·§9](../stream-connector/32-stream-connector.ko.md): response·error·heartbeat는 **수신 한도에 넣지 않는다**. request id가 부합하지 않는 error는 `RemoteError` | pending request와 매칭되지 않은 `Response`는 application 수신 큐에 넣지 않고 폐기한다. 매칭되지 않은 `Error`는 request sequence 유무와 관계없이 `RemoteError` 표면으로 전달한다 |
| **IMP-DN-12** | [http 07 §7.3](../http-client/07-auth-tls-proxy.ko.md): proxy 인증 정보는 **대상 서버로 새지 않아야 한다**(**CONNECT tunnel 요청에만** 실림) | proxy credential은 request header 조립에서 제거하고 `HttpTransportFactory`가 만드는 `WebProxy.Credentials`에만 설정한다. 따라서 proxy 인증은 CONNECT/평문 proxy 요청에서 transport가 처리하며 origin request message에는 노출되지 않는다 |
| **IMP-DN-13** | [32 §4.7](../stream-connector/32-stream-connector.ko.md): 한도는 payload 바이트에만 적용하며 **압축을 쓰면 압축된 payload 기준**이다 | frame sender가 선택적 압축을 먼저 끝내고 실제 wire payload 길이를 한 번 검사한다. 압축으로 한도 아래가 된 payload는 허용하고 커스텀 codec 결과가 한도를 넘으면 transport write 전에 거부한다 |

## 라운드 3 (2026-07-14) — 근거 없는 표면 · 조용한 no-op · 경합

라운드 3은 질문을 뒤집었다 — **"코드가 스펙이 허용하지 않는 걸 하는가?"**

**public 타입 이름은 깨끗하다.** `Zlink.Framework.Contracts`의 interface 106개 + 타입 144개가 전부
카탈로그에 있고 `Runtime/**`은 아무것도 export하지 않는다. **문제는 전부 "받아서 검증하고 버리는
옵션"과 경합이다.**

### 체크리스트

- [x] **IMP-DN-14** (결함) — `IZLinkSocketConfig` 14개 중 **9개를 적용하지 않고**, `Linger`는 **앱 몰래 0으로 강제**한다
  — 공통 socket option mapper가 14개 설정을 실제 binding option에 적용하고 adapter의 강제 `Linger=0`을 제거했다. binding option 테스트 7건과 package consumer build가 통과했다.
- [x] **IMP-DN-15** (결함) — `IZLinkRouteConfig`/`IZLinkOutboundRouteConfig`가 **설정만 되고 읽히지 않는다**
  — client-server bundle 생성 시 server ROUTER의 mandatory·handover·probe·connect routing id와 client DEALER의 probe 설정을 backend option으로 적용한다. 설정 매핑 테스트 3건, 전체 unit 636건, sample regression 39건이 통과했다.
- [x] **IMP-DN-16** (결함) — SpotNode의 role config 표면이 **완전한 no-op**이다
  — binding에 빠진 publisher/subscriber 역할별 option을 공개하고 framework wrapper가 router·publisher·subscriber 설정을 모두 적용한다. binding option 테스트와 framework 단위 테스트가 통과했다.
- [x] **IMP-DN-17** (결함) — **actor가 든 spot을 닫을 수 있다** (check-then-act 경합)
  — actor 수 확인과 종료 예약을 Spot activation의 직렬 실행 줄에서 한 작업으로 처리한다. join이 먼저 시작되어 membership commit을 마친 뒤 close가 판정되는 결정적 경합 테스트, 전체 unit 637건, sample regression 39건이 통과했다.
- [x] **IMP-DN-18** (결함) — 첫 `GetOrCreate` 호출자의 취소가 **같은 spot을 기다리는 다른 호출자 전부를 실패**시킨다
  — 공유 생성은 runtime 종료 token으로 계속하고, 각 호출자는 공유 completion을 자기 token으로 기다린다. 첫 호출자만 취소되고 두 번째 호출자는 같은 생성 결과를 받는 실패 게이트, 관련 GetOrCreate/dispose 테스트 3건, 전체 unit 638건, sample regression 39건이 통과했다.

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-DN-14** | `IZLinkSocketConfig`의 각 항목은 소켓에 적용된다 | 적용 경로(`ZLinkChannelBundleFactory.cs:167-176`)가 다루는 건 `MaxMessageSize`·`SendHighWaterMark`·`ReceiveHighWaterMark` **셋뿐**이다. `Linger`·`TcpNoDelay`·`IPv6`·`Immediate`·`ConnectTimeout`·`HandshakeInterval`·`SendBufferSize`·`ReceiveBufferSize`·`ReceiveTimeout`은 **읽는 곳이 없다.** 더 나쁜 건 `ZLinkDotNetBackendAdapters.cs:23,31,39,47,75`가 DEALER/ROUTER/PUB/SUB에 **`Linger = TimeSpan.Zero`를 하드코딩**한다는 것이다. ⇒ `Linger = 1s`를 설정하면 **수락되고 getter로 1초로 읽히는데** 소켓은 Linger 0으로 돈다. 종료 시 큐에 남은 메시지가 **전부 버려진다** — 그 설정이 막으려던 바로 그 일이 |
| **IMP-DN-15** | `RequireKnownPeer`/`AllowPeerHandover`/`EnablePeerProbe` 등은 route 동작을 정한다 | 재검증 결과 상수 정책이 있는 `ZLinkRouteChannelInitializer`·`ZLinkRouteConnectionSet`은 별도 route-mesh 경로였다. 실제 결함은 client-server bundle이 `ConfigureServerRouting()`과 `ConfigureClientRouting()` 결과를 읽지 않는 점이었다. bundle factory가 server ROUTER와 client DEALER 생성 시 각 routing option을 적용한다 |
| **IMP-DN-16** | `ConfigurePubSubPublisher()` 등으로 SpotNode 소켓을 설정한다 | framework config를 읽는 곳은 여전히 **0개**다. 현재 binding의 공개 `ISpotNode`는 router/pub-sub HWM과 publisher no-drop·send timeout은 제공하지만 publisher linger와 subscriber receive timeout·linger는 제공하지 않는다. framework의 `IZLinkBackendSpotNode`는 공개된 일부 option도 연결하지 않는다. ⇒ `SendHighWaterMark = 100_000; NoDrop = true`를 설정해도 오류 없이 수락된 뒤 적용되지 않으며, 나머지 option은 binding 공개 표면만으로 적용할 수도 없다 |
| **IMP-DN-17** | [21 §close](../server/21-mesh-node.ko.md): **actor가 남아 있는 user Spot은 종료하지 않고 실패를 반환한다** | catalog의 actor 수 선확인을 제거했다. activation이 join commit과 같은 직렬 실행 줄에서 actor 수를 확인하고 종료를 예약하므로, 먼저 제출된 join이 commit된 뒤 close 판정이 실행되면 `false`를 반환하고 Spot과 location을 유지한다 |
| **IMP-DN-18** | [21](../server/21-mesh-node.ko.md): `GetOrCreate`는 하나의 activation을 모든 호출자가 공유한다. [54 §6](../server/54-graceful-drain-handoff.ko.md): **호출자의 취소는 그 호출자의 대기만 중단한다** | 최초 호출자는 공유 생성 작업을 시작한 뒤 다른 호출자와 같은 completion을 기다린다. 공유 작업은 runtime 종료에만 취소되며, 각 호출자의 token은 자기 `WaitAsync`에만 적용되므로 짧은 deadline이 다른 대기자를 실패시키지 않는다 |

## 교차 언어 결함 — 이 언어에서 무엇을 고치나

**교차 언어 결함이라도 고치는 일은 이 언어에서 한다.** [갭 인덱스](../90-implementation-gap.ko.md) §15.3이
**왜**(계약과 결정)를 소유하고, 아래 표가 **무엇을**(이 언어의 작업)을 소유한다.

| 교차 결함 | 무엇이 깨지나 | 이 언어의 작업 |
|---|---|---|
| **IMP-X3** | startup validation이 스펙의 설정 오류를 통과시킨다 | IMP-DN-02 · IMP-DN-07 · IMP-DN-10 |
| **IMP-X4** | location store read에 5초 취소 상한이 없다 | **이 언어 전용 ID 없음** — `Runtime/Locations/`에 `StoreReadTimeout` 개념 자체가 없다. [54 §3.4](../server/54-graceful-drain-handoff.ko.md)가 요구하는 5초 상한을 store read 경계마다 적용한다 |
| **IMP-X7** | connector send payload 한도를 압축 *전* payload에 적용 | IMP-DN-13 |
| **IMP-X9** | HTTP client가 proxy 자격증명을 대상 서버로 흘린다 | IMP-DN-12 |
| **IMP-X10** | SPOT timer 등록 검증이 startup이 아니다 | IMP-DN-10 |
| **IMP-X11** | `fanout.received`가 미등록 topic까지 라벨로 단다 | IMP-DN-08 |
| **IMP-X12** | actor가 든 spot을 닫을 수 있다 (경합) | IMP-DN-17 |
| **IMP-X13** | 서버가 `correlation_id`를 `request_seq`로 날조 | IMP-DN-09 |
| **IMP-X14** | `listPageSize`가 무시된다 | IMP-DN-06 (1000을 하드코딩) |
| **IMP-X17** | `GetOrCreate` 취소가 다른 호출자 전부를 실패시킨다 | IMP-DN-18 |
| **IMP-X18** | Redis fixture 바이트 단위 일치 주장이 거짓 | 빈 컬렉션 표현이 fixture와 다르다 |

## 이전 기록 — 기준선 대조 (2026-07-13 이전)

> **이 절은 과거 기록이다.** 당시 계약 기준으로 확인한 내용이며, 그 뒤 계약이 바뀐 항목이 있다
> (특히 실행 terminator — [갭 인덱스 §12.21](../90-implementation-gap.ko.md) 참조).
> **현재 작업 목록은 이 문서 위쪽의 체크리스트다.**

`.NET` public declaration과 package는 이 문서에서 추적하던 계약 차이를 해소했다.
actor membership은 nullable `SpotRid`만 상태 기준으로 사용하고, join 결과는 승인/거절
sealed record로 유효한 상태만 표현한다.

다음 타입은 기존 interface catalog에서 이름이나 전체 시그니처를 찾기 어려웠다.
현재 `.NET` interface 문서의 전체 inventory, 보완 시그니처와 공통 기능 커버리지 표에
반영했다.

```text
IZLinkActorClient
IZLinkActorDirectory
IZLinkActorJoinCall
IZLinkActorLocationStore
IZLinkActorRequestCall
IZLinkActorSendCall
IZLinkChannelRuntimeOptions
IZLinkClientServerChannelOptions
IZLinkCodecExtension
IZLinkCodecRegistrar
IZLinkLocationReadiness
IZLinkOwnerLeaseStore
IZLinkPeerLocationStore
IZLinkRouteLocationStore
IZLinkRouteMeshChannelOptions
IZLinkSpotActorLifecycle
IZLinkSpotCommonContext
IZLinkSpotLocationStore
IZLinkStreamCompressionBuilder
IZLinkUnhandledDispatchOptions
IZLinkWorkerCall
IZLinkWorkerOptions
```

`IZLinkActorSendCall`은 다른 one-way call과 같은 `void Submit(CancellationToken)` 계약을
제공한다. `SpotHandle`, capability별 `IZLinkEndpointConnections`, sealed monitoring event와
typed packet identity 단일 소유도 contract/unit/E2E 및 실제 package consumer로 검증한다.

runtime metrics, flow correlation, graceful drain과 session closing도 정식 계약, package와
Bingo 공개 예제, Config 1~11의 공통 E2E 181개로 검증했다.

> **실행 terminator는 예외였다.** 위 목록이 만들어질 당시에는 "request·actor join·worker의 yield
> 전용 타입을 제거하고 단일 완료 terminator가 자동으로 turn을 관리한다"가 계약이었고, 그 기준으로
> 갭이 닫힌 것으로 기록했다. **그 계약은 폐기됐다.** 현재 정본은 세 terminator
> (`submit`/`async`/`yield`)이며([04 §1.1](../04-async-execution-policy.ko.md)), `SMP-DN-01`을
> 닫으면서 request·actor join·기존 worker에 이 구분과 실행 turn 동작을 구현했다. 따라서
> [§12.21](#1221-yield-terminator-부재-전-언어)의 `.NET` 구현 차이는 해소됐다. 이 기록 뒤에
> [§12.20](#1220-응답에-packet-name을-싣는다-전-언어),
> [§12.22](#1222-http-client가-framework-계약-밖에-있다-전-언어),
> [§12.23](#1223-worker-축-분리와-yield-부재-전-언어)의 CPU·I/O worker 축 분리도 구현하고
> contract/unit/E2E로 검증했다.

## 라운드 4 (2026-07-14) — 샘플 · E2E

### 체크리스트

- [x] **SMP-DN-01** (미구현) — Bingo의 **player-record / `yield` 축이 코드에 통째로 없다**
  — request·actor join·worker의 `Async`는 turn을 유지하고 `Yield`는 반납·재진입하도록 구현했다. Bingo는 Api 소유 전적 store와 네 메시지, `Wins`/`Losses`, join/leave의 `Yield`, 재개 후 상태 재검증, `PlayerJoinedNotify` client 단언을 갖춘다. 실패 우선 worker gate, unit 640건, contract 42건, sample regression 50건, packaged contract와 실제 Bingo runner가 통과했다.
- [x] **SMP-DN-02** (결함) — 정본 샘플 6개 중 **4개가 여전히 환경변수로 endpoint·Redis를 받는다**
  — Bingo, GameQuest, ShoppingMall, SupportChat의 역할별 endpoint·Redis·routing id·로그 경로를 임시 JSON 설정 파일로 옮겼다. TicTacToe의 로그 환경 변수와 서버 개별 CLI override도 제거했다. 다섯 실제 sample runner, 네 aggregate build, sample regression 49건이 통과했고 정본 애플리케이션 코드의 환경 변수 직접 접근은 0건이다.
- [x] **SMP-DN-03** (결함) — TicTacToe가 **위치 인자로 역할을 전환하는 단일 실행 파일**이다
  — 공통 server 구현은 library에 두고 Api와 Play에 각각 고정된 얇은 실행 프로젝트를 추가했다. 위치 역할 인자 없이 역할별 config만 전달하며, solution build, 실제 sample runner, sample regression 39건이 통과했다.
- [x] **SMP-DN-04** (결함) — DeliveryDispatch 메시지 계약 drift
  — `BindCourierSessionRes.Actor`를 정식 actor snapshot으로 보내고 상태는 이름 있는 문자열로 직렬화하며 `CustomerId`와 서버 자기 단언 wire를 제거했다. client가 공개 응답과 알림 순서를 직접 단언하며 실제 runner가 통과했다.
- [x] **SMP-DN-05** (결함) — GameQuest 메시지 계약 drift
  — 정본 `GameplayMsg`와 byte payload, `QuestProgress.LastSourceEventId`·`Version`을 적용하고 wire DTO와 domain model 사이 변환을 infrastructure 경계에 모았다. GameQuest runner와 regression이 통과했다.
- [x] **SMP-DN-06** (결함) — SupportChat의 **"반드시 오류로 검증한다" 5개 중 3개를 안 본다**
  — 인증 전 요청, agent의 대화 생성, 비참여자 메시지를 추가하고 timeout이 아닌 `RemoteError`만 인정한다. sample runner와 regression 32건이 통과했다.
- [x] ~~**SMP-DN-07** (결함) — ZoneWorld에 **`.NET` 전용 두 번째 클라이언트**가 있다(문서: TypeScript 하나만)~~
  — 갭 아님: 공통 문서는 TypeScript 브라우저 client와 언어별 headless 시나리오 client를 서로 다른 계약으로 명시한다.
- [x] **SMP-DN-08** (결함) — 클라이언트 단언이 문서보다 약하다(Bingo 7·8단계, DD 순서)
  — Bingo가 제출 카드와 양쪽 draw state를 대조하고, DeliveryDispatch가 단일 수신 루프로 상태 도착 순서를 검증한다. 두 sample runner와 regression 31건이 통과했다.
- [x] **E2E-DN-01** (결함) — `ObservabilityOps`가 **e2e 앱이 아니다** — 샘플 바이너리를 셸로 구동한다
  — sample 참조와 Python 증거 해석을 제거하고 session·play·workflow 역할 서버와 검증 client를 독립 프로젝트로 구성했다. `Client/Scenarios/`의 13개 파일이 OBS-A1부터 OBS-C5까지 공개 API로 실행·단언한다. 전체 runner, 관측성 테스트 2건, 관련 runtime 테스트 39건, regression 39건이 통과했다.
- [x] **E2E-DN-02** (결함) — Config 9·10에 **`Client/Scenarios/`가 없다**(Program.cs 954줄·519줄)
  — Config 9의 20개 `ST-*`와 Config 10의 7개 `TA-*` 요청·검증 본문을 ID별 scenario 파일로 옮기고, 공통 client 동작은 각 config의 context가 맡도록 정리했다. 두 client `Program.cs`에는 옵션·scenario 선택·호출 순서만 남겼다. Config 10 전체 runner와 Config 9의 20개 scenario runner, regression 37건이 통과했다.
- [x] **E2E-DN-03** (결함) — Config 10이 **세 역할을 한 프로젝트로 뭉갰다**
  — ActorNode와 SessionGateway를 독립 프로젝트·host factory로 분리하고 원격 session binding 확인은 framework 내부 프로토콜이 소유한다. 앱 전용 우회 API 없이 Config 10 전체 runner가 통과했다.
- [x] **E2E-DN-04** (결함) — readiness 기본값이 **30초**(SpotService **60초**) — 문서는 3초
  — 11개 runner가 local readiness 기본값 3초와 0.1초 poll을 사용한다. regression 33건과 전 runner 기동을 확인했다.
- [x] **E2E-DN-05** (결함) — `RuntimeMonitoring`에 **시나리오 실행 전용 `Trigger` 역할**이 있고 **다른 서버의 로그 파일을 읽어** 검증한다
  — Trigger 서버를 제거하고 채널 요청·잘못된 handshake·등록 검증을 client support로 옮겼다. MON-C1은 throwing 역할 서버의 bounded evidence와 후속 요청 성공으로 격리·회복을 검증하며 stderr 문자열을 읽지 않는다. RuntimeMonitoring 9개 시나리오와 regression 38건이 통과했다.
- [x] **E2E-DN-06** (결함) — `RM-C9`(backpressure)가 **이름뿐**이고 `RM-A4`(P0)가 주장하는 것을 검증하지 않는다
  — 송수신 HWM을 4로 제한하고 느린 처리보다 빠르게 64건을 제출한다. 역할 서버의 bounded wait로 HWM 초과 처리와 적체 정지를 확인한 뒤 후속 request와 provider evidence 회복을 검증한다. RM-C9와 regression 35건이 통과했다.
- [x] **E2E-DN-07** (결함) — 역할 서버가 **30초 재시도 루프로 route 수렴 실패를 가린다**
  — request/send/route endpoint가 첫 framework 호출 결과를 그대로 반환한다. RM-A1/A2/C1/C2와 regression 34건이 통과했다.
- [x] **E2E-DN-08** (결함) — 클라이언트가 bounded wait endpoint 대신 **GET 폴링 루프**를 돈다(실측 5개 파일)
  — LocationMessaging topology, StoreFailure peer/status, ResilienceLifecycle evidence 관찰을 역할 서버의 bounded wait endpoint로 옮겼다. 관련 8개 시나리오와 regression 36건이 통과했다.
- [x] **E2E-DN-09** (결함) — 시나리오 파일 명명·커버리지 장부
  — Config 8 feature-map을 정본 `TD-A1`~`TD-G1` 27개로 교체하고 각 ID를 설명형 이름의 독립 시나리오 파일로 고정했다. 기존 `ATD-*` 파일은 정본 시나리오와 내부 probe로 분리했으며 selector와 활성 소스에서 가공 ID 공간을 제거했다. PubSub 7개, RegistrationCodec 11개, SpotService 51개도 `<ScenarioId><Name>Scenario.cs` 형식으로 바꾸고, 한 파일에 섞여 있던 `SM-F3`/`SM-F5`를 분리했다. 명명·장부 regression 4축, unit 640건, contract 42건, HTTP client 56건, sample regression 55건, 변경 소비자 14개 build와 packaged contract가 통과했다. Config 8 full runner는 27/27을 통과했고(`logs/20260715-064947-2031409`), 별도 shutdown/recovery도 통과했다(`logs/20260715-064806-2026972`).

### 재검증 후 완료한 항목

이전 재검증에서 막혔던 여섯 항목은 정식 계약을 바꾸지 않고 구현 경계를 바로잡아 닫았다.

- **IMP-DN-03** — binding에 bound-session send-ready 알림을 추가하고 framework 내부의 bounded
  pending queue가 이를 소비한다. 호출자는 재시도 순서나 native HWM을 알 필요가 없다.
- **IMP-DN-14·IMP-DN-16** — binding에 실제로 빠진 socket·SpotNode option을 보완하고 공통 mapper가
  역할별 backend에 적용한다. framework가 option을 받아서 버리거나 `Linger`를 덮어쓰는 경로는 없다.
- **SMP-DN-04·SMP-DN-05** — 이미 확정된 공통 sample wire를 그대로 적용했다. DeliveryDispatch는
  정식 actor snapshot과 문자열 status를 사용하고, GameQuest는 `GameplayMsg`와 versioned projection을
  사용한다. 두 샘플 모두 wire DTO를 domain model로 직접 사용하지 않는다.
- **E2E-DN-03** — 공개 앱 API를 추가하지 않고 ActorNode와 SessionGateway 역할을 분리했다. 원격
  actor-session binding 확인은 framework가 소유하는 내부 프로토콜로 처리해 역할 서버에 transport
  세부 정보나 우회 helper를 노출하지 않았다.

### 가장 무거운 것

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-DN-01** | [e2e §2.2·§2.4·§2.5](../../common/e2e/README.ko.md) | `e2e/ObservabilityOps/`에 **`Client/`가 없다.** `Server/Program.cs`가 **Bingo·ShoppingMall 샘플 서버를 import**해 `options.Role`로 스위치하고, 클라이언트로 **`samples/Bingo/Client`를 쓴다.** 시나리오 로직은 `run_e2e.sh` 안의 **인라인 파이썬 783줄**에 있다. 그런데 feature-map은 OBS 13개를 전부 "구현"으로 적는다 |
| **E2E-DN-07** | [e2e §2](../../common/e2e/README.ko.md): **수렴 직후 첫 요청**은 **재시도나 sleep으로 가리지 않는다** — 첫 요청이 바로 성공하는 것 **자체가 검증 대상**이다. *"workaround를 넣은 테스트는 완료로 보지 않는다"* | `LocationMessaging/Server/Provider/Endpoints/ProviderEndpoints.cs:120-141` — `RequestProfileWithRetryAsync`가 **30초 동안 100ms 간격으로 재시도**하며 `ZLinkFrameworkException`을 삼킨다. Config 1의 **모든** `/profile/request`가 이걸 통과한다 |
| **E2E-DN-06** | [config-1 RM-C9](../../common/e2e/config-1-location-messaging.ko.md): 처리 속도보다 빠르게 **다량** 보내 송신 큐를 **HWM까지 채운다** | `RmC9BackpressureScenario.cs:11` — `SlowSendCount = 8`. **one-way send 8번**으로는 어떤 HWM에도 못 닿는다. 그러고는 **10초 자고**(`:25`) 후속 request가 되는지 본다. **backpressure가 만들어지지 않는다** |
| **E2E-DN-04** | [e2e §2.1](../../common/e2e/README.ko.md): local readiness **3초**. *"긴 대기는 버그를 늦게 발견하게 만들기 때문에 완료 조건으로 인정하지 않는다"* | 모든 runner가 **기본 30초**(`SpotService` **60초**)이고 **환경변수로 덮어쓸 수 있다.** 문서는 환경변수를 "느린 CI나 진단용 override"로만 허용한다 |

### 통째로 없는 축

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-DN-01** (완료) | [bingo §7.1](../../common/sample/bingo/README.ko.md):452-475 — **player 전적은 Api 서버가 소유한다.** room Spot은 그것을 계산하지도 들고 있지도 않고, `OnJoinedActor`에서 `GetPlayerRecordReq`를, `OnLeaveActor`에서 `ReportBingoResultReq`를 **`yield`로** 물어본다(:463-464). `PlayerJoinedNotify`의 `State.Players`에는 그렇게 가져온 `Wins`/`Losses`가 실리고(:846-847), client 검증 5단계가 그 값이 채워졌는지 본다(:567-571) | Api의 프로세스 메모리 store와 조회·보고 handler, protobuf 네 메시지와 `Wins`/`Losses`를 추가했다. room은 player join/leave에서 channel request의 `Yield`를 사용하고, join continuation은 room 상태를 다시 확인한 뒤 domain join을 수행한다. observer는 전적 경로를 사용하지 않는다. client는 `PlayerJoinedNotify.State.Players`의 두 전적을 확인하며 runner는 조회 2건과 결과 보고 2건을 destroy 전에 검증한다. |

### 실패할 수 없는 단언

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-DN-06** (결함) | [supportchat §10](../../common/sample/supportchat/README.ko.md):613-619 — **"아래 경우는 반드시 오류로 검증한다"** 5개: ① 인증 전 `OpenConversationReq`·`SendChatMessageReq` ② customer가 아닌 actor의 `OpenConversationReq` ③ customer actor의 `SetAgentAvailableReq` ④ 비참여자의 `SendChatMessageReq` ⑤ `Closed` 대화에 메시지·close | `Client/SupportChatClientScenario.cs` — 검증하는 건 ③(`:189`)과 ⑤(`:154` 중복 close, `:169` 후속 메시지) **둘뿐**이다. **①②④는 한 번도 시도하지 않는다.** 모든 connector가 요청 전에 반드시 `AuthenticateReq`부터 보내고(`:29,38,76,113,126,185`) 인증 전 요청 경로를 만들지 않는다. agent가 `OpenConversationReq`를 보내는 시도도, 비참여자가 `SendChatMessageReq`를 보내는 시도도 없다. ⇒ 서버의 role·participant 가드를 **통째로 지워도 스모크가 초록**이다. 문서는 이 절차를 *"눈으로 읽기 위한 로그가 아니라 sample release gate"*라고 못박았다 |
| **SMP-DN-08** (결함) | [bingo §10](../../common/sample/bingo/README.ko.md):574-577 — **7단계**: card 제출 **response state에 두 player card가 모두 9칸**으로 들어갔는지 확인한다. **8단계**: 양쪽 push의 `DrawSeq`, `Number`, **state**가 서로 같은지 확인한다. [deliverydispatch §10](../../common/sample/deliverydispatch/README.ko.md):681-685 — `Assigned`→`Accepted`→`PickedUp`→`Delivered`가, 재배정은 `Assigned`→`Reassigned`→`Accepted`→`Delivered`가 **순서대로 도착하는지** 확인한다 | **Bingo 7단계**: `Client/BingoClientScenario.cs:96,105` — 제출 응답에서 보는 건 `State.Status == Running` **하나뿐**이다. 9칸 검사는 **게임 종료 push**(`:161`)에만 있어, 제출 응답이 card를 통째로 빠뜨려도 통과한다. **Bingo 8단계**: `:124-127`이 `DrawSeq`와 `Number`는 대조하는데 **두 push의 `State`는 비교하지 않는다** — 한쪽 client의 state가 깨져도 통과한다. **DD 순서**: `Client/DeliveryDispatchClientScenario.cs:66-69`(성공)·`:124-127`(재배정)이 상태별 wait future **4개를 미리 다 만들어 두고** 선언 순서대로 `await`한다(`:89-92`, `:149-152`). 각 future는 자기 status만 필터하므로 **`Delivered`가 `Assigned`보다 먼저 도착해도 전부 완료되고 모든 `Ensure`가 통과한다.** 순서를 단언하는 코드가 **한 줄도 없다** — C++ SMP-CP-37과 같은 결함이다 |

### 구조 위반

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-DN-02** (결함) | [e2e §2.2](../../common/e2e/README.ko.md):236 — **`Client/Scenarios/<ScenarioId><Name>Scenario.*`: 시나리오 ID 하나마다 파일 하나.** [§2.5](../../common/e2e/README.ko.md):305-310 — **`Client/Program.*`에는 옵션 파싱·HTTP client 생성·호출 순서만** 둔다. *"개별 scenario의 요청·검증 본문은 `Program.cs`에 두지 않는다"* | **Config 9·10에 `Client/Scenarios/`가 없다.** `e2e/SpotActorTransfer/Client/`는 `Program.cs` **954줄** 하나이고 20개 시나리오(`ST-A1`~`ST-F6`)가 그 안의 로컬 함수 딕셔너리다(`:10-31`). `e2e/ToActorMessaging/Client/`는 `Program.cs` **519줄**에 7개 시나리오가 **인라인 람다**로 들어 있고(`:13-60`), 최종 evidence 대조까지 top-level 문으로 이어진다(`:172-176`). ⇒ 시나리오 ID ↔ 파일 대응이 없어 다른 언어가 같은 단위로 옮길 수 없다. (Config 9 쪽은 E2E-DN-21과 같은 사실을 가리킨다) |
| **E2E-DN-03** (결함) | [config-10 §2](../../common/e2e/config-10-spot-actor-transfer.ko.md):30-38 — 서버 구성은 **세 역할**이다: actor 노드 2(`actor-a`,`actor-b`) · session gateway 2(`session-a`,`session-b`) · transfer controller 1. [e2e §2.4](../../common/e2e/README.ko.md): **역할이 다르면 별도 실행 프로젝트**이고, 같은 `Program.cs`를 복사해 default role만 바꾸는 것도 금지다. `Program.cs`는 진입점만 두고 host 구성은 `*HostFactory.*`로 뺀다 | `e2e/SpotActorTransfer/Server/`에 프로젝트가 **`ActorNode` 하나뿐**이다. 그 하나가 actor 노드(`Program.cs:40-58` — spot mesh·actor factory·transfer adapter)이면서 **session gateway**(`:59-61` — `AddStreamNode(...).Bind(...).RegisterSession<TransferSession>()`)이면서 **transfer controller**(`:64-246` — `/spots`·`/actors`·`/actors/{id}/join`·`/bound-push`를 `Program.cs`에 직접 매핑)다. `run_e2e.sh:95-110`의 `start_node()`가 **같은 프로젝트를 rid만 바꿔 3번 띄운다.** `*HostFactory`도 `Endpoints/`도 `Handlers/`도 없이 **760줄 `Program.cs`** 하나다. ⇒ config-10이 검증하려는 "session gateway가 원격 transfer 뒤에도 push를 잇는가"가 **항상 같은 프로세스 안**에서 돈다 |
| **E2E-DN-05** (결함) | [config-7 §2](../../common/e2e/config-7-monitoring.ko.md):25-30 — 역할은 **service 노드 2개**와 **trigger *client*** 뿐이다. [e2e §2.3·§2.4](../../common/e2e/README.ko.md): 시나리오 실행만 위임받는 server는 **폴더 이름이 달라도 금지**이며, *"이름을 `Main`·`Coordinator`·`Control`·`Scenario`처럼 바꿔도 실제 기능을 제공하지 않는 server라면 만들 수 없다"*. evidence는 **실제로 처리한 역할 server**가 노출한다 | `e2e/RuntimeMonitoring/Server/Trigger/`가 **실행 프로젝트로 존재한다.** 문서의 *trigger client*를 그대로 **server로 승격**한 것이고, 제품 기능은 0이다 — `TriggerEndpoints.cs:41-67`은 요청마다 **임시 framework host를 띄워**(`TriggerClientRequests.RequestWithTransientHostAsync`) 대신 request를 쏘고, `:90-93`은 잘못된 handshake를 대신 보내고, `:95-102`는 등록 검증을 **자기 프로세스 안에서** 수행해 결과 문자열을 돌려준다. 더 나쁜 건 `:68-89`다 — **다른 프로세스(`svc-throw`)의 stderr 파일을 디스크에서 읽어**(`Support/TriggerLogReader.cs:12-24`, 100ms 폴링) 돌려주고, `Client/Scenarios/MonC1DispatchFailureScenario.cs:34-37`이 그 줄을 grep해 MON-C1을 판정한다. 그 로그 줄은 runner가 `ZLINK_DEBUG_FRAMEWORK_TASKS=1`을 켜야만 나온다(`run_e2e.sh:190`). ⇒ dispatch 실패 격리라는 **계약이 프레임워크 디버그 로그의 문자열 모양에 묶여 있다.** E2E-DN-13·E2E-DN-15가 이 역할의 다른 얼굴이다 |

### 계약 drift — wire 메시지

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-DN-04** (결함) | [deliverydispatch §7.2·§7.3](../../common/sample/deliverydispatch/README.ko.md):312,326 — `BindCourierSessionRes`는 `CourierId`·**`Actor`**(배송원 actor 참조)·`SessionRoute`를 싣는다. `DeliveryStatusChangedReq`는 `DeliveryId`·`Status`·`CourierId`·`OccurredAt` **넷**이다. `Status` 값은 `Assigned`·`Reassigned`·`Accepted`·`PickedUp`·`Delivered` 같은 **이름 있는 값**이다(:334-343) | `samples/DeliveryDispatch/Shared/Contracts/Messages.cs` — ① `BindCourierSessionRes`(`:44-53`)가 `Actor` 대신 **`NodeRid` 문자열**을 싣고, `Actor`는 그 문자열로 **샘플이 직접 정의한 `CourierActorBindingSnapshot(NodeRid)`**(`:52-53`)를 만들어 주는 계산 속성이다. **actor id도 generation도 wire에 없다** — 클라이언트는 어느 actor에 bind됐는지 알 수 없고 `Client/DeliveryDispatchClientScenario.cs:35,39`는 node rid만 대조한다(C++ SMP-CP-44의 `.NET` 쪽). ② `DeliveryStatusChangedReq`(`:124-129`)에 **문서에 없는 `CustomerId`**가 하나 더 실린다. ③ `DeliveryStatus`가 `enum`(`:5-14`)인데 codec에 `JsonStringEnumConverter` 등록이 **0건**이라 wire에 **정수**로 나간다 — 문서·C++는 문자열이다. **client-facing `DeliveryStatusNotify`에서 교차 언어로 깨진다.** ④ `ServerAssertionReq/Res`(`:147-152`)는 계약에 없는 wire 타입이고, client 최종 판정을 **서버 자기 단언**(`Server/Dispatch/DispatchServerHostFactory.cs:97-113`)에 위임한다 |
| **SMP-DN-05** (결함) | [gamequest §11.2·§11.4](../../common/sample/event/gamequest.ko.md):500-543 — entry-spot → owner spot 내부 메시지는 **`GameplayMsg { EventId, PlayerId, Type, Payload: bytes, OccurredAtUnixMs }`**. projection `QuestProgress`는 `PlayerId`·`QuestId`·`Status`·`CurrentCount`·`RequiredCount`·**`LastSourceEventId`**·**`Version: int64`**·`UpdatedAtUnixMs` | `samples/GameQuest/Shared/Messages.cs` — ① `QuestProgress`(`:61-68`)에 **`Version`이 아예 없고**, `LastSourceEventId`는 **`LastEventId`로 이름이 다르다.** projection의 낙관적 갱신 기준값이 wire에서 사라진 것이다(C++ SMP-CP-39의 `.NET` 쪽). ② `GameplayMsg`가 없다 — 대신 **`GameplayEventEnvelope`**(`:70-78`)가 `IdempotencyKey`·`Value`·`Count`·`SourceApi`를 들고 `Payload: bytes` 자리를 대신한다. 이름은 [샘플 메시지 이름 원칙](../../common/sample/README.ko.md)의 어느 접미어에도 속하지 않는다. ③ **계약에 없는 wire 타입 7종**: `ClosePlayerQuestOwnerReq/Res`(`:35-37`), `GetGameplaySnapshotReq/Res`(`:39-48`), `NotifyQuestProgressReq/Res`(`:122-124` — owner spot이 session server로 **request**를 되쏜다. 문서의 push는 `QuestProgressNotify` 한 방향이다), `GameQuestServerAssertRes`(`:126-128`). ④ event stream 이름 `QuestReconciled`가 **`QuestProgressReconciledEvent`**(`:104-110`)로, `StoredQuestEvent.Type`이 **`EventType`**(`:117`)으로 갈렸다 |

### 규약 위반

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-DN-02** (결함) | [설정 정책 §2.1·§2](../../common/sample-e2e-configuration-policy.ko.md):37,46 — framework host의 CLI에는 **`--config <path>` 하나만** 전달하고, endpoint·Redis·routing id·로그 경로를 환경 변수로 넘기지 않는다. ***"Sample과 E2E의 애플리케이션 코드에서 직접 사용할 수 있는 환경 변수는 0개다"*** | Bingo, GameQuest, ShoppingMall, SupportChat의 Configuration 경계가 역할별 JSON 설정을 읽고 시작 전에 필수 값을 검증한다. runner는 실행별 설정 파일을 만들고 framework host에는 `--config <path>`만 전달한다. TicTacToe도 로그 환경 변수와 서버의 endpoint·Redis·routing id 개별 CLI option을 제거했다. 정본 다섯 샘플의 C# 코드에서 환경 변수 직접 접근은 0건이며, 이를 고정하는 sample regression을 추가했다 |
| **SMP-DN-03** (결함) | [설정 정책 §2.1](../../common/sample-e2e-configuration-policy.ko.md):38 — ***"Server role마다 별도 실행 진입점을 사용한다. `--role`이나 `--mode`로 하나의 실행 파일을 여러 server role로 전환하지 않는다."*** [샘플 목록](../../common/sample/README.ko.md): TicTacToe는 **`Api` 2개 + `Play` 2개** 구성이다 | `Server.Api`와 `Server.Play`가 각각 고정된 진입점을 제공한다. 두 프로젝트는 공통 server library의 역할 구현을 호출하며, runner는 같은 역할의 인스턴스 두 개에 서로 다른 `--config` 파일만 전달한다. 역할 선택 인자와 역할 switch는 제거했다 |
| **E2E-DN-08** (**수치 정정**) | [e2e §2.3·§2.5](../../common/e2e/README.ko.md) — 값이 바뀌기를 기다려야 하면 역할 server에 **bounded wait endpoint**(`/evidence/wait`·`/topology/wait`)를 둔다. ***"client가 같은 GET을 수십 번 반복해 값 변화를 관찰하는 방식은 쓰지 않는다"*** | **위반은 실재하지만 "24개 파일"은 재현되지 않았다.** 실측: `Client/` 아래에서 `.Get(`과 `Task.Delay`를 함께 쓰는 파일은 **22개**이고, 그중 대부분은 프로세스 재시작 뒤 `/health`를 다시 확인하는 **readiness 폴링**(§2.1이 허용)이다. **상태 관찰 GET 폴링은 5개 파일**이다 — `LocationMessaging/Client/Scenarios/RmB1ScaleOutScenario.cs:100-115`(`/locations/peers`를 **30초 동안 200ms 간격**으로 GET), 같은 패턴의 `RmB2ScaleInScenario.cs:134-141`·`RmA4SameRidFailoverScenario.cs:72-80`, `StoreFailure/Client/Support/SfProbe.cs:39-44,61-66`(`/query/peers`·`/query/status` 150ms 폴링), `ResilienceLifecycle/Client/Support/ProviderTrafficProbe.cs:62-83`(`/evidence`를 **300회 × 100ms**). 나머지 config는 `/evidence/wait`를 쓴다(client 84곳). ⇒ 위반 항목은 살리되 **범위를 5개 파일 · topology wait endpoint 부재로 정정한다** |
| **E2E-DN-09** (완료) | [e2e §2.2](../../common/e2e/README.ko.md):236 — 시나리오 파일은 **`<ScenarioId><Name>Scenario.*`**, 시나리오 ID 하나에 파일 하나. [§2.8](../../common/e2e/README.ko.md):440 — feature-map은 ***"config 문서의 모든 시나리오 ID를 행으로 둔다"*** | PubSub 7개, RegistrationCodec 11개, SpotService 51개와 Config 8의 27개 정본 시나리오가 ID와 설명형 이름을 함께 가진 독립 파일이다. `SM-F3`/`SM-F5`도 각각의 파일로 분리했다. Config 8 feature-map은 `config-8-execution-turn.ko.md`와 `TD-A1`~`TD-G1`만 사용하고, 활성 source와 selector에는 `ATD-*`가 없다. regression이 네 fixture의 feature-map ID 수, 시나리오 파일 수, ID prefix, 설명형 suffix, ID당 파일 하나를 고정한다. 실제 Config 8 full 27/27과 shutdown/recovery, `SM-F3`/`SM-F5` runner가 통과했다. |

### 검증 실패 — 갭이 아니다

| ID | 계약 | 실제로 확인한 것 |
|----|------|------------------|
| **SMP-DN-07** (**갭 아님**) | 체크리스트 주장: *"ZoneWorld에 `.NET` 전용 두 번째 클라이언트가 있다(문서: TypeScript 하나만)"* | **거짓이다.** [zoneworld §0.2](../../common/sample/zoneworld/README.ko.md):29-52가 두 client를 **명시적으로 구분**한다 — 최상위 `client/`는 모든 언어 server가 공유하는 **TypeScript 브라우저 client**(언어별 디렉터리에 복제 금지)이고, ***"언어별 디렉터리의 `Client/`는 다른 것이다 — 기존 정본 6종과 같은 형태의 headless 시나리오 client"***로 `ZW-*`를 실행하라고 요구한다. 워킹트리에는 `shared_sample/zoneworld/client/`(TS 브라우저) 하나와 `dotnet/samples/ZoneWorld/Client/`(headless 시나리오) 하나뿐이며, **둘은 서로 다른 계약이다.** 상위 README의 "client는 TypeScript 하나만"은 **브라우저 client**를 말한다. ⇒ **이 ID는 닫는다.** 문서 아래 ZoneWorld 절의 같은 결론과 중복된다 |

## 라운드 5 (2026-07-14) — e2e Config 7·9 심층 발견 기록

아래 표는 수정 전 기준선에서 발견한 문제를 보존한 기록이다. 현재 완료 근거는 문서 끝의
「2026-07-15 완료 재검증」에 있다.

### 실패할 수 없는 단언

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-DN-10** (**가짜 통과**) | [config-7 §2·§5](../../common/e2e/config-7-monitoring.ko.md): socket 이벤트 kind는 **닫힌 enum**(`Connected`·`ConnectionReady`·`Disconnected`·`HandshakeFailed`·`PeerAdmissionChanged`·`Closed`)에 속해야 한다 | `MonA5FixedKindsScenario.cs:18-21` — `kind=HandshakeFailed` **또는 `kind=Internal`**을 받아들인다. `Internal`은 **그 닫힌 집합에 없다.** ⇒ 이 시나리오의 존재 이유가 "framework가 잘못된 handshake를 `HandshakeFailed`로 분류한다"를 증명하는 건데, **그 분류를 지우고 catch-all로 떨어져도 통과한다.** 게다가 evidence store가 누적이라 앞선 시나리오가 낸 아무 `Internal` 이벤트가 **트리거 전에 이미 조건을 만족시킨다.** feature-map은 `구현`으로 적으면서 본문엔 fallback을 **자백한다** |
| **E2E-DN-11** (**가짜 통과**) | [config-7 MON-D1](../../common/e2e/config-7-monitoring.ko.md): svc-b가 **떠났다가 돌아오는 전이**를 관측한다 | `MonD1FailureRecoveryScenario.cs:84-86` — 누적 카운터에 대한 **`>= 3`**이다. MON-D1이 시작되기 전에 이미 부팅 수렴(≥1, MON-A2가 단언)과 MON-A4의 drain·restore(각 1)로 **문턱을 넘는다.** ⇒ **svc-b를 멈추기도 전에 첫 루프에서 통과한다.** 카운터를 세는 것으로는 remove/re-add 전이를 구분할 수 없다 |
| **E2E-DN-12** (**가짜 통과**) | [config-7 MON-A2·MON-A3](../../common/e2e/config-7-monitoring.ko.md): **트리거를 발생시킨다**(svc-b 추가/종료, spot subject 변경) | 두 시나리오 모두 **`/evidence/wait` 한 번이 전부다**(MON-A2는 34줄). **아무것도 추가하지 않고 아무것도 멈추지 않는다.** ⇒ **부팅 수렴과 100ms 폴링의 초기 diff만으로 통과한다** |
| **E2E-DN-13** (**가짜 통과**) | [e2e §2.3](../../common/e2e/README.ko.md): **시나리오 실행 전용 server가 만든 marker만으로 성공을 판정하지 않는다** | `MON-B2`의 evidence를 **`Server/Trigger`가 자기 안에서 임시 host를 만들어 단언하고 손으로 조립한 문자열**로 돌려준다(`TriggerValidation.cs:60` — **리터럴 상수**를 반환한다). 클라이언트는 그 문자열을 grep한다. **e2e 옷을 입은 in-process contract test다** |
| **E2E-DN-14** (**가짜 통과**) | [config-7 MON-A4](../../common/e2e/config-7-monitoring.ko.md): **failover** + drain/restore | failover 다리가 **아예 없다.** 그리고 "drain evidence" 단언이 **방금 클라이언트가 호출한 그 엔드포인트가 무조건 쓰는 marker**다(`ServiceHostFactory.cs:125`) — 모니터링에 대해 **아무것도 단언하지 않는다** |

### 구조 위반

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-DN-15** (결함) | [e2e §2.3·§2.4](../../common/e2e/README.ko.md): 시나리오 실행만 위임받는 server는 **폴더 이름이 달라도 금지 대상**이다. evidence는 **실제로 처리한 역할 server**가 노출한다 | `Server/Trigger`가 제품 기능이 없다. 그리고 **MON-C1이 다른 프로세스의 stderr 파일을 디스크에서 읽어** dispatch 실패를 검증한다(`TriggerLogReader.cs:15`). 그 로그 줄은 runner가 `ZLINK_DEBUG_FRAMEWORK_TASKS=1`을 켜야만 나온다. **C++에서 본 결함과 같다** |
| **E2E-DN-16** (결함) | [e2e §2.4](../../common/e2e/README.ko.md): 하나의 서버 프로젝트를 mode로 역할 전환하지 않는다. **같은 `Program.cs`를 복사해 default role만 바꾸는 것도 금지** | `FilteredService`·`ThrowingService`가 **`ServiceHostFactory.Create(args, profile)` 하나에 enum으로 분기**한다. 결과: config-7이 요구하는 **동일한 두 service 노드** 중 `svc-b`에 **spot mesh가 아예 없다** |
| **E2E-DN-17** (결함) | [config-9 §5](../../common/e2e/config-9-to-actor-messaging.ko.md): 실패 분류는 **framework가 낸 public error kind**여야 한다 | `Server/Caller/Program.cs:43-48` — 역할 서버가 **시나리오 ID로 분기**하고(`request.Scenario.StartsWith("TA-B1")`), **`ZLinkFrameworkErrorKind.ActorRouteNotFound`를 직접 만들어 던진다.** ⇒ 앞으로 `/request`를 타는 시나리오는 **진짜와 구별되지 않는 가짜 분류**를 받는다 |
| **E2E-DN-18** (미구현) | [config-9 §2·§5](../../common/e2e/config-9-to-actor-messaging.ko.md): actor의 **bound-session snapshot marker**로 bind 비오염을 대조한다 | 그 marker가 **어디에도 없다.** TA-A2/A3는 대신 **push를 시도해 실패하는 것**을 bind 상태 프로브로 쓴다 — config-9이 존재 검사에 대해 **명시적으로 금지한 형태**다. TA-A1의 "새 bind가 생기지 않았다"는 negative는 **session gateway가 marker를 내는데도 읽지 않는다** |
| **E2E-DN-19** (결함) | [e2e §3.1](../../common/e2e/README.ko.md): **수렴 직후 첫 요청**을 재시도나 sleep으로 가리지 않는다 | TA-B3의 **복구 후 첫 요청**을 10초 재시도 루프로 감싼다(`AssertCallWithRetryAsync`). 실패 분류 단언도 마찬가지라, **10초 동안 틀린 kind가 나와도 통과한다** |
| **E2E-DN-20** (미구현) | [e2e §3.1](../../common/e2e/README.ko.md): **`route mesh 없음` 축이 Config 9의 P0**다 | `Server/Caller/Program.cs:27-28`이 `ConnectRouter`를 **하드와이어**한다. route mesh 없는 변형을 **실행할 수 없다.** README가 그 축이 잡는 버그 부류까지 명시했는데(원격 actor join relay가 route mesh 등록을 전제하던 구현) **feature-map에 기록도 없다** |
| **E2E-DN-21** (결함) | [e2e §2.5](../../common/e2e/README.ko.md) | Config 9에 **`Client/Scenarios/`가 없다** — 7개 시나리오가 `Program.cs`의 람다 딕셔너리다 |

## 라운드 5 — ZoneWorld 발견 기록 (`shared_sample`)

> 아래 표는 수정 전 워킹트리에서 발견한 문제다. 현재 완료 근거는 문서 끝의
> 「2026-07-15 완료 재검증」에 있다.

### 진짜 버그

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-DN-09** (**버그**) | [zoneworld §2.4·§2.5](../../common/sample/zoneworld/README.ko.md): border snapshot은 **`Tick`이 보관 중인 값보다 작거나 같으면 무시**한다. zone spot 생성 시 **`Tick = 0`**이다 | `ZoneState.cs:19` — `_adjacentHighWater`를 별도로 들고, `ExpireStaleSnapshots`(`:60-68`)가 **`_adjacent`만 지우고 `_adjacentHighWater`는 영영 안 지운다.** ⇒ zone-node-2를 재시작하면 그 spot의 `Tick`이 **0부터 다시 시작**하는데, 살아남은 zone-node-1의 high-water는 **≈400**이다. 재시작된 노드가 보내는 `Tick=1,2,3…`이 전부 `tick <= newest`에 걸려 **영구히 버려진다. 그 순간부터 border sync가 죽는다.** 만료된 뒤엔 "보관 중인 값"이 없으므로 새 `Tick=1`은 **받아들여야** 한다. **runner가 실제로 zone-node-2를 재시작하는데**(ZW-B4·C2·C3·E5), **이걸 잡을 ZW-B1이 첫 재시작 앞에서 돌아** 스위트는 초록으로 남는다 |
| **SMP-DN-10** (**가짜 통과**) | [zoneworld §8.1·§11 ZW-C1](../../common/sample/zoneworld/README.ko.md): `Registered`는 **location event**에서, `Connected`는 **socket event**에서 온다. 문서가 위험을 직접 적어 뒀다 — *"각각 다른 출처에서 오므로, 하나만 보면 다른 하나의 배선이 죽어 있어도 통과한다"* | `NodeRegistry.cs:29-36` — 1초마다 오는 **report 메시지가 `Registered = true`를 찍는다.** ⇒ **두 플래그가 같은 배선(report channel)에서 나온다.** `LocationEventHandler`를 **통째로 지워도 ZW-C1이 통과한다.** 문서가 경고한 바로 그 실패다 |
| **SMP-DN-11** (결함) | [zoneworld §2.4](../../common/sample/zoneworld/README.ko.md): 같은 `PlayerId` 재입장 시 **좌표와 zone은 유지된다** | `ZoneEntrySpot.cs:70-74` — `JoinWorldReq` handler가 **무조건 고정 스폰(25,25)으로 재입장**시킨다. 게다가 그 handler는 **entry spot에 있을 때만** dispatch되므로, zone spot에 살아 있는 actor에겐 `JoinWorldReq` handler가 **아예 없다.** 시나리오는 매번 GUID 접미사를 붙여서 **같은 `PlayerId`로 재입장하는 경우가 한 번도 없다** |

### 규약 위반

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-DN-12** (**절대 규칙 위반**) | [샘플 규약](../../common/sample/README.ko.md): **TicTacToe만** 수동 연결을 쓸 수 있다 | `ZoneNode/Program.cs:88-91,107` — `ConnectRouter`·`ConnectPeerPub`·`EnableClient(endpoint)`. peer endpoint가 `ZoneWorldSettings.cs`에 박혀 있고 **주석이 후속 에이전트에게 지우지 말라고 지시한다.** [갭 인덱스 §13.2] 참조 |
| **SMP-DN-13** (결함) | [샘플 규약](../../common/sample/README.ko.md): 앱 코드가 쓸 수 있는 **환경변수는 0개** | `ZoneWorldSettings.cs:20-35`에서 **21개**를 읽는다. 그중 둘은 설정이 아니라 **동작 스위치**다 — `ZONEWORLD_FAULT_TICK_ZONE`을 **모든 zone spot의 100ms tick마다 환경에서 다시 읽어** 예외를 던지고, `ZONEWORLD_DISABLE_BOTS`도 마찬가지다. **다른 5개 샘플은 최근 커밋에서 전부 config 파일로 옮겼다** |
| **SMP-DN-14** (결함) | [샘플 규약](../../common/sample/README.ko.md): 실행마다 **전용 Docker Redis**를 만든다. **host Redis 공유 금지, key prefix만 다르게 하는 것도 안 된다** | `run_sample.sh:12-13,43-44` — **host Redis 6379**를 쓰고 key prefix로만 격리한다. 게다가 시작할 때 `pkill -f "bin/Debug/net8.0/ZoneWorld.Server"`를 해서 **동시에 도는 다른 실행을 죽인다** |

**깨끗한 축(확인함):** actor cross-node transfer(상태 유실 없음, 좌표·zone 교차검증), bot이 bound
session 없이 도는 것, fanout topic에 동적 id 없음, 발행자가 노드 목록을 모름, border band·인접·병합
규칙, move 검증 순서, tick 순서, 자동 handler 등록, 계층 디렉토리.

**`.NET` `Client/`는 위반이 아니다** — [zoneworld §0.2](../../common/sample/zoneworld/README.ko.md)가
언어별 headless 시나리오 client를 명시적으로 허용한다. 상위 README의 "client는 TypeScript 하나만"은
**브라우저 client**를 말한다. 공유 TypeScript browser client의 구현 범위는 이 `.NET` 갭
체크리스트에 포함하지 않는다.

## 2026-07-16 완료 재검증

### 라운드 5 발견 항목

- **E2E-DN-10~16** — RuntimeMonitoring은 scenario 시작 뒤의 baseline으로 실제 add/remove·spot
  변경·failover를 일으켜 검증한다. `Internal` fallback, 누적 횟수 문턱, Trigger 역할, 다른 프로세스
  로그 읽기와 profile switch를 제거했다. ValidationHost는 startup 등록 실패만 검증하는 고정 역할이다.
- **E2E-DN-17~21** — ToActorMessaging은 framework가 반환한 오류만 사용하고 actor binding snapshot을
  직접 비교한다. route가 없는 Caller를 별도로 실행하며 복구 직후 첫 요청에 재시도나 고정 대기를
  사용하지 않는다. 일곱 시나리오는 ID별 파일로 분리했다.
- **SMP-DN-09~14** — ZoneWorld는 만료된 border high-water를 함께 제거하고, registration과 connection을
  각각 location·socket event에서만 갱신하며, 같은 player 재입장 상태를 actor가 보존한다. 수동 peer
  연결과 환경 변수 설정을 제거했고 실행별 JSON 설정과 전용 Docker Redis를 사용한다. 재시작 준비는
  고정 대기 대신 새 프로세스의 topology·status 보고와 Ops의 새 connection event를 확인한다.

### POSD 위험 신호와 선택

| 위험 신호 | 검토한 대안 | 선택과 복잡성 감소 |
|---|---|---|
| HWM 실패를 호출자 재시도·sleep으로 가림 | blocking send / 호출자 retry / framework bounded queue | send-ready가 깨우는 bounded queue를 선택했다. 호출자는 native HWM과 재시도 순서를 모른다. |
| 원격 session binding 지식이 앱 역할에 노출됨 | 공개 제어 API / 앱 HTTP relay / framework 내부 protocol | 내부 protocol과 전용 handler를 선택했다. 앱 public surface와 transport 지식이 늘지 않았다. |
| ingress 처리 전체 await 또는 무순서 분리 | 전체 직렬 await / fire-and-forget / FIFO admission pump | FIFO admission pump를 선택했다. 순서는 보존하고 handler 대기는 actor turn 경계가 소유한다. |
| 역할 분기와 거대한 진입점 | profile switch / 복사한 Program / 고정 host factory | 역할별 프로젝트와 host factory를 선택했다. 진입점은 설정·기동만 담당한다. |
| wire DTO가 domain 불변식을 대신 판단 | DTO를 domain model로 사용 / application mapper / domain의 framework 의존 | application·infrastructure mapper를 선택했다. Domain은 순수 값과 불변식만 소유한다. |
| join에서 호출자가 lifecycle 순서를 조정 | caller orchestration / 별도 helper / owner call 내부 orchestration | owner call 내부 orchestration과 명시적 `Yield`를 선택했다. caller는 대상·요청만 지정한다. |
| 누적 monitoring evidence로 전이를 추정 | 총횟수 문턱 / 로그 문자열 / identity baseline | scenario별 identity baseline과 bounded wait를 선택했다. 이전 시나리오 증거로 통과할 수 없다. |
| 재시작 준비를 고정 시간으로 추정 | 긴 sleep / 무제한 retry / 새 실행 이후의 증거 gate | topology·status·connection 증거 gate를 선택했다. 느림을 숨기지 않고 실패 지점을 바로 보고한다. |
| Quest 상태와 전이 규칙이 수동 projection builder와 application에 분산됨 | passive state+builder 유지 / stateless domain service / event-sourced aggregate | `QuestProgressAggregate`가 rehydrate·source event 중복 판정·전이·event version을 함께 소유하게 했다. application의 사전 중복 조회와 store의 version 재작성 책임이 없어졌다. |
| 원격 actor-session bind가 여러 계층에 부분 상태를 남길 수 있음 | 호출자 rollback / 공개 2단계 bind / coordinator 내부 commit-last | coordinator가 native bind 뒤의 확인 실패를 내부 보상하고, 원격 노드는 native route를 설정한 뒤 관리 binding을 commit한다. 호출자에게 보상 순서나 새 공개 API를 노출하지 않는다. |
| `IZLinkBoundSessionFactory.DisposeAsync()`가 runtime 재시작 세대를 종료한 뒤 같은 객체를 다시 사용함 | 세대마다 새 factory 생성 / reusable service reset / dispose 뒤 재사용 유지 | 실제 책임을 드러내는 `IZLinkBoundSessionService`와 `ResetAsync()`를 선택했다. factory라는 얕은 이름과 잘못된 dispose 의미를 제거했다. |
| actor join handler 두 개가 같은 생성·join·evidence 순서를 복제함 | 복제 유지 / generic base handler / application use case | `ActorJoinTargetUseCase`가 join 절차를 소유하고 두 handler는 framework adapter로만 남겼다. framework 타입 제약을 상속 계층으로 퍼뜨리지 않았다. |
| RuntimeMonitoring 역할 host가 location·channel·monitoring·evidence 설정을 복제함 | 역할 profile 하나로 합침 / 단계별 helper / 공통 deep role host | 고정 실행 역할은 유지하고 `ChannelMonitoringRoleHost`가 공통 설정과 endpoint를 흡수한다. 각 역할 factory에는 차이인 event filter와 throwing handler만 남았다. |
| ObservabilityOps domain actor가 framework context와 handler 변경에 직접 의존함 | framework actor를 domain으로 유지 / context port 도입 / 순수 entity+infrastructure adapter | 순수 `Player` entity와 infrastructure `PlayerActor` adapter를 선택했다. room 이동 불변식은 Domain에, actor lifecycle과 context는 infrastructure에 남았다. |
| `QuestProgressService`와 async route resolver가 인자를 그대로 전달함 | 새 orchestration을 억지로 추가 / 계층 유지 / pass-through 제거 | 책임이 없는 계층을 제거해 handler가 기존 port를 직접 사용하고, 동기 route 조회 앞의 cancellation 검사는 실제 호출부에 두었다. |
| MON-A4가 연결하지 않은 weight 0 peer의 socket event를 요구함 | sleep 뒤 재시도 / 로그·location 상태로 대체 / 관찰 가능한 연결에서 전이 발생 | 두 peer가 연결 가능한 상태에서 drain·restore를 일으키고 identity별 admission event 뒤 첫 요청을 단언한다. 시간 지연이나 다른 관측 표면으로 실패를 가리지 않는다. |

### DDD·public surface·잔여 코드 재검토

- .NET sample·E2E·ZoneWorld의 모든 `Domain/`을 다시 검색했고 `Zlink.Framework`·`Systems.Zlink`·Redis·
  HTTP·logging 의존은 0건이다. wire DTO와 domain model 변환은 application 또는 infrastructure
  경계에 있다. ObservabilityOps의 `PlayerActor`도 순수 `Player`와 infrastructure actor adapter로
  분리했다.
- sample·E2E에서 framework `Runtime/**` 내부 타입이나 non-public reflection으로 binding 내부에
  접근하는 코드는 없다. binding에 필요한 기능은 공개 API로 추가했다.
- 제거한 retry·Trigger·profile helper 이름과 계약 밖 DeliveryDispatch wire 타입은 활성 source에서
  검색되지 않는다. 새 protocol·mapper·host factory는 각각 단일 책임의 실제 호출자가 있다.
- GameQuest의 `quest-projection`은 aggregate의 두 번째 진실이 아니라 삭제·재구성을 시연하는 CQRS
  read model이다. event stream에서 매 조회마다 직접 계산하는 대안은 projection 삭제·rebuild 계약을
  없애므로 채택하지 않았다.
- 응답 wire, terminator, HTTP client, worker, actor join, socket·SpotNode option을 contract와 다시
  대조했다. 새 앱 public API나 test 전용 우회 표면은 추가하지 않았다.

### 검증

- framework unit **670/670**, contract **44/44**, stream connector **137/137**, HTTP client **63/63**.
- Redis 저장소 unit은 standalone 실행에서 환경 독립 항목 **43/43**, Node/.NET Redis 양방향 전용 runner **2/2**, binding context option
  **7/7**. 교차 언어 2건은 Node fixture와 실행별 Redis endpoint를 함께 준비하는 전용 runner로
  검증했다.
- 실제 runner: SpotActorTransfer 전체 20개, RuntimeMonitoring 전체 MON-A1~D1, ObservabilityOps 전체
  OBS-A1~C5, ToActorMessaging 전체, ZoneWorld 전체, Bingo·DeliveryDispatch·GameQuest·ShoppingMall·
  SupportChat·TicTacToe. ZoneWorld는 공유 브라우저 smoke와 노드 중단·재시작을 포함한 전체 시나리오가
  통과했다. sample regression은 **88/88**이다.
- 새로 빌드한 `core/build/lib/libzlink.so.9.0.4`를 직접 사용해 .NET multi benchmark의
  `MULTI_DEALER_ROUTER`(TCP, 64 B, client 4, 1초)를 실행했다. **23.174 Kops/s**, 평균
  **0.084 ms**, p95 **0.127 ms**, p99 **0.168 ms**로 완료됐고 결과 상태는 `complete`다. bounded
  queue·worker 포화·비활성 metric allocation 단위 테스트와 SpotActorTransfer의 순서·handoff
  stress 시나리오도 함께 통과했다.

최종 재리뷰에서 의미 있는 POSD/DDD 위험 신호와 미완료 계약 갭은 남지 않았다. **LOOP CLEAN**.

## connector 공통 test helper 표면 ([32 §10.2](../stream-connector/32-stream-connector.ko.md))

**계약이 확정됐다**(spec §10.2 + connector 언어별 문서 03 §…). connector가 push 관측
표면(`expectNone`·`waitForSequence`)과 범용 단언 유틸(`ensure`·`expectFailure`·`expectTimeout`)을
공개 API로 제공한다.

**이 검증들은 각 언어가 이미 지역 helper로 손수 구현해 관련 갭을 닫아 둔 상태다**(그래서 아래 참조
SMP 항목들이 이미 `[x]`다). 이 작업은 **그 지역 helper를 connector의 공통 표면으로 끌어올려** 다섯
언어가 같은 API를 쓰게 하고, 앞으로 시나리오가 다시 손수 재구현하지 않게 한다. 교차 언어 순서
검증 항목 [SMP-X3](../90-implementation-gap.ko.md)의 "공통 게이트"가 바로 이 `waitForSequence`다.

- [x] **TH-DN-01** (미구현) — connector에 `ExpectNone`·`WaitForSequence`와 `ZlinkStreamAssert`(`Ensure`/`ExpectFailureAsync`/`ExpectTimeoutAsync`)를 [03 §8.1](../stream-connector/languages/dotnet/03-stream-connector.ko.md)대로 구현한다. `Send`/`WaitFor`와 같은 builder·완료 규약을 따른다.
  — 새 API를 참조하는 집중 테스트가 `CS1061`·`CS0103`으로 실패하는 것을 먼저 확인했다. 구현 뒤 실제 TCP로 negative와 순서 오류를 검증하는 connector 테스트 **135/135**, public contract **42/42**, packaged contract가 통과했다. 코드 커밋 `8da696b1f`.
- [x] **TH-DN-02** (리팩토링) — DeliveryDispatch 시나리오의 지역 helper(`WaitForStatusSequenceAsync`·`ExpectNoPushAsync`)를 **삭제하고** 이 connector API로 교체한다. **둘을 병존시키지 마라 — 지역 helper가 하나라도 남으면 미완료다.** 지역 helper로 이미 닫은 SMP-DN-08을 표준 표면으로 대체하는 것이며, 그러면 교차 언어 SMP-X3의 "공통 게이트"가 채워진다.
  — 재검증 시 `ExpectNoPushAsync`는 DeliveryDispatch의 현재 코드와 해당 파일 이력에 없었으므로 새 지역 helper를 만들지 않았다. 남아 있던 `WaitForStatusSequenceAsync`를 typed `WaitForSequence`로 삭제하고, courier-a가 수락한 배송이 courier-b로 잘못 재할당되지 않는 검증은 typed `ExpectNone`으로 직접 추가했다. `ZlinkStreamAssert.Ensure`도 필수 메시지와 함께 사용한다. sample regression **59/59**와 실제 DeliveryDispatch runner의 `deliverydispatch-runner-evidence=completed`가 통과했다. 코드 커밋 `8da696b1f`.

POSD 재검토에서는 수신 callback을 따로 구독하는 방식, 시나리오가 `WaitFor`를 반복하는 방식, 기존 수신 큐와 builder가 순서와 전체 timeout을 소유하는 방식을 비교했다. 기존 수신 큐를 사용하는 방식을 선택해 queue 소비 규칙과 timeout 계산을 connector 안에 유지했다. status 전용 API와 도메인 REST 대기는 추가하지 않았고, 대상인 DeliveryDispatch에는 두 지역 helper 이름이 남지 않았다. **LOOP CLEAN**.

## 2026-07-16 최종 구현·재리뷰

이 절은 위의 과거 실행 수치보다 최신 checkout을 기준으로 한다. 정식 spec, 언어별 interface,
sample·E2E 공통 문서와 구현을 다시 대조했고 다음 불일치와 실제 원인을 수정했다.

- location provider 증거 대기는 먼저 완료된 한 provider의 빈 결과를 전체 실패로 확정했다.
  `ProviderEvidence`가 두 provider 중 첫 **성공 결과**를 선택하고 나머지 작업을 취소·관찰하도록 고쳤다.
- graceful drain은 저장소에 weight 0 row가 반영되기 전에 요청을 보내면 stale weight로 선택될 수 있었다.
  drain의 게시 완료를 동기화하고 `RM-B2`가 실제 지속 트래픽으로 수렴을 확인하도록 고쳤다.
- bound session은 actor 이동 뒤 directory의 새 참조와 session에 저장된 이전 참조가 달라졌다.
  relay coordinator가 logical actor id를 기준으로 현재 참조를 확인하고 handoff이면 내부에서 재결합하며,
  actor 자체가 사라진 경우에는 `ActorRouteNotFound`로 끝내도록 고쳤다.
- 동적 user Spot 생성 직후 native subscription receiver attachment가 일시적으로 internal error 704를
  반환할 수 있었지만 startup 경로가 이를 영구 생성 실패로 확정했다. 동적 activation 경계에서 이 오류만
  전체 request timeout 안에서 재시도하고, 다른 설정 오류는 즉시 전달하도록 고쳤다.
- Bingo API가 load-balanced allocation channel에 요청하면서 별도 preferred owner를 payload로 전달해,
  실제 handler node와 보고된 owner가 달라질 수 있었다. API가 정식 Entry Spot route로 선택한 play node에
  직접 요청하고 그 node가 room을 만들도록 고쳤다.
- `RM-B3`는 crash 전에 시작한 요청만 관찰한 뒤 stale row 제거를 기다려, 장애 직후 신규 부하 지속을
  증명하지 못했다. crash 직후부터 row 제거까지 무지정 request worker를 계속 실행하고, B 처리 증거가
  확인된 시점에도 A row가 남아 있음을 단언한 뒤 expiry 후 수렴과 targeted NotFound를 확인한다.
- sample·E2E의 stream 실패·순서·무수신 단언을 connector의 `ZlinkStreamAssert`, `WaitForSequence`,
  `ExpectNone`으로 옮겼다. 역할별 설정은 typed JSON 객체를 생성하고 framework host에는
  `--config <path>`만 전달하며, Bingo와 DeliveryDispatch runner artifact는 실행별 임시 디렉토리로
  격리한다.

### POSD·DDD 재검토

| 위험 신호 | 검토한 대안 | 선택한 설계 |
|---|---|---|
| handler와 caller가 actor 이동을 각각 추적함 | caller 재결합 / 실패 후 재시도 / relay coordinator가 현재 참조 확인 | coordinator가 directory 지식과 재결합을 숨긴다. caller 표면은 바뀌지 않는다. |
| subscription 일시 오류를 sample이 회피할 가능성 | subscribe 제거 / sample retry / activation 내부 bounded retry | framework activation이 native attachment의 일시 오류만 흡수한다. |
| Bingo의 owner 선호와 실제 실행 위치가 분리됨 | load balance 뒤 owner 보고 / receiver가 owner를 재선택 / 선택한 Entry Spot으로 direct route | allocation 위치와 room owner를 같은 경계로 만들었다. |
| 장애 수렴을 고정 sleep으로 추정함 | 긴 sleep / row만 확인 / 조건 wait와 지속 실제 traffic | role server의 bounded wait와 실제 request 결과를 함께 사용한다. |
| stream 검증 정책이 시나리오마다 중복됨 | 지역 helper 유지 / 범용 test library 추가 / connector 수신 큐 표면 재사용 | connector가 순서·timeout·failure kind를 소유하고 sample은 업무 단언만 남긴다. |
| wire DTO가 domain 상태와 생성 책임을 함께 가짐 | DTO 유지 / 범용 mapper / application 결과와 domain reservation 분리 | Bingo reservation과 allocation 결과를 분리해 domain이 transport를 알지 않는다. |

새 public API, raw-frame 우회, message별 codec 등록, binding non-public reflection은 추가하지 않았다.
Domain 디렉토리에서 framework·Redis·HTTP 의존도 다시 검색했으며 새 의존은 없다.

### 최종 증거

- solution build: warning 0, error 0
- framework unit **692/692**, contract **45/45**, stream connector **140/140**,
  HTTP client **63/63**, sample regression **110/110**
- Redis 저장소의 환경 독립 항목 **43/43**. 교차 언어 2건은 Node fixture와 실행별 Redis endpoint를
  준비하는 전용 runner의 기존 **2/2** 증거를 유지한다.
- 실제 runner: 수정 후 `RM-B3` **PASS (50s)**, Bingo 전체와 DeliveryDispatch 전체 PASS
- 여덟 package archive·export, aggregate consumer와 HTTP client 단독 consumer 검증 PASS
- API snapshot 8개, package snapshot 8개, 정식 문서 SHA-256 ledger를 최신 checkout과 다시 대조했다.

독립 재리뷰는 RM-B3 stale-row 구간, Bingo allocation, Spot subscription retry, actor ref 재결합,
first-success evidence, typed 설정, connector helper 이관과 ledger를 다시 확인했고
**NO HIGH/MEDIUM NET-NEW ISSUES**로 판정했다. 현재 확인된 미완료 .NET spec gap은 없다.
**LOOP CLEAN**.
