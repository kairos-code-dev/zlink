# Public contract POSD 재설계 적용 — C++

> [framework-public-contract-posd-redesign.ko.md](framework-public-contract-posd-redesign.ko.md)의
> 변경 목록(L1~L20, A1~D3)을 C++ framework에 적용하는 진행 추적 문서다. **계약 내용은 반복하지
> 않는다** — 항목 상세는 공통 문서와 dotnet plan 2개가 정본이고, 충돌하면 공통 문서가 기준이다.
>
> 선행 조건: dotnet 레퍼런스 구현 그린 + node·java 적용 완료(적용 순서 node → java → cpp).
> 1차 location store 이식([porting-cpp](../framework/common/draft/framework-location-resolver-store-porting-cpp.ko.md))은
> 완료된 별개 작업이며, 이 문서는 그 위의 2차 wave다.
> 다른 언어: [node](framework-public-contract-posd-redesign-node.ko.md) ·
> [java(+kotlin)](framework-public-contract-posd-redesign-java.ko.md)
>
> 참고: framework C++는 entry-spot actor dispatch 등 일부 기능의 레퍼런스 구현이기도 하다. 이
> wave에서 계약 형태가 바뀌어도 그 동작 의미론(레퍼런스)은 보존한다.

경로: `framework/languages/cpp/framework/include/zlink/framework/contracts/` (public 헤더:
actors/channels/codecs/configuration/dispatch/errors/eventing/handlers/locations/messaging/spots/
streams/timers/workers) + 구현 소스, e2e, samples.

## 1. 상태 보드

상태 표기: ⬜ 미착수 · 🟨 진행 중 · ✅ 완료

| 단계 | 대상 항목 | 상태 | 완료일 |
|------|-----------|:---:|--------|
| S0. 사전 조사 — 현재 표면과 변경 목록 대조 (보고서: `codex-companion result task-mr5sdua6-sfadij`) | 전체 | ✅ | 2026-07-04 |
| S1. 계약 모델·enum | L4, L5, L10, L11, L20, A2, A3, D1 | ✅ | 2026-07-04 |
| S2. store·runtime 계약 | L1, L2, L3, A4 | ✅ | 2026-07-04 |
| S3. resolver·운영 조회·watch | L6, L7, L8, L9 | ✅ | 2026-07-04 |
| S4. 사용자 편의 표면 | L12~L17, C1~C4, C7, C8, B1~B6 | ✅ | 2026-07-04 (S4-b 포함) |
| S5. 샘플·E2E 전수 전환 — 소비자 이행, 구 표면·구 헤더 경로 grep 0 (`e2e/`·샘플 포함) | L18 + L/A~D 소비자 반영 | ✅ | 2026-07-04 (S5-b·S5-c·RT-FIX·CM-FIX 포함, ctest 42/42) |
| S6. contract 형태 고정 테스트 + grep 가드 | 공통 문서 6절 | ✅ | 2026-07-04 (ctest 42/42) |
| G. 완료 게이트 — codex 리뷰(누락 0건) | 전체 | ✅ | 2026-07-04 (POSD 리뷰 "이슈 없음" · contract 리뷰 1차 3건(fixture UpdatedAt·actor ref 필드·e2e README) 수정, 2차 잔여는 DeliveryDispatch 문서 registry 서술 6곳 — 감독자 직권 교정·grep 0. ctest 42/42) |

## 2. 언어 매핑

| 계약 요소 | C++ 표현 |
|-----------|----------|
| 비동기 | 기존 framework cpp 관례(콜백/future/코루틴 중 무엇인지 S0에서 확인)를 따르되, L13의 계약 — "터미널에서 실패를 반드시 받을 수 있고, 결과 무시형 터미널은 없다" — 를 그 관례로 표현한다 |
| 실패 분류 | 예외 비사용 코드베이스라면 error code/expected 계열로 — kind와 retriable 여부는 공통 값 테이블 그대로 |
| 닫힌 합(L8 typed key) | `std::variant` (kind별 key 타입) |
| record 모델 | struct (불변 규약 주석) |
| enum 명시 값 | `enum class` + 명시 값(공통 문서 5절 값 테이블 그대로) |
| 타입 유도 lookup | 템플릿 파라미터로 actor 타입 유도 가능하면 제공, 아니면 actor id 단독 시그니처가 기본형(계약은 id 단독이 정본) |
| D3 등록 규약 | attribute 대응물 없음 — 명시 등록 API가 유일한 형태이므로 등록 API 이름·파라미터 규약만 공통 문서와 1:1 확인 |
| C6 ownership | 소유권이 언어의 1급 개념 — payload 생성 경로의 복사/이동/뷰 구분을 타입으로 표현(span 뷰 vs owned buffer), 공통 문서의 "생성 경로별 의미 명문화"를 가장 강하게 이행할 수 있는 언어 |

## 3. C++ 특이 사항

- contracts 아래 `registry` 디렉터리 잔재 확인 — 1차 이식의 정리 범위였는지 S0에서 확인하고,
  남았으면 A2(auto-connect enum 이중 정의)와 함께 처리한다.
- A1(leave-actor 명명): cpp 명명 규약(snake_case 또는 camelCase — 코드베이스 관례 S0 확인)에 맞춘
  개념 이름 일치만 확인.
- **오류 kind drift(P0 조사-4 실측)**: `framework_error_kind_t`(contracts/errors/error.hpp)는
  25종으로, dotnet 20종 대비 `actor_stale_generation`이 ordinal 4에 삽입되어 이후 값이 밀렸고
  `timeout`/`shutdown`/`disconnected`/`closed`가 추가됐으며 `worker_timeout` 명칭도 다르다.
  S1에서 공통 값 테이블로 정렬하되, cpp 전용 5종의 공통 승격/정리 판정은 공통 문서 5.2의 잔여
  판정 항목을 따른다. `actor_stale_generation`(generation stale)은 신설 `ActorLocationStale`
  (owner-lease stale)과 다른 개념이므로 병합하지 않는다.
- C5(set-only 속성): cpp에 속성 개념이 없으므로 setter-only config 클래스가 있는지만 확인.
- CMake 타깃·헤더 재배치(L11)는 include 경로가 public 표면의 일부이므로 변경 대비표에 헤더 경로
  변경도 포함한다(샘플·e2e의 include 수정 범위 산정).

## 4. 사전 조사(S0) 체크리스트

- [x] 변경 목록 L1~L20, A1~D3 각각에 대해 cpp 현재 표면의 대응 심볼·헤더를 표로 만든다
      (없음/이미 충족/변경 필요 3분류).
- [x] `contracts/registry/` 잔재 목록과 처리 방침.
- [x] 비동기·오류 전달 관례 확인(콜백/future/expected) — C7 대조표와 L13 터미널 계약의 cpp 열.
- [x] 헤더 경로 변경 목록(L11) — 소비자(e2e/samples/connector) include 파급 산정.

## 5. 완료 판정

- 상태 보드 전 단계 완료, 구 이름·구 헤더 경로 잔존 grep 0.
- contract 형태 고정 테스트(또는 컴파일 타임 계약 검증)가 있다.
- 샘플·E2E가 L18 금지 규칙을 지킨다.
- codex 리뷰 게이트 `이슈 없음`.
