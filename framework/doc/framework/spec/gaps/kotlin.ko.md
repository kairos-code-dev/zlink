# Kotlin — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> Java 런타임을 공유하므로 **Kotlin 고유 표면(`suspend`·`Flow`·DSL)**의 갭만 여기 둔다. 런타임 동작 갭은 [java](java.ko.md)가 소유한다.

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

**전체 8건. 완료 4건.**

### 언어별 표면 차이 (기준선 대조)

- [x] **§12.3** — Kotlin wrapper의 계약 밖 disconnect/reconnect를 제거하고, 공유 Java connector의 동시 connect와 자동 reconnect가 하나의 진행 중 시도를 공유하도록 고쳤다. Java 전체 Gradle 테스트, Kotlin ObservabilityOps Trigger build, Java GameQuest 전체 self-check, OBS-B1 통과. 구현 커밋 `943486d05`(2026-07-15).
- [x] **§12.14** — Kotlin compression option helper가 `maxReceivedMessages`를 그대로 보존한다. 집중 회귀 테스트와 Kotlin module 전체 테스트 통과. 구현 커밋 `f7787358d`(2026-07-15).
- [x] **§12.19** — Java typed 호출이 raw payload를 거부하고 Kotlin request 완료 표면을 `awaitReply<T>()`로 통일했다. 집중 계약 테스트, Java connector·Kotlin module 전체 테스트, Kotlin SpotService 전체 E2E와 GameQuest·Bingo·TicTacToe 전체 self-check 통과. 구현 커밋 `c372ebbfc`(2026-07-15).

### 전 언어 공통 계약 갭 (모든 언어가 함께 닫는다)

- [x] **§12.20** (결함) — Kotlin이 공유하는 Java connector에서 응답 header의 `name_len`을 0으로 고정하고, pending request의 원래 이름으로 완료 payload를 구성한다. Java·Node 상호운용과 Kotlin module 전체 테스트 통과(2026-07-15).
- [ ] **§12.21** (결함+미구현) — `yield` terminator 부재 + `async`가 자동으로 turn을 반납
- [x] **§12.22** — 공유 Java 서버 HTTP client의 네 완료 방식과 Spring execution turn bean을 사용하고 coroutine용 turn 유지 `await`와 turn 반납 `yieldAwait`를 제공한다. Java/Kotlin HTTP client와 Spring starter 테스트가 통과했다. 구현 커밋 `6a62b031d`, `49c40c2fe`.
- [x] **§12.23** — 공유 Java 런타임의 `runCpuWorker`와 비동기 `runIoWorker`를 사용하며 두 표면의 `submit`·`yield`를 coroutine에서 기다릴 수 있다. I/O 집중 테스트에서 CPU pool thread·queue 사용량이 0임을 확인했고 core·Kotlin 테스트가 통과했다. 구현 커밋 `146afe0a5`.
- [x] **§12.24** — 공유 Java runtime이 같은 node local join을 caller turn에서 orchestrate하고 source
  `OnLeaveActor`와 target commit·`OnJoinedActor`를 순서대로 완료한다. Kotlin Config 8의 user Spot
  A→B `TD-E2`와 A→B·B→A 동시 `TD-E3`, core·Kotlin 전체 테스트가 통과했다. 구현 커밋
  `175d60d13`(2026-07-16).

본문은 [갭 인덱스](../90-implementation-gap.ko.md)가 소유한다. **§12.21과 §12.24는 한 묶음이다** — join orchestration을 먼저 바로잡지 않고 자동 turn dispatch만 걷어내면 user Spot → user Spot join이 즉시 막힌다.

## 2. 언어별 표면 차이 상세

### §12.3 근거 없는 공개 표면과 connect 상태 처리 (Java, Kotlin)

**해결(Java, Kotlin).** Kotlin wrapper와 공유 Java connector에서 계약에 없는 수동 `disconnect()`와
`reconnect()`를 제거했다. 재접속은 자동 reconnect가 담당하며, `Connecting`과 `Reconnecting`에서
`connect()`를 다시 호출하면 진행 중인 future를 기다린다.

호출마다 새 시도를 만들고 중복을 사후 정리하는 안과 lifecycle이 시도 future 하나를 소유하는 안을
비교해 후자를 선택했다. coroutine 호출자는 reconnect scheduler나 transport 시도 수를 알 필요 없이
`connect().await()`만 사용한다. 공유 Java 표면에서는 원격 node를 직접 지정하던
`ZLinkActorPlacement(preferredNodeRid, routeMesh)`와 ensure overload도 제거했다.

구현 전 공개 메서드·placement 타입 부재 검사와 동시 connect 단일 transport 검사가 모두 실패했다.
OBS-B1을 자동 재접속으로 바꾸는 과정에서 드러난 `ZLinkSessionContext.close()`의 아무 동작도 하지 않는
구현도 runtime이 종료 control 전송을 소유하도록 고쳤다. 구현 뒤 Java 전체 Gradle 테스트, Kotlin
ObservabilityOps Trigger build, Java GameQuest 전체 self-check와 실제 자동 재접속 3회를 수행하는 OBS-B1이
통과했다. 구현 커밋
`943486d05`(2026-07-15).

### §12.14 Kotlin option helper가 수신 한도를 되돌린다 (Kotlin)

**해결(Kotlin).** compression option helper가 options를 복사할 때 `maxReceivedMessages`도 함께
전달한다. 별도의 복사 전용 추상화를 추가하는 안과 기존의 한 곳뿐인 복사 함수에서 빠진 값을
보완하는 안을 비교해 후자를 선택했다. 새 interface를 만들지 않으면서 수신 대기열 정책의 복사
지점을 한 곳으로 유지한다. 한도를 7로 지정한 뒤 compression을 켜거나 꺼도 7이 유지되는 집중
회귀 테스트와 Kotlin module 전체 테스트가 통과했다. 구현 커밋 `f7787358d`(2026-07-15).

### §12.19 typed 표면 경계 (Java, Kotlin)

**해결.** Kotlin wrapper에서 목표 계약에 없던 request `await<T>()` overload 2개를 제거하고 typed·raw
request의 generic 완료 이름을 모두 `awaitReply<T>()`로 통일했다. 호환 별칭을 남기는 안과 계약 이름만
유지하는 안을 비교해 후자를 선택했다. 같은 완료 동작을 두 이름으로 노출하지 않아 coroutine 호출자가
raw 완료와 typed 완료의 이름 차이를 추측하지 않아도 된다.

공유 Java connector의 typed `send(Object)`와 `request(Object)`도 runtime type이
`ZLinkStreamEncodedPayload`이면 raw overload를 사용하라는 `IllegalArgumentException`을 발생시킨다.
raw 값을 typed codec이 다시 인코딩하지 않으므로 raw payload의 packet name과 bytes는 raw 표면이
소유한다. SpotService와 Kotlin 샘플의 호출부는 `awaitReply<T>()`로 옮겼고, HTTP client의 별도 coroutine
`await<T>()`는 변경하지 않았다.

구현 전 `typedCallsRejectRawEncodedPayloadHiddenAsObject`와
`kotlinRequestCompletionSurfaceUsesOnlyContractNames`가 각각 raw payload 수용과 계약 밖 overload를
검출해 실패하는 것을 확인했다. 구현 뒤 두 집중 테스트, Java connector·Kotlin module 전체 테스트,
Kotlin SpotService 전체 E2E와 GameQuest·Bingo·TicTacToe 전체 self-check가 통과했다. 구현 커밋
`c372ebbfc`(2026-07-15).

## 라운드 2 (2026-07-14)

**Kotlin 고유 갭은 새로 나오지 않았다.** Kotlin은 Java 런타임을 공유하므로 라운드 2의 Java 항목
(**IMP-JV-11 ~ IMP-JV-20**)과 교차 언어 항목(**IMP-X5·IMP-X6**)이 **그대로 적용된다.**
[java 체크리스트](java.ko.md)를 함께 본다.

## 라운드 3 (2026-07-14)

**Kotlin 고유 갭은 이번에도 나오지 않았다.** 공개 표면 전체가 Kotlin 카탈로그와 일치한다.

Kotlin은 Java 런타임을 공유하므로 라운드 3의 Java 항목(**IMP-JV-21 ~ IMP-JV-33**)이 **그대로
적용된다.** 특히 아래 둘은 Kotlin 사용자에게도 그대로 열려 있다.

- **IMP-JV-21** — `systems.zlink.framework.execution`의 내부 실행기가 public이다. Kotlin 앱도
  spot의 turn 큐에 직접 작업을 밀어 넣거나 공유 worker pool을 `close()`할 수 있다.
- **IMP-JV-24** — Spring host의 두 자동 drain 경로는 스펙과 같은 30초를 사용한다. Kotlin Spring Boot 앱도 검증된 같은 경로를 탄다(구현 커밋 `a0e2bb977`).

공유 런타임의 **IMP-JV-03**과 **IMP-JV-06**도 닫혔다. Kotlin 호출자가 drain waiter에 timeout을
적용해도 공유 drain 상태를 바꾸지 않으며, 값을 버리던 channel `metadata(k,v)` 표면은 Kotlin에서도
더 이상 노출되지 않는다. 집중 테스트와 Java core 전체 테스트가 통과했다(구현 커밋 `3db218ee0`).

공유 startup validator의 **IMP-JV-05**도 닫혔다. Kotlin 구성에서도 router/pub-sub capability가
없거나 활성 capability의 bind endpoint가 없으면 시작 전에 설정 오류로 거부한다. 세 집중 테스트와
Java core 전체 테스트가 통과했다(구현 커밋 `d7a62647e`).

공유 drain runtime의 **IMP-JV-01**도 닫혔다. Kotlin actor factory가 등록된 Spot peer는
`actor:<type>` capability를 게시하며, drain은 정확히 일치하는 type을 지원하는 원격 노드만 고른다.
exact-match 집중 테스트, Java core와 Kotlin module 전체 테스트가 통과했다(구현 커밋 `23f066b2e`).

공유 location resolver의 **IMP-JV-09/IMP-X1**도 닫혔다. live owner row라도 `actorRef`가 없는
pending actor는 resolve miss로 처리한다. pending 집중 테스트, Java core와 Kotlin module 전체
테스트가 통과했다(구현 커밋 `0af3e6ec6`).

공유 HTTP client의 **IMP-JV-18/19**도 닫혔다. proxy 자격증명은 proxy challenge에만 제공되고,
redirect hop과 body read는 retry attempt 하나의 deadline을 함께 사용한다. 두 집중 실패 테스트와
HTTP client 전체 테스트가 통과했다(구현 커밋 `6fc35fb4a`).

[java 체크리스트](java.ko.md)를 함께 본다.

## 라운드 4 (2026-07-14) — 샘플 · E2E

> **"Kotlin 고유 갭은 없다"는 앞의 판단을 정정한다.** 그건 **런타임에만** 맞다.
> **샘플과 e2e는 별개 코드베이스이고, Kotlin은 양방향으로 갈린다.**

**Kotlin이 더 나은 축**: SupportChat 자동 연결, ShoppingMall의 읽기 전용 query와 완전한 §15 단언,
DeliveryDispatch의 actor relay(Java는 건너뛴다).

**Kotlin이 더 나쁜 축**:

- [ ] **SMP-KT-01** (**절대 규칙 위반**) — TicTacToe 밖 샘플이 **수동 연결을 쓴다**(12곳)
- [ ] **SMP-KT-02** (결함) — ShoppingMall이 **문서가 "사라진다"고 한 saga 오케스트레이터를 되살렸다** — 비내구 in-process 큐라 크래시 시 continuation을 잃는다(무손실 요구 위반)
- [ ] **SMP-KT-03** (미구현) — ShoppingMall에 **HTTP edge가 없고**, 내부 메시지 `ContinueOrderWorkflowReq`를 **클라이언트가 직접** 보낸다
- [ ] **SMP-KT-04** (미구현) — GameQuest·ShoppingMall에 **owner Spot이 없다**
- [x] **SMP-KT-05** (결함) — DeliveryDispatch와 ShoppingMall의 coroutine 실행 경계를 명시했다
  - 근거: suspend handler를 가진 모든 역할 host가 `Dispatchers.Default`를 명시하고, ShoppingMall의
    suspend 재시도·poll 대기는 `delay`를 사용한다. 두 runner의 source gate와 전체 실행이 통과했다.
- [x] **SMP-KT-06** (결함) — DeliveryDispatch 기본 클라이언트가 **HTTP 폴링 루프**다(규약 금지)
  - 근거: scaffold 분기와 notification HTTP polling을 삭제해 public stream connector 경로 하나만
    남겼다. 금지 source gate와 DeliveryDispatch 전체 runner가 통과했다.
- [ ] **SMP-KT-08** (결함) — DeliveryDispatch가 모든 고객 상태 push를 **`customer-1` actor로 보낸다**
  - 부분 구현: Dispatch가 생성 요청의 `customerId`를 상태 변경 계약에 싣고 Tracking이 그 값으로
    actor를 찾도록 고쳤다. 다만 `SubscribeDeliveryReq`에는 고객 식별자가 없고 session은 여전히
    `customer-1`로 고정된다. 공개 인증/session identity 계약 없이 wire 필드를 발명할 수 없어 §0.8에
    따라 이 부분은 open이다.
- [x] **SMP-KT-09** (**실패할 수 없는 단언**) — DeliveryDispatch client가 상태 push의 **도착 순서를 검증하지 않는다**
  - 근거: 상태별 `waitFor` completion callback이 실제 도착 순서를 기록하고, success와 reassignment가
    정본 순서와 정확히 같은지 단언한다. 전체 DeliveryDispatch runner가 통과했다.
- [ ] **SMP-KT-10** (미구현) — Bingo의 번호 매긴 release gate가 **join·start·card·draw·reward 필드를 빠뜨린다**
  - 부분 구현: card 두 장, draw 양쪽 state, reward room/draw sequence 단언을 추가해 전체 runner가
    통과했다. 전적은 현재 wire에 필드가 없고 joining actor의 bound session은 `onJoinedActor` 중에는
    push 준비 전이라 두 번째 start notify를 보낼 공개 lifecycle 시점이 없어 §0.8에 따라 open이다.
- [x] **SMP-KT-11** (미구현) — TicTacToe의 번호 매긴 release gate가 **topology·player·join·milestone 필드를 빠뜨린다**
  - 근거: endpoint 중복과 전체 node 매핑, 두 player의 사용자 정보, join과 milestone의 사용자·room
    필드를 직접 단언한다. runner의 source gate와 `PASS TicTacToe.Kotlin` 전체 실행이 통과했다.
- [x] **E2E-KT-01** (결함) — SpotService Client를 일반 클라이언트 프로세스로 분리했다
  - 근거: Client의 Spring framework host와 driver spot을 삭제하고 시나리오가 역할 서버의 작은 HTTP
    operation endpoint만 호출하도록 바꿨다. runner의 금지 source gate와 `SM-F3`, `SM-B1`, 전체
    `all` 실행으로 검증한다.
- [ ] **E2E-KT-02** (결함) — **Config 10이 2/20**이고 나머지 18개를 **Java 클라이언트에 위임**한다. `Shared/`도 `feature-map`도 없고 Redis 컨테이너 접두사도 틀렸다
- [ ] **E2E-KT-03** (결함) — Config 2에서 시나리오 **6개 누락**. `RC-A6`(**P0**)는 클라이언트 시나리오 없이 **셸 `grep`으로** 검증하는데 feature-map은 "구현 완료"로 적는다
- [ ] **E2E-KT-04** (결함) — `DiscoveryRegistryHa`에 **`Client/Scenarios/`가 없다** — 32줄 `when`이 477줄 god-context로 분기한다(규약이 금지한 `AllScenario` 형태)

## 라운드 6 — E2E 전 config 구성 축·Config 1 심층

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-KT-07** (결함) | [E2E README §2.1:150-177](../../common/e2e/README.ko.md): readiness 기본값은 **3초**, poll은 0.1초이며 runner 상단의 명시적 상수로 둔다 | `SpotService/run_e2e.sh:31-33`은 30초, `RuntimeMonitoring/run_e2e.sh:23-24`와 `DiscoveryRegistryHa/run_e2e.sh:29-30`은 60초다. `RegistryMessaging`·`PubSub`도 `LOCAL_READINESS_ATTEMPTS=200`과 0.1초 poll로 20초를 허용한다(`RegistryMessaging/run_e2e.sh:12-14`, `PubSub/run_e2e.sh:24-26`). 긴 대기가 네 config의 수렴 실패를 가린다 |
| **E2E-KT-08** (미구현) | [E2E README:499-512](../../common/e2e/README.ko.md): 기본 외에 **reverse 1회 + 고정 seed shuffle 1회**를 최소 실행한다 | `e2e-kotlin/run_e2e_all.sh:24-28,50-53`은 모든 config에 `all`만 한 번 전달한다. `E2E_START_ORDER`를 실제로 읽는 Kotlin runner는 `ToActorMessaging/run_e2e.sh:15-16` 하나뿐이며 통합 게이트가 reverse/shuffle을 호출하는 곳은 0건이다. ⇒ 대부분 config는 축이 없고, 유일하게 구현한 config도 기본 게이트에서 forward만 돈다 |
| **E2E-KT-09** (미구현) | [E2E README:487-497,546-547](../../common/e2e/README.ko.md): Config 2·9 P0는 **route mesh 없음 × session/spot 분리 배치** 조합을 실행한다 | Config 2의 Play와 Session이 route mesh를 조건 없이 등록한다(`SpotService/.../PlayApplication.kt:82-98`, `.../SessionApplication.kt:78-84`). runner의 P0 topology selector에도 route mesh 제거 변형이 없다(`SpotService/run_e2e.sh:734-765`). Config 9는 E2E-JV-16과 같은 두 역할(actor/caller)뿐이라 session 분리 자체가 없다. ⇒ 요구 조합이 생성되지 않는다 |
| **E2E-KT-10** (**가짜 통과**) | [config-1 RM-C2:174-182](../../common/e2e/config-1-location-messaging.ko.md)는 미존재 rid의 **public error**를, [RM-C4:194-202](../../common/e2e/config-1-location-messaging.ko.md)는 **timeout + 늦은 handler 완료**를 요구한다 | `RmC2TargetedRouteScenario.kt:26-27`은 앱이 만든 `failed` boolean만 본다. `RmC4TimeoutIsolationScenario.kt:11-24`도 `failed` 하나와 follow-up evidence만 보고 slow 요청의 완료 evidence는 검사하지 않는다. ⇒ 임의 예외나 즉시 실패도 두 시나리오를 통과시킨다 |
| **E2E-KT-11** (해결) | [config-1 RM-C8:228-238](../../common/e2e/config-1-location-messaging.ko.md): 상한 근접 왕복뿐 아니라 **`MaxMessageSize` 초과 거부와 이후 회복**을 검증한다 | Java 공용 runtime의 build-time server socket 설정을 Kotlin provider가 사용한다. 2 MiB 상한에서 3 MiB request의 `TimeoutException`과 후속 정상 request를 검증했으며 `./run_e2e.sh RM-C8`이 통과했다. 구현 커밋 `e1144b5bf`. |
| **E2E-KT-12** (결함) | [E2E README §2.6:336-353](../../common/e2e/README.ko.md): **로그/evidence 경로를 JVM system property로 전달하지 않는다** | Kotlin E2E의 `System.getProperty` 세 곳은 모두 `java.io.tmpdir`을 **로그 경로 기본값**으로 읽는다 — `RegistryMessaging/.../ConsumerOptions.kt:30`, `.../Provider/.../ServerOptions.kt:37`, `.../Workflow/.../ServerOptions.kt:30`. 즉 금지 대상을 정확히 읽고 있으며, 이 config의 역할 서버 셋이 CLI/config 파일 밖의 JVM 전역 상태에 의존한다 |
| **E2E-KT-13** (미구현) | [E2E README:514-519](../../common/e2e/README.ko.md): 표면을 넘은 actor ref는 **`generation > 0`**을 어서션한다 | `EnsureActorHandler.kt:18-25`가 generation을 응답에 싣고 `RemoteActorAuthHandler.kt:26-35`가 그대로 `ActorRef` 생성에 사용하지만 양수 검사는 없다. client-visible `ActorAuthRes`는 generation을 아예 버린다(`SpotService/Shared/.../Contracts.kt:263-270`). Kotlin E2E production source 전체에 generation 양수 단언은 0건이다 |

### Kotlin 샘플 추가 감사 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-KT-08** | `common/sample/deliverydispatch/README.ko.md:300,326,478-482` — 배송 생성 요청의 `CustomerId`가 해당 배송의 고객을 정하며, Tracking의 상태 변경은 `CustomerEntry`와 `CustomerActor`를 거쳐 **그 고객의 stream client**로 push된다 | **부분 구현:** `DeliveryStatusChangedReq`에 정본의 `customerId`를 복원하고 Dispatch → Tracking → `DeliveryStatusUpdatedMsg`까지 그대로 전달한다. Tracking의 `customer-1` 하드코딩은 제거했고 runner가 회귀를 막는다. 그러나 공통 `SubscribeDeliveryReq`는 `DeliveryId`만 가지며 현재 session identity는 상수다. 두 고객 session을 구분하려면 인증 identity 계약이나 공통 wire 결정이 먼저 필요하므로 이 항목은 open이다. |
| **SMP-KT-09** | `common/sample/deliverydispatch/README.ko.md:671-687` — 성공 배송은 `Assigned → Accepted → PickedUp → Delivered`, 재배정 배송은 `Assigned → Reassigned → Accepted → Delivered`가 **도착한 순서대로** 검증되어야 한다 | **해결:** public `waitFor`는 그대로 사용하되 각 completion callback에서 실제 도착 상태를 기록한다. 모든 wait가 끝난 뒤 기록 순서를 정본 목록과 정확히 비교하고 courier도 같은 notification 목록에서 확인한다. POSD 재리뷰에서는 독립 future를 기대 순서로 await해 시간 정보를 잃는 것을 위험 신호로 보았다. 별도 inbox/polling을 추가하는 안보다 기존 public wait completion에서 순서를 기록하는 안이 새 대기 표면과 중복 subscription을 만들지 않아 이를 선택했다. domain 경계 변화는 없으며 전체 DeliveryDispatch runner가 통과했다. |
| **SMP-KT-10** | `common/sample/bingo/README.ko.md:567-584` — join push의 전적, **두 client의** game-start, card 제출 응답의 두 9칸 card, draw 양쪽 state 일치, reward의 `RoomId`·`DrawSeq`까지 단계별로 직접 확인한다 | **부분 구현:** client는 join state의 두 actor, 두 card의 9칸 상태, 매 draw의 전체 state 동일성, reward room과 마지막 draw sequence를 직접 단언하며 전체 runner가 통과했다. 그러나 `BingoPlayerState` wire에는 `Wins`·`Losses`가 없고, joining actor를 start event recipient에 포함하는 red 실험은 bound-session relay 준비 전 push를 유실해 timeout으로 실패했다. sleep이나 별도 비동기 worker로 우회하지 않았다. 공통 wire 전적 필드와 join 완료 뒤 push 가능한 lifecycle 계약이 필요하므로 open이다. |
| **SMP-KT-11** | `common/sample/tictactoe/README.ko.md:523-557` — `PlayEndpoints` 두 개와 전체 `PlayNodes` 매핑, host·guest의 display name/level, join push의 `DisplayName`·`Level`·`RoomId`, milestone의 `DisplayName`·`RoomId`를 확인한다 | **해결:** client가 서로 다른 endpoint 두 개 이상, owner 포함, endpoint와 `PlayNodes`의 일대일 집합 매핑, 비어 있지 않은 node rid를 확인한다. host·guest 인증의 display name과 최소 level, join notify의 display name·level·room, milestone의 actor·display name·room을 기존 wins·receiving rid와 함께 단언한다. POSD 재리뷰에서는 topology 검증을 별도 shallow helper로 숨기는 안보다 응답을 소비하는 시나리오 위치에 계약 단언을 두는 안을 선택했다. domain과 transport 경계는 바뀌지 않았고 전체 runner가 통과했다. |

## 라운드 4 상세 — 샘플 · E2E (뒤늦게 채운 근거)

**이 절은 라운드 4 체크리스트(SMP-KT-01~06 · E2E-KT-01~04)의 근거를 채운 것이다.**
당시 체크리스트만 적고 계약↔구현 대조를 남기지 않아 작업자가 집어서 고칠 수 없었다.

### 샘플

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-KT-01** (**절대 규칙 위반**) | [샘플 규약:133-143](../../common/sample/README.ko.md): 서버 간 연결은 공유 location store 기반 자동 연결로 구성한다. **절대 규칙: TicTacToe만 수동 연결을 사용할 수 있다.** "즉 `EnableClient(endpoint)`, `ConnectRouter(...)`, `ConnectPeerPub(...)` ... 를 사용하지 않는다", "위반이 하나라도 있으면 해당 샘플 변경은 완료된 것으로 판단하지 않는다" | TicTacToe 밖 샘플의 수동 연결이 `src` 기준 **12곳**이다 — `enableClient(endpoint)` 3곳(`Bingo/Server/Api/.../ApiServerApplication.kt:42-43`, `GameQuest/Server/GameApi/.../Program.kt:99`)과 `connectRouter`/`connectPeerPub` 9곳(`Bingo/Server/Session/.../SessionServerApplication.kt:49`, `Bingo/Server/Play/.../PlayServerApplication.kt:70`, `DeliveryDispatch/Server/CourierSession/.../CourierSessionApplication.kt:39,43`, `.../Dispatch/.../DispatchServerApplication.kt:53,57`, `.../CourierGateway/.../CourierGatewayApplication.kt:41,45`, `.../CourierSpotNode/.../CourierSpotNodeApplication.kt:41`). 같은 언어의 SupportChat·ShoppingMall은 인자 없는 `enableClient()`만 쓰므로 **가능하다는 것을 스스로 증명한다.** ⇒ location store 등록·조회·연결 lifecycle이 끊겨도 이 샘플들은 초록으로 뜬다 |
| **SMP-KT-02** (결함) | [shoppingmall:219](../../common/sample/event/shoppingmall.ko.md): "saga 오케스트레이터 / 단계 소비자"는 **사라지고** owner spot이 이벤트 접기로 다음 단계를 판정해 직접 진행한다. [:555-566](../../common/sample/event/shoppingmall.ko.md): `StartOrderWorkflowReq` handler는 `Created`까지만 돌리고 같은 흐름 안에서 **`ContinueOrderWorkflowReq` 호출을 기다리지 않고 예약**한다 — crash 뒤 재개와 **같은 메커니즘**이다 | `Server/OrderWorkflow/.../handlers/StartOrderWorkflowHandler.kt:22` — framework 재개 호출 대신 `continuations.enqueue(request.orderId)`로 프로세스 메모리 큐에 넣는다(`WorkflowContinuationQueue.kt:11-16`, 맨 `LinkedBlockingQueue`). `WorkflowSagaWorker.kt:13-27,37-53`이 daemon Thread 하나(`"shoppingmall-workflow-saga-worker"`)로 그 큐를 빼서 `workflow.continueWorkflow(orderId)`를 **직접** 호출한다. ⇒ 문서가 사라진다고 한 **saga worker가 이름까지 그대로 살아 있다.** 큐가 비내구·프로세스 로컬이라 노드가 죽으면 대기 중이던 continuation이 사라지고, 재개가 framework 메시지 경로를 타지 않으므로 owner 라우팅·순서 실행 보장도 받지 못한다 |
| **SMP-KT-03** (미구현) | [shoppingmall:18,266,305](../../common/sample/event/shoppingmall.ko.md): 클라이언트는 `CommerceApi`에 **HTTP로** 주문 시작·상태 조회를 요청하고, 클라이언트가 마주하는 창구는 `CommerceApi` 하나뿐이다. [:450-452,559-561](../../common/sample/event/shoppingmall.ko.md): `ContinueOrderWorkflowReq`는 owner spot이 **자기 자신에게 예약하는 내부 재개 명령**이다 | `Server/CommerceApi/.../CommerceApiApplication.kt:67` — `.web(WebApplicationType.NONE)`. HTTP endpoint가 0개다(`@RestController`·`@PostMapping` grep 0건). 클라이언트가 대신 **자기가 framework 호스트**가 되어(`Client/.../ClientApplication.kt:18-19,29-35`) `requestToChannel(commerceApiChannel(...), ...)`로 채널 요청을 보내고, `Client/.../ShoppingMallClientScenario.kt:133-141`에서 내부 재개 명령 `ContinueOrderWorkflowReq`를 **클라이언트가 직접** 보낸다. `.NET`은 같은 자리에 `MapPost`/`MapGet` HTTP edge를 둔다(`dotnet/samples/ShoppingMall/Server/CommerceApi/Program.cs`). ⇒ "HTTP 진입 + 내부 메시지 은닉"이라는 이 샘플의 구도가 통째로 뒤집혔다 |
| **SMP-KT-04** (미구현) | [gamequest:13,19,124-127](../../common/sample/event/gamequest.ko.md): player별 quest 판정은 **`PlayerQuestSpot`**이 맡고 `PlayerId` 기준 owner로 event를 직렬 처리한다. [shoppingmall:350](../../common/sample/event/shoppingmall.ko.md): 어느 `CommerceApi`로 들어와도 owner 라우팅이 항상 같은 **`OrderWorkflowSpot`**으로 보낸다 | 두 샘플 소스 전체에 `spot` 문자열이 **0건**이다(`samples/kotlin/{GameQuest,ShoppingMall}/**/src`). GameQuest는 `Server/QuestMission/.../Program.kt:79-81`에서 평범한 client-server channel(`questOwnerChannelFor(instanceName)`) + `quest-owner` handler group으로 끝나고, ShoppingMall은 `Server/OrderWorkflow/.../OrderWorkflowApplication.kt:43-45`가 `workflowChannel(instanceId)` server 하나다. owner 선택도 framework spot 배치가 아니라 앱이 한다 — `Server/CommerceApi/.../OrderWorkflowRouter.kt:38-39`의 `topology.workflowInstanceForOrder(orderId)`. ⇒ 이 두 샘플의 존재 이유인 **owner spot의 직렬 실행·이동·location 조회를 한 번도 실행하지 않는다** |
| **SMP-KT-05** (결함) | [kotlin guide 02:30-33](../../kotlin/guide/02-getting-started.ko.md): `useCoroutineHandlers(...)`는 suspend handler를 실행할 coroutine dispatcher/scope를 지정하는 설정이다. [kotlin guide 03 §6:53-55](../../kotlin/guide/03-concepts.ko.md): **"handler 안에서 blocking 호출(`Thread.sleep`, blocking JDBC, `CompletableFuture.join` 등)을 직접 쓰지 않는다."** 불가피하면 `withContext(Dispatchers.IO)`로 옮긴다 | **해결:** DeliveryDispatch의 handler host 6개와 ShoppingMall의 CommerceApi·OrderWorkflow가 `useCoroutineHandlers(Dispatchers.Default)`를 직접 설정한다. ShoppingMall의 channel 재시도와 client 상태 poll은 `LockSupport.parkNanos` 대신 suspending `delay`를 사용한다. 동기식 `CommerceStore`의 파일 lock 획득은 coroutine handler가 아닌 임계 구역이라 이 변경에 섞지 않았다. POSD 재리뷰에서는 fallback 의존을 설명만 하는 안과 각 host가 실행 정책을 명시하는 안을 비교해 후자를 선택했고, 별도 pass-through 설정 helper 없이 정책이 필요한 구성 지점에 둔다. DDD 모델과 wire 계약은 바뀌지 않았다. 검증 중 삭제된 `CommerceApiInstanceOptions` 참조와 `--instance` runner가 typed config 계약과 어긋난 기존 결함도 발견해 중앙 `SampleTopology`와 역할별 config 파일로 통합했다. 두 runner는 설정·blocking 경로 회귀 gate를 거쳐 전체 client/server 실행을 통과했다. |
| **SMP-KT-06** (결함) | [샘플 규약:365-371](../../common/sample/README.ko.md): push message 대기는 **sample-local polling 함수가 아니라 stream connector의 public wait interface**를 사용한다. "notification 수집용 inbox나 로그 queue는 ... push 도착을 기다리는 기준 경로가 되어서는 안 된다." [deliverydispatch:689-691](../../common/sample/deliverydispatch/README.ko.md)도 같은 규칙 | **해결:** `runScaffold`, `waitNotifications`, `/notifications` polling, `--stream-runtime` 선택 축과 사용되지 않는 role URL option을 삭제했다. client는 항상 typed stream connector의 `waitFor`로 push를 기다린다. POSD 재리뷰에서는 금지 polling을 남기고 기본값만 바꾸는 안과 두 구현 중 connector 경로만 남기는 안을 비교해 후자를 선택했다. 시나리오 정책·option·HTTP helper 중복 230줄을 제거했고 domain 경계는 바뀌지 않았다. source gate와 전체 runner가 통과했다. |

### E2E

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-KT-01** (결함) | [E2E README §2:47-50](../../common/e2e/README.ko.md): "client 코드에서 channel/fanout/spot framework client, **framework host 구성**, test-only helper를 직접 사용하지 않는다". [:52-53](../../common/e2e/README.ko.md)·[§2.7:385-387](../../common/e2e/README.ko.md): request/send/publish 같은 framework 호출은 **실제 역할 server endpoint 내부에서만** 수행한다 | **해결:** `ClientApplication`과 `ClientDriverSpot`을 삭제해 Client가 Spring framework host, route server, spot router, location store를 구성하지 않는다. `ClientProgram`은 일반 프로세스에서 scenario를 실행하고, `SpotHttpDriver`는 Play 역할의 작은 state·outbound·route endpoint만 호출한다. route request는 대상과 다른 Play 역할에서 실행해 self-request timeout도 제거했다. runner는 Client source의 framework host·runtime 호출 재도입을 금지한다. POSD 재리뷰에서는 Client 안의 framework 호출을 wrapper로 숨기는 안과 역할 서버가 framework 책임을 흡수하는 안을 비교해 후자를 선택했다. 이로써 topology·location·runtime lifecycle 정보가 Client에서 제거됐고 domain 경계는 바뀌지 않았다. `SM-F3` route 묶음과 `SM-B1` actor-session 묶음이 통과했으며 전체 `all` runner로 최종 검증했다. |
| **E2E-KT-02** (결함) | [config-10](../../common/e2e/config-10-spot-actor-transfer.ko.md): ST 시나리오 **20개**(`ST-A1`~`ST-F6`). [E2E README §2.2:220-226,241-242](../../common/e2e/README.ko.md): config마다 `Shared/`와 `feature-map.ko.md`를 둔다. [§2.7:403-405](../../common/e2e/README.ko.md): Redis container 이름은 Kotlin e2e면 `zlink-redis-kotlin-e2e...`처럼 언어 범위를 드러낸다 | `SpotActorTransfer/Client/.../Program.kt:25-30` — `when (scenario) { "ST-E1","ST-E2" -> KotlinBoundSessionScenario(scenario).run(); else -> systems.zlink.e2e.spotactortransfer.client.Program.main() }`. **나머지 18개를 Java e2e client의 `main()`에 그대로 넘긴다.** `run_e2e.sh:5,12`도 12줄짜리 shim으로 `../../e2e/SpotActorTransfer/run_e2e.sh`를 실행할 뿐이라, Redis container를 만드는 것도 Java runner이고 이름은 `zlink-redis-java-e2e-spot-transfer`다(`e2e/SpotActorTransfer/run_e2e.sh:71`). config 루트에 `Shared/`도 `feature-map.ko.md`도 없다. ⇒ Kotlin이 Config 10을 검증한다고 말할 수 없다 |
| **E2E-KT-03** (결함) | [config-2](../../common/e2e/config-2-spot-service.ko.md): Track A~G 시나리오 **51개**. [config-4 RC-A6:103-110](../../common/e2e/config-4-registration-codec.ko.md)(**P0**): duplicate kind+packet·잘못된 handler group·미지원 channel kind 조합을 **각각** startup에서 거부하는지 본다. [E2E README §2.5:310,330-332](../../common/e2e/README.ko.md): 시나리오 ID 하나 = client scenario 파일 하나. [§2.8:437-446](../../common/e2e/README.ko.md): feature-map은 skip 목록이 아니라 근거를 남기는 표다 | (a) `SpotService/Client/.../scenarios/`에 47개 파일이 있으나 51개 중 **`SM-B2`·`SM-B4`·`SM-B9`·`SM-C5`·`SM-D2`·`SM-D15` 6개가 없고**, 문서에 ID가 없는 `SmQ9Scenario.kt`·`SmRemoteActorSessionScenario.kt` 2개가 대신 들어 있다. (b) `RC-A6`에는 client 시나리오 파일이 **아예 없다** — `RegistrationCodec/run_e2e.sh:151-166`이 invalid 서버를 띄워 종료 코드가 0이 아닌지 본 뒤 로그를 `grep -Eq "duplicate\|Duplicate\|registration\|packet"`으로 훑고 `echo "scenario RC-A6 passed"`를 찍는다. 세 축 중 duplicate 하나만 만들고, 저 grep은 "packet"이나 "registration"이 든 **어떤 기동 실패 메시지든** 통과시킨다. 그런데 `RegistrationCodec/feature-map.ko.md:18`은 상태를 `구현 완료`로 적는다. (체크리스트 한 줄이 두 config를 붙여 놨다 — **누락 6개는 Config 2**, **`RC-A6`은 Config 4**다. Java·`.NET`은 둘 다 `InvalidRegistrationScenario`라는 client scenario 파일로 이걸 검증한다) |
| **E2E-KT-04** (결함) | [E2E README §2.5:310,328-332](../../common/e2e/README.ko.md): 시나리오는 `Client/Scenarios/` 아래에 scenario별 파일로 분리하고, "여러 시나리오를 하나의 `AllScenario`, `ScenarioSet`, `DriverScenario` 파일로 묶어 driver에 위임하지 않는다". [:312-313](../../common/e2e/README.ko.md): support 코드에는 option parsing·assertion·process lifecycle 같은 보조 코드만 둔다 | `DiscoveryRegistryHa/Client/`에 `Scenarios/` 디렉토리가 없다. `ClientScenario.kt:12-31`(32줄)이 `when`으로 15개 selector(`SF-A1`~`SF-E1`과 `-RECOVERED`/`-HEALTHY` 변형)를 분기하고, 시나리오 본문 전부가 `client/Support/ClientScenarioContext.kt`(**477줄**) 한 클래스의 메서드다(`:21,28,41,51,64,75,85,91,98,107,126,134,149`). 규약이 이름을 짚어 금지한 `AllScenario` 형태이며, 하필 보조 코드만 두라는 `Support/` 안에 있다. 같은 언어의 `RegistryMessaging`은 `Client/Scenarios/`에 ID별 파일을 두므로 **config마다 client 형태가 갈린다** |

## connector 공통 test helper 표면 ([32 §10.2](../stream-connector/32-stream-connector.ko.md))

**계약이 확정됐다**(spec §10.2 + connector 언어별 문서 03 §…). connector가 push 관측
표면(`expectNone`·`waitForSequence`)과 범용 단언 유틸(`ensure`·`expectFailure`·`expectTimeout`)을
공개 API로 제공한다.

**이 검증들은 각 언어가 이미 지역 helper로 손수 구현해 관련 갭을 닫아 둔 상태다**(그래서 아래 참조
SMP 항목들이 이미 `[x]`다). 이 작업은 **그 지역 helper를 connector의 공통 표면으로 끌어올려** 다섯
언어가 같은 API를 쓰게 하고, 앞으로 시나리오가 다시 손수 재구현하지 않게 한다. 교차 언어 순서
검증 항목 [SMP-X3](../90-implementation-gap.ko.md)의 "공통 게이트"가 바로 이 `waitForSequence`다.

- [x] **TH-KT-01** (미구현) — [java doc §13](../stream-connector/languages/java/03-stream-connector.ko.md)에 먼저 고정한 시그니처대로 `expectNone`·`waitForSequence`의 suspend `await()` wrapper와 `ZLinkKotlinStreamAssert`를 구현했다. 관측과 오류 분류는 Java connector에 남겨 정책 중복을 만들지 않았다. coroutine 관용 진입점과 suspend action 실행을 재검토했고 `KotlinConnectorWrapperTest`와 Kotlin module 전체 테스트가 통과했다. 구현 커밋 `22484d93e`(2026-07-15).
- [x] **TH-KT-02** (리팩토링) — DeliveryDispatch의 지역 `StatusWaits`·독립 `waitFor` 목록·잔여 `waitStatuses` wrapper를 삭제하고 Kotlin `waitForSequence`를 직접 사용한다. `.NET` TH-DN-02와 같은 결정으로 성공 배송이 다른 courier에게 전달되지 않는지도 coroutine 시작 시 등록한 `expectNone`으로 검증하고, 단언에는 필수 메시지가 있는 `ZLinkKotlinStreamAssert`를 사용한다. SupportChat의 수동 failure·timeout·negative push helper도 coroutine assert와 `expectNone`으로 교체했다. Bingo·TicTacToe·SpotService·AutomaticTurnDispatch의 AUTO dispatch negative 단언 역시 동작 전에 등록하는 `expectNone(...).within(...)`을 유지한다. Kotlin module 전체 테스트, DeliveryDispatch·SupportChat·Bingo·TicTacToe 전체 self-check, SpotService `SM-D6`, AutomaticTurnDispatch `ATD-D4`, runner 재도입 방지 검사가 통과했다. 구현 커밋 `22484d93e`, 추가 재검토 커밋 `9b5a8527e`, 최종 지역 helper 제거 커밋 `bdce6f188`(2026-07-15).
