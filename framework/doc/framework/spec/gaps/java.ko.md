# Java — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> Kotlin adapter가 이 런타임을 공유한다. Kotlin 고유 갭은 [kotlin](kotlin.ko.md)에 있다.

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

**전체 33건. 완료 10건.**

### 구현 감사에서 발굴 (2026-07-14, 스펙↔코드 직접 대조)

- [ ] **IMP-JV-01** (결함) — 40 §2.1
- [ ] **IMP-JV-02** (결함) — 24 §3·§5
- [ ] **IMP-JV-03** (결함) — 54 §6
- [ ] **IMP-JV-04** (결함) — 24 §4.1·05 §2.3
- [ ] **IMP-JV-05** (결함) — 20 §8
- [ ] **IMP-JV-06** (결함) — 05 §2.x
- [ ] **IMP-JV-07** (미구현) — 54 §9
- [ ] **IMP-JV-08** (미구현) — 40 §9
- [ ] **IMP-JV-09** (결함) — 40 §2.3
- [ ] **IMP-JV-10** (미구현) — 54 §3.4

### 교차 언어 결함 (여러 구현에 같은 문제)

- [ ] **IMP-X1** — pending actor row(`ActorRef` 비어 있음)를 resolve 성공으로 반환한다
- [ ] **IMP-X2** — location event source(`location-peer/spot/actor/route`, `StoreFailure`/`StoreRecovered`)가 없다
- [ ] **IMP-X3** — startup validation이 스펙의 설정 오류를 통과시킨다
- [ ] **IMP-X4** — location store read에 5초 취소 상한이 없다

### 언어별 표면 차이 (기준선 대조)

- [ ] **§12.1** — STREAM connector 수신 큐 overflow (Java)
- [ ] **§12.2** — actor join admission이 선택 사항 (Java, C++)
- [x] **§12.3** — 계약 밖 수동 disconnect/reconnect와 원격 actor placement 표면을 제거하고, 동시 connect와 자동 reconnect가 하나의 진행 중 시도를 공유하도록 고쳤다. Java 전체 Gradle 테스트, Kotlin ObservabilityOps Trigger build, Java GameQuest 전체 self-check, OBS-B1 통과. 구현 커밋 `943486d05`(2026-07-15).
- [x] **§12.4** — raw·typed send/request call에 `packetName(String)`을 추가하고 명시한 이름이 타입 기반 기본 이름보다 우선하도록 구현했다. 공개 표면 집중 테스트의 실패를 먼저 확인했고, 네 호출 경로의 wire 이름 테스트와 Java connector·Kotlin module 전체 테스트가 통과했다. 구현 커밋 `4071e369f`(2026-07-15).
- [ ] **§12.8** — monitoring 표면 (Java)
- [ ] **§12.9** — spot 전송 표면에 channel 이름을 함께 받는다 (Java)
- [x] **§12.10** — `ZLinkStreamTransport` 네 멤버를 공개하고 endpoint scheme 해석과 실제 연결 분기가 이 enum을 사용하도록 통합했다. 공개 표면 집중 테스트의 실패를 먼저 확인했고, Java connector·Kotlin module 전체 테스트가 통과했다. 구현 커밋 `aad0f8074`(2026-07-15).
- [x] **§12.12** — dispatch mode를 `MANUAL`/`IMMEDIATE`로 맞추고, Manual의 message·state·disconnected·error callback을 dispatch queue에서 실행하며 `dispatch().submit()`이 비동기 callback 완료까지 기다리도록 고쳤다. 집중 실패 테스트와 Java 전체 Gradle 테스트가 통과했다. 구현 커밋 `d339d4386`(2026-07-15).
- [x] **§12.13** — observer notification 큐 상한과 payload preview 바이트 한도를 options에 추가하고 실제 dispatcher가 사용하도록 연결했다. 공개 표면 테스트의 실패를 먼저 확인했고, 작은 큐 overflow·preview 절단 테스트와 Java connector·Kotlin module 전체 테스트가 통과했다. 구현 커밋 `900da7c0b`(2026-07-15).
- [ ] **§12.15** — 예외 정규화 부재 (Java)
- [x] **§12.16** — metadata wire 블록의 총 크기가 1024바이트를 넘으면 encode 전에 거부하도록 고쳤다. 경계값 1024 허용·1025 거부 테스트의 실패를 먼저 확인했고, Java connector·Kotlin module 전체 테스트가 통과했다. 구현 커밋 `47f7898af`(2026-07-15).
- [x] **§12.17** — Error JSON의 `code`·`message`를 검증해 파싱하고, 일치하는 `request_seq`가 있으면 pending request만 실패시키며 stream error callback에는 중복 발행하지 않도록 고쳤다. 집중 테스트의 실패를 먼저 확인했고 Java connector·Kotlin module 전체 테스트가 통과했다. 구현 커밋 `e00b77cc7`(2026-07-15).
- [x] **§12.18** — inbound callback 실행 범위에 내부 flow context를 설정하고 그 안에서 시작한 send/request가 같은 `flow_id`와 origin을 사용하도록 고쳤다. 실제 inbound→outbound wire 집중 테스트의 실패를 먼저 확인했고 Java connector·Kotlin module 전체 테스트가 통과했다. 구현 커밋 `e52cf94e2`(2026-07-15).
- [x] **§12.19** — Java typed 호출이 raw payload를 거부하고 Kotlin request 완료 표면을 `awaitReply<T>()`로 통일했다. 집중 계약 테스트, Java connector·Kotlin module 전체 테스트, Kotlin SpotService 전체 E2E와 GameQuest·Bingo·TicTacToe 전체 self-check 통과. 구현 커밋 `c372ebbfc`(2026-07-15).

### 전 언어 공통 계약 갭 (모든 언어가 함께 닫는다)

- [x] **§12.20** (결함) — 응답 header의 `name_len`을 0으로 고정하고, pending request가 보관한 원래 이름을 완료 payload에 사용한다. 구형 peer가 보낸 응답 이름은 decode 후 매칭에 사용하지 않는다. `ZLinkStreamWireProtocolTest`, `JavaNodeStreamInteropTest`, connector 전체 테스트 통과(2026-07-15).
- [ ] **§12.21** (결함+미구현) — `yield` terminator 부재 + `async`가 자동으로 turn을 반납
- [ ] **§12.22** (결함+미구현) — HTTP client가 framework 계약 밖에 있다
- [ ] **§12.23** (미구현) — worker 축 분리와 `yield` 부재
- [ ] **§12.24** (결함) — actor join의 orchestration이 뒤집혀 있다

본문은 [갭 인덱스](../90-implementation-gap.ko.md)가 소유한다. **§12.21과 §12.24는 한 묶음이다** — join orchestration을 먼저 바로잡지 않고 자동 turn dispatch만 걷어내면 user Spot → user Spot join이 즉시 막힌다.

## 2. 구현 감사 상세

| ID | 종류 | 계약 | 구현이 하는 일 |
|----|------|------|----------------|
| **IMP-JV-01** | 결함 | [40 §2.1](../server/40-location-runtime.ko.md): actor type마다 `actor:<type>` **capability**를 기록하고, handoff는 **정확히 일치하는** capability를 가진 노드만 고른다. **application metadata로 대신 기록하지 않는다** | `ZLinkLocationAutoConnectHost.java:26-27,66-70` — `metadata["zlink.framework.actor-host"]="true"` **불리언 하나**뿐이고 `capabilities`는 **항상 `null`**(:144-146). `ZLinkFrameworkRuntime.java:566-575`는 **actor type을 보지 않는다.** ⇒ `Warrior`만 만들 줄 아는 노드에 `Mage`를 넘겨 **drain 중 actor 유실**. 게다가 stream node가 있으면 그 플래그를 아예 안 써서, 두 노드가 모두 stream을 호스팅하는 흔한 구성에선 **handoff 대상이 0** |
| **IMP-JV-02** | 결함 | [24 §3·§5](../server/24-spot-address-messaging.ko.md): **정상 전송 경로는 store를 읽지 않는다.** handle이 snapshot을 들고, stale 실패 시 **1회 갱신 + 1회 재전송** | `FrameworkSpotHandle.java:6` — `record FrameworkSpotHandle(RoutingId spotRid)`, **rid 하나뿐**. snapshot도 swap도 없다. 그래서 `ZLinkChannelSpotCalls.java:128,200` 등 **모든 spot 전송이 매번 store를 읽는다.** ⇒ 룸 핫패스마다 Redis 왕복, store 장애 시 라우트 소켓이 멀쩡해도 **모든 spot 전송 실패**(스펙은 fail-static 요구) |
| **IMP-JV-03** | 결함 | [54 §6](../server/54-graceful-drain-handoff.ko.md): **호출자의 취소는 그 호출자의 대기만 중단한다** | `ZLinkFrameworkRuntime.java:72-74,463-465` — `drain()`/`awaitDrained()`가 **런타임 내부 `CompletableFuture`를 그대로 반환**한다. 호출자가 `orTimeout(5s)`를 걸면 **런타임의 공유 종료 future가 예외로 완료**되고, 실제 drain이 끝나도 lifecycle 훅이 예외를 받는다 |
| **IMP-JV-04** | 결함 | [24 §4.1](../server/24-spot-address-messaging.ko.md)·[05 §2.3](../05-framework-api.ko.md): `SpotRouteNotFound`/`RouteNotConnected`/`RequestTargetNotFound`를 구분한다 | `ZLinkChannelSpotCalls.java:228-236` — 전부 `ZLinkConfigurationException`(kind=`REQUEST_FAILED`, retriable=false). ⇒ **"spot이 사라졌다"와 "mesh가 아직 수렴 중이다"를 구분할 수 없다.** retriable 기반 재시도 정책이 영영 안 돈다 |
| **IMP-JV-05** | 결함 | [20 §8](../server/20-spot-messaging.ko.md): router/pub-sub 미설정, bind endpoint 없음, route bridge 대상 없음은 **설정 오류** | `SpotNodeRegistration.java:220-238` — 셋 다 검사하지 않는다. `enableRouter()`가 bind 없이도 통과(:106-108)하고, `ZLinkLocationAutoConnectHost.java:79`가 **빈 endpoint peer row를 조용히 게시**한다. ⇒ 아무도 dial하지 못하는 노드가 정상 기동하고, remote join·transfer·handoff가 **말없이 전부 실패** |
| **IMP-JV-06** | 결함 | [05 §2.x](../05-framework-api.ko.md): 없는 것을 있는 척하지 않는다 | `ZLinkSendCall`/`ZLinkRequestCall`/`ZLinkPublishCall`의 `metadata(k,v)` — 스펙에 없는 표면인데다 **구현 11곳이 전부 인자를 버리고 `return this`**. `.metadata("tenant","acme")`가 컴파일되고 돌아가는데 수신 handler는 그 값을 **영영 못 본다** |
| **IMP-JV-07** | 미구현 | [54 §9](../server/54-graceful-drain-handoff.ko.md): `zlink.drain.state`(gauge), `zlink.drain.duration`(`outcome`), `zlink.drain.forced`(`kind`는 `actor\|spot\|request\|session`으로 **고정**) | 앞의 둘이 **없다.** `zlink.drain.forced`는 `kind=runtime`(닫힌 집합 밖)을 **한 번만** 올린다(`ZLinkFrameworkRuntime.java:652-653`) |
| **IMP-JV-08** | 미구현 | [40 §9](../server/40-location-runtime.ko.md) | location event source 5개 중 **4개가 없다**(IMP-X2) |
| **IMP-JV-09** | 결함 | [40 §2.3](../server/40-location-runtime.ko.md) | pending actor row를 성공 resolve로 반환한다(IMP-X1). ⇒ 두 노드가 claim을 경쟁하면 **actor 객체가 아직 없는 노드로 packet이 dispatch**된다 |
| **IMP-JV-10** | 미구현 | [54 §3.4](../server/54-graceful-drain-handoff.ko.md) | store read 5초 상한 없음(IMP-X4) |

## 3. 언어별 표면 차이 상세

### §12.1 STREAM connector 수신 큐 overflow (Java)

**미충족(Java).** [32 §10](../stream-connector/32-stream-connector.ko.md)은 수신 메시지 큐가 가득 차면 **새로 도착한
메시지를 버리고** `ReceivedMessageDropped`를 보고하도록 규정한다. 기본 상한은 1024다.

**근본 원인은 수신 저장소의 구조가 다르다는 것이다.** 기준선은 handler 조회와 무관한 **독립
unread-history**에 수신 메시지를 먼저 기록한다. handler 호출은 그와 별개로 진행되고, `waitFor`가
history에서 메시지를 꺼내며, `receivedCount`는 history에 남은 수를 읽는다. Java는 그런 history가
없고 **manual dispatch callback 큐**를 그 자리에 쓴다. 그래서 다음이 전부 어긋난다.

- overflow 시 **가장 오래된 항목을 버린다.** 기준선은 새로 도착한 메시지를 버린다.
- 수신 큐 기본 상한이 `Integer.MAX_VALUE`라 이 경로가 평소 발화하지 않는다.
- drop 시 오류를 발생시키지 않아 **메시지가 조용히 유실된다.** `ZLinkStreamErrorCode`에
  `RECEIVED_MESSAGE_DROPPED`가 없다.
- **등록된 handler가 없는 메시지는 보관되지 않고 즉시 버려진다.** 기준선은 history에 남긴다.
- **`waitFor`가 이미 도착한 메시지를 소비하지 못한다.** `submit()` 시점에 일회성 handler를 걸기
  때문에 그 이전에 온 메시지는 영영 못 받는다.
- **`receivedCount`의 의미가 다르다.** unread-history의 메시지 수가 아니라 manual 큐에 남은
  callback 수다.
- **`AUTO`(= `Immediate`) 모드에서는 큐 자체를 쓰지 않아** 수신 한도가 적용되지 않는다.

독립 unread-history를 도입해야 위 항목이 함께 해소된다.

### §12.2 actor join admission이 선택 사항 (Java, C++)

**미충족(Java, C++).** [22 §8](../server/22-actor-model.ko.md)과 [23 §12](../server/23-spot-actor.ko.md)는 actor join
admission을 **필수 등록 축**으로 규정한다. `.NET`은 이를 default 구현 없는 interface member로 두어
구현 누락 자체가 불가능하다.

Java는 `onActorJoin`에 default 구현이 있고 그 기본값이 **거절**이다. C++은 duck typing으로 존재할
때만 호출하며, 일반 spot에서 없으면 **거절**로 대체한다. 두 경우 모두 admission을 빠뜨리면
컴파일과 시작은 통과하고 **모든 actor join이 조용히 거절**되는 실패 모드가 생긴다.

### §12.3 근거 없는 공개 표면과 connect 상태 처리 (Java, Kotlin)

**해결(Java, Kotlin).** Java connector와 Kotlin wrapper에서 계약에 없는 수동 `disconnect()`와
`reconnect()`를 제거했다. 재접속은 자동 reconnect가 담당하며, `Disconnected` 상태의 명시적
`connect()`도 같은 lifecycle 모듈이 처리한다. `Connecting`과 `Reconnecting`에서는 진행 중인 future를
공유하므로 동시 호출이 두 transport를 만들거나 예약된 자동 재접속과 경쟁하지 않는다.

호출마다 새 시도를 만들고 뒤늦게 중복을 정리하는 안과 lifecycle이 시도 future 하나를 소유하는 안을
비교해 후자를 선택했다. 연결 상태와 시도 소유권이 한 모듈에 있어 호출자가 재접속 scheduler를 알
필요가 없다. 자동 재접속의 최대 횟수, 무제한 재시도, 재접속 중 close도 계약 테스트로 유지했다.

원격 node를 직접 지정하던 `ZLinkActorPlacement(preferredNodeRid, routeMesh)`와 이를 받는 ensure overload도
제거했다. 무시되는 호환 인자를 남기는 안은 거짓 표면이 되므로 선택하지 않았다. GameQuest의 새 session
검증은 닫힌 connector를 재사용하지 않고 새 connector를 만들도록 바꿨다.

구현 전 공개 메서드·placement 타입 부재 검사와 동시 connect 단일 transport 검사가 모두 실패했다.
OBS-B1을 자동 재접속으로 바꾸는 과정에서는 `ZLinkSessionContext.close()`가 아무 동작도 하지 않는 문제도
확인해, runtime이 session 종료 control 전송을 소유하도록 고쳤다. 구현 뒤 Java 전체 Gradle 테스트,
Kotlin ObservabilityOps Trigger build, Java GameQuest 전체 self-check와 실제 자동 재접속 3회를 수행하는
OBS-B1이 통과했다. 구현 커밋 `943486d05`(2026-07-15).

### §12.4 connector 호출별 packet name override (Java)

**충족(Java).** raw·typed send/request call 네 곳에 `packetName(String)`을 추가했다. 호출 객체를 불변으로
복사하면서 이름만 바꾸므로 원래 payload·metadata·codec은 유지되고, 명시한 이름이 타입 기반 기본 이름보다
우선한다. 이름을 connector 생성 시점에 미리 인코딩하는 안보다 호출별 설정 책임을 call 객체 안에 두는 안이
호출자 표면과 내부 책임을 더 단순하게 유지하므로 이를 선택했다.

구현 전에는 네 인터페이스에서 `packetName(String)`을 찾는 집중 계약 테스트가
`NoSuchMethodException`으로 실패했다. 구현 뒤 raw·typed send/request 네 경로에서 실제 wire header 이름이
덮어써지는 테스트와 `:zlink-stream-connector:test :zlink-framework-kotlin:test`가 통과했다. 구현 커밋
`4071e369f`(2026-07-15).

### §12.8 monitoring 표면 (Java)

**미충족(Java).** 세 항목이다.

- runtime event 모델이 **sealed 계층이 아니라 flat record + kind enum**이다. 기준선은 event 종류마다
  필요한 payload만 필수 인자로 갖는 sealed hierarchy이며, [00 §5](../00-public-contract-governance.ko.md)의
  "같은 상태를 kind와 nullable 값 두 축으로 표현하지 않는다"에 해당한다. Java 언어 스펙이 고정한 목표
  선언(`ZLinkLocationRuntimeEvent` / `ZLinkSpotEvent` sealed interface + permitted record)을 따라야 한다.
- `ZLinkMonitoringOptions`에 `addLocationPeerEvents` / `addLocationSpotEvents` /
  `addLocationActorEvents` / `addLocationRouteEvents` 4개가 없다.
- `ZLinkRuntimeEventHandler.handle`이 `void`를 반환해 비동기 handler를 표현할 수 없다. 계약은
  `CompletionStage<Void>`다.

### §12.9 spot 전송 표면에 channel 이름을 함께 받는다 (Java)

**계약 위반(Java).** [24 §3](../server/24-spot-address-messaging.ko.md)은 "handle이 전송 mesh를 소유하므로
caller가 route channel을 함께 고르지 않는다"고 규정한다. Java `ZLinkRouteClient.sendToSpot` /
`requestToSpot`은 `(channelName, SpotHandle, message)`를 받아 caller가 mesh를 다시 고르게 만든다.
계약은 `(SpotHandle, message)`다.

### §12.10 connector transport enum 부재 (Java)

**충족(Java).** Java 언어 스펙이 고정한 `ZLinkStreamTransport`(`TCP`/`TLS`/`WEB_SOCKET`/
`WEB_SOCKET_SECURE`)를 추가했다. 이름만 있는 enum을 두지 않고 endpoint scheme 해석 결과와 TCP·TLS·WebSocket
연결 분기가 이 enum을 사용하게 했다. 기존 문자열 조건을 유지하면서 enum만 덧붙이는 안보다 transport 선택을
한 번만 해석하는 안이 지원 집합과 실제 동작의 불일치를 막으므로 이를 선택했다.

구현 전 공개 enum과 네 멤버를 찾는 집중 계약 테스트는 `ClassNotFoundException`으로 실패했다. 구현 뒤 이
테스트와 TCP·TLS·WebSocket 경로를 포함한 `:zlink-stream-connector:test`, Kotlin 공유 runtime 영향 게이트인
`:zlink-framework-kotlin:test`가 통과했다. 구현 커밋 `aad0f8074`(2026-07-15).

### §12.12 connector dispatch mode 이름 (Java)

**충족(Java).** dispatch mode의 닫힌 집합을 `MANUAL`/`IMMEDIATE`로 맞추고 Java·Kotlin 샘플과 E2E의
호출부도 계약 이름으로 옮겼다. Manual에서는 message·connection-state·disconnected·error callback을
모두 하나의 dispatch queue에서 실행한다. queue 항목이 `CompletionStage<Void>`를 반환하게 해
`dispatch().submit()`은 callback이 완료된 뒤에만 끝난다.

callback마다 별도 blocking wait를 넣는 안과 queue가 비동기 완료를 합성하는 안을 비교해 후자를
선택했다. transport thread를 막지 않고 callback 실행 문맥과 완료 순서를 queue 한 곳이 소유한다.
구현 전 enum 이름 테스트와 느린 callback 완료 테스트가 각각 실패했다. 구현 뒤 두 테스트와 Manual
lifecycle callback 문맥 테스트, Java 전체 `./gradlew test` 44개 task가 통과했다. 구현 커밋
`d339d4386`(2026-07-15).

### §12.13 connector inbound observer option 부재 (Java)

**충족(Java).** `ZLinkStreamConnectorOptions`에 `maxInboundObserverNotifications`와
`maxInboundObserverPayloadPreviewBytes`를 추가하고 각각 기본값 1024개와 0바이트를 적용했다. 두 값은
양수·음수가 아닌 값인지 시작 전에 검증하며, observer dispatcher의 실제 bounded queue와 snapshot preview
절단에 사용된다. option getter만 추가하는 안은 실제 동작을 바꾸지 않는 거짓 계약이므로 선택하지 않았다.

구현 전 두 option 접근자를 찾는 집중 계약 테스트는 `NoSuchMethodException`으로 실패했다. 구현 뒤 큐
상한 1에서 `OBSERVER_DROPPED`가 발생해도 request가 완료되는 테스트, preview 한도 3에서 4바이트 payload가
3바이트 snapshot으로 전달되는 테스트와 `:zlink-stream-connector:test :zlink-framework-kotlin:test`가
통과했다. 구현 커밋 `900da7c0b`(2026-07-15).

### §12.15 예외 정규화 부재 (Java)

**미충족(Java).** 기준선은 connector의 비동기 실패를 `ZLinkStreamErrorCode`를 담은 공통 예외
타입으로 정규화해, 호출자가 실패 원인을 닫힌 집합으로 판별할 수 있게 한다. Java는 raw
`TimeoutException`, `IllegalStateException`, `IllegalArgumentException`을 그대로 던져 오류 코드를
잃는다([32 §9](../stream-connector/32-stream-connector.ko.md)).

### §12.16 metadata 총 크기 한도 미검사 (Java)

**충족(Java).** [32 §4](../stream-connector/32-stream-connector.ko.md)가 규정한 metadata wire 블록의
**총합 1024바이트** 한도를 encode 과정에서 검사한다. 항목 수와 개별 key/value 검사 뒤에 별도 전체
순회를 추가하는 안 대신 기존 단일 encode 순회에서 누적 크기를 검증해 같은 정보를 두 번 계산하지 않는다.

구현 전에는 정확히 1024바이트인 블록은 허용하고 1025바이트인 블록은 거부하는 집중 테스트 중 후자가
실패했다. 구현 뒤 경계값 테스트와 `:zlink-stream-connector:test :zlink-framework-kotlin:test`가
통과했다. 구현 커밋 `47f7898af`(2026-07-15).

### §12.17 correlated Error 처리 (Java)

**충족(Java).** Error payload를 UTF-8 JSON 객체로 decode하고 문자열 `code`·`message` 필드를 검증한다.
일치하는 `request_seq`가 있으면 pending request만 `code: message` 상세로 실패시키고, request가 없거나
sequence가 일치하지 않을 때만 stream error callback에 `REMOTE_ERROR`를 전달한다. callback에 먼저
발행한 뒤 pending을 찾는 안보다 pending map이 단일 전달 경로를 결정하는 안이 중복 관측을 구조적으로
막으므로 이를 선택했다. 잘못된 Error JSON은 `FRAME_DECODE_FAILED`로 분류한다.

구현 전에는 uncorrelated Error의 message가 JSON 원문이었고, correlated Error가 pending 실패와 stream
callback에 두 번 전달되는 집중 테스트가 모두 실패했다. 구현 뒤 두 테스트와 Kotlin error flow의 JSON
계약 테스트, `:zlink-stream-connector:test :zlink-framework-kotlin:test`가 통과했다. 구현 커밋
`e00b77cc7`(2026-07-15).

### §12.18 flow_id 미전파 (Java)

**충족(Java).** inbound callback 실행 범위에 connector 내부 flow context를 설정한다. 이 범위에서
시작한 send/request는 inbound header의 `flow_id`와 origin을 그대로 사용하고, inbound에 flow가 없으면
UUIDv7과 `INBOUND` origin을 만든다. callback 밖의 application 호출만 새 UUIDv7과 `APPLICATION` origin을
사용한다.

flow 값을 send/request 공개 인자로 노출하는 안과 callback 실행 문맥을 내부에서 보존하는 안을 비교해
후자를 선택했다. 호출자가 tracing header를 알아야 하는 복잡성을 만들지 않고 receive dispatcher가 flow
수명을 소유한다. 구현 전 known flow를 가진 inbound callback에서 시작한 outbound send가 다른 UUID와
`APPLICATION` origin을 보내 집중 테스트가 실패했다. 구현 뒤 같은 wire 테스트와
`:zlink-stream-connector:test :zlink-framework-kotlin:test`가 통과했다. 구현 커밋 `e52cf94e2`(2026-07-15).

### §12.19 typed 표면 경계 (Java, Kotlin)

**해결.** Java의 typed `send(Object)`와 `request(Object)`는 runtime type이
`ZLinkStreamEncodedPayload`이면 raw overload를 사용하라는 `IllegalArgumentException`을 발생시킨다.
raw 값을 typed codec이 다시 인코딩하도록 허용하는 안과 경계에서 거부하는 안을 비교해 후자를
선택했다. 이 방식은 raw payload의 packet name과 bytes를 raw 표면 한 곳에서만 해석하게 한다.

Kotlin wrapper에서는 목표 계약에 없던 request `await<T>()` overload 2개를 제거하고 typed·raw request의
generic 완료 이름을 모두 `awaitReply<T>()`로 통일했다. 호환 별칭을 남기는 안은 같은 완료 동작을 두
이름으로 노출하므로 선택하지 않았다. SpotService와 Kotlin 샘플의 호출부도 계약 이름으로 옮겼으며,
HTTP client의 별도 coroutine `await<T>()`는 변경하지 않았다.

구현 전 `typedCallsRejectRawEncodedPayloadHiddenAsObject`와
`kotlinRequestCompletionSurfaceUsesOnlyContractNames`가 각각 raw payload 수용과 계약 밖 overload를
검출해 실패하는 것을 확인했다. 구현 뒤 두 집중 테스트, Java connector·Kotlin module 전체 테스트,
Kotlin SpotService 전체 E2E와 GameQuest·Bingo·TicTacToe 전체 self-check가 통과했다. 구현 커밋
`c372ebbfc`(2026-07-15).

## 라운드 2 (2026-07-14) — 관측 · channel topology · companion 패키지

### 체크리스트

- [ ] **IMP-JV-11** (결함) — `flow_id`를 envelope header가 아닌 **자체 message part**로 나른다 (교차 언어 wire 위반)
- [ ] **IMP-JV-12** (결함) — per-source polling 간격을 **전역 최소값 하나로 붕괴**시킨다
- [ ] **IMP-JV-13** (미구현) — 계기 12개 결측(그중 `channel.messages.dropped`가 치명적)
- [ ] **IMP-JV-14** (결함) — runtime-event handler 예외를 **error sink에 보고하지 않는다**
- [ ] **IMP-JV-15** (미구현) — `fanout.published`/`received`에 `topic` 라벨이 없다
- [ ] **IMP-JV-16** (결함) — **수동 endpoint가 그 역할의 자동 연결 reconcile을 끄지 않는다**
- [ ] **IMP-JV-17** (결함) — 자동 연결 역할에 대한 런타임 `connect()`가 **거부되지 않는다**
- [ ] **IMP-JV-18** (결함) — HTTP client가 **proxy 자격증명을 대상 서버로 흘린다**
- [ ] **IMP-JV-19** (결함) — HTTP attempt timeout이 **redirect hop마다** 적용된다
- [x] **IMP-JV-20** (결함) — connector send payload 한도를 압축된 wire payload에 적용한다. 압축 전에는 한도를 넘지만 압축 후에는 한도 안인 집중 테스트의 실패를 먼저 확인했고, Java connector·Kotlin module 전체 테스트가 통과했다. 구현 커밋 `cc1ea63b1`(2026-07-15).

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-JV-11** | [53 §3.1·§3.4](../server/53-flow-correlation.ko.md): `flow_id`/`flow_origin`은 **envelope header의 1급 필드**다. 다르게 나르는 relay를 두지 않는다 | `ZLinkChannelFlowFrame.java:9-27` — `"__zlink.flow\n<uuid>\n<ORIGIN>"`를 **세 번째 message part**로 인코딩한다(`ZLinkChannelCallRuntime.java:215-223`). **다른 어떤 구현도 그 part를 쓰거나 읽지 않고**, Java는 header의 flow 필드를 **읽지 않는다.** ⇒ Java가 낀 흐름은 fleet 추적에서 **끊긴다** |
| **IMP-JV-12** | [50 §4](../server/50-runtime-monitoring.ko.md): polling 주기는 등록 시점에 **항상 명시**한다. **숨은 기본 주기를 두지 않는다** | `DefaultZLinkMonitoringOptions.java:61-67` — 모든 source 간격의 **최소값 하나**로 `scheduleWithFixedDelay` 하나를 돌리고, 매 tick에 **모든 source를 poll**한다. ⇒ `addSpotEvents("play",200ms)` + `addLocationRuntimeEvents("loc",60s)`면 Redis topology 페이징이 **200ms마다** 돈다 — 설정한 비용의 **300배** |
| **IMP-JV-13** | [51](../server/51-runtime-metrics.ko.md)·[52 §2](../server/52-message-flow-tracing.ko.md): **metric/counter는 trace mode와 무관하게 계속 발생한다** | 계기 12개가 없다 — `stream.session.bind.duration`, `stream.{inbound,outbound}.bytes`, `spot.timer.tick.lateness`, `actor.count`, `actor.mailbox.depth`, **`channel.messages.dropped`**, `location.peers`, `location.store.errors`, `location.owner_lease.renew.failures`, `location.write.conflicts`, **`observability.observer.overflow`**. ⇒ trace를 끄면 drop에 대한 **관측 신호가 0** |
| **IMP-JV-14** | [50 §3.2](../server/50-runtime-monitoring.ko.md): handler 예외는 **runtime error sink로 보고한다** | `ZLinkRuntimeEventDispatcher.java:38-47` — `catch { handlerFailureCount.incrementAndGet(); }`. 공개 reader가 없는 **내부 카운터**로만 남는다 |
| **IMP-JV-15** | [51 §4.4b](../server/51-runtime-metrics.ko.md): `fanout.published`/`received`에 `topic`(닫힌 집합) 라벨 | `ZLinkChannelDirectCalls.java:126` 등 전부 `Map.of()`. ⇒ **topic별 발행/수신 차이**를 계산할 수 없다 — 이 한 쌍이 존재하는 이유가 그건데 |
| **IMP-JV-16** | [10 §5.2](../server/10-channel-topology.ko.md): 같은 역할에 수동 endpoint가 **하나라도** 있으면 그 역할은 수동으로 확정되고, **자동 연결 reconcile이 돌지 않는다** | `ZLinkLocationAutoConnectHost.java:107-127` — 모든 surface에 **무조건** reconciler를 만든다. 유일한 완화는 `ConnectableSocketExecutor.connect`(:174-186)가 **문자열이 정확히 일치하는** 수동 endpoint만 건너뛰는 것. ⇒ `enableClient("tcp://10.0.0.5:5001")` + location store면 DEALER가 **store의 staging 서버들까지 물고 라운드로빈**한다 |
| **IMP-JV-17** | [10 §5.2](../server/10-channel-topology.ko.md): 자동 연결로 확정된 역할에 런타임 수동 endpoint를 추가하려 하면 **그때 거부된다** | `RuntimeEndpointConnections.java:19-30` — 검증 후 그냥 연결한다. frozen/auto 모드가 **없다**(`.NET`은 `Freeze`, C++은 `frozen` 상태를 갖는다). ⇒ **역할마다 진실의 원천이 하나**라는 불변식이 깨진다 |
| **IMP-JV-18** | [http 07 §7.3](../http-client/07-auth-tls-proxy.ko.md) | `RequestPerformer.java:160-162`가 `proxy-authorization`을 요청 헤더에 넣고, `JavaHttpClientFactory.java:27-30`은 `.authenticator(...)` 없이 `ProxySelector`만 준다. `.NET`(IMP-DN-12)과 **같은 결함** |
| **IMP-JV-19** | [http 06 §6.2](../http-client/06-redirect-retry-cookie.ko.md): timeout은 **시도(attempt)당** 적용한다 | `RequestPerformer.java:176-181` — `hop()`마다 timeout을 **새로 건다.** ⇒ `timeout(3s)` + `followRedirects(5)` + `retry(2)`가 계약상 ~9초여야 하는데 **~45초**를 태울 수 있다 |
| **IMP-JV-20** | [32 §4.7](../stream-connector/32-stream-connector.ko.md) | **해결:** 압축하지 않은 호출은 원본 payload, 압축 호출은 codec이 만든 wire payload의 크기를 transport write 전에 검사한다. 압축 전 검사를 유지한 채 압축 후 검사만 더하는 안은 압축으로 한도 안에 들어오는 payload를 계속 거부하므로 선택하지 않았다. 압축 전에는 한도를 넘는 payload가 2바이트로 압축되는 집중 테스트와 Java connector·Kotlin module 전체 테스트 통과. 구현 커밋 `cc1ea63b1`(2026-07-15). |

## 라운드 3 (2026-07-14) — 근거 없는 표면 · 조용한 no-op · 경합

**Java에는 `module-info.java`가 없다.** 그래서 `zlink-framework-core`의 **모든 `public` 클래스가
application API**다. 이 사실이 아래 여러 항목의 근본이다.

### 체크리스트

- [ ] **IMP-JV-21** (결함) — `systems.zlink.framework.execution` 패키지가 **framework 내부 실행기를 공개**한다
- [ ] **IMP-JV-22** (결함) — raw STREAM frame/header codec이 core의 **public API**다
- [ ] **IMP-JV-23** (결함) — header decode 실패를 **날조한 packet으로 바꾸고**, 그 요청에 **응답할 수 없게** 만든다
- [x] **IMP-JV-24** (결함) — Spring host의 두 자동 종료 경로가 framework 기본값과 같은 30초 drain deadline을 사용하도록 고쳤다. 집중 테스트의 실패를 먼저 확인했고 Spring Boot starter 전체 테스트가 통과했다. 구현 커밋 `a0e2bb977`(2026-07-15).
- [ ] **IMP-JV-25** (결함) — `addForwardedMetadataKey(...)`가 **조용한 no-op**
- [x] **IMP-JV-26** (결함) — error callback 실패를 로그로 남기고 실패한 handler를 제외한 다른 error handler에 `USER_CALLBACK_FAILED`로 한 번 전달한다. 집중 테스트의 실패를 먼저 확인했고 Java connector·Kotlin module 전체 테스트가 통과했다. 구현 커밋 `cba4d186c`(2026-07-15).
- [ ] **IMP-JV-27** (결함) — `includeNativeDiagnostics`를 **읽는 곳이 없다**
- [ ] **IMP-JV-28** (결함) — `ZLinkStoreSpotHandleResolver`가 **내부 transport 주소 타입을 공개 표면으로 흘린다**
- [ ] **IMP-JV-29** (결함) — connector의 `ZLinkStreamJson`·`ZLinkStreamCompressionCodecs`가 **스펙 근거가 없다**
- [ ] **IMP-JV-30** (결함) — **actor가 든 spot을 닫을 수 있다** (`.NET` IMP-DN-17과 동형)
- [ ] **IMP-JV-31** (결함) — 서버가 `correlation_id`를 `request_seq`로 **날조한다**
- [ ] **IMP-JV-32** (결함) — `listPageSize`를 **읽는 곳이 없다.** 내부 기본값이 1000이 아니라 **무한**이다
- [ ] **IMP-JV-33** (미구현) — `storeFailureGrace`를 **읽는 곳이 없다.** fail-static 유예 정책 자체가 없다

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-JV-21** | [25 §3](../server/25-stage-wrapper-on-spot.ko.md): **사용자에게 내부 실행기(mailbox·queue·drain loop)를 노출하지 않는다.** 사용자가 보는 것은 등록 표면뿐이다 | `execution/ZLinkSpotDispatchQueue.java:33,42,51,61`(public 생성자 + spot 직렬 줄에 `enqueue`), `ZLinkAsyncSerialQueue.java`, `ZLinkWorkerPool.java:21,30,77,97,125`(public 생성자 + `execute` + **`close()`**). 스펙 어디에도 이 이름들이 없다. ⇒ 앱이 **프로세스 전체 spot이 공유하는 worker pool을 `close()`할 수 있고**, spot의 turn 큐에 **임의 작업을 직접 밀어 넣어** turn 모델을 통째로 우회할 수 있다 |
| **IMP-JV-22** | [32 §5](../stream-connector/32-stream-connector.ko.md): **임의 header bytes를 다루는 API를 공개 표면에 두지 않는다** | `runtime/streams/ZLinkStreamFrameCodec.java:12,20`·`ZLinkStreamHeaderCodec.java:29,118`이 public이다. **connector는 제대로 한다**(`ZLinkStreamWireProtocol.java:10`이 package-private). core만 뚫려 있다 |
| **IMP-JV-23** | [11 §3.1](../server/11-channel-messaging.ko.md): 잘못된 frame은 **로그 + drop**. [51 §4.4](../server/51-runtime-metrics.ko.md): `dropped{reason="decode_error"}` | `runtime/spots/ActorPacketFrames.java:21-37` — header decode가 실패하면 **catch해서 raw header 바이트를 UTF-8로 읽은 값을 packet name으로 삼는 Header를 날조한다.** plain-header 경로는 이미 `decodeOrPlain`이 처리하므로, 이 catch는 **진짜로 손상된 frame에서만** 튄다. drop도 metric도 없다. 게다가 `streamHeader=false`라서 `encodeReply`가 **응답 헤더 없는 맨 payload**를 내보낸다 — 호출자의 connector는 header의 `request_seq`로 매칭하므로 **그 요청은 30초 timeout까지 매달린다** |
| **IMP-JV-24** | [54 §6](../server/54-graceful-drain-handoff.ko.md): 기본 deadline은 **모든 언어에서 30초**다. **인자 없는 overload와 host 자동 drain이 같은 값을 쓴다** | **해결:** Spring `stop()`과 callback 기반 `stop(Runnable)`이 공유하는 deadline을 30초로 맞췄다. 경로마다 상수를 따로 두는 안 대신 두 종료 경로가 하나의 값을 계속 공유하게 했다. 25초에서 실패하는 집중 테스트와 Spring Boot starter 전체 테스트 통과. 구현 커밋 `a0e2bb977`(2026-07-15). |
| **IMP-JV-25** | 스펙이 선언한 metadata 전달 정책 | `ZLinkMetadataPolicyRegistration.java:10,17,20` — `forwardedApplicationKeys`의 **소비자가 트리 전체에 없다**(getter round-trip 유닛테스트뿐). `configureMetadata().addForwardedMetadataKey("tenant")`가 **아무것도 전달하지 않는다.** 기록된 `metadata(k,v)` no-op(IMP-JV-06)과 **같은 병**이 설정 축에서 반복된다 |
| **IMP-JV-26** | [32 §9](../stream-connector/32-stream-connector.ko.md): 사용자 callback 실패는 `UserCallbackFailed`. **error handler에 예외 조항이 없다** | **해결:** 동기 throw와 비동기 실패를 모두 로그에 남기고, 실패한 handler 자신을 제외한 다른 error handler에 `USER_CALLBACK_FAILED`로 전달한다. 같은 handler에 다시 보내는 안은 재귀 실패를 만들므로 제외했고, failure 통지를 처리하는 handler의 실패는 다시 재발행하지 않아 handler 간 순환도 막았다. 두 error handler 집중 테스트와 Java connector·Kotlin module 전체 테스트 통과. 구현 커밋 `cba4d186c`(2026-07-15). |
| **IMP-JV-27** | — | `ZLinkDispatchOptionsRegistration.java:160,179,219`가 전부. 형제 옵션(`includeMessageSizes`·`sampleRate`·`logFile`)은 살아 있는데 이것만 죽었다 |
| **IMP-JV-28** | [00 §5](../00-public-contract-governance.ko.md): transport 주소는 framework 내부다 | `spots/ZLinkStoreSpotHandleResolver.java:10-11,34`가 **사용자 대면 `framework.spots` 패키지에서 public**이고 `runtime.internal.spots.SpotTransportAddress`를 반환한다. **코드베이스 자신이 그 타입을 `runtime/internal/` 아래 둔다** |
| **IMP-JV-29** | [00 §3](../00-public-contract-governance.ko.md): 스펙 근거 없이 public API를 만들지 않는다 | connector의 `ZLinkStreamJson`·`ZLinkStreamCompressionCodecs` — 스펙 트리 grep **0건**(형제 connector 타입은 전부 항목이 있다). `ZLinkStreamJson`은 고정된 `send`/`request`/`on` 표면을 **중복하는 두 번째 static facade**다 |
| **IMP-JV-30** | [21 §close](../server/21-spot-node.ko.md) | `ZLinkSpotLifecycle.java:134-142` — `hasActorsInSpot()`이 **락 없이** actor registry를 순회하고, `joinedSpotRid`를 **쓰는** commit은 spot dispatch 줄에서 돈다. `.NET` IMP-DN-17과 **같은 경합** |
| **IMP-JV-31** | [52 §9](../server/52-message-flow-tracing.ko.md) | `ZLinkStreamRuntime.java:272-273` — `.orElseGet(() -> requestSequence()...)`. `.NET` IMP-DN-09과 **같은 결함**(C++만 올바르다) |
| **IMP-JV-32** | [40 §3·§8.2](../server/40-location-runtime.ko.md): 목록 조회는 `list page size`(기본 **1000**)를 따른다 | `ZLinkLocationOptions.java:12,40-48` — **읽는 곳 0.** 내부 조회가 `ZLinkPageRequest.firstPage()`(pageSize 0)를 써서 Redis `SMEMBERS`로 **kind 인덱스 전체**를 읽는다. ⇒ 모든 `listSpots`/`listActors`가 **O(N) 전체 읽기**이고, 그걸 제한하라는 옵션이 **아무 일도 안 한다** |
| **IMP-JV-33** | [40 §6.1·§8.2](../server/40-location-runtime.ko.md): store 장애 유예 30초 | **읽는 곳 0.** ⇒ Java e2e의 `SF-B2 GraceExceeded`가 **존재하지 않는 정책을 검증하고 있다** |

## 교차 언어 결함 — 이 언어에서 무엇을 고치나

**교차 언어 결함이라도 고치는 일은 이 언어에서 한다.** [갭 인덱스](../90-implementation-gap.ko.md) §15.3이
**왜**(계약과 결정)를 소유하고, 아래 표가 **무엇을**(이 언어의 작업)을 소유한다.

| 교차 결함 | 무엇이 깨지나 | 이 언어의 작업 |
|---|---|---|
| **IMP-X1** | pending actor row를 resolve 성공으로 반환 | IMP-JV-09 |
| **IMP-X2** | location event source 4종 결측 | IMP-JV-08 |
| **IMP-X3** | startup validation이 설정 오류를 통과 | IMP-JV-05 |
| **IMP-X4** | location store read에 5초 상한 없음 | **이 언어 전용 ID 없음** — `runtime/locations/`(`ZLinkStoreLocationResolvers`·`ZLinkLiveLocationRows`·`ZLinkOwnerLeaseTracker`·`ZLinkAutoConnectLoop`)가 store를 **무제한**으로 호출한다. 5초 취소 상한을 적용한다 |
| **IMP-X5** | message-flow 관측자가 로그 모드에 묶여 침묵 | **이 언어 전용 ID 없음** — `ZLinkMessageFlowTracer.java:65-78`의 `enabled()`가 **로그 모드만** 읽고, 샘플 게이트까지 통과해야 :89의 관측자 dispatch에 닿는다. [52 §3](../server/52-message-flow-tracing.ko.md)은 "관측자는 모드와 무관하게 발화한다"이다. `.NET`(`ZLinkMessageFlowTracer.cs:44`)처럼 `ShouldLog(outcome) || ObserverEnabled`로 고친다 |
| **IMP-X6** | `origin=lifecycle`을 생성하지 않는다 | **이 언어 전용 ID 없음** — `ZLinkMessageFlowTracer.java:119-123`의 `originFor()`가 `RECEIVED`가 아닌 모든 것을 `APPLICATION`으로 매핑한다. enum은 wire 디코더(`ZLinkStreamHeaderCodec.java:243`)에만 있다. drain·startup·shutdown이 새 flow를 `lifecycle`로 시작해야 한다 |
| **IMP-X7** | connector send payload 한도를 압축 전에 적용 | IMP-JV-20 |
| **IMP-X8** | 수동 endpoint가 auto-reconcile을 끄지 않는다 | IMP-JV-16 |
| **IMP-X9** | HTTP client proxy 자격증명 유출 | IMP-JV-18 |
| **IMP-X12** | actor가 든 spot을 닫을 수 있다 (경합) | IMP-JV-30 |
| **IMP-X13** | `correlation_id` 날조 | IMP-JV-31 |
| **IMP-X14** | `listPageSize`가 죽어 있다 | IMP-JV-32 |
| **IMP-X15** | `storeFailureGrace`가 죽어 있다 | IMP-JV-33 |
| **IMP-X16** | `includeNativeDiagnostics`가 죽어 있다 | IMP-JV-27 |
| **IMP-X18** | Redis fixture 불일치 | `putInstant`가 null instant에 `1970-01-01T00:00:00Z`를 낸다 — fixture는 `0001-01-01T00:00:00+00:00` |

## 이전 기록 — 기준선 대조 (2026-07-13 이전)

> **이 절은 과거 기록이다.** 당시 계약 기준으로 확인한 내용이며, 그 뒤 계약이 바뀐 항목이 있다
> (특히 실행 terminator — [갭 인덱스 §12.21](../90-implementation-gap.ko.md) 참조).
> **현재 작업 목록은 이 문서 위쪽의 체크리스트다.**

### 3.1 handler 비동기 완료

Java request, send, publish, Spot, actor와 session handler는 `CompletionStage<T>` 또는
`CompletionStage<Void>`를 반환한다.

> **turn 의미는 갭이다.** 현재 구현의 automatic turn은 handler가 stage를 **반환할 때까지**만 다음
> handler의 시작을 막고, 반환된 incomplete stage의 **완료는 기다리지 않는다.** 정본 계약은
> `async`가 **완료까지 turn을 유지**하는 것이다([04 §1.1](../04-async-execution-policy.ko.md)).
> 아래 근거는 **폐기된 계약 기준의 기록**이며, 현재 갭은
> [§12.21](#1221-yield-terminator-부재-전-언어)이 소유한다.

확인 근거(구 계약 기준):

- `JavaTargetContractGapTest.handlersFactoriesAndLifecycleExposeCompletionStages`
- Config 8 `AutomaticTurnDispatch` 전체 selector — 이 config는 [config-8 실행 turn과
  terminator](../../common/e2e/config-8-execution-turn.ko.md)(`TD-*`)로 대체됐다

Kotlin adapter는 lifecycle과 actor callback의 coroutine을 `CoroutineScope.future`로
`CompletionStage`에 연결한다. `CompletionStage.await()`는
`suspendCancellableCoroutine`과 stage 완료 callback으로 coroutine을 재개하므로 callback
실행 줄을 blocking wait로 점유하지 않는다. waiter cancellation은 공유 framework stage를
취소하지 않고, stage의 완료 오류는 원래 원인으로 풀어서 전달한다.

현재 확인 위치:

- `zlink-framework-kotlin/.../ZLinkSuspendingHandlers.kt`
- `zlink-framework-kotlin/.../ZLinkCoroutineTurnAwait.kt`

### 3.2 one-way call 완료 표면

`ZLinkSendCall`, `ZLinkSessionSendCall`, `ZLinkSessionReplyCall`과
`ZLinkBoundSessionSendCall`의 one-way `submit()`은 `void`다. `ZLinkSubmitStage`, public
`await`와 yield call은 production source에 없다. 전송 실패는 framework error observer와
runtime 진단 경로로 보고한다.

### 3.3 typed session handler

`ZLinkTypedSessionPacketHandler`는 raw application handler를 상속하지 않는다. message type
descriptor와 typed `CompletionStage<Void> handle(...)`을 제공하며 framework dispatcher와
application handler의 등록 경계가 분리되어 있다.

### 3.4 Actor join 계약

`ZLinkActorContext.joinSpot(...)`과 `joinEntrySpot(...)`은 요청을 필수로 받는다. 요청 없는
overload와 default throw는 없으며, 단일 `ZLinkActorJoinCall`과 sealed 승인·거절 결과를 사용한다.

### 3.5 interface inventory 문서 상태

다음 타입은 기존 Java interface catalog에서 찾기 어려웠으며 현재 언어별 interface
inventory에 정식 public contract로 반영했다.

```text
ActorSpotHandleResolver
ManualEndpointListBuilder
SpotHandleResolver
ZLinkActorClient
ZLinkActorDirectory
ZLinkActorJoinCall
ZLinkActorLocationStore
ZLinkActorRequestCall
ZLinkActorSendCall
ZLinkChannelRuntimeOptions
ZLinkClientServerChannelRuntimeOptions
ZLinkCodecRegistrar
ZLinkLocationChangeStampStore
ZLinkLocationKey
ZLinkLocationReadiness
ZLinkLocationRuntimeQuery
ZLinkLocationStore
ZLinkLocationWatchStore
ZLinkOwnerLeaseStore
ZLinkPeerLocationResolver
ZLinkPeerLocationStore
ZLinkRouteLocationStore
ZLinkSocketRuntimeOptions
ZLinkSpotActorLifecycle
ZLinkSpotLocationStore
ZLinkSpotPacketHandler
ZLinkSpotRequestHandler
ZLinkSpotSubscriptionHandler
ZLinkSpotTimerHandler
ZLinkStreamCompressionBuilder
ZLinkTypedSessionPacketHandler
```

Kotlin 전용 public type과 top-level extension도 Kotlin interface catalog의 type 및
function inventory에 반영했다.

```text
ZLinkCoroutineSuspendHandlerInvoker
ZLinkKotlinLifecycleCall
ZLinkKotlinSendCall
ZLinkKotlinStreamConnector
ZLinkStreamTypedWaitCall
ZLinkSuspendingLocationStore
await
awaitJoinReply
awaitOwnerLeases
send
publishToTopic
resolveActorSpotHandle
resolveSpotHandle
useCoroutineHandlers
messages
errors
```

### 3.6 Actor membership와 join 결과

현재 actor context는 nullable Spot 식별자와 join boolean을 따로 노출한다. 두 값을
순서대로 읽는 동안 상태가 바뀌거나 구현이 서로 다른 값을 돌려주면 모순이 생긴다.
목표 계약은 nullable Spot 식별자 하나를 join 상태의 단일 기준으로 사용한다.

현재 join 결과도 result code 또는 승인 여부와 nullable actor를 독립 필드로 제공한다.
목표 계약은 sealed 승인/거절 결과로 바꾼다. 승인 결과만 필수 actor ref를 가지며 두
결과 모두 reply를 가진다. Kotlin은 Java sealed 계약을 그대로 사용한다.

location store/query, compression과 connector에 선언된 Kotlin public extension은 Kotlin
문서의 전체 function inventory를 기준으로 별도 검증한다. Java 완료 판정이 Kotlin 완료를
의미하지 않는다.

### 3.7 Java/Kotlin 검증 상태

Java target public declaration은 `JavaTargetContractGapTest` 전체 통과와 production symbol
검색으로 확인했다. Java Config 1~10과 Config 11 `ObservabilityOps` 전체 selector가 real E2E를
통과했다. `ZLinkMessageFlowTracerTest.dispatchErrorsUseContractLogLevels`는 handler 예외를
one-way 여부와 관계없이 Error로 기록하고, handler 없음·decode 실패·invalid frame의 기본 수준을
send는 Warning, publish는 Debug로 기록하는 계약을 고정한다. Kotlin channel handler도 같은
Java dispatch reporter를 사용한다.

Kotlin은 `KotlinPublicSurfaceContractTest`, 전체 unit/integration test와 언어별 E2E로 확인했다.
`KotlinFlowContextBridgeTest`는 suspending lifecycle의 flow가 suspension 전후에 유지되고 다음
호출에 남지 않는지 검증한다. `KotlinCompletionStageAwaitIntegrationTest`는 drain waiter를 취소해도
공유 drain stage가 취소되지 않는지 검증한다. Config 8 전체 실행은 **구 계약(`ATD-*`) 기준** 기록이며
pending await 중 Play 재시작 같은 routing id recovery를 포함해 통과했다. 그 config는
[config-8 실행 turn과 terminator](../../common/e2e/config-8-execution-turn.ko.md)(`TD-*`)로 대체됐다.
Config 11 전체 실행도 각 selector를
새 Redis와 새 토폴로지에서 실행하여 OBS-A1~C5가 모두 통과했다.

## 라운드 4 (2026-07-14) — 샘플 · E2E

### 체크리스트

- [ ] **SMP-JV-01** (**절대 규칙 위반**) — TicTacToe 밖 샘플이 **수동 연결을 쓴다**(live 재검증 15곳 → 4곳)
  - 부분 해결: Bingo·DeliveryDispatch·ShoppingMall의 11개 수동 channel/Spot peer 연결을 제거했고 세 전체 runner가 location store 자동 연결로 통과했다. SupportChat은 재검증 시 이미 0개였다.
  - §0.8 중단: GameQuest의 4개 channel client도 인자 없는 `enableClient()`로 바꾸면 최초 시나리오는 통과하지만 mission 재기동 직후 rehydrate 요청이 자동 재연결 완료 전에 `NOT_ADMITTED`로 실패한다. 공개 `ZLinkLocationReadiness` interface는 있으나 Spring starter bean이 없어 sample이 공개 DI 경로로 readiness를 기다릴 수 없다. 선택지는 (1) starter가 public readiness bean을 제공하거나, (2) runtime channel request가 자동 재연결 완료까지 계약상 대기하는 것이다. 둘 다 현재 writable scope 밖이므로 GameQuest 수동 연결은 유지하고 항목을 open으로 남긴다.
- [x] **SMP-JV-02** (미구현) — GameQuest에 **owner Spot이 아예 없다.** 소유권을 클라이언트 해시로 흉내낸다
  - 증거: `addSpotMesh`·`PlayerQuestSpot`과 전역 monitor 부재를 요구한 gate가 기존 코드에서 실패했다. 두 QuestMission이 같은 spot mesh에 참여하고 channel은 ingress로만 남으며, 모든 quest 요청은 `PlayerId` routing id의 owner Spot으로 전달된다. 전체 runner가 `surface=SPOT ... packet=GameplayMsg` flow와 재기동 복원까지 통과했다.
- [x] **SMP-JV-03** (미구현) — GameQuest가 **event sourcing이 아니다.** "rehydrate" 게이트를 카운터로 통과한다
  - 증거: SMP-JV-19의 domain event delta fold, SMP-JV-13의 Redis replay, SMP-JV-12의 실제 process restart/channel 조회, SMP-JV-11의 `PlayerQuestSpot` owner turn이 함께 적용됐다. runner는 owner SPOT flow, mission 재기동, count `5` 복원과 rehydrate client를 모두 검증한다.
- [ ] **SMP-JV-04** (미구현) — Bingo의 정본 `yield` 사용처가 코드에 없다
- [ ] **SMP-JV-05** (결함) — Java 구현 완료. 범위 밖 release gate가 여전히 **MessagePack**을 요구한다
- [ ] **SMP-JV-06** (결함) — publish 메시지를 `Msg`로 잘못 이름 붙였다. 올바른 `Event`는 **선언만 되고 죽어 있다**
  - §0.8 중단: `BingoWinnerMsg`를 `BingoRewardAcquiredEvent`로 바꾸면 공유 proto wire 타입과 다른 언어의 handler 계약이 함께 바뀐다. 선택지는 (1) 공통 계약의 `Event`를 확정하고 다섯 언어 wire를 함께 이행하거나, (2) 공통 계약을 `Msg`로 바꾸고 publish naming 규칙 예외를 승인하는 것이다. 현재 범위에는 갭 인덱스·공통 spec 수정 권한이 없으므로 Java만 구현하지 않고 open으로 남긴다.
- [x] **SMP-JV-07** (결함) — DeliveryDispatch에 **문서에 없는 죽은 `CourierGateway` 프로세스**가 있고, Java가 **actor relay를 건너뛴다**
  - 증거: 기존 설정은 dead `CourierGateway` 금지 gate에서 실패했고, 이를 제거한 뒤에도 기존 직접 응답 구현은 `courier-bind-relayed=courier-a` gate에서 실패했다. `CourierSession`이 actor 위치와 session route를 채워 기존 actor handler로 relay하도록 수정하고 등록되지 않은 `customer-route` handler를 제거한 뒤 `./run_sample.sh`의 client/server self-check와 courier-a/b actor relay gate가 모두 통과했다.
- [x] **SMP-JV-08** (결함) — Bingo·DeliveryDispatch가 여전히 **환경변수·JVM system property**를 읽는다
  - 증거: 수정 전 애플리케이션 코드의 `System.getProperty`·`System.getenv` 14곳을 금지하는 runner gate가 실패했다. 두 샘플은 runner가 만든 properties 파일을 각 프로세스의 `--config` 인자로 받고, `SampleTopology`가 시작 시 한 번 읽는다. Bash runner의 전체 client/server self-check, Bingo PowerShell runner, 애플리케이션 코드 0건 gate가 모두 통과했다.
- [x] **SMP-JV-09** (결함) — 클라이언트 self-check가 문서보다 약하다(릴리즈 게이트)
  - 증거: 기존 코드의 `receivedCount(...)` 사용을 금지하는 gate가 실패하는 것을 확인했다. Bingo와 TicTacToe는 AUTO connector의 typed callback으로 자기 join 알림을 실제 계수하고, Bingo는 card 제출 응답의 9칸 상태와 양쪽 draw state 동일성도 확인한다. DeliveryDispatch는 기존 public `waitFor` 대기를 유지하면서 typed callback으로 알림 도착 순서를 별도 기록해 두 시나리오의 상태 순서를 단언한다. 세 샘플 runner가 모두 exit 0으로 통과했다.
- [x] **SMP-JV-10** (결함) — ShoppingMall `GetOrderStateReq`가 **읽기 전용이어야 하는데 read model을 재구축**한다
  - 근거: 삭제 직후 조회가 `null` 상태를 반환하고 명시적 rebuild 뒤에만 상태가 복원되도록 self-check를 강화했으며, Java ShoppingMall runner가 완료 표식까지 통과했다.
- [x] **SMP-JV-14** (버그) — offline Bob의 progress push가 Alice session으로 오배송된다
  - 증거: Alice connector가 Bob의 `QuestProgressNotify`를 typed callback으로 계수하도록 한 gate가 기존 구현에서 실패했다. owner 응답의 대상 player가 현재 session에 bind된 player와 같을 때만 push하도록 수정한 뒤 Alice의 Bob 알림 계수는 0이고, Bob과 reconnect한 Alice의 정상 push 검증을 포함한 전체 runner가 exit 0으로 통과했다.
- [x] **SMP-JV-15** (버그) — GameQuest sync가 `GameplayStateStore` 대신 상수 `4`로 보정한다
  - 증거: 미발행 kill을 2개 주입하고 정확한 누적값 `5`를 요구한 gate가 기존 상수 구현에서 실패했다. API와 owner가 공유하는 Redis-backed `GameplayStateStore`를 추가하고 동일 event id를 한 번만 기록하도록 한 뒤, sync와 재조회가 모두 정확히 `5`를 반환하고 GameQuest 전체 runner가 exit 0으로 통과했다.
- [x] **SMP-JV-16** (버그) — GameQuest idempotency gate가 완료 clamp 때문에 실패할 수 없다
  - 증거: 완료 전 중복 검증이 없으면 실패하는 구조 gate를 확인했다. 첫 kill 직후 같은 idempotency key를 다시 보내고 event id가 같으며 owner 조회 진행도가 정확히 `1`인지 단언하도록 바꿨다. dedupe가 없으면 clamp 전에 `2`가 되는 경로이며 GameQuest 전체 runner가 exit 0으로 통과했다.
- [x] **SMP-JV-17** (버그) — GameQuest reward idempotency gate가 domain status guard만 검증한다
  - 증거: reward assertion에 dedupe 분기 evidence가 없으면 실패하는 구조 gate를 확인했다. owner가 중복 `kill-3`을 거부할 때 event id를 Redis evidence에 기록하고, 최종 assertion이 해당 dedupe 표식과 `QuestRewardGrantedEvent` 정확히 1개를 함께 요구하도록 바꿨다. GameQuest 전체 runner가 exit 0으로 통과했다.
- [x] **SMP-JV-18** (버그) — GameQuest client가 실제 reconnect와 다른 Session Server 복원을 검증하지 않는다
  - 증거: Alice가 다른 API 노드로 다시 연결되는 코드가 없으면 실패하는 구조 gate를 확인했다. api-a를 disconnect하고 api-b connector를 다시 연결해 Alice로 bind한 뒤 owner projection 복원과 새 herb progress push를 확인하고 다시 disconnect했다. server evidence도 `player-alice:api-b` binding history와 active binding 해제를 단언하며 전체 runner가 exit 0으로 통과했다.
- [x] **SMP-JV-19** (버그) — GameQuest replay가 event delta fold가 아니라 마지막 스냅샷 덮어쓰기다
  - 증거: `StoredQuestEvent`에 `delta`와 그 합을 검증하는 server gate를 먼저 추가했을 때 기존 producer 6곳이 컴파일에 실패했다. `QuestProgressedEvent`가 실제 증가량을 기록하고 projection rebuild가 event type별로 delta 누적·reconcile·completion·reward를 fold하도록 수정했다. delete/rebuild 시나리오와 delta 합 `3` 단언을 포함한 GameQuest 전체 runner가 exit 0으로 통과했다.
- [x] **SMP-JV-13** (버그) — GameQuest owner가 Redis event stream을 시작할 때 replay하지 않는다
  - 증거: client 시나리오 뒤 Alice owner인 mission-a를 종료하고 같은 설정으로 재기동한 뒤 in-memory event stream의 FirstHunt reconcile count `5`를 요구한 gate가 기존 구현에서 빈 배열을 반환해 실패했다. `QuestStore`가 생성 시 Redis event stream을 읽어 공통 fold 함수로 player/quest projection과 SourceEventId dedupe 상태를 복원하도록 수정한 뒤 같은 gate와 전체 runner가 exit 0으로 통과했다.
- [x] **SMP-JV-12** (버그) — GameQuest rehydrate gate가 실제 재시작 없이 카운터로 통과한다
  - 증거: `markRehydrated`·`recordRehydrated`·`owner-rehydrates`가 남아 있으면 실패하는 source gate가 기존 server 코드 7곳을 검출했다. 가짜 close endpoint, gameplay 호출별 카운터와 server assertion을 제거했다. runner는 mission-a를 실제 종료·재기동하고 두 번째 client가 정상 stream/channel 경로로 Alice를 bind한 뒤 FirstHunt 상태가 `RewardGranted`, count `5`인지 단언하며 `gamequest-rehydrate=completed`까지 통과했다.
- [x] **SMP-JV-11** (버그) — GameQuest player가 owner Spot 없이 전역 QuestStore monitor를 공유한다
  - 증거: `addSpotMesh`·`PlayerQuestSpot` 존재와 `QuestStore`의 `public synchronized` 부재를 요구한 gate가 기존 구현에서 실패했다. `PlayerId` routing id로 spot을 get-or-create하고 spot route request로 gameplay/query/sync/rebuild/delete를 직렬 처리한다. store는 player별 state로 분리해 서로 다른 owner turn이 전역 monitor를 공유하지 않으며, 전체 runner가 실제 SPOT GameplayMsg flow와 restart/replay를 통과했다.
- [x] **SMP-JV-21** (버그) — `GameplayMsg`가 응답을 발명한 request/reply로 동작한다
  - 증거: `ZLinkRequestHandler<Messages.GameplayMsg,...>`가 남아 있으면 실패하는 source gate가 기존 handler를 검출했다. API는 결정적 EventId를 client에 응답하고 `GameplayMsg`를 channel/owner Spot에 one-way SEND로 전달한다. owner 결과는 API별 역방향 channel의 `QuestProcessingMsg`로 bound session registry에 전달한다. runner는 CHANNEL·SPOT_ROUTE의 SEND와 역방향 SEND를 확인하고 GameplayMsg REQUEST flow가 0건인지 단언한 뒤 전체 시나리오를 통과했다.
- [x] **SMP-JV-20** (버그) — GameQuest scale-out gate가 두 player를 순차 실행하고 owner 분산을 확인하지 않는다
  - 증거: scale-out marker를 요구한 gate가 기존 client에서 실패했다. 두 connector의 player join 뒤 request와 push future를 모두 시작하고 `CompletableFuture.allOf`에서 합류하므로 Alice 구간 종료 뒤 Bob을 시작하지 않는다. runner는 `player-scale-a` GameplayMsg가 mission-a owner Spot, `player-scale-b`가 mission-b owner Spot에서 처리된 flow와 `gamequest-scale-out=completed`를 확인하며 전체 시나리오가 통과했다.
- [x] **E2E-JV-01** (결함) — `ObservabilityOps`가 전용 Delay·Play·Session 역할 서버와 Client 시나리오를 소유하며, 인접 config의 실행 파일과 OBS selector를 더 이상 빌려 쓰지 않는다. `OBS-A1`~`OBS-C5` 단독 실행과 `all` runner 통과(2026-07-15).
- [x] **E2E-JV-02** (결함) — Config 2 커버리지 구멍(`SM-F3` 누락), 문서에 없는 `SM-Q9`
  - 근거: 수정 전 `run_e2e.sh SM-F3`가 `not mapped to an implemented client mode`로 실패했다. `SM-F3`를 기존 route-mesh 검증에 연결한 뒤 단독 실행이 통과했다. 공통 문서에 없는 `SM-Q9`는 전체 실행 목록, selector, feature-map과 시나리오 출력에서 제거했다.
- [x] **E2E-JV-03** (결함) — Client와 서버 위임 경로의 raw HTTP 0
  - 근거: `SpotService`의 2,013줄 server driver와 raw JDK client를 제거했다. Client는
    `ZLinkHttpClient`로 Gateway의 역할 operation endpoint만 호출하며 전체 selector 묶음이 통과했다.
- [ ] **E2E-JV-04** (결함) — 앱 코드의 **환경변수 읽기 535곳**인데 feature-map에 기록 **0**
- [x] **E2E-JV-05** (결함) — 11개 runner의 readiness·route settle 책임을 Client/runner가 소유한다
  - 증거: 마지막 ResilienceLifecycle server driver를 제거한 뒤 Client Suite가 역할 기동과 현재 endpoint를 소유하고 `RL-A1~D5` 전체 실행이 통과했다. 기존 3초 readiness gate와 5초 route settle 정책은 유지했다.
- [x] **E2E-JV-06** (결함) — StoreFailure·RuntimeMonitoring·ResilienceLifecycle·SpotService의 시나리오를 Client가 소유한다
  - 근거: SpotService의 51개 정식 ID를 Client scenario 파일에 연결하고 단계와 단언을 옮겼다.
    server에는 framework primitive operation만 남았으며 `/scenario`·`runMode` 금지 gate와 전체 실행이 통과했다.
- [ ] **E2E-JV-07** (결함) — **`SF-B2`가 `SF-B1`과 구별되는 것을 아무것도 단언하지 않고**, 죽은 옵션으로 시간을 잰다
  - 재검증 중단: provider 재시작과 survivor-only gate를 추가하면 기존 runner에서는 `api-b` 응답을 잡아 red가 된다. grace 초과 뒤 `api-b`를 종료하고 Redis 중단 상태에서 같은 endpoint로 재시작해도 consumer가 새 연결을 만들고 `sf-b2-restarted` 요청을 전달했다. E2E만 고치면 영구 실패하므로 Java runtime 범위가 필요하다.
- [x] **E2E-JV-08** (결함) — `feature-map` 누락, `YieldDispatch`에 **`run_e2e.sh`가 없어** 실행 불가
  - 근거: 없는 `SpotActorTransfer/feature-map.ko.md`를 요구한 파일 gate가 실패했다. 새 feature-map은 공통 Config 10, Java runner와 같은 20개 ID를 가지며, E2E-JV-17·18의 알려진 증거 결함은 부분 구현으로 남겼다. `YieldDispatch`는 Git 추적 파일이 0건이므로 이 항목의 구현 대상이 아님을 다시 확인했다.

### 가장 무거운 것

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-JV-01** | [샘플 규약](../../common/sample/README.ko.md)의 **절대 규칙**: TicTacToe만 수동 연결을 쓸 수 있다. *"위반이 하나라도 있으면 해당 샘플 변경은 완료된 것으로 판단하지 않는다"* | **부분 해결:** live 재검증 수치는 과거 29곳이 아니라 15곳이었다. Bingo 4곳, DeliveryDispatch 5곳, ShoppingMall 2곳은 인자 없는 `enableClient()`와 location-store Spot peer 발견으로 바꿔 각 전체 runner가 통과했으며 SupportChat은 0곳이었다. **남은 4곳:** GameQuest의 owner/notification channel은 자동 연결로 최초 client와 scale-out까지 통과하지만 mission 재기동 직후 request가 `NOT_ADMITTED`로 실패한다. 내부 runtime/query 접근이나 업무 요청 retry는 사용하지 않았다. starter의 public readiness bean 제공 또는 runtime request의 reconnect 대기 계약이 필요해 현재 범위에서는 open이다. |
| **SMP-JV-02** | [GameQuest §1](../../common/sample/event/gamequest.ko.md): 이 샘플의 존재 이유가 **`PlayerId`별 owner spot을 노드에 분산**하는 것이다 | **해결:** 두 QuestMission이 `gamequest.player-quests` spot mesh에 참여하고 `PlayerQuestSpot`을 등록한다. 기존 player hash channel은 ingress 선택에만 쓰이며, 실제 소유권과 직렬 처리는 `PlayerId` routing id의 spot owner가 담당한다. |
| **E2E-JV-07** | [config-6 SF-B2](../../common/e2e/config-6-store-failure-recovery.ko.md): 유예가 지나면 **새 outbound connect가 멈춘다**(장애 중 재시작한 provider를 store 복구 전에 dial하면 안 된다) | **재검증 중단:** 기존 SF-B2 뒤에 survivor-only gate를 붙이면 계속 실행 중인 `api-b` 응답을 잡아 예상대로 실패한다. 이어 grace 초과 뒤 `api-b`를 종료하고 Redis 중단 상태에서 같은 endpoint로 재시작했지만, consumer가 새 outbound 연결을 만들고 `sf-b2-restarted` 요청을 `api-b`에 전달했다. 이는 gate 누락뿐 아니라 Java runtime 계약 위반이다. **선택지:** (1) 허용 범위를 `zlink-framework-core`까지 넓혀 store failure grace 초과 시 reconnect/new connect를 억제한 뒤 provider 재시작 gate를 정식으로 넣는다. (2) 현재 범위를 유지하고 이 항목을 open으로 남긴다. |

### 샘플 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-JV-03** (미구현) | [gamequest:280](../../common/sample/event/gamequest.ko.md): "노드를 재시작해도 owner spot이 **`QuestEventStore` replay로 aggregate를 rehydrate**한다". §14(`:603`)도 rehydrate를 "노드 재시작 → replay로 aggregate 복원"으로 못박는다 | **해결:** `PlayerQuestSpot` owner turn이 player별 hot state 변경을 직렬화하고, `QuestStore`는 Redis stream을 domain event delta로 fold해 최초 활성 상태를 복원한다. runner가 실제 owner process 종료·재기동 뒤 정상 stream/channel 조회로 FirstHunt `RewardGranted`, count `5`를 확인한다. 가짜 rehydrate counter와 close endpoint는 없다. |
| **SMP-JV-04** (미구현) | [bingo §7.1:452-464](../../common/sample/bingo/README.ko.md): `BingoRoom.OnJoinedActor`는 Api 서버에 `GetPlayerRecordReq`를, `OnLeaveActor`는 `ReportBingoResultReq`를 **`yield`로** 보낸다. §5(`:192,200`)가 그 왕복을 프로세스 구성표에 넣고, `:846`이 `BingoRoomState.Wins`/`Losses`를 그 응답값으로 채운다고 적는다 | **정본 왕복이 메시지부터 없다.** `Bingo/Shared/src/main/proto/bingo_messages.proto`에 `GetPlayerRecordReq`·`ReportBingoResultReq`도, `Wins`·`Losses` 필드도 **0건**이다. `BingoRoomSpot.java:70-86`의 `onJoinedActor`/`onLeaveActor`는 로컬 맵만 만지고 Api 서버를 부르지 않는다. Java 샘플 트리 전체에 `.yield()` **0건**(언어 표면 자체가 없다 — §12.21). ⇒ **`yield`가 왜 존재하는지 보여 주는 정본 예제가 통째로 빠졌다.** room 실행 줄을 붙잡는 대기가 애초에 만들어지지 않아, terminator 축이 회귀해도 Bingo는 초록으로 남는다 |
| **SMP-JV-05** (결함) | [tictactoe §4:46,54-56](../../common/sample/tictactoe/README.ko.md)·[§9:250-251](../../common/sample/tictactoe/README.ko.md): "TicTacToe의 payload codec은 JSON이다 … **MessagePack이나 Protobuf로 바꾸지 않는다**" | **Java 구현 완료, release gate 갱신 차단:** runner에 MessagePack source·dependency 금지 gate를 추가하자 기존 서버 2곳, client 1곳과 build 의존성 2곳을 검출해 먼저 실패했다. 명시적 MessagePack 선택과 의존성을 제거해 framework의 typed JSON 기본 경로를 사용하며 전체 runner가 `PASS TicTacToe.Java`로 통과했다. POSD/DDD 재리뷰에서는 codec 정책이 호출부와 build 파일에 중복된 것을 정보 누출로 분류했다. 명시적 JSON 등록을 다섯 곳에 다시 두는 안보다 framework 기본값을 사용하는 안이 호출자 설정과 변경 지점을 줄이고 메시지별 codec 등록을 만들지 않으므로 이를 선택했으며 domain 경계 변화는 없다. 그러나 쓰기 범위 밖의 `SampleReleaseGateContractTest:677-692,759`는 여전히 MessagePack 등록·의존성을 요구해 해당 단독 contract test가 실패한다. 이 gate가 JSON 계약으로 갱신되기 전까지 open이다. SMP-JV-26과 같은 항목이다. |
| **SMP-JV-06** (결함) | [공통 샘플 §메시지 이름 원칙:56-58,68](../../common/sample/README.ko.md): `Msg`는 **응답 없는 단방향 send**, `Event`는 **publish 호출에만** 쓴다. [bingo:36,808,991,1026](../../common/sample/bingo/README.ko.md)은 room reward fanout 메시지를 **`BingoRewardAcquiredEvent`**로, 그 handler를 `BingoRewardAcquiredEventHandler`(`:280,426`)로 고정한다 | **§0.8 중단:** Java는 `BingoWinnerMsg`를 실제 publish하고 `BingoRewardAcquiredEvent`는 선언만 한다. 이를 Java만 rename하면 공유 proto wire 타입과 다른 언어 handler가 깨진다. **결정 선택지:** (1) 공통 `Event` 계약을 유지하고 전 언어 wire를 함께 이행, (2) 공통 문서를 `Msg`로 바꾸고 publish naming 예외를 승인. 갭 인덱스·공통 spec이 writable scope 밖이므로 결정 전 구현하지 않는다. |
| **SMP-JV-07** (결함) | [deliverydispatch §5:191-199](../../common/sample/deliverydispatch/README.ko.md)의 프로세스 표는 `Dispatch`·`CourierSession`·`CourierSpotNode1/2`·`Tracking`·`CustomerGateway`·`Client`뿐이다. `:243-245`는 *"courier별 session route는 **별도 gateway나 registry가 아니라** 해당 courier actor가 기억한다"* | **해결:** 문서에 없고 요청 송신자도 없던 `Server/CourierGateway`를 project와 runner에서 제거했다. `CourierSession`은 actor를 찾거나 만든 뒤 현재 actor 위치와 session route를 `BindCourierSessionReq`에 채워 `BindCourierSessionActorHandler`로 relay하며, actor의 `BindCourierSessionRes`가 원래 client 요청에 응답한다. 등록되지 않은 `customer-route` handler도 제거해 status push는 기존 `Tracking` → `sendToActor` 경로 하나만 사용한다. runner는 금지된 role/handler 잔존과 courier-a/b actor relay 표식을 검사한다. 공유 `BindCourierReq`·`BindCourierRes` wire 타입은 다른 언어 호환성을 깨지 않도록 유지했다. |
| **SMP-JV-08** (결함) | [공통 샘플:196-197](../../common/sample/README.ko.md): "Endpoint, Redis, routing id, timeout과 로그 경로를 환경 변수나 JVM system property로 전달하지 않으며, server와 client 애플리케이션 코드에서 **직접 사용할 수 있는 환경 변수는 0개다**" | **해결:** live 재검증에서 확인한 Bingo 6곳과 DeliveryDispatch 8곳의 `System.getProperty`·`System.getenv`를 제거했다. runner가 실행별 endpoint, Redis, routing id와 로그 경로를 properties 파일에 기록하고, 각 role과 client는 `--config`로 파일 경로 하나만 받아 `SampleTopology`에서 읽는다. system property를 helper 뒤에 숨기는 대안은 금지된 설정 통로를 유지하므로 사용하지 않았다. Bash runner 두 개와 Bingo PowerShell runner가 실제 프로세스 구성으로 통과하며, runner의 정적 gate가 애플리케이션 코드의 직접 호출 0건을 계속 확인한다. |
| **SMP-JV-09** (**실패할 수 없는 단언**) | [공통 샘플 §Client self-check 기준:358](../../common/sample/README.ko.md): "**자기 자신에게 보내면 안 되는 join notify는 받지 않았음을 확인한다**" | **해결:** Bingo와 TicTacToe는 AUTO mode에서 갱신되지 않는 `receivedCount(...)`를 사용하지 않는다. 각 connector에 typed `PlayerJoinedNotify` callback을 등록해 자기 actor id 알림을 실제 계수하고 전체 시나리오 뒤 0인지 단언한다. Bingo는 두 card가 9칸인 제출 응답과 draw별 `DrawSeq`·`Number`·전체 state 동일성도 확인한다. DeliveryDispatch는 도착 대기에 public `waitFor`를 계속 사용하고, 별도 typed callback으로 상태 알림을 기록해 success와 reassignment의 실제 순서를 정확히 비교한다. |
| **SMP-JV-10** (**버그**) | [shoppingmall §8:433](../../common/sample/event/shoppingmall.ko.md): "`GetOrderStateReq`는 `OrderReadModelStore` 조회 모델만 읽는다. **조회가 주문을 진행시키거나 이벤트를 기록하면 안 된다.**" `:425`는 "`CommerceApi`는 … **조회 모델 재생성을 직접 호출하지 않는다**" | `CommerceApiService.java:56-62` — projection이 없으면 `rebuildProjection(orderId)`으로 **owner workflow spot에 `RebuildOrderProjectionReq`를 보낸다**(`:68-74`). 그 끝은 `RedisCommerceStore.java:293-300`의 `saveProjection(rebuilt)` — **쓰기다.** ⇒ 단순 `GET /orders/{id}` 폴링(`Program.java:119-121`)이 owner spot의 turn을 소비하고 store에 write를 낸다. 덤으로 client self-check의 "projection 삭제 → rebuild" 게이트(`Client/Program.java:96,102`)도 **다음 폴링이 알아서 되살려 주므로** 아무것도 증명하지 못한다 |

### E2E 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-JV-01** (결함) | [E2E README §2.2:218-236](../../common/e2e/README.ko.md): config마다 역할 서버 앱과 `Client/Program.*`·`Client/Scenarios/`를 둔다. `:109`는 *"프로젝트 안에서 옵션만 바꿔 구동하지 않는다"* | **해결:** `ObservabilityOps`에 Delay·Play·Session 역할 서버 진입점과 13개 공식 OBS 시나리오 파일을 가진 Client를 추가했다. runner는 이 config의 `observability-ops-delay`·`play`·`session`·`client`만 빌드하고 실행하며, 인접 config Client에서 OBS selector도 제거했다. 공통 topology 동작을 모두 복제하는 안과 이미 검증된 source support를 공유하되 config별 진입점·selector를 소유하는 안을 비교해 후자를 선택했다. 따라서 실행 파일과 시나리오 책임은 config-11 경계 안에 있고 topology 세부 지식은 지원 모듈에 남는다. runner의 정적 게이트가 인접 실행 파일 차용과 OBS selector 재도입을 막는다. `OBS-A1`~`OBS-C5` 단독 실행과 `./run_e2e.sh all`이 통과했다. Kotlin의 같은 결함은 E2E-KT-06으로 별도 추적한다. |
| **E2E-JV-02** (결함) | [config-2:558](../../common/e2e/config-2-spot-service.ko.md)에 `SM-F3`("한 channel에 일반 packet과 spot route packet 혼재")가 있다. [E2E README §2.8:435-447](../../common/e2e/README.ko.md)은 feature-map이 **config 문서의 모든 시나리오 ID를 행으로** 두라고 한다 | **해결:** `SM-F3` selector가 일반 route request와 spot request/send를 함께 검증하는 기존 `route-mesh` 모드를 단독 실행한다. 공통 문서에 없는 `SM-Q9`는 전체 실행과 정식 시나리오 목록에서 제거했으며, 내부 multi-node 진단은 계약 ID를 출력하지 않는다. |
| **E2E-JV-03** (결함) | [E2E README:43-44](../../common/e2e/README.ko.md): client는 **언어별 HTTP client wrapper**를 사용하고 raw `HttpClient`로 app endpoint를 호출하지 않는다 | **해결:** SpotService의 마지막 raw 경로인 Shared `ClientScenario`를 제거했다. Client의 `SpotServiceScenarioContext`가 public `ZLinkHttpClient`로 Gateway의 작은 operation endpoint를 호출하며 stream connector 검증은 Client가 직접 수행한다. Client·Shared source gate는 raw JDK client와 `/scenario` endpoint 재도입을 막는다. |
| **E2E-JV-04** (결함) | [E2E README §2.6:343-344](../../common/e2e/README.ko.md): "timeout, 로그와 evidence 경로를 환경 변수나 JVM system property로 전달하지 않으며, server와 client 애플리케이션 코드에서 직접 사용할 수 있는 **환경 변수는 0개다**". §2.8은 미충족 항목을 feature-map에 gap으로 남기라고 한다 | config마다 `Shared/Env.java`(또는 `Configuration/Env.java`) 같은 얇은 `System.getenv` 래퍼를 두고 앱 코드가 그것을 **324곳**에서 부른다. `ZLINK_JAVA_E2E_*` 이름 문자열이 java 소스에 **440번**, **서로 다른 122개**가 등장한다 — endpoint·node rid·redis key prefix·drain policy·message-flow 모드·log dir이 전부 이 통로다. ⇒ 0개를 요구한 축이 **사실상 유일한 설정 통로**다. 그런데 **feature-map 10개 어디에도 이 gap이 기록돼 있지 않다.** (체크리스트의 "535곳"은 재현하지 못했다. 위 세 수치가 실측이다) |
| **E2E-JV-05** (결함) | [E2E README §2.1:157-171](../../common/e2e/README.ko.md): local readiness timeout **3초**, route settle **5초**, scenario settle **3초**. *"이 값 안에 준비되지 않는 로컬 e2e는 대기 시간을 늘려서 통과시키지 않는다 … 긴 대기는 버그를 늦게 발견하게 만들기 때문에 완료 조건으로 인정하지 않는다"* | **해결:** `RuntimeMonitoring`·`PubSub`·`RegistryMessaging`·`AutomaticTurnDispatch`·`RegistrationCodec`·`StoreFailure`·`SpotService`·`ToActorMessaging`·`SpotActorTransfer`·`ObservabilityOps`는 readiness를 3초로 제한하고 이름 있는 5초 route settle을 적용했다. ObservabilityOps는 port·HTTP·metrics 준비 확인만 0.1초 × 30회로 제한하고 drain 완료·metric 관측·종료 증거 대기는 시나리오 계약에 따라 유지했다. 기존 상한과 settle 부재를 잡는 gate가 먼저 실패했으며 OBS-A1 대표 실행과 OBS-A1~C5 전체 실행이 통과했다. POSD 재리뷰에서는 서로 다른 반복 횟수에 흩어진 readiness 정책과 이름 없는 topology 대기를 정보 중복 위험으로 분류했다. 별도 대기 helper를 추가하는 안보다 기존 wait 함수 안에서 공통 상수를 사용하고 호출 지점에는 route settle을 직접 드러내는 안을 선택해 얕은 pass-through 계층 없이 정책 변경 지점을 줄였으며, domain 코드는 건드리지 않아 DDD 경계 변화는 없다. 다만 역할 앱을 AutomaticTurnDispatch에서 빌려 쓰는 E2E-JV-01은 별도 open이다. SpotActorTransfer는 정수 `SECONDS` 경계 때문에 실제 3초보다 짧아지던 deadline도 0.1초 × 30회로 고쳤고 ST-A1~F6 전체가 통과했다. 첫 전체 실행은 ST-B3 node가 2.443초에 시작했는데도 이 정수 deadline 때문에 실패했으며 수정 후 해당 selector와 나머지 selector가 통과했다. ToActorMessaging은 Redis TCP, 역할 HTTP, 역할 application marker의 인라인 30초 wait를 모두 같은 3초 상한으로 묶었다. SpotService는 느린 CI용 override로 기본값을 30초까지 늘리던 경로도 제거하고 scenario settle을 3초로 이름 붙였다. ATD는 60회 readiness client 재시도를 제거해 settle 직후 한 번의 요청만 허용하며 restart 뒤에도 같은 경로를 쓴다. 각 runner에서 기존 상한이나 settle 부재를 잡는 gate가 먼저 실패했다. MON, PS, RM, ATD, RC, TA, ST 전체와 SpotService의 default-batch·SM-F6·SM-G1~G4가 새 기본값으로 통과했다. 기존 bind 전용 retry는 port 경합에만 사용했다. StoreFailure 전체 실행은 SF-A1~D1까지 통과한 뒤 SF-D2 traffic stall로 한 번 실패했지만 SF-D2 단독 재실행과 뒤의 SF-D3·E1은 통과했다. ResilienceLifecycle도 server driver 제거 뒤 Client Suite가 역할 process와 현재 endpoint를 소유하며 `RL-A1~D5` 전체 실행이 통과했다. |
| **E2E-JV-06** (결함) | [E2E README:236](../../common/e2e/README.ko.md): `Client/Scenarios/<ScenarioId><Name>Scenario.*`는 시나리오 ID마다 파일 하나를 두고, [§2.5:324-329](../../common/e2e/README.ko.md)는 server의 `/run`·`/scenario` endpoint에 검증 전체를 위임하지 못하게 한다 | **해결:** SpotService의 51개 정식 ID가 각 Client scenario 파일에서 순서와 단언을 소유한다. `ScenarioSuite`는 selector 연결만 하고, 공통 context는 HTTP·stream primitive와 evidence probe만 제공한다. Gateway는 framework runtime을 호출하는 7개 primitive operation만 제공하며 whole-scenario endpoint는 없다. default-batch·SM-F6·SM-G2~G4가 통과했고, 전용 launcher의 stream endpoint 누락을 고친 뒤 SM-G1도 통과했다. |
| **E2E-JV-08** (미구현) | [E2E README §2.8:435-447](../../common/e2e/README.ko.md): 언어별 e2e에는 **config별 `feature-map.ko.md`를 둔다.** [§2.7:355](../../common/e2e/README.ko.md)은 `run_e2e.*`가 build·기동·client 실행을 책임진다고 규정한다 | **해결:** `SpotActorTransfer/feature-map.ko.md`가 공통 Config 10과 runner의 ST-* 20개 ID를 빠짐없이 연결한다. 알려진 증거 결함은 완료로 숨기지 않고 E2E-JV-17·18을 참조하는 부분 구현으로 기록했다. `YieldDispatch/`는 Git 추적 파일과 통합 runner 항목이 모두 없으므로 이 E2E gap의 대상이 아니며, `yield` 기능 부재는 SMP-JV-04가 계속 추적한다. |

#### E2E-JV-03 진행 기록

- raw client를 금지하는 실행 전 gate가 각 변경 전 실패하는 것을 확인했다. `RuntimeMonitoring`,
  `AutomaticTurnDispatch`, `ObservabilityOps`, `StoreFailure`, `SpotActorTransfer`, `PubSub`,
  `SpotService`, `ToActorMessaging`, `ResilienceLifecycle`의 Client 주 경로를 public
  `ZLinkHttpClient`로 전환했다. 기존 wrapper 경로인 `RegistryMessaging`과 `RegistrationCodec`을
  합치면 Client 주 경로 실측은 wrapper 11 : raw 0이다.
- timeout과 수명은 역할별 기존 계약을 유지했다. 대표적으로 RuntimeMonitoring은 5분,
  SpotActorTransfer는 14초 POST·5초 GET, SpotService는 2분이며 ResilienceLifecycle은 framework
  operation별 300ms~15초와 300ms health probe를 유지한다. ToActorMessaging은 30초 route-failure 결과를
  수용하도록 35초 HTTP 상한을 명시했다.
- `ResilienceLifecycle`은 문서에 없던 `ResilienceProcessManager` raw health probe도 red gate가
  검출했다. 두 Client 경로를 전환한 뒤 `RL-A1` 대표 실행과 `RL-A1~D5` 전체 실행이 통과했다.
  이후 server `/scenario/<mode>` driver도 제거해 역할 동작만 HTTP wrapper로 호출하며, 시나리오와
  process 수명은 Client가 소유한다.
- `RegistrationCodec`의 별도 evidence 조회도 Client 전용 wrapper를 소유하도록 바꿨다. 기존 3초
  timeout을 유지하고 프로그램 종료 때 자원을 닫는다. Client 전체 raw 금지 gate가 변경 전에
  실패했고, `RC-A1~B5` 전체 실행이 통과했다.
- `RegistryMessaging`의 동적 역할 health probe도 wrapper로 바꿨다. launcher 하나가 JDK client를
  공유하는 안 대신 각 동적 process가 자신의 endpoint·300ms timeout·wrapper 수명을 소유하게 해
  변경 지식을 한곳에 모았다. Client 전체 raw 금지 gate가 변경 전에 실패했고, `RM-A1~C9`와
  동적 역할 시나리오 `RM-C7`·`RM-B1`·`RM-B2`·`RM-A4`를 포함한 전체 실행이 통과했다.
- RuntimeMonitoring은 `runScenario`·`/scenario/`·`TriggerScenario`가 남으면 실패하는 gate가 기존
  server driver와 Client 위임 9곳을 검출했다. Trigger는 framework request, validation 시도와 evidence
  조회만 제공하고, 시나리오의 단계·프로세스 수명·단언은 Client 파일로 옮겼다. Trigger의 raw JDK
  client도 함께 제거했으며 `MON-A1~D1` 전체 실행이 통과했다.
- ResilienceLifecycle은 `runMode`·`/scenario/`·`ConsumerScenario`가 남으면 실패하는 gate가 1,014줄
  server driver와 Client 위임 20곳을 검출했다. Consumer는 framework request·send·topology 조회만
  제공하고 모든 단계와 단언은 Client 시나리오 파일로 옮겼다. raw JDK client도 함께 제거했으며
  `RL-A1` 대표 실행, `RL-B1` 단독 실행과 `RL-A1~D5` 전체 실행이 통과했다.
- 전체 app을 다시 검색해 raw JDK client가 0건임을 확인했다. SpotService의 Shared server driver와
  whole-scenario endpoint도 제거했고 Client wrapper 경로로 통합했다.
- POSD/DDD 재리뷰에서는 JDK transport 타입 누출, Shared의 server/client 책임 혼합, endpoint·상태·수명
  정책 중복을 위험 신호로 분류했다. Shared에 wrapper 의존성을 추가하는 안과 Client 역할 객체로
  분리하는 안을 비교해 후자를 선택했고, domain model은 변경하지 않았다. PubSub는 가변 endpoint
  wrapper 수명을 Client 내부 모듈 하나에 가두고, StoreFailure와 ToActorMessaging은 client 요청을
  Shared server helper에서 Client로 옮겼다. SpotService까지 같은 경계를 적용한 뒤 이 묶음에 의미 있는
  raw transport 책임 누출은 남지 않았다.

#### E2E-JV-06 진행 기록

- StoreFailure runner에 `context.runStoreFailure*()` 위임을 금지하는 gate를 먼저 추가했고, 기존
  `ClientContext`의 시나리오 메서드 14개와 시나리오 파일 11곳을 검출해 red를 확인했다.
- 각 시나리오 파일이 outage·recovery 단계, 시간 경계와 결과 단언을 직접 소유하도록 옮겼다.
  `ClientContext`는 HTTP 요청, traffic 생성, topology·status polling처럼 여러 시나리오가 함께 쓰는
  probe만 제공한다. 사용되지 않던 expected-provider helper와 endpoint 검증 중복도 제거했다.
- 전체 runner는 SF-A1~D1까지 통과한 뒤 기존 SF-D2 traffic-stall 경계가 6.0678초로 한 번 실패했다.
  대기 확대나 기능 retry 없이 SF-D2 단독 재실행이 통과했고, 뒤의 SF-D3·E1도 각각 통과했다.
- POSD/DDD 리뷰에서는 한 줄 scenario의 시간적 분해와 532줄 context의 특수·범용 코드 혼합을
  위험 신호로 분류했다. 모든 HTTP·polling 코드를 scenario마다 복제하는 안과, scenario가 계약 순서와
  단언을 소유하고 재사용 probe만 context에 남기는 안을 비교해 후자를 선택했다. 호출부에서 각 계약이
  드러나면서 transport 구현 중복은 늘지 않았고 domain model은 변경하지 않았다.
- RuntimeMonitoring runner의 위임 금지 gate가 기존 server driver와 Client의 한 줄 위임 9곳을 먼저
  검출했다. 각 시나리오 파일이 계약 순서와 단언을 소유하고, `MonitoringScenarioContext`는 public HTTP
  wrapper, evidence probe와 자신이 시작한 service-b 프로세스 수명만 캡슐화한다. Trigger는 framework
  request·validation 같은 역할 동작만 제공한다. 실패 주입 타이머가 첫 실패 delivery를 무제한 반복해
  5초 안에 12만 건·약 47 MiB evidence를 만들던 문제는 첫 실패 뒤 정상 완료하도록 유한하게 정의했다.
  body 제한 확대나 대기 확대 없이 `MON-A1`과 `MON-A1~D1` 전체 실행이 통과했다.
- POSD/DDD 재리뷰에서는 663줄 Trigger가 transport·프로세스·시나리오 정책을 함께 가진 god-driver인 점과
  Client가 `Process`를 직접 소유하는 수명 누출을 위험 신호로 분류했다. (1) 기존 driver를 wrapper로만
  바꾸는 안과 (2) server는 역할 동작만 제공하고 Client가 시나리오를 소유하는 안을 비교해 후자를
  선택했다. 재시작 process도 context 내부에서 종료까지 책임지게 해 호출자는 포트 종료와 process 종료를
  구별할 필요가 없다. domain model은 바뀌지 않았다.
- ResilienceLifecycle의 위임 금지 gate가 1,014줄 `ConsumerScenario`, `/scenario` endpoint와 Client의
  한 줄 위임 20곳을 먼저 검출했다. Consumer는 work request·send·unhandled request·peer 조회만
  제공하고, 시나리오 파일은 provider 재기동·drain·store outage·storm의 계약 순서와 단언을 직접
  소유한다. Suite는 역할 프로세스와 현재 provider endpoint를 Context 생성 시 전달한다. `RL-A1`,
  `RL-B1`, `RL-A1~D5` 전체 실행이 통과했다.
- POSD/DDD 재리뷰에서는 server의 transport·topology·시나리오 정책 혼합, Client 한 줄 위임, A2 이후
  현재 endpoint를 최초 환경값에서 다시 읽는 정보 중복을 위험 신호로 분류했다. (1) 기존 driver의 raw
  HTTP만 wrapper로 바꾸는 안과 (2) server는 역할 동작만 제공하고 Client가 시나리오와 현재 topology를
  소유하는 안을 비교해 후자를 선택했다. 실패한 비동기 시나리오를 signal timeout으로 숨기지 않도록
  process manager도 scenario future를 함께 관찰한다. domain model은 바뀌지 않았다.
- SpotService의 위임 금지 gate가 2,013줄 `ClientScenario`, `/scenario` endpoint와 Client의 한 줄
  위임 51곳을 먼저 검출했다. 각 시나리오 파일이 계약 순서와 단언을 소유하고,
  `SpotServiceScenarioContext`는 여러 시나리오가 공유하는 HTTP·stream operation과 evidence probe만
  제공한다. Gateway의 `GatewayOperationSpot`은 framework runtime 지식을 한곳에 가두고 7개 primitive
  operation만 노출한다.
- POSD/DDD 재리뷰에서는 Shared의 server/client 책임 혼합, whole-scenario endpoint, 51개 한 줄 위임을
  위험 신호로 분류했다. (1) 기존 server driver를 wrapper 뒤에 유지하는 안과 (2) Client가 정책을
  소유하고 Gateway가 runtime primitive만 제공하는 안을 비교해 후자를 선택했다. Client를 framework
  host로 만드는 안은 공통 E2E 계약을 위반하므로 배제했다. domain model은 변경하지 않았고, context의
  재사용 operation과 scenario 정책이 분리되어 의미 있는 pass-through driver는 남지 않았다.

## 라운드 5 (2026-07-14) — GameQuest 심층

**얕은 패스는 "owner Spot이 없다"까지만 봤다. 깊이 파니 샘플 전체가 전제를 구현하지 않았다.**

Java와 Kotlin GameQuest는 **같은 코드베이스의 두 문법**이다. Spot도, spot-mesh도, event
sourcing도, location-store binding도, 자동 연결도 **없다.** 있는 것은 프로세스 전역 `HashMap` 위에
얹은 2-shard request/reply 서비스와, **쓰기만 하고 읽지 않는** Redis 감사 로그다.

**그리고 self-check의 가장 중요한 게이트 5개가 구조적으로 실패할 수 없다.**

### 진짜 버그

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-JV-11** (**버그**) | [gamequest §1·§8](../../common/sample/event/gamequest.ko.md): 이 샘플의 존재 이유가 **`PlayerId`별 owner spot을 spot-mesh에 분산**하는 것이다 | **해결:** channel handler는 `PlayerQuestRouter`에 위임하는 ingress adapter이며, router는 `PlayerId`로 `PlayerQuestSpot`을 get-or-create하고 framework spot route로 요청한다. spot은 create request의 player와 routing id 일치를 확인하고 모든 요청의 player도 owner와 같은지 검사한다. `QuestStore`는 player별 projection/event/dedupe state를 보관하며 전역 `synchronized`를 사용하지 않는다. runner는 SPOT surface의 실제 `GameplayMsg` flow를 요구한다. |
| **SMP-JV-12** (**버그**) | [gamequest §14](../../common/sample/event/gamequest.ko.md): rehydrate는 **노드 재시작 → event replay로 aggregate 복원**이다 | **해결:** `/self-check/owner`와 gameplay handler의 `markRehydrated`, Redis 카운터와 이를 읽는 assertion을 모두 제거했다. runner는 source gate로 이 표식의 재도입을 막고, Alice owner인 mission-a를 실제 종료·재기동한다. 두 번째 client는 api-a stream으로 다시 연결해 정상 channel 요청으로 projection을 조회하고 FirstHunt가 `RewardGranted`, count `5`인지 확인한다. |
| **SMP-JV-13** (**버그**) | [gamequest §9](../../common/sample/event/gamequest.ko.md): owner는 **최초 활성 시 event stream을 replay**한다 | **해결:** `QuestStore` 생성 시 Redis의 append-only event stream을 읽고 player/quest별로 정렬해 SMP-JV-19의 공통 fold 함수로 projection을 복원한다. persisted `SourceEventId`도 dedupe 상태로 복원한다. runner는 전체 시나리오 뒤 Alice owner인 mission-a 프로세스를 종료하고 같은 설정으로 재기동한 다음, 기존 `/self-check/events`에서 FirstHunt reconcile count `5`가 프로세스 메모리에 복원됐는지 검사한다. |
| **SMP-JV-14** (**버그**) | [gamequest §7](../../common/sample/event/gamequest.ko.md): notify는 **session binding을 가진 노드로 route**하고, **binding이 없으면 생략**한다 | **해결:** `GameQuestSession`은 owner 응답의 대상 player와 현재 session에 bind된 player가 같은 경우에만 progress/completed notification을 client로 보낸다. client gate는 Alice connector에 typed callback을 등록한 뒤 offline Bob event를 Alice 요청 경로로 주입하고 Bob 알림 계수가 0인지 확인한다. 이후 Bob이 api-b에 bind한 상태의 정상 completion push와 Alice의 api-b reconnect 후 정상 progress push도 계속 검증한다. |
| **SMP-JV-15** (**버그**) | [gamequest §9](../../common/sample/event/gamequest.ko.md): 유실되면 `GameplayStateStore`의 **누적 fact로 재계산**한다 | **해결:** `GameplayStateStore`가 API 노드의 kill·item·mission·feature·area fact를 Redis에 기록하고, owner의 `QuestStore.sync(playerId)`가 같은 저장소에서 wolf kill 누적값을 읽는다. 동일 event id는 저장소 내부 Redis set으로 한 번만 반영한다. self-check는 미발행 kill 2개를 다른 API 노드에 기록한 뒤 상수 하한이 아니라 정확한 누적값 `5`를 sync 응답과 재조회에서 모두 확인한다. |
| **SMP-JV-16** (**버그**) | [gamequest §14](../../common/sample/event/gamequest.ko.md): 같은 `IdempotencyKey` 재전송 → **진행 중복 증가 없음** | **해결:** 첫 kill로 진행도 1을 확인한 직후 같은 `kill-1` 요청을 다시 보낸다. 응답 event id가 최초 응답과 같고, owner의 `GetQuestProgressReq` 결과가 완료 clamp 전에도 정확히 1인지 확인한다. dedupe가 없으면 진행도가 2가 되므로 결정적 event id 문자열이나 완료 상태 clamp만으로 통과할 수 없다. 최종 server assertion도 `QuestProgressedEvent`가 정확히 3개인지 계속 확인한다. |
| **SMP-JV-17** (**버그**) | [gamequest §14](../../common/sample/event/gamequest.ko.md): reward idempotency | **해결:** owner의 idempotency map이 중복 `kill-3`을 거부할 때 event id를 공유 Redis evidence에 기록한다. 최종 server assertion은 `deduplicated:player-alice-kill-3` 표식과 `QuestRewardGrantedEvent`가 정확히 1개라는 조건을 함께 요구한다. dedupe 분기가 제거되면 domain status guard가 reward 중복만 막더라도 dedupe evidence 조건이 실패한다. |
| **SMP-JV-18** (**버그**) | [gamequest §14](../../common/sample/event/gamequest.ko.md): reconnect = **연결 끊고 binding 해제 → 다른 노드로 재접속 → 조회로 복원** | **해결:** Alice의 api-a connector를 disconnect한 뒤 Bob이 사용했던 api-b connector도 disconnect하고 같은 endpoint에 다시 connect한다. Alice로 bind한 응답에서 기존 `FirstHunt` 완료 projection을 확인하고, 새 `CollectItemReq`의 `QuestProgressNotify`가 api-b 연결로 오는지 검증한 뒤 다시 disconnect한다. server assertion은 `player-alice:api-b` binding history와 Alice active binding 해제를 함께 확인한다. |
| **SMP-JV-19** (**버그**) | [gamequest §9](../../common/sample/event/gamequest.ko.md): **상태 = 이벤트의 fold**. `QuestProgressed`는 `Delta`를 갖는다 | **해결:** Java 내부 event-store 행에 `delta`를 기록한다. projection rebuild는 `QuestProgressedEvent`의 delta를 누적하고 reconcile은 절대 보정값을 적용하며 completion/reward event로 완료 상태를 복원한다. server assertion은 Alice의 세 progress event가 delta 합 `3`을 이루는지 검사하고, 기존 delete/rebuild client 시나리오가 fold 결과를 검증한다. 이 저장 행은 Java 샘플의 Redis와 self-check에서만 사용하며 다른 언어와 교환되는 wire는 바꾸지 않았다. |
| **SMP-JV-20** (**버그**) | [gamequest §8](../../common/sample/event/gamequest.ko.md): scale-out은 두 player가 **다른 owner에서 동시 처리**된다 | **해결:** 별도 scale-out 구간이 api-a의 `player-scale-a`와 api-b의 `player-scale-b`를 bind한 뒤 두 gameplay request와 두 push 대기를 join 없이 시작하고 하나의 `allOf`에서 합류한다. runner는 각 GameplayMsg가 각각 mission-a와 mission-b의 서로 다른 owner Spot에서 처리됐는지 flow log로 확인한다. 서로 다른 JVM의 시각을 비교하지 않는다. |
| **SMP-JV-21** (**버그**) | [샘플 규약](../../common/sample/README.ko.md): `Msg`는 **응답 없는 단방향**이다. request/reply는 `Req`/`Res`여야 한다. entry-spot → owner spot 내부 메시지도 **예외가 아니다** | **해결:** API는 gameplay command를 검증해 결정적 EventId를 client에 즉시 응답하고, `GameplayMsg`를 channel과 owner Spot에 모두 one-way SEND로 전달한다. owner는 처리 결과를 `QuestProcessingMsg`로 source API의 역방향 channel에 보내며, API의 player별 session registry가 현재 bound session에만 notify를 push한다. 기존 `QuestProcessingRes`와 GameplayMsg request handler는 제거했다. offline event 뒤에는 같은 공개 연결의 `GetQuestProgressReq` 한 번으로 owner 적용을 확인하므로 sleep·retry를 사용하지 않는다. |
| **SMP-JV-22** (결함) | [샘플 규약](../../common/sample/README.ko.md): **다른 언어 구현을 복사 기준으로 삼지 않는다** | `sample-porting-inventory.ko.md:3` — **"기준: dotnet samples/GameQuest"**. 그리고 `:19` — **"남은 gap 또는 partial 항목이 없다"**. 위 11개 버그가 전부 그 "완료" 행 아래에 있다 |

**ZoneWorld는 Java/Kotlin 구현이 아직 없다** — 계획된 순서(`dotnet → java → kotlin → node → cpp`)상 정상이며 갭이 아니다.

## 라운드 5 — TicTacToe · SupportChat 심층

**Java/Kotlin은 SupportChat에서 C++보다 훨씬 건강하다** — 날조된 게이트도, seq-vs-wallclock idle
timer도, 고객의 자기 상담원 등록도 없다. 그런데 **TicTacToe에서 새 버그가 쏟아졌다.**

### 진짜 버그

- [x] **SMP-JV-23** (**버그**) — TicTacToe room owner가 항상 첫 Play로 고정된다.
  - 근거: 동일 API의 연속 생성 두 건이 서로 다른 Play와 서로 다른 room ID를 반환하도록 self-check를 추가했으며, 수정 전 단언 실패와 수정 후 `PASS TicTacToe.Java`를 확인했다.
- [x] **SMP-JV-24** (**버그**) — 거절된 room join을 성공처럼 커밋한다.
  - 근거: 세 번째 actor의 typed rejection이 성공 응답으로 바뀌는 실패 게이트를 확인한 뒤 `Accepted`에서만 actor 상태를 갱신하도록 수정했고, `PASS TicTacToe.Java`를 확인했다.
- [x] **SMP-JV-25** (**절대 규칙 위반**) — SupportChat이 router를 수동 연결한다.
  - 근거: 수동 연결 두 곳을 출력하며 실패하는 runner gate를 먼저 추가하고 호출을 제거했으며, 자동 연결만으로 `supportchat full client/server self-check completed`를 확인했다.
- [x] **SMP-JV-27** (**버그**) — 게임 진행 중 leave가 actor를 제거한다.
  - 근거: 진행 중 leave 뒤 정상 수 요청이 timeout으로 실패하는 게이트를 확인한 뒤 조기 leave를 상태 변경 없는 동작으로 제한했으며, 같은 흐름이 `PASS TicTacToe.Java`까지 통과했다.
- [x] **SMP-JV-28** (**버그**) — API 응답의 Play node rid를 이름 규칙으로 만들어 낸다.
  - 근거: runner의 rid를 `play-node-N`과 무관한 값으로 바꾸자 milestone 매핑 단언이 실패했으며, 설정의 endpoint/rid 쌍을 사용하도록 수정한 뒤 `PASS TicTacToe.Java`를 확인했다.
- [x] **SMP-JV-29** (**버그**) — timer가 `LeaveGameReq` 없이 actor를 정리한다.
  - 근거: timer cleanup 호출을 출력하며 실패하는 gate를 먼저 추가하고 timer에서 lifecycle 책임을 제거했으며, client leave만으로 두 actor destroy와 `PASS TicTacToe.Java`를 확인했다.

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-JV-23** (**버그**) | [tictactoe §6](../../common/sample/tictactoe/README.ko.md): room owner는 **deterministic round-robin**으로 고른다 — 첫 room은 `play-a`, 다음 room은 `play-b`처럼 선택한다 | **해결:** API handler가 프로세스 로컬 원자 카운터로 Play를 선택한다. 게이트를 복구하면서 Play별 로컬 시퀀스가 같은 `RoomId`를 만드는 추가 결함이 드러나 SpotNode rid를 room ID에 포함했다. 공유 wire는 바꾸지 않았다. Kotlin의 별도 결함은 이 작업 범위에서 수정하지 않았다. |
| **SMP-JV-24** (**버그**) | [tictactoe §16](../../common/sample/tictactoe/README.ko.md): 조건을 만족하지 못하면 **join을 거부하거나 오류 response를 반환해야 한다** | **해결:** room이 가득 찬 경우 상태를 담은 typed rejection을 반환하고, entry handler는 `Accepted` 결과에서만 actor의 room 소속을 갱신한다. 세 번째 actor의 request가 오류로 끝나는 self-check가 rejection 분기를 직접 검증한다. |
| **SMP-JV-25** (**절대 규칙 위반**) | [샘플 규약](../../common/sample/README.ko.md) | **해결:** Support와 Session의 수동 `connectRouter(...)` 호출을 제거했다. runner는 SupportChat Java source에 이 호출이 다시 생기면 역할을 시작하기 전에 실패하고, runtime self-check는 공유 location store 기반 자동 연결을 검증한다. |
| **SMP-JV-26** (**버그**) | [tictactoe §4](../../common/sample/tictactoe/README.ko.md): payload codec은 **JSON**이다. **MessagePack이나 Protobuf로 바꾸지 않는다** | **Java 구현 완료:** SMP-JV-05와 같은 수정으로 server·client 모두 framework의 typed JSON 기본 경로를 사용하며 전체 runner가 통과했다. Java release gate의 반대 단언과 Kotlin 구현은 각각 별도 범위에 남는다. |
| **SMP-JV-27** (**버그**) | [tictactoe §17](../../common/sample/tictactoe/README.ko.md): leave는 **게임 종료 후** 단계다 | **해결:** room 상태가 terminal일 때만 destroy 표시와 `leaveActor`를 실행한다. 진행 중 one-way leave는 응답할 오류가 없으므로 상태를 바꾸지 않으며, 바로 이어지는 정상 수가 처리되는 self-check로 이를 검증한다. |
| **SMP-KT-07** (**버그**) | [tictactoe §6](../../common/sample/tictactoe/README.ko.md): `OwnerPlayEndpoint`가 **실제 room을 만든 Play endpoint와 같아야** 한다 | **Kotlin만** — `TicTacToeGameCreator.kt:15-21`이 **모든 play endpoint에 대해 round-robin**을 도는데, 그게 **이미 `CreateGameReq`를 받은 Play 서버 위에서** 돈다. ⇒ play-a에서 만든 2번째 방이 **play-b를 owner라고 광고한다.** SMP-JV-23이 owner를 play-a에 고정해 놔서 **지금은 가려져 있다** |
| **SMP-JV-28** (**버그**) | [tictactoe §6](../../common/sample/tictactoe/README.ko.md): client는 API 응답의 `PlayNodes`로 매핑을 확인하므로 **샘플 설정의 내부 naming convention을 알 필요가 없다** | **해결:** `PlayNodeInfo`는 현재 Play와 peer Play의 설정에 있는 endpoint/rid 쌍으로 만들어진다. runner는 `play-node-N`과 무관한 rid를 사용해 이름 규칙을 다시 도입하면 milestone 단언이 실패하도록 한다. |
| **SMP-JV-29** (**버그**) | [tictactoe §17](../../common/sample/tictactoe/README.ko.md): destroy 시퀀스는 **`LeaveGameReq`가 구동**한다 | **해결:** timer는 turn timeout 상태 계산과 알림만 담당한다. actor의 destroy 표시는 terminal 상태의 `LeaveGameReq` handler에서만 설정되며, runner는 timer cleanup helper가 다시 생기면 역할 실행 전에 실패한다. |
| **SMP-JV-30** (**절대 규칙 위반**) | [샘플 규약](../../common/sample/README.ko.md): **TicTacToe만 수동 등록을 사용한다** | **Java 구현 완료, release gate 갱신 차단:** API·Play request handler는 각 channel builder에, session packet handler는 stream node에, actor·subscription handler는 해당 Spot에 직접 등록한다. runner의 package scan 금지 gate와 전체 시나리오가 `PASS TicTacToe.Java`로 통과했다. 그러나 쓰기 범위 밖의 `SampleReleaseGateContractTest:790-827`은 여전히 Java TicTacToe가 package scan과 handler group을 사용하고 `CreateGameHandler.create(...)`를 제공해야 한다고 반대로 단언하므로 release gate 16개 중 1개가 실패한다. testkit 계약을 새 수동 등록 기준으로 갱신하기 전까지 이 gap은 open이다. Kotlin 누락도 별도로 남는다. |
| **SMP-JV-31** (미구현) | [tictactoe §10 step 12](../../common/sample/tictactoe/README.ko.md): inbound observer + `stream-inbound` marker | **Java 해결:** marker source gate가 기존 client에서 실패하는 것을 확인했다. host·guest·observer connector를 만든 직후, `connect` 전에 inbound observer를 등록한다. heartbeat control은 기본 출력에서 제외하고 sample, client 역할, message kind, packet name, request sequence, payload byte length를 기록한다. runner는 세 역할의 marker와 RESPONSE·SEND 양쪽을 확인한 뒤 `PASS TicTacToe.Java`로 통과했다. Kotlin 누락은 별도 gap으로 남는다. |

**Java의 TicTacToe runner가 특정 Play 이름에 게이트를 건다**(`play-b` 포트, `play-node-2`,
`play-a.log`) — 계약이 *"검증 기준은 특정 Play 이름이 아니다"*라고 못 박은 것이다.
**SMP-JV-23을 고치는 순간 그 게이트가 깨진다.**

## 라운드 5 — e2e Config 5·6·7·9·10·11 심층

**"실패할 수 없는 단언"이 이 여섯 config를 관통한다.** 아래는 그중 게이트가 **구조적으로 실패
불가능**한 것만 추렸다.

- [x] **E2E-JV-09** (**가짜 통과**) — RL-B4가 drain된 provider의 신규 request evidence 부재를 확인하지 않는다.
  - 근거: drain 호출을 제거한 fault injection에서 새 gate가 실패하고 `api-a`의 실제 request flow가 기록됐으며, 정상 drain 뒤 고정된 40개 요청이 모두 `api-b`에서 처리되고 `api-a` evidence에는 해당 요청 prefix가 없음을 확인해 `scenario RL-B4 passed`를 얻었다.
- [x] **E2E-JV-10** (**가짜 통과**) — RL-B3가 종료된 provider의 peer row 부재를 확인하지 않는다.
  - 근거: provider를 유지한 fault injection에서 새 부재 gate가 실패했으며, 정상 종료 뒤 `api-b` row 부재와 `api-a` 안정 수렴을 확인해 `scenario RL-B3 passed`를 얻었다.
- [x] **E2E-JV-14** (**가짜 통과**) — RL-D2가 observer 자체 marker를 runtime error sink 보고로 오인한다.
  - 근거: 실제 runtime error event를 요구한 gate가 기존 구현에서 실패했으며, 공개 `ZLinkRuntimeEventHandler<ZLinkRuntimeErrorEvent>` bean으로 `MESSAGE_FLOW_OBSERVER_FAILED/message-flow-observer` evidence를 기록한 뒤 `scenario RL-D2 passed`를 얻었다.
- [x] **E2E-JV-13** (**가짜 통과**) — MON-A2·MON-A3가 scenario 중 location topology와 Spot subject를 바꾸지 않는다.
  - 근거: trigger 없이 payload delta를 요구한 gate가 각각 실패했으며, MON-A2는 `svc-b` 종료·재시작 뒤 `TOPOLOGY_CHANGED`의 감소·복원을, MON-A3는 subscription handler가 있는 Spot 생성 뒤 `SUBJECTS_CHANGED`의 `subjects: 0→1`을 확인해 두 scenario가 통과했다.
- [x] **E2E-JV-11** (**가짜 통과**) — SF-D2가 Redis process를 재기동하지 않고 proxy만 일시 정지한다.
  - 근거: proxy pause 뒤 base container process가 계속 실행 중임을 새 gate가 잡았으며, 고정 host port의 Redis container를 실제 stop/start한 뒤 PING readiness, `api-a` row 재관측, `api-b` 제외, survivor-only 요청을 확인해 `scenario SF-D2 passed`를 얻었다. 공통 proxy 경로 회귀로 `SF-D1`도 다시 통과했다.
- [x] **E2E-JV-12** (**가짜 통과**) — SF-E1이 앱에서 만든 비동기 타이머만 재고 실제 Redis I/O 지연을 검증하지 않는다.
  - 근거: 실제 Redis 지연 marker를 요구한 gate가 기존 앱 데코레이터에서 실패했다. 데코레이터를 제거하고 TCP proxy가 peer 목록을 읽는 Redis 응답을 1.2초 늦추도록 바꾼 뒤 marker, 지연된 store read, 동시 메시징 p99 예산을 모두 확인해 `scenario SF-E1 passed`를 얻었다. 공통 proxy 경로 회귀로 `SF-D1`과 `SF-D2`도 다시 통과했다.
- [x] **E2E-JV-18** (**가짜 통과**) — 서로 다른 JVM의 `System.nanoTime()`을 합쳐 remote transfer 순서를 정렬한다.
  - 근거: runner에 프로세스마다 따로 측정한 timestamp 사용 금지 gate를 추가하자 기존 코드가 즉시 실패했다. timestamp 필드를 제거하고 source·target evidence의 삽입 순서를 역할별로 확인하며, target join 완료 뒤 돌아오는 `commit_ack`를 역할 사이 인과 경계로 사용하도록 바꿨다. ST-B1·ST-B4와 local join 회귀 ST-A1이 통과했다.

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-KT-05** (**빈 시나리오**) | [config-5 RL-D1](../../common/e2e/config-5-resilience-lifecycle.ko.md): **많은 subscriber/consumer로 높은 fanout 부하**를 주고 누락·붕괴 없이 처리되는지 본다 | Kotlin `RlD1HighFanoutScenario.kt` **파일 전체**가 이것이다 — `fun ClientScenarioContext.runHighFanoutEvidenceScenario() { println("scenario RL-D1 passed") }`. runner가 그 줄을 grep한다. **검증 코드가 0줄이다.** feature-map은 "high fanout burst에서 정상 reply를 유지하는지 확인한다"고 적는다 |
| **E2E-JV-09** (**가짜 통과**) | [config-5 RL-B4(**P0**)](../../common/e2e/config-5-resilience-lifecycle.ko.md): drain 후 신규 request가 **그 노드 evidence에 더 기록되지 않고** 다른 노드가 받는다 | **해결:** drain 이후 고정된 40개 요청의 모든 응답이 `api-b`인지 확인하고, 같은 요청 prefix가 `api-a`의 `/evidence`에 한 건도 없는지 직접 검증한다. |
| **E2E-JV-10** (**가짜 통과**) | [config-5 RL-B3](../../common/e2e/config-5-resilience-lifecycle.ko.md): 종료 후 provider의 peer row가 store에서 **제거된다** | **해결:** `waitForTopologyWithout("api-b", 30)`으로 row 부재를 직접 확인한 뒤, 연결 reconcile 중 일시 실패를 허용하면서 최종 성공 구간에는 `api-a`만 나타나는지 검증한다. |
| **E2E-JV-11** (**가짜 통과**) | [config-6 §3](../../common/e2e/config-6-store-failure-recovery.ko.md): harness가 Redis process를 **정지했다가 재기동**한다. SF-D2는 복구 후 각 노드가 **자기 row를 다시 upsert**하는지 본다 | **해결:** SF-D2는 고정 host port로 만든 전용 Redis container를 실제 stop/start하고, container가 중단됐는지와 restart 후 Redis PING readiness를 확인한다. 기존 proxy는 endpoint만 유지하며, 복구 뒤 live row 재관측과 dead row 제외를 검증한다. |
| **E2E-JV-12** (**가짜 통과**) | [config-6 SF-E1](../../common/e2e/config-6-store-failure-recovery.ko.md): store client가 스레드나 이벤트 루프를 **점유하지 않음을 실측으로 증명**한다 | **해결:** 앱의 지연 데코레이터를 제거했다. SF-E1은 consumer 앞의 TCP proxy에서 실제 `SMEMBERS <prefix>:keys:peer` 응답을 1.2초 늦추고 그 marker를 확인한다. 같은 시간에 실행한 메시징의 p99가 baseline 기반 예산 안에 있는지 검증한다. provider는 base Redis에 직접 연결해 지연 대상과 무관한 lease 만료를 피한다. |
| **E2E-JV-13** (**가짜 통과**) | [config-7 MON-A2·MON-A3(**P0**)](../../common/e2e/config-7-monitoring.ko.md): **노드를 추가/종료**하고 **spot subject를 바꾼다** | **해결:** MON-A2가 `svc-b`를 실제 종료·재시작하고 scenario 시작 이후 topology 수의 감소·복원을 확인한다. MON-A3는 새 subscription topic을 가진 Spot을 생성하고, 시작 이후 `SUBJECTS_CHANGED` payload의 subject 수 증가를 직접 확인한다. |
| **E2E-JV-14** (**가짜 통과**) | [config-5 RL-D2](../../common/e2e/config-5-resilience-lifecycle.ko.md): observer 예외는 **runtime error sink로 보고된다** | **해결:** provider가 공개 runtime-error event handler로 `MESSAGE_FLOW_OBSERVER_FAILED`와 callback 이름을 기록하고, consumer는 그 sink evidence와 후속 request 성공을 검증한다. observer가 예외 직전에 쓰던 자체 marker는 제거했다. |
| **E2E-JV-15** (**가짜 통과**) | [config-9 TA-B1(**P0**)](../../common/e2e/config-9-to-actor-messaging.ko.md): 실패 분류는 **framework가 낸 public error kind**여야 한다 | **재검증 중단:** TA-B1을 public `ActorRef` direct call로 바꾸면 nonzero generation은 Java runtime이 `ACTOR_LOCATION_STALE`로 분류하고, generation 0은 `ZlinkSubmitException`을 그대로 노출한다. 앱에서 `ACTOR_ROUTE_NOT_FOUND`를 합성하면 같은 가짜 통과가 되므로 구현하지 않았다. **선택지:** (1) 허용 범위를 `zlink-framework-core`까지 넓혀 backend `NOT_FOUND`의 direct-ref 분류를 공통 계약에 맞게 고친 뒤 E2E를 전환한다. (2) 현재 범위를 유지하고 이 항목을 open으로 남긴다. |
| **E2E-JV-16** (**미구현**) | [config-9 Track A(**P0 4개**)](../../common/e2e/config-9-to-actor-messaging.ko.md): **bind 상태 매트릭스**가 이 config의 표제다 | **해결:** session gateway 역할이 없으면 runner가 즉시 실패하는 gate를 먼저 추가해 기존 코드에서 실패를 확인했다. 독립된 Spot·stream endpoint를 사용하는 gateway 두 개와 실제 stream connector를 추가했다. TA-A1은 같은 bind에서 Before/After push와 외부 send/request를, TA-A2는 bind 부재와 push 실패를, TA-A3는 bind 전 send/request·push 실패와 session-b late bind 뒤 성공을 검증한다. TA-A4는 공개 `boundSession().disconnect()`로 정상 unbind한 뒤 actor 생존과 send/request 성공을 확인하고, 명시적 destroy 뒤 같은 actor 호출이 `ACTOR_ROUTE_NOT_FOUND`이며 handler에 도달하지 않는지 확인한다. TA-A1~A4와 TA-B2·B3 회귀가 각각 통과했다. |
| **E2E-JV-17** (**가짜 통과**) | [config-10 §5](../../common/e2e/config-10-spot-actor-transfer.ko.md): callback order는 **단순 로그 문자열 grep이 아니라** 역할 server evidence와 flow correlation id로 검증한다 | **부분 해결·재검증 중단:** actor 노드는 공개 message-flow observer가 받은 typed event를 evidence로 저장하고, transfer id·event correlation id·flow id를 함께 남긴다. ST-A1은 공개 actor 조회로 location이 보이는 것을 기록한 뒤에만 성공 응답 marker를 남긴다. ST-B1·B4는 `commit_request`·`location_committed`·`source_cleanup`과 application `commit_ack`가 같은 transfer id인지 확인한다. runner의 flow 로그 grep은 제거했다. 그러나 정상 ST-F1·F2의 late backlog 경로는 source `handoff_backlog`만 발행하고 target `backlog_enqueued`를 발행하지 않는다. moving 초기에 packet을 넣어 commit backlog 경로를 강제로 열면 target marker는 나오지만 replay가 `actor was destroyed while moving`으로 실패한다. 이 결함은 Java runtime의 handoff replay 경로에 있으며 현재 허용 범위 밖이다. **선택지:** (1) 허용 범위를 `zlink-framework-core`까지 넓혀 committed/late backlog 모두 target enqueue marker를 남기고 moving actor를 파괴하지 않도록 고친 뒤 ST-F1·F2의 arrival index와 publish 전 적재 순서를 gate로 고정한다. (2) 현재 범위를 유지하고 이 항목을 open으로 남긴다. |
| **E2E-JV-18** (**가짜 통과**) | [config-10](../../common/e2e/config-10-spot-actor-transfer.ko.md) | **해결:** evidence에서 프로세스마다 따로 측정한 timestamp를 제거했다. remote transfer는 source와 target의 삽입 순서를 각각 확인하고, target join이 완료된 뒤 source에 기록되는 `commit_ack`로 역할 사이 인과 경계를 확인한다. runner는 `observedAtNanos`가 다시 들어오면 실행 전에 실패한다. |
| **E2E-JV-19** (**가짜 통과**) | [config-11 OBS-A2(**P0**)](../../common/e2e/config-11-observability-ops.ko.md): **dispatch error 라인**에 `flow=`가 있어야 한다 | **재검증 중단:** 기존 성공 artifact의 `session-flow.log`에는 같은 flow의 `RECEIVED`만 있고, Java runtime의 `ZLinkStreamSessionContextState.completeDispatchError()`는 error reply만 보내며 `ERROR` flow event를 발행하지 않는다. extractor만 server line으로 바꾸면 증거가 없어 실패하고, E2E 앱이 error line을 합성하면 계약을 검증하지 못한다. **선택지:** (1) 허용 범위를 `zlink-framework-core`까지 넓혀 server dispatch error event에 request flow를 보존한 뒤 extractor를 server log로 전환한다. (2) 현재 범위를 유지하고 이 항목을 open으로 남긴다. |
| **E2E-KT-06** (**가짜 통과**) | [config-11](../../common/e2e/config-11-observability-ops.ko.md) | **Kotlin config-11이 Java의 AutomaticTurnDispatch 바이너리를 역할 서버로 통째로 돌린다.** ⇒ **Kotlin 호스트가 하나도 안 뜬다.** Kotlin 고유의 metric·drain·flow 결함은 **원리적으로 안 보인다.** feature-map은 13행 전부 PASS |

**공통:** 여섯 config 어디에도 `Client/Scenarios/`가 없거나 12줄 위임 껍데기다(본문은 532~959줄
god-context). `§2.6` 환경변수 0개 규칙이 **전면 위반**인데 **feature-map 6개 중 기록한 곳이 0개**다.
Java의 `ResilienceLifecycle` Consumer와 `RuntimeMonitoring` Trigger는 **README가 이름을 찍어 금지한
시나리오 driver 서버**다 — **Kotlin은 둘 다 고쳤다.**

**`AutomaticTurnDispatch`(Java·Kotlin)가 config-8인데 `ATD-*` 네임스페이스를 쓴다** — 계약의 ID는
`TD-A1…TD-G1`이다. 두 feature-map이 **존재하지 않는 파일**(`config-8-automatic-turn-dispatch.ko.md`)을
인용한다. (Java `e2e/YieldDispatch`는 **git 추적 파일 0건** — 리포에 없는 로컬 잔해이므로 작업 대상이 아니다. E2E-JV-08 참조)

## 라운드 6 — E2E 전 config 구성 축·Config 1 심층

- [x] **E2E-JV-22** (**가짜 통과**) — RM-C2·RM-C4가 실패 종류와 느린 handler의 최종 완료를 검증하지 않는다.
  - 근거: endpoint가 기존처럼 `failed`만 반환하도록 되돌린 오류 주입에서 RM-C2가 `expected public TimeoutException, got null`로 실패했다. 실제 예외를 공통 오류 변환 코드로 해석한 뒤 RM-C2와 RM-C4가 Java의 공개 timeout 표현인 `TimeoutException`을 확인한다. RM-C4는 timeout 뒤 정상 요청 두 건과 `ProfileReq`의 `value=slow` 완료 evidence까지 확인하며, 두 scenario와 공유 응답 계약을 쓰는 RM-C5가 통과했다.
- [x] **E2E-JV-24** (미구현) — stream 응답에 포함된 actor ref의 generation을 버리고 양수인지 확인하지 않는다.
  - 근거: 기존 누락 상태를 `generation=0`으로 유지한 채 새 단언을 실행하자 SM-B2가 `remote auth actor generation was not concrete`로 실패했다. `bound.ref().generation()`을 `ActorAuthRes`에 보존하고 수신 측에서 양수인지 확인한 뒤 remote actor session인 SM-B2와 local actor push chain인 SM-D15가 통과했다.

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-JV-20** (미구현) | [E2E README:499-512](../../common/e2e/README.ko.md): config runner는 서버 기동 순서를 받아야 하고, 기본 외에 **reverse 1회 + 고정 seed shuffle 1회**를 최소 실행한다 | `e2e/run_e2e_all.sh:55-59,82-85`는 모든 config를 selector `all`로 **한 번씩만** 실행하며 `E2E_START_ORDER`를 설정하지 않는다. 개별 runner 중 이 입력을 읽는 것은 `SpotService/run_e2e.sh:18-20`과 `ToActorMessaging/run_e2e.sh:15-17`뿐이고, 통합 게이트가 reverse/shuffle을 호출하는 곳은 0건이다. ⇒ 순서 축을 구현한 두 config조차 기본 게이트에서는 forward만 돌고, 나머지는 축 자체가 없다 |
| **E2E-JV-21** (미구현) | [E2E README:487-497,546-547](../../common/e2e/README.ko.md): Config 2·9의 P0는 **route mesh 없음 × session/spot 분리 배치** 조합을 실행한다 | **해결:** Config 2의 기존 SM-F6에서 multi-node만 RouteMesh를 끄고 gateway는 계속 등록하는 결함을 재현했다. 세 역할의 비활성 marker를 요구하는 gate가 기존 코드에서 `SM-F6 gateway still registers RouteMesh`로 실패했다. gateway도 같은 spot-only 구성 입력을 받아 RouteMesh 등록을 생략하고, runner는 gateway와 multi-node 두 개의 marker를 모두 확인한다. SM-F6의 원격 spot request/send와 actor join이 통과했고 RouteMesh 구성 회귀 SM-F3도 통과했다. Config 9는 actor owner와 session gateway 두 개를 별도 프로세스로 실행하며 RouteMesh 없이 SpotMesh와 location store만 등록한다. 실제 stream bind를 사용하는 TA-A1~A4가 이 분리 구성에서 통과했다. |
| **E2E-JV-22** (**가짜 통과**) | [config-1 RM-C2:174-182](../../common/e2e/config-1-location-messaging.ko.md)는 미존재 rid가 **public error**로 실패해야 한다. [RM-C4:194-202](../../common/e2e/config-1-location-messaging.ko.md)는 첫 실패가 **timeout**이고 느린 handler가 결국 완료됐는지까지 본다 | **해결:** 두 endpoint는 완료 예외를 벗긴 뒤 framework 오류면 `ZLinkFrameworkErrorKind`, 그 밖의 공개 경계 오류면 예외 타입 이름을 응답한다. RM-C2와 RM-C4는 실제 `TimeoutException`을 확인한다. RM-C4는 후속 정상 reply뿐 아니라 느린 handler의 `ProfileReq` 완료 evidence도 직접 기다린다. |
| **E2E-JV-23** (미구현) | [config-1 RM-C8:228-238](../../common/e2e/config-1-location-messaging.ko.md): `MaxMessageSize` 근접 payload는 왕복하고 **상한 초과 payload는 public error로 거부**된 뒤 정상 request가 동작해야 한다 | **재검증 중단:** Java의 정식 interface spec과 실제 `ZLinkSocketRuntimeOptions`는 `weight()`만 제공하며 `MaxMessageSize` 설정이 없다. binding에는 socket option이 있지만 framework가 만든 live channel socket에 적용할 공개 경로가 없다. E2E 앱이 payload 길이만 보고 임의로 거부하면 framework 상한을 검증하지 못하므로 구현하지 않았다. **선택지:** (1) `MaxMessageSize`를 framework 공통 계약으로 받아들일지 먼저 리뷰하고, 받아들이면 공통 spec·Java interface spec·`zlink-framework-core`까지 범위를 넓혀 공개 설정을 구현한 뒤 RM-C8을 확장한다. (2) 현재 범위를 유지하고 feature-map에 적힌 runtime gap과 이 항목을 open으로 남긴다. |
| **E2E-JV-24** (미구현) | [E2E README:514-519](../../common/e2e/README.ko.md): 표면을 넘은 actor ref는 node rid가 비어 있지 않고 **`generation > 0`**인지 어서션한다 | **해결:** `ActorAuthRes`가 `bound.ref().generation()`을 함께 전달한다. local·remote stream 경계에서 수신한 generation이 양수인지 직접 확인하므로 필드 누락이나 기본값 0으로의 퇴행을 잡는다. |

## connector 공통 test helper 표면 ([32 §10.2](../stream-connector/32-stream-connector.ko.md))

**계약이 확정됐다**(spec §10.2 + connector 언어별 문서 03 §…). connector가 push 관측
표면(`expectNone`·`waitForSequence`)과 범용 단언 유틸(`ensure`·`expectFailure`·`expectTimeout`)을
공개 API로 제공한다.

**이 검증들은 각 언어가 이미 지역 helper로 손수 구현해 관련 갭을 닫아 둔 상태다**(그래서 아래 참조
SMP 항목들이 이미 `[x]`다). 이 작업은 **그 지역 helper를 connector의 공통 표면으로 끌어올려** 다섯
언어가 같은 API를 쓰게 하고, 앞으로 시나리오가 다시 손수 재구현하지 않게 한다. 교차 언어 순서
검증 항목 [SMP-X3](../90-implementation-gap.ko.md)의 "공통 게이트"가 바로 이 `waitForSequence`다.

- [x] **TH-JV-01** (미구현) — connector에 `expectNone`·`waitForSequence`와 `ZLinkStreamAssert`(`ensure`/`expectFailure`/`expectTimeout`)를 [03 §7.1](../stream-connector/languages/java/03-stream-connector.ko.md)대로 구현했다. 관측·순서·오류 분류를 connector 내부에 모으고, timeout·동시 callback·payload 소유권을 한 모듈에서 처리한다. `String`과 `Class<?>` 진입점, action 실행, 오류 종류 확인, timeout 외 오류 재전파를 재검토했고 `ZLinkStreamTestHelperTest`와 connector 전체 테스트가 통과했다. 구현 커밋 `22484d93e`(2026-07-15).
- [x] **TH-JV-02** (리팩토링) — DeliveryDispatch의 지역 `assertStatusOrder`와 독립 `waitFor` 목록을 connector `waitForSequence`로 교체하고 재배정 흐름의 `PickedUp` 단언을 복구했다. 추가 재검토에서는 AUTO dispatch에서 항상 참이 될 수 있던 SpotService의 `receivedCount(...) == 0`을 동작 전에 등록하는 `expectNone(...).within(...)`으로 교체했다. Java DeliveryDispatch 전체 self-check, SpotService `SM-D6`, runner 재도입 방지 검사 통과. 구현 커밋 `22484d93e`, 추가 재검토 커밋 `9b5a8527e`(2026-07-15).
