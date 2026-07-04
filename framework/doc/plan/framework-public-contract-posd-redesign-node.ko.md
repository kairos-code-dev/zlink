# Public contract POSD 재설계 적용 — Node (NestJS)

> [framework-public-contract-posd-redesign.ko.md](framework-public-contract-posd-redesign.ko.md)의
> 변경 목록(L1~L20, A1~D3)을 Node framework에 적용하는 진행 추적 문서다. **계약 내용은 반복하지
> 않는다** — 항목 상세는 공통 문서와 dotnet plan 2개가 정본이고, 두 문서가 충돌하면 공통 문서가
> 기준이다.
>
> 선행 조건: dotnet 레퍼런스 구현 그린. 1차 location store 이식
> ([porting-node](../framework/common/draft/framework-location-resolver-store-porting-node.ko.md))은 완료된 별개 작업이며,
> 이 문서는 그 위의 2차 wave다.
> 다른 언어: [java(+kotlin)](framework-public-contract-posd-redesign-java.ko.md) ·
> [cpp](framework-public-contract-posd-redesign-cpp.ko.md)

경로: `framework/languages/node/packages/framework/src/contracts/` (+ `runtime/`, locations Redis
extension, e2e, samples)

## 1. 상태 보드

상태 표기: ⬜ 미착수 · 🟨 진행 중 · ✅ 완료

| 단계 | 대상 항목 | 상태 | 완료일 |
|------|-----------|:---:|--------|
| S0. 사전 조사 — 현재 표면과 변경 목록 대조 (보고서: `codex-companion result task-mr5p8492-dmqs9h`) | 전체 | ✅ | 2026-07-04 |
| S1. 계약 모델·enum | L4, L5, L10, L11, L20, A2, A3, D1 | ✅ | 2026-07-04 |
| S2. store·runtime 계약 | L1, L2, L3, A4 | ✅ | 2026-07-04 |
| S3. resolver·운영 조회·watch | L6, L7, L8, L9 | ✅ | 2026-07-04 (CC-FIX: stop이 채널 루프 종료 대기) |
| S4. 사용자 편의 표면 | L12~L17, C1~C4, C7, C8, B1~B6 | ✅ | 2026-07-04 |
| S5. 샘플·E2E 전수 전환 — 전체 변경 목록의 소비자 이행, 구 표면 grep 0 (`e2e/`·샘플 포함) | L18 + L/A~D 소비자 반영 | ✅ | 2026-07-04 |
| S6. contract 형태 고정 테스트 + grep 가드 | 공통 문서 6절 | ✅ | 2026-07-04 |
| G. 완료 게이트 — codex 리뷰(누락 0건) | 전체 | ✅ | 2026-07-04 (POSD 리뷰 2차 "이슈 없음" · contract 리뷰 2차는 L17 잔여 1건(bind-courier snapshot 직접 조립) 지적 — 감독자가 framework 팩토리 경유로 직권 수정·빌드 그린 확인 후 마감. 1차 이슈 6건(L9·L17·L19·teardown·wire codec 2건)도 수정 완료) |

## 2. 언어 매핑

| 계약 요소 | Node 표현 |
|-----------|-----------|
| 비동기 | `Promise<T>`. call 터미널은 `async(signal?)` — L13의 "await 가능 단독 터미널, void submit 금지" 그대로 |
| 닫힌 합(L8 typed key) | discriminated union (`kind` 태그 필드 + 타입별 인터페이스) |
| record 모델 | readonly interface + factory 함수(기존 contracts 관례 확인, S0) |
| enum 명시 값 | TS enum 또는 const object — 공통 문서 5절 값 테이블 그대로. 문자열 enum 사용 시에도 숫자 값 병기 여부 S0에서 결정 |
| 타입 유도 lookup | 해당 없음 — 리플렉션 제너릭이 없으므로 actor client/directory는 **actor id 단독 시그니처가 그대로 기본형**이다(idiom 차이 아님, 계약 그대로) |
| D3 등록 규약 | NestJS decorator·모듈 등록이 .NET attribute의 대응물. decorator 이름·파라미터가 등록 API와 1:1인지 S0에서 표로 정리 |
| C6 ownership | payload 생성 시 Buffer 복사 규칙 명문화(공유 Buffer aliasing 금지 또는 명시) |

## 3. Node 특이 사항

- **registry 계약 잔재와 A2가 겹친다**: `contracts/Registry/`의 `ZLinkAutoConnectType`,
  `ZLinkServiceRole` 등이 1차 이식 후에도 남아 있으면 A2(이중 정의 해소)의 node 측 대상이 된다.
  S0에서 잔재 목록을 만들고 S1에서 함께 정리한다.
- A1(leave-actor 명명): camelCase가 언어 규약이므로 표기는 정상 — 개념 이름과 비동기 반환 규약
  일치만 확인.
- C5(set-only 속성): TS에 set-only 속성 관례가 없으므로 해당 없음 예상 — S0에서 확인 후 표기.
- 1차 이식이 POSD 리팩토링 루프(P11)까지 마친 코드베이스이므로, 이 wave의 변경은 기존 구조 위의
  계약 형태 변경이 대부분이다. god-file 재발 여부만 S6 가드에 포함.

## 4. 사전 조사(S0) 체크리스트

- [x] 변경 목록 L1~L20, A1~D3 각각에 대해 node 현재 표면의 대응 심볼·파일을 표로 만든다
      (없음/이미 충족/변경 필요 3분류).
- [x] `contracts/Registry/` 등 1차 이식 잔재 목록. (S0 보고서·S1에서 정리 완료)
- [x] call 터미널 현황(submit/async 시그니처, AbortSignal 지원 여부) — C7 대조표의 node 열.
- [x] decorator ↔ 등록 API 대응표 — D3의 node 열.

## 5. 완료 판정

- 상태 보드 전 단계 완료, 공통 문서의 변경 목록이 node 표면에서 grep으로 검증된다(구 이름 잔존 0).
- contract 형태 고정 테스트가 있고, 샘플·E2E가 L18 금지 규칙을 지킨다.
- codex 리뷰 게이트 `이슈 없음`.
