# Framework 문서 정식화 + 기능 동등성(parity) 진행 계획

> 이 문서는 `framework/` 전체 문서를 **코드 기준으로 최신화**하고 **정식 문서**로
> 개편하며, 동시에 `dotnet`을 기준으로 `java`/`kotlin`/`node` framework가 **동일한
> 기능**을 제공하도록 맞추는 작업의 순서와 완료 기준을 정의한다. 기능 갭이 있으면
> 문서만 맞추지 않고 **codex 에이전트로 구현 작업까지** 진행한다(사용자 지시).

작성일: 2026-06-16

---

## 0. 목표

1. **공통 문서**(`framework/doc/`)를 정식 문서로 개편하고 `plan/`·`spec/draft/`의
   임시 문서를 정리한다.
2. **언어별 문서**(`cpp`, `dotnet`, `java`, `node`)를 정식 문서로 개편한다.
3. `java`·`node` 문서는 **`dotnet` 문서의 스타일·형태를 그대로** 가져가고, 차이는
   **언어별 특징만** 기술한다(개념·의미론은 공통 스펙/ dotnet을 따른다).
4. 모든 문서를 **코드 기준**으로 최신화한다. 문서와 코드가 어긋나면 코드가 기준이다.
5. `dotnet`을 기준 기능 집합으로 두고 `java`/`kotlin`/`node`가 **동일한 기능**을
   제공하는지 갭 분석한다. 부족하면 **codex 에이전트로 기능 구현**을 진행한 뒤
   문서를 맞춘다.

## 1. 기준선과 원칙

- **기준 언어**: `dotnet`. 구조·기능·사용성·샘플의 기준이다.
- **진실 원천**: 기능의 최종 기준은 **코드**다. 문서는 코드에 맞춘다(역방향 금지).
- **kotlin**: 별도 런타임이 아니라 `java` 런타임을 공유한다(`zlink-framework-kotlin`
  모듈). kotlin 문서는 `java` 문서 묶음 안에서 coroutine wrapper 등 kotlin 특징만
  덧붙인다. 별도 doc 트리를 새로 만들지 않는다.
- **cpp**: 이미 정식 구조이며 `dotnet`과 다른 구성(DI container, configuration,
  http-hosting)이 **정당한 언어 차이**다. cpp는 dotnet 형태로 강제 재배치하지 않고,
  임시 문서 정리와 코드 기준 최신화만 한다. 기능 parity 대상에서 cpp는 제외한다
  (parity 대상 = dotnet/java/kotlin/node).
- **언어 차이 vs 기능 갭 구분**(기존 정책 유지): 리플렉션/attribute/coroutine 같은
  언어 고유 표현 차이는 그대로 두고, **진짜 능력 갭만** 코드 수정 트랙으로 보낸다.
- **공통 샘플 정본(확정)**: 샘플의 진실 원천은 **`doc/spec/sample/`** 의 언어 중립
  시나리오 스펙이다. `dotnet`은 가장 완성도 높은 참조 구현이지만 기준 문서는 아니다.
  정본 시나리오 6종 —
  `Bingo`, `TicTacToe`, `SupportChat`, `DeliveryDispatch`, `ShoppingMallCheckout`,
  `GameQuest` — 은 **모든 framework 언어(dotnet/java/kotlin/node/cpp)가 동일하게**
  제공한다. 각 언어 구현은 spec이 정한 서버 역할 분리, request/response/notify 이름,
  상태 필드, smoke 검증 순서를 동일하게 따르고, 문법·API 모양만 언어 관용에 맞춘다.
- **샘플 전량 이식(확정)**: 위 6종을 각 언어가 **코드 + `guide/samples/` 문서**로 모두
  갖춘다. 링크/참조 대체는 하지 않는다. 현재 격차: `node`/`cpp`는 `Bingo`·`TicTacToe`
  2종만, `java`도 일부만 구현됨. 나머지는 codex 에이전트로 구현한다.
- **agent 사용 정책**: 조사는 read-only 에이전트, **parity 구현은 codex 에이전트**
  (사용자 명시 지시). 문서 편집은 메인 루프에서 직접 한다. **완료 후 작업이 제대로
  되었는지 검증하는 최종 리뷰도 codex 에이전트로 수행한다**(사용자 명시 지시).
- **회귀 테스트 동기화(필수 제약)**: 문서 파일을 추가/이동/삭제하면 아래 테스트를
  같은 PR에서 함께 갱신해야 빌드가 깨지지 않는다.
  - `languages/dotnet/tests/Zlink.Framework.UnitTests/Documentation/Regression.cs`
    — `DotNetDraftDocuments`(spec/internals/samples 전체 목록), `GuideNarrativeDocuments`
    (01–12), `GuideCaseStudyDocuments`(13–18)를 하드코딩한다.
  - `languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_layout_contract.cpp`
    — `doc/internals/cpp-framework-implementation-plan.ko.md`·`cpp-framework-overview.ko.md`의
    "reference document tracking table"이 `spec/`·`internals/`의 각 파일을 **정확히
    한 번** 참조하도록 강제한다.
  - `languages/dotnet/.../Samples/Regression.cs`,
    `languages/cpp/.../test_cpp_framework_sample_parity.cpp`,
    `languages/java/zlink-framework-testkit/.../SampleReleaseGateContractTest.java`
    — 샘플 문서/샘플 코드 parity 게이트.

## 2. 현재 상태 진단

| 영역 | 상태 | 정리 대상 |
|------|------|-----------|
| 공통 `doc/spec/` | 정식(코드 최신화 점검 필요) | `spec/draft/`(6), `spec/archive/`는 참고 보존 |
| 공통 `doc/plan/` | 임시(작업 계획·worklog) | 완료분 archive/삭제, 진행분 유지 |
| `dotnet` | **정식 기준**(guide 01–12 + case-study 13–18 + samples 10 + spec + internals). **단, 문서↔회귀 테스트 불일치 존재**(아래) | 코드 기준 최신화 + 회귀 테스트 목록 재정합 |
| `java` | guide/spec/internals는 정식 형태이나 **장 순서가 dotnet과 다름**(feature-map이 04), `draft/`(37) 대량 잔존, 샘플 3개뿐 | `draft/` 정리, 장 번호 정렬, 샘플 보강 |
| `node` | "구현 기준" 상태(아직 정식 아님), 루트에 `IMPLEMENTATION-PLAN/PROMPT`·`sample-implementation-plan`, `draft/`(12), guide 12=cross-language, `guide/samples/` 없음 | 정식 승격, 루트 plan 정리, 장 정렬, 샘플 신설 |
| `cpp` | 정식(16장 구조, 언어 고유) | `internals/`의 implementation-plan·posd-log 등 임시성 문서 정리(단, 위 contract test 제약 준수) |

## 3. 목표 구조

### 3.1 공통 `framework/doc/`
```
doc/
  README.ko.md            # 진입점(언어별 상태 표 포함)
  spec/                   # 언어 중립 정식 계약
    (overview, interaction-model, message-model, channel-topology,
     framework-api, actor-model, session-actor-dispatch, sample/, use-cases/)
    archive/              # 제거 이력 보존(참고용)
  plan/                   # 활성 작업 계획만(이 문서 포함). 완료분은 archive.
```
- `spec/draft/`는 확정분을 `spec/` 본문에 흡수하고 폐기하거나, 미확정분만 남긴다.

### 3.2 언어별(`dotnet` 형태 = java/node 목표)
```
languages/<lang>/doc/
  README.ko.md
  guide/
    01-overview, 02-getting-started, 03-concepts,
    04-channel-messaging, 05-spot, 06-actor-session, 07-stream,
    08-registry, 09-monitoring, 10-feature-map,
    11-interface-catalog, 12-grpc-alternative
    case-studies/   # 13–18 (도입 판단·아키텍처 매핑)
    samples/        # 실행 가능한 등록·handler·client 코드 모음
  spec/             # 그 언어의 공개 계약
  internals/        # 구현·검증 기준(회귀 매트릭스 포함)
```
- `cpp`는 위와 별개로 자기 16장 구조 유지(언어 고유 장 보존), `draft` 없음.

### 3.3 용어 정리 — "공통 샘플" vs "case-study"
혼동을 막기 위해 둘을 명확히 구분한다.
- **공통 샘플 = `doc/spec/sample/`** : 실행 가능한 정본 시나리오 6종(Bingo,
  TicTacToe, SupportChat, DeliveryDispatch, ShoppingMallCheckout, GameQuest).
  **모든 언어가 동일하게 코드+문서로 구현**(1절 "샘플 전량 이식" 확정 사항).
- **case-study = dotnet `guide/case-studies/` 13–18** : 실행 샘플이 아니라
  도입판단·아키텍처 분석 **산문**(ecommerce-checkout, microservice-mesh,
  realtime-game, ride-hailing, chat 계열, trading 등). 별개 항목이다.

### 3.4 java/node에서의 "언어별 특징만 기술" 적용
- 장 구성·순서·역할·문체는 dotnet과 동일하게 맞춘다.
- 본문은 개념 재정의 없이 **언어 표면**(Spring DI/annotation, NestJS decorator,
  `Promise`/`CompletionStage`/coroutine)만 dotnet 대비 차이로 기술한다.
- **case-study(13–18) 처리(확정 — 전량 복제)**: java/kotlin·node guide 에도 13–18
  케이스 스터디 9개를 **각 언어 문맥으로 전량 복제**한다(dotnet 형태·번호·역할 동일,
  코드 조각만 해당 언어로). 링크 대체는 하지 않는다. cpp 는 자기 형태를 유지하므로 이
  복제 대상에서 제외한다.

## 4. 작업 트랙

### Track A — 공통 문서 정식화 + 임시 정리
1. `doc/spec/*` 를 코드 기준으로 점검·최신화.
2. `doc/spec/draft/`(6) 확정분 흡수/미확정분 분리, README 갱신.
3. `doc/plan/` 완료 계획·worklog를 `archive/`로 이동 또는 삭제, 활성 계획만 유지.
4. `doc/README.ko.md` 언어 상태 표·링크 갱신.

### Track B — dotnet 기준선 확정(코드 최신화)
1. dotnet 코드(`languages/dotnet/src`)와 문서 대조, 어긋난 문서 코드 기준 수정.
2. **dotnet 문서↔회귀 테스트 재정합(선행)**:
   `Documentation/Regression.cs`의 `DotNetDraftDocuments`가 실제 파일과 어긋나 있다.
   - 목록에만 있고 디스크에 없는 파일: `actor-gateway-session-relay`,
     `registry-backed-routing-defaults`, `spot-timer-policy`,
     `session-attached-actor-route`, `channel-handler-exposure-and-spot-route-transport`,
     `stream-open-items`.
   - 디스크에만 있고 목록에 없는 샘플: `deliverydispatch-sample`, `gamequest-sample`,
     `shoppingmall-checkout-sample`, `supportchat-sample`, `streaming-client`.
   기준 언어가 그린이 아니면 parity 비교가 무의미하므로, 코드 기준으로 목록을
   바로잡아 dotnet 테스트를 먼저 그린으로 만든다.
3. dotnet의 **정식 기능 집합 목록**(feature-map + interface-catalog)을 parity의
   기준 체크리스트로 확정한다. 이후 트랙의 비교 기준이 된다.

### Track C — java/kotlin 기능 동등성 + 문서
1. **갭 분석**: dotnet 기준 기능 집합 vs java 모듈 실제 구현 비교
   (`zlink-framework-core`, `zlink-framework-spring-boot-starter`,
   `zlink-framework-kotlin` 등 / 입력: `internals/dotnet-to-java-surface-mapping`,
   `guide/04-feature-map`). 언어 차이/진짜 갭 분류.
2. **구현(codex)**: 진짜 기능 갭을 codex 에이전트로 구현 → 빌드·테스트 통과.
3. kotlin coroutine wrapper 등 kotlin 표면 동등성 확인.
4. **샘플 구현(codex)**: `doc/spec/sample/` 정본 6종 중 미구현분을 java/kotlin
   코드로 구현(현재 일부만), spec의 역할·메시지·smoke 순서 준수.
5. **문서**: guide 장 번호를 dotnet과 정렬(현재 feature-map 04 → 10 등), `draft/`(37)
   에서 확정분 흡수 후 폐기, `guide/samples/` 를 정본 6종에 맞춰 보강,
   `guide/case-studies/` 13–18 을 java/kotlin 문맥으로 **전량 복제**(코드 조각만 해당 언어).

### Track D — node 기능 동등성 + 문서
1. **갭 분석**: 입력으로 `internals/node-binding-public-api-gap-list`,
   `internals/dotnet-to-node-surface-mapping` 활용.
2. **구현(codex)**: 진짜 기능 갭 구현 → 빌드·테스트 통과.
3. **샘플 구현(codex)**: 정본 6종 중 미구현분(현재 Bingo·TicTacToe만) node 코드로 구현.
4. **문서**: 루트 `IMPLEMENTATION-PLAN/PROMPT`·`sample-implementation-plan`을
   `plan/`(또는 archive)로 정리, `draft/`(12) 흡수/폐기, guide를 dotnet 01–12로 정렬
   (cross-language는 보조 장 또는 internals로), `guide/samples/` 신설(정본 6종 기준),
   `guide/case-studies/` 13–18 을 node 문맥으로 **전량 복제**(코드 조각만 해당 언어).
5. README를 "구현 기준" → **정식**으로 승격.

### Track E — cpp 문서 정식화 + 임시 정리
1. `internals/`의 implementation-plan·posd-refactoring-log 등 임시성 문서를 코드
   기준으로 최신화하되, **layout contract test가 요구하는 tracking table 구조는 유지**.
2. 완료된 plan/log는 요약만 남기고 정리, spec/guide 코드 대조 최신화.
3. **샘플 구현(codex)**: 정본 6종 중 미구현분(현재 Bingo·TicTacToe만) cpp 코드로 구현,
   `guide/samples/` 문서 보강.

### Track F — 전체 마감
1. 각 언어 README·네비게이션 링크 정합화.
2. **회귀 테스트 동기화**: 1절의 모든 테스트를 새 파일 목록에 맞게 갱신.
3. 전 테스트 그린 확인(dotnet/java/cpp/node 빌드+테스트, 샘플 parity 게이트).
4. **codex 최종 리뷰**: 작업이 제대로 되었는지 codex 에이전트로 검증한다. 점검 범위는
   ① 문서↔코드 일치, ② dotnet 형태 정합(java/kotlin/node), ③ 정본 6종 샘플 동등성,
   ④ dotnet 기준 기능 parity, ⑤ 임시 문서 0개. 지적 사항은 수정 후 재검토하고,
   통과해야 작업 완료로 본다.

## 5. 진행 순서(엄격 순차, 병렬 금지)

**제일 먼저 할 일은 framework 공통 문서(`framework/doc/`) 정리**다(Track A). 공통
문서를 먼저 정리해 기준을 세운 뒤에야 언어 작업으로 넘어간다.

그 다음 **언어 작업은 한 번에 하나씩만** 진행한다. 여러 언어를 동시에 작업하지
않는다. 진행 순서는 **dotnet → (java, kotlin) → node** 다. 각 언어는 앞 언어가
그린으로 끝난 뒤에 시작한다.

전체 순서 요약: **공통 문서 정리 → dotnet → (java, kotlin) → node → cpp → 최종 마감**.

- **dotnet 은 코드 쪽 내용 최신화만 반영한다.** 구조·장 구성·형태는 바꾸지 않고,
  코드와 어긋난 서술/시그니처/기능만 코드 기준으로 고친다. dotnet 이 그 형태의
  **기준 형태**가 된다.
- **java/kotlin, node 는 dotnet 의 형태에 맞춰** 문서를 작성한다(장 구성·순서·역할·
  문체 동일, 본문은 언어 특징만). 기능 갭은 codex 로 구현한 뒤 문서를 맞춘다.
- cpp 는 위 parity 순서에 포함하지 않는다. 자기 형태를 유지하며 별도로 정리한다.

1. **Phase 0 — 합의**: 이 계획 검토. (case-study(13–18) 처리 = **전량 복제** 확정됨.)
2. **Phase 1 — 공통 문서(Track A)**: `doc/spec/*` 코드 기준 점검, `spec/draft/`·`plan/`
   임시 문서 정리 시작, README 갱신.
3. **Phase 2 — dotnet(Track B)**: 코드 내용 최신화 + 문서↔회귀 테스트 재정합 →
   dotnet 그린. 기준 형태/기능 체크리스트 확정. (형태 개편 없음.)
4. **Phase 3 — java/kotlin(Track C)**: 갭 분석 → codex 기능 구현 → 샘플 구현 →
   dotnet 형태로 문서 작성/정렬 → 그린.
5. **Phase 4 — node(Track D)**: 갭 분석 → codex 기능 구현 → 샘플 구현 → dotnet
   형태로 문서 작성/정렬 → README 정식 승격 → 그린.
6. **Phase 5 — cpp(Track E)**: 임시 문서 정리(contract 구조 유지) + 코드 최신화 +
   샘플 보강 → 그린.
7. **Phase 6 — 최종 마감(Track F)**: 전 언어 README·링크 정합화, 회귀 테스트 동기화,
   **모든 draft·plan 임시 문서 제거**(아래 6절·7절), 전 테스트 그린 확인.
8. **Phase 7 — codex 최종 리뷰(Track F-4)**: 작업이 제대로 되었는지 codex 에이전트로
   검증하고, 지적 사항을 수정해 통과시킨 뒤 완료한다.

## 6. 완료 기준(DoD)

- **최종 상태에 임시 문서가 0개다.** 공통·전 언어를 통틀어 `draft/` 디렉토리와
  `plan/`(작업 계획·worklog·IMPLEMENTATION-* 등) 임시 문서가 남지 않는다. 확정 내용은
  정식 문서로 흡수하고, 보존 가치가 있는 이력만 `archive/`로 옮긴다. **이 계획 문서
  자체도 최종 마감 단계에서 제거하거나 `archive/`로 옮긴다.**
- `java`·`node` guide 장 구성·순서·역할이 `dotnet`과 동일하고, 본문은 언어 특징만
  다룬다. `node` README가 정식으로 승격된다.
- `java`/`kotlin`·`node` 에 case-study(13–18) 9개가 **각 언어로 전량 복제**되어 있다.
- `dotnet`/`java`/`kotlin`/`node`가 dotnet 기준 기능 체크리스트를 모두 충족한다
  (진짜 갭 0; 언어 표현 차이는 허용·문서화).
- `doc/spec/sample/` 정본 6종(Bingo, TicTacToe, SupportChat, DeliveryDispatch,
  ShoppingMallCheckout, GameQuest)이 **모든 언어**(dotnet/java/kotlin/node/cpp)에
  코드+문서로 구현되고 smoke 검증을 통과한다.
- 모든 문서가 코드와 일치한다(코드 기준).
- dotnet/java/cpp/node 빌드·테스트·샘플 parity 게이트가 전부 그린이다.
- **codex 에이전트 최종 리뷰를 통과한다**(문서↔코드 일치, dotnet 형태 정합, 샘플 동등성,
  기능 parity, 임시 문서 0개 확인).

## 7. 정리(제거) 대상 목록 — 최종에 0개로

아래는 최종 상태에서 **모두 사라져야 하는** 임시 문서다. 확정 내용은 정식 문서로
흡수하고, 이력만 `archive/`로 옮긴다.

- 공통: `doc/plan/`(12, 이 계획 문서 포함), `doc/spec/draft/`(6).
- java: `languages/java/doc/draft/`(37) — guide/spec/internals 중복분 + 실행계획.
- node: 루트 `IMPLEMENTATION-PLAN.ko.md`, `IMPLEMENTATION-PROMPT.ko.md`,
  `sample-implementation-plan.ko.md` + `doc/draft/`(12).
- cpp: `internals/cpp-framework-implementation-plan.ko.md`,
  `cpp-framework-posd-refactoring-log.ko.md` 등.
  - ⚠️ **주의**: 이 두 cpp 문서는 layout contract test가 직접 참조·강제한다(1절).
    "임시 문서 0개"를 지키려면 (a) 내용을 정식 internals 문서로 재편해 흡수하고
    테스트를 그 새 파일로 갱신하거나, (b) 문서와 해당 테스트 항목을 함께 제거해야
    한다. 문서만 지우면 cpp 빌드가 깨진다.

## 8. 리스크 / 주의

- **회귀 테스트가 파일 목록을 하드코딩**한다(1절). 파일 이동/삭제 시 같은 변경에서
  테스트를 갱신하지 않으면 즉시 빌드 실패.
- **기준 언어 dotnet의 문서↔테스트가 이미 어긋나 있다**(Track B 2단계). 다른 언어를
  맞추기 전에 dotnet을 먼저 그린으로 만들어야 비교 기준이 신뢰 가능하다.
- parity **구현 범위가 갭 분석 결과에 따라 커질 수 있다**. Phase 2 보고 후 사용자와
  범위를 합의한 뒤 Phase 3에 들어간다(무단 대형 구현 금지).
- cpp contract test의 tracking table 제약 때문에 cpp 임시 문서는 단순 삭제가 아니라
  구조 보존 정리가 필요하다.
- 언어 차이를 갭으로 오판하지 않는다(리플렉션/attribute/coroutine 등은 차이 유지).
