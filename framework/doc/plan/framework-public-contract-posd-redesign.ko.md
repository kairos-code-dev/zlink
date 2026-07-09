# Framework public contract POSD 재설계 — 공통 계약 변경안

> **상태: 변경 후보(draft).** 이 문서는 4개 framework(dotnet·java(+kotlin)·node·cpp)의 public
> contract에 공통 적용할 POSD 재설계 변경 목록의 **언어 중립 정본**이다. 계약 상세 설계와 근거는
> 아래 원본 문서에 있으며 여기서 반복하지 않는다. 충돌하면 이 문서의 변경 목록이 언어 간 적용의
> 기준이고, 계약 본문 자체의 정본은 확정 시점에 갱신되는 location 원본 draft다.
>
> 상세 설계 원본:
> - location 계약 재설계: `framework/doc/plan/framework-dotnet-location-contract-posd-redesign-plan.ko.md`
> - contract 전반 POSD 검토: `framework/doc/plan/framework-dotnet-public-contract-posd-review.ko.md`
>
> 계약 본문 draft: [framework-location-resolver-store.ko.md](../framework/common/draft/framework-location-resolver-store.ko.md) ·
> [framework-spot-address-messaging.ko.md](../framework/common/draft/framework-spot-address-messaging.ko.md)
>
> 언어별 진행 문서:
> [java(+kotlin)](framework-public-contract-posd-redesign-java.ko.md) ·
> [node](framework-public-contract-posd-redesign-node.ko.md) ·
> [cpp](framework-public-contract-posd-redesign-cpp.ko.md)
> — dotnet은 위 plan 문서 2개가 이행 문서를 겸한다(레퍼런스 구현).

## 1. 문서 지도와 정본 규칙

```text
이 문서 (언어 중립 변경 목록 L1~L20, A~D)      ← 4언어 적용 기준
  ├─ location 원본 draft                      ← 계약 본문 정본 (P0에서 이 목록대로 갱신)
  ├─ dotnet plan 2개                          ← 상세 설계 + .NET 레퍼런스 이행
  └─ 언어별 진행 문서 (java/node/cpp)          ← 상태 보드 + 언어 매핑, 계약 내용 반복 금지
```

- 1차 location store 이식(porting-{node,java,cpp} 문서)은 **완료된 별개 작업**이다. 이 재설계는 그
  위에 얹는 2차 wave이며, 1차 문서는 완료 기록으로 유지한다.
- 적용 순서: **P0 계약 확정(문서) → dotnet 레퍼런스 그린 → node → java(+kotlin) → cpp**.
  P0 확정 이후 모든 언어의 신규 작업은 구계약이 아니라 이 목록을 따른다.
- CLAUDE.md parity 정책이 그대로 적용된다: 한 언어에만 있는 public API 금지, spec/draft 없는
  public API 선행 구현 금지.

## 2. 계약 필수 vs 언어 idiom

"4언어 동일"의 정의. 아래 왼쪽 열은 언어 간 반드시 같아야 하고, 오른쪽 열은 언어 관례를 따른다.

| 계약 필수 (동일) | 언어 idiom (자유) |
|------------------|-------------------|
| 타입·메서드의 개념 이름과 의미 | 명명 표기(PascalCase/camelCase/snake_case, Async 접미) |
| 실패 분류(오류 kind, retriable 여부)와 발생 조건 | 예외 타입 체계의 표현 |
| enum 멤버와 **명시 숫자 값**, canonical 문자열 | enum의 언어 표현(enum class 등) |
| await 완료 의미, "확인 불가 = false" 같은 의미론 | 비동기 타입(ValueTask/CompletableFuture·suspend/Promise/coroutine·future) |
| 닫힌 합(union)의 구성원 집합 | union 표현(abstract record/sealed interface/discriminated union/std::variant) |
| 금지 사항(void 터미널 부재, silent drop 금지 등) | fluent call의 문법 세부 |
| 표면의 독자 구분(사용자/SPI/운영/internal) | 모듈·파일 배치 |
| lookup 파라미터 집합(actor id 단독 등) | 타입 유도(제너릭) vs name 토큰 — 리플렉션 없는 언어는 name 기반 |
| 등록 API의 의미(무엇을 등록하는가) | attribute/annotation/decorator/명시 등록 |

## 3. 변경 목록 L — location 계약 재설계

각 항목의 상세(시그니처·근거·실패 표)는 dotnet location plan이 정본이다. 여기는 언어 중립 요지와
적용 범위만 적는다.

| ID | 변경 | 요지 |
|----|------|------|
| L1 | owner 정리 단일화 | kind별 remove-by-owner 4개 삭제, 통합 store에 "owner의 모든 row 제거" 하나(가능하면 원자적, 반환=제거 행 수) |
| L2 | 오류 규칙 단일화 | write status에서 store-unavailable 삭제. 예상된 경합(stored/ignored-stale/rejected-conflict)=상태값, 인프라 장애=원인 보존 예외. fail-static은 runtime 정책 |
| L3 | lease 표면 교정 | renew → lease renewal(만료 시각+store 시계), remove → bool. write result 재사용 금지 |
| L4 | actor row 재정의 | actor ref는 typed nullable(publish 전 행은 공개 조회 비노출), 중복 spot-kind 필드 삭제(location kind 단독), spot mesh name 필수, actor type은 nullable 진단·생성 정보 |
| L5 | actor identity | actor id 전역 unique 계약(애플리케이션 의무). key=actor id 단독. ensure/claim은 type 불일치 시 거부(기존 `ActorTypeMismatch` 재사용 — P0 확정, silent wrong-actor 금지). id 단독 lookup의 type 검증 불가 한계 명시 |
| L6 | resolver 재편 | live peer 조회(liveness 조인, 이름에 live 명시) + spot address + actor address 3종만. route resolver는 public 제거(내부화). actor-ref resolver는 만들지 않는다(directory find가 답) |
| L7 | 운영 조회 분리 | runtime query를 별도 표면·모듈로. list 계열은 "location row" 조회임이 이름에 드러나야 하고 **live row 반환(owner liveness 조인 — P0 확정, e2e 의존 의미론)**. stale 관측은 topology(Lost)·summary(Stopped) 담당, 원시 row 진단은 store SPI. 계층 이름 구분은 유지 |
| L8 | watch typed key | 변경 event가 문자열 key 대신 kind별 typed key의 닫힌 합을 나른다. 문자열 인코딩은 backend codec 내부 |
| L9 | canonical 문자열 내부화 | public helper 제거, 언어별 내부 단일 매핑 테이블. 문자열 값의 정본은 공통 store codec 문서 |
| L10 | enum 규약 | 명시 숫자 값 + 상태류는 Invalid=0. location kind에 Invalid 추가 |
| L11 | 계약 모듈 재구성 | 관심사 분리: values/rows/keys/writes/watch/diagnostics + resolvers/runtime-query/stores/options. 파일·모듈 배치는 언어 관례, 관심사 경계는 유지 |
| L12 | actor directory | find(id) / ensure(id, create-request, placement). 실패 계약: 같은 type 기존 actor=정상 반환, `ActorTypeMismatch`(재사용, P0 확정)·`ActorCreateRejected`(신설)=재시도 무의미, route 미연결=retriable, store 장애=예외, 생성 경합=먼저 만든 쪽 ref |
| L13 | actor client — **⏸ 보류(2026-07-04, core 협력 필요)** | send-to-actor / request-to-actor (actor id 단독, 타입 유도는 idiom). actor 전용 call 타입, 터미널은 await 가능 단독(**void submit 금지** — resolve 실패를 삼키는 경로 차단). await 의미="resolve 성공+로컬 인계"(전달 보장 아님). 실패 분류: `ActorRouteNotFound`/`ActorLocationStale`(내부 1회 re-resolve 후)/`RouteNotConnected`(retriable). silent drop·auto-create·메시지 파킹 금지. hot path 표면 아님(고빈도는 resolve-once-hold). **보류 사유는 9절 질문 목록 Q1 — 해결 전까지 4언어 모두 L13 미구현, L18의 "resolve 후 send-to-spot 직접 조합 금지" 조항도 함께 보류** |
| L14 | 수신 계약 | 신규 수신 API 없음 — 기존 spot/entry-spot actor handler 재사용. envelope는 actor-forwarding framing 재사용으로 확정(P0 조사-3: bound-session 경로는 세션 전제라 불가, forwarding 경로는 actor ref+generation+source rid만 검증). 단 서버 송신이 수신측 bound-session route 갱신을 유발하지 않는 변형 필요(잔여 설계 조건). envelope 타입·메타데이터 키는 internal |
| L15 | session bind-or-get | 기존 session actors 표면에 bind-or-get 하나 추가(신설 인터페이스 아님). 기존 find/bind 유지 여부는 P0 결정 |
| L16 | readiness | is-peer-ready(mesh, role, node?) → bool. 확인 불가(store 장애 포함)=false — "장애=예외" 규칙의 명시된 예외. location 도메인 질의라 location 계약에 둔다 |
| L17 | actor ref wire 모델 | framework 제공 snapshot(node rid, actor id, generation)만 사용. 샘플별 DTO·문자열 파싱 금지. protobuf 등 wire 제약은 경계 1곳 변환만 |
| L18 | 표면 금지 규칙 | 샘플·업무 코드에서 store SPI 직접 사용 금지, runtime query는 진단·self-check만, resolve 후 send-to-spot 직접 조합 금지(actor client 사용) |
| L19 | internal 분류 | value codec(닫힌 집합 테이블), row key 문자열 codec(framework 장부용+backend별), actor lifecycle(claim/publish, claim result), actor-addressed envelope — 전부 internal |
| L20 | 문구 계약 | peer key의 identity 규칙(다섯 구성요소 전체, null 포함) 명문화. route row payload는 "framework 내부 payload, application key-value 아님" 명시 |

## 4. 변경 목록 A~D — contract 전반 POSD 수정

ID는 검토 문서(`framework-dotnet-public-contract-posd-review.ko.md`)와 동일하다. "적용" 열:
**공통**=4언어 동일 적용, **idiom**=언어별 대응물 각자, **조사**=언어별 해당 여부를 S0에서 확인.

| ID | 요지 | 적용 |
|----|------|------|
| A1 | spot context의 leave-actor 메서드 명명 교정 | 조사 — .NET은 소문자 결함(`leaveActor`→`LeaveActorAsync`). camelCase가 규약인 언어(java/node)는 표기 자체는 정상이므로 이름·비동기 접미 규약 일치만 확인 |
| A2 | auto-connect type enum 이중 정의 해소(채널판·location판, DealerMesh 결번 drift) | 공통 — 통합 또는 부분집합 관계·결번 이유 명문화. canonical 문자열과 함께 확정 |
| A3 | framework 오류 kind: 명시 값 + kind→retriable 단일 매핑. 신설 전 기존 kind 대조(`ActorTypeMismatch`/`ActorAlreadyExists`/`ActorCreateFailed`/`RequestRejected` 재사용 우선) | 공통 |
| A4 | per-role location store 등록 5종 삭제(all-or-nothing 계약과 모순) — 통합 등록+in-memory만 | 공통 |
| B1 | actor join call 2종(스팟/entry) 완전 중복 해소 | 공통 |
| B2 | accept/reject 결과 타입 2종 중복 해소 | 공통 |
| B3 | spot ↔ entry-spot의 actor 멤버십 콜백·context 멤버 중복 — 선언 공유 기반 검토(사용자 구현 표면 개수는 유지) | 공통(형태는 idiom) |
| B4 | actor 핸들러 등록의 send/request 가짜 구분(둘 다 packet 위임) 해소 | 공통 |
| B5 | request call과 route request call — yield 유무만 다른 사본 해소(C1과 함께) | 공통 |
| B6 | handler invocation의 채널·패킷명 중복 보관 제거 | 공통 |
| C1 | yield 능력이 런타임 예외로만 드러남(3곳) — yield 지원 call의 타입 분리 또는 닫힌 지원 목록 명시 | 공통 |
| C2 | spot manager의 약타입(object) overload 삭제, message/typed 2벌 규약 | 공통(overload 형태는 idiom) |
| C3 | 로컬 actor manager의 type 문자열 규약 — "생성 계열만 type, lookup은 id 단독, type 정의는 factory 등록이 유일" (L5와 정렬) | 공통 |
| C4 | actor join 결과의 거부 시 불완전 상태(비어야 할 ref가 non-null) 표현 불가화 | 공통 |
| C5 | set-only 옵션 속성 → 읽기 가능으로 통일 | 조사 — .NET 특이일 수 있음 |
| C6 | encoded payload의 생성 경로별 복사/aliasing 의미 이중성 제거 — ownership 규칙 명문화 | 공통(표현은 언어 메모리 모델) |
| C7 | send call 터미널·옵션(ct 유무, compress 유무) 표면 간 대조표로 통일 | 공통 |
| C8 | 명명 문법 통일: "동사+To+대상"(route client의 bare send/request 포함), publish 3벌 파라미터 규약, route-mesh 빌더의 Channel 접미 드리프트 | 공통 |
| D1 | 암묵 ordinal public enum 전수 명시 값화(+상태류 Invalid=0). message-flow 계열은 4언어 관측 데이터라 우선 | 공통 |
| D2 | monitoring 모델의 시간 표현(ms ulong vs timestamp)·raw errno 혼재 — monitoring spec 개정으로 분리 | 공통(별도 draft) |
| D3 | handler attribute 17종 규약 통일 | idiom — .NET attribute / java annotation / node decorator·등록 API / cpp 명시 등록, 각자 규약 정리. 이름·의미는 등록 API와 1:1 |

## 5. 닫힌 값 집합 — 값 테이블

enum 명시 값과 canonical 문자열은 언어 간 어긋나면 저장·관측 데이터가 어긋난다. 각 언어는 이 표만
복사한다(자체 판단 금지). 아래 값은 dotnet 현행 계약 코드에서 실측 확정한 것이며(재설계 변경분
반영), "결정 대기" 표기는 P0 조사·결정 후 채운다.

### 5.1 location 계열 (확정)

| enum | 값 | canonical 문자열 |
|------|-----|------------------|
| LocationAutoConnectType | Invalid=0, RouteMesh=1, ClientServer=2, DealerMesh=3, Fanout=4, SpotMesh=5 | route-mesh / client-server / dealer-mesh / fanout / spot-mesh |
| LocationRole | Invalid=0, Spot=2, Router=3, Dealer=4, Pub=5, Sub=6 — **확정: uint16 폭 유지(core wire `uint16_t service_role` 정합), 값 1은 제거된 gateway role의 예약 결번, 숫자 값은 core wire·Redis row JSON에 직렬화되므로 불변(P0 조사-1)** | spot / router / dealer / pub / sub |
| RouteKind | Invalid=0, ActorSession=1, SpotName=2, FrameworkRoute=3 | — |
| LocationKind | **Invalid=0(L10 신설)**, Peer=1, Spot=2, Actor=3, Route=4 | — |
| WriteIntent | NewClaim=1, Renew=2, Takeover=3 | — |
| WriteStatus | Stored=1, IgnoredStale=2, RejectedConflict=3 — **StoreUnavailable 삭제(L2)** | — |
| LocationChangeType | Upserted=1, Removed=2, Expired=3 | — |
| LocationTopologyState | Discovered=1, Connecting=2, Ready=3, Lost=4, Error=5, Stopped=6 | — |
| SpotKind | Invalid=0, Entry=1, User=2 | — |

canonical 문자열의 정본은 공통 store codec 문서이며 이 표는 그 참조다.

### 5.2 결정 대기 (P0 조사 후 확정)

- **framework error kind** — **확정(P0 조사-4)**: 이름 집합 = 기존 20종 + 신설 2종
  (`ActorLocationStale`, `ActorCreateRejected`). `ActorIdConflict`는 신설하지 않고 기존
  `ActorTypeMismatch` 재사용(throw 지점 의미 일치). 명시 값 = dotnet 현행 순서 0~19 고정 + 신설
  20·21 — 숫자는 wire·저장에 노출되지 않음을 확인했고(node는 string enum), 오류 kind는 값 0이
  유효 멤버여도 되는 "Invalid=0 규약의 명시된 예외"다. kind→retriable 단일 매핑 테이블로 통일
  (실측 불일치 4건: `ActorSessionNotBound`/`RequestRejected`/`RequestFailed`/`SpotRouteNotFound`
  해소). **잔여 판정 항목**: cpp `framework_error_kind_t`의 추가 5종(`actor_stale_generation` —
  owner-lease stale과 다른 개념, `timeout`/`shutdown`/`disconnected`/`closed`)은 한 언어 전용
  public 값이므로 공통 승격 또는 정리 여부를 draft에서 판정. java는 kind enum 자체가 없어 S1에서
  신설.
- **channels AutoConnectType** — A2: location판과의 통합 형태(현행 값 0,1,2,4,5 — DealerMesh=3
  결번 drift).
- **dispatch·message-flow 계열** — D1: 현행 순서 기준 명시 값 부여(값 변경 없이 고정만).
- **spot monitoring enum**(SpotNodeState 1~5 등) — D1: 현행 값 유지, 명시 값만 부여.

## 6. parity 강제 장치 (후속 항목)

문서 약속만으로 drift를 막을 수 없으므로, 계약 확정 후 다음을 별도 항목으로 진행한다.

1. **contract 형태 고정 테스트** — 각 언어 contract test에 public 표면의 멤버 목록·시그니처를
   고정하는 테스트(dotnet DocumentationRegressionTests 방식). 계약 변경 시 4언어 테스트가 함께
   깨져야 정상.
2. **계약 인벤토리 CI 체커** — 이 문서의 변경 목록·값 테이블을 기계 판독 가능한 인벤토리로 두고,
   각 언어 계약 심볼을 추출·대조하는 스크립트(`check_doc_tabs.py`와 같은 계열). 한 언어에만 있는
   public API를 머지 전에 잡는다.

## 7. 작업 단계

- [x] ✅2026-07-04 완료(조사 8건+결정 기록, 원본 draft §0 반영, 값 테이블 5절 확정 — 단 L13은 9절 Q1 보류) **P0. 계약 확정** — 이 문서의 L·A~D 목록을 승인 상태로 올리고, location 원본 draft
      (`framework-location-resolver-store.ko.md`)와 spot-address draft에 L 항목을 반영한다.
      5절 값 테이블을 채운다. dotnet plan 2개의 P0 결정 항목(오류 kind 대조, envelope 규칙,
      actor id 충돌, liveness 문구 등)이 여기서 함께 닫힌다.
- [x] **P1. dotnet 레퍼런스 구현** ✅ 2026-07-04 (P0~P7 완료, 배터리 4종+e2e 8/8 그린, 리뷰 게이트 2축 "이슈 없음" — Q1 보류 3건 제외) — dotnet plan 2개의 단계(P1~P7)를 수행하고 그린 확인.
      **e2e·샘플 전환 포함** — 계약 파일만 바꾸고 소비자를 남기면 완료가 아니다.
- [x] **P2. node 적용** ✅ 2026-07-04 (S0~S6+G 완료, 전체 테스트 그린, 리뷰 2축 "이슈 없음") — [진행 문서](framework-public-contract-posd-redesign-node.ko.md). e2e·샘플 포함.
- [x] **P3. java(+kotlin) 적용** ✅ 2026-07-04 (S0~S6+G 완료, 4모듈+e2e 8+샘플 그린, 리뷰 2축 통과) — [진행 문서](framework-public-contract-posd-redesign-java.ko.md). e2e·샘플 포함.
- [x] **P4. cpp 적용** ✅ 2026-07-04 (S0~S6+G 완료, 빌드+ctest 42/42 그린, 리뷰 2축 통과) — [진행 문서](framework-public-contract-posd-redesign-cpp.ko.md). e2e·샘플 포함.
- [x] **P5. parity 강제 장치** ✅ 2026-07-04 — ①형태 고정 테스트=4언어 S6에서 확보 ②계약 인벤토리 CI 체커(framework/doc/contract-inventory/framework-public-contract-inventory.json + doc/site/scripts/check_framework_contract_inventory.py, docs.yml 연결, 실측 통과: types=52 methods=34 enums=9).
- [x] **P6. 문서 승격** ✅ 2026-07-04 (location resolver/store 계약 spec 승격+draft 상태 갱신+porting draft 3종 완료 반영 — L13은 CORE 트랙 완료까지 draft 유지) — 구현된 계약만 공통 spec/guide로 승격, draft 상태 해제. 언어별 문서의
      상태 보드 완료 확인.

**적용 범위 규칙**: 모든 변경 항목(L1~L20, A1~D3)의 완료 조건은 계약 파일 수정이 아니라 **소비자
이행 완료**다 — framework 구현, e2e, 샘플, 테스트가 전부 새 표면만 사용하고 구 표면 grep이 0이어야
그 항목이 완료다. e2e·샘플은 사용자가 따라 하는 계약 예시이므로 전환에서 제외될 수 없다.

## 8. 실행 운영 — codex 실행, 감독자 리뷰 체계

이 재설계의 실행 주체와 절차. 실행은 codex 에이전트가, 감독·리뷰·진행 책임은 감독자(Claude 메인
세션)가 맡는다.

### 8.1 역할

- **codex 에이전트(실행)**: 한 요청에 **한 작업 항목**만 수행한다(리뷰 규칙과 동일). 요청서에 적힌
  대상 파일 밖을 수정하지 않는다.
- **감독자(Claude)**: 작업 요청서 작성 → codex 결과 diff 리뷰(계약 문서 대조) → 테스트·빌드 실측
  → 문서 상태 보드/체크박스 갱신 → 게이트 판정. 체크박스는 감독자 검증 후에만 갱신한다 — codex의
  "완료" 보고를 그대로 믿지 않는다.

### 8.2 작업 요청서 규칙

codex 요청서에는 반드시 포함한다: (1) 항목 ID(이 문서의 L/A~D 또는 각 문서 체크박스), (2) 정본
문서 경로와 해당 절, (3) 대상 파일 목록(이 밖은 수정 금지), (4) 완료 조건(소비자 이행·grep 0 포함),
(5) 검증 명령(테스트/grep), (6) 금지 사항(해당 항목의 계약 금지 규칙).

### 8.3 병렬 규칙

1. read-only 조사는 무제한 병렬.
2. 코드 변경은 **대상 파일 집합이 겹치지 않고 같은 단계에 속한 항목**만 병렬.
3. 계약 파일(각 언어 Contracts 표면)은 단일 소유 — 두 codex가 동시에 만지지 않는다.
4. ~~언어 wave는 순차(node → java → cpp)~~ **개정(2026-07-04, 사용자 지시)**: dotnet 계약이
   확정(P0~P6 완료, P7 수렴 중 — 계약 형태 동결)된 시점부터 **언어 wave 3개는 병렬 진행**한다.
   언어 간 코드베이스가 분리돼 파일 충돌이 없고, 각 언어 안에서는 계약 표면 단일 소유 규칙이
   그대로 적용된다. dotnet P7 수렴에서 계약 변경이 발생하면(예외적) 해당 차분만 3개 wave에
   전파한다.

### 8.4 실행 DAG

```text
[P0 조사 — 병렬]                          [P0 확정 — 사용자 결정 포함]
 role ushort/결번 근거      ─┐
 runtime query liveness 현황 ─┼─→ 값 테이블·오류 kind 대조·envelope 규칙 확정
 envelope 세션 전제 확인     ─┤        │
 기존 error kind 전수 대조   ─┘        ▼
                              [dotnet 레퍼런스]
                               독립 항목 병렬: A1(Spots) · A4(Builders) · C5(Configs) · D3(Attributes)
                               location 본선 순차: P1 계약 파일 → P2 store → P3 resolver·편의 표면
                                 → P4 lifecycle/Redis → P5 테스트·e2e·샘플 전환 → P6 문서 → P7 리뷰
                                      ▼
                              [node S0~G] → [java S0~G] → [cpp S0~G]   (각 wave에 e2e·샘플 포함,
                                      ▼                                 S0는 선행 병렬 가능)
                              [parity 장치] → [spec 승격]
```

### 8.5 게이트와 누락 방지

- 단계 종료마다: 감독자 diff 리뷰 + 변경 대비표 grep 가드(**framework·e2e·샘플·테스트 전체 범위**)
  + 테스트 그린. dotnet plan P7과 언어별 G 단계의 codex 교차 리뷰는 요청당 한 항목으로 발주한다.
- 진행 보드의 정본은 **각 문서의 체크박스·상태 보드**다(별도 트래커를 만들지 않는다 — 같은 지식
  두 곳 금지). 감독자가 갱신 책임을 진다.
- 최종 감사: 완료 선언 전에 감독자가 6개 문서의 미체크 항목 0건, 변경 대비표 구 이름 grep 0건
  (e2e·샘플 포함), 4언어 테스트 그린을 한 번에 재검증한다.

## 9. 보류·질문 목록 (사용자 결정 대기)

### Q1. L13 actor client의 전송 경로 — core 협력 필요 (2026-07-04, dotnet P3b 설계 보고)

`SendToActor`는 core 수정 없이 성립하지 않음이 실측으로 확정됐다: 수신 dispatcher가 actor packet의
source rid를 **무조건** bound-session route로 갱신하고(`ZLinkEntrySpotActorDispatcher.cs:119`,
`ZLinkSpotActivationDispatcher.cs:331`), core gateway protocol에는 no-bind kind/flag가 없으며
(`service_spot_actor_gateway_protocol_internal.hpp`), `RequestToActor`의 reply는 bound-session에
의존한다. 선택지:

- **(a) core gateway protocol 확장** — `server_to_actor_no_bind` packet kind(또는 skip-bind flag)
  추가 + reply correlation 경로 설계. core 1회 변경으로 4언어가 공유하지만 core protocol
  변경 절차(core spec/draft) 필요. P0 조사-3의 "forwarding framing 재사용" 결정의 정공법 연장.
- **(b) framework 내부 route-packet adapter** — 기존 route send/request(자체 reply correlation
  보유)에 내부 actor-dispatch 표지를 실어 framework가 actor mailbox로 dispatch. core 무변경,
  사용자 handler 계약 유지. 단 P0 조사-3의 기록된 결정(actor-forwarding framing 재사용)을 뒤집고,
  actor packet의 내부 전송 경로가 2벌이 되며, 언어별 4회 구현.

**✅ 결정(2026-07-04, 사용자): (a) core gateway protocol 확장으로 진행.**

실행 절차(사용자 지시 확정):
1. core 설계 draft(`server_to_actor_no_bind` packet kind + reply correlation) → core 구현 + **회귀 테스트 작성**(actor gateway 경로).
2. 버전 업(VERSION 파일 + core/include/zlink.h의 ZLINK_VERSION_* — 두 곳 동기, 현행 8.4.3 → protocol 추가이므로 8.5.0) → `core/v8.5.0` 태그 push → GitHub Actions(build.yml)가 빌드·릴리즈. **릴리즈 런은 gh CLI로 모니터링**(`gh run watch`).
3. `scripts/local-package/native/update-zlink-libs.sh core/v8.5.0 --expect-version 8.5.0`으로 7개 바인딩 native 교체+버전 마커 갱신.
4. 신규 kind가 바인딩 표면에 필요하면 bindings 라이브러리 수정(doc/spec/bindings가 target blueprint) + 바인딩 테스트.
5. framework L13 구현 재개: dotnet 레퍼런스 → node/java/cpp, 샘플 Tracking 이식, `session-actor-dispatch.ko.md` 동명 API 구분 각주, e2e.
6. **e2e 시나리오 추가(사용자 지시 2026-07-04)**: to-actor 메시지의 bind 상태 매트릭스 — ⑴ bind된 actor ⑵ bind 안 된 actor ⑶ bind 안 됐다가 이후 bind ⑷ bind 됐다가 unbind/disconnect 된 후 — 각각 send/request 전송·수신 확인(+bind 비오염 검증). 공통 e2e 문서에 시나리오 정의 후 4언어 구현, 실측 그린 확인.
7. 작업 전반: **논리 단위로 중간 커밋·푸시**하며 진행(사용자 지시).

이 절차 완료 전까지 L13 관련 체크박스는 보류 유지. 나머지 변경 목록은 L13에 의존하지 않으므로 계속 진행한다.

이름 충돌 주의(P6 발견): `session-actor-dispatch.ko.md` spec에 **과거 제거된** session gateway의
`SendToActor(...)`/`RequestActor(...)` 표면 기록이 남아 있다(제거 이력 서술). L13 구현 시 같은
이름의 신설 표면과 혼동되지 않도록 해당 spec에 구분 각주를 함께 추가할 것.

## 10. 완료 판정

- location 원본 draft와 이 문서가 같은 계약을 말하고, 값 테이블이 채워져 있다.
- 4언어의 public contract가 변경 목록 L1~L20, A1~D3을 모두 반영했고, 각 언어 진행 문서의 상태
  보드가 전부 완료다.
- **4언어의 e2e와 샘플이 새 계약 표면만 사용한다** — 변경 대비표의 구 이름이 framework·e2e·샘플
  어디에도 grep되지 않는다.
- 각 언어에 contract 형태 고정 테스트가 있고, 계약 인벤토리 체커가 CI에서 동작한다.
- 언어별 codex 리뷰 게이트(각 진행 문서의 G 단계)가 전부 `이슈 없음`이다.
