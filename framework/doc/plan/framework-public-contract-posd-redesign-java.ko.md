# Public contract POSD 재설계 적용 — Java (+Kotlin)

> [framework-public-contract-posd-redesign.ko.md](framework-public-contract-posd-redesign.ko.md)의
> 변경 목록(L1~L20, A1~D3)을 Java/Kotlin framework에 적용하는 진행 추적 문서다. **계약 내용은
> 반복하지 않는다** — 항목 상세는 공통 문서와 dotnet plan 2개가 정본이고, 충돌하면 공통 문서가
> 기준이다.
>
> 선행 조건: dotnet 레퍼런스 구현 그린 + node 적용 완료(적용 순서 node → java → cpp).
> 1차 location store 이식([porting-java](../framework/common/draft/framework-location-resolver-store-porting-java.ko.md))은
> 완료된 별개 작업이며, 이 문서는 그 위의 2차 wave다.
> 다른 언어: [node](framework-public-contract-posd-redesign-node.ko.md) ·
> [cpp](framework-public-contract-posd-redesign-cpp.ko.md)

경로: `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/`
(public 표면 패키지: actors/channels/configuration/errors/handlers/locations/messaging/monitoring/
spots/streams). Kotlin 표면: `zlink-framework-kotlin`. Redis extension:
`zlink-framework-locations-redis`. Spring 등록: `zlink-framework-spring-boot-starter`.

## 1. 상태 보드

상태 표기: ⬜ 미착수 · 🟨 진행 중 · ✅ 완료

| 단계 | 대상 항목 | 상태 | 완료일 |
|------|-----------|:---:|--------|
| S0. 사전 조사 — 현재 표면과 변경 목록 대조 (보고서: `codex-companion result task-mr5sdsz7-o6dm3k`) | 전체 | ✅ | 2026-07-04 |
| S1. 계약 모델·enum | L4, L5, L10, L11, L20, A2, A3, D1 | ✅ | 2026-07-04 |
| S2. store·runtime 계약 | L1, L2, L3, A4 | ✅ | 2026-07-04 |
| S3. resolver·운영 조회·watch | L6, L7, L8, L9 | ✅ | 2026-07-04 |
| S4. 사용자 편의 표면 | L12~L17, C1~C4, C7, C8, B1~B6 | ✅ | 2026-07-04 (C7 awaitable stage=java idiom 유지) |
| S4k. Kotlin 표면 동기화 (suspend 확장) | S4와 동일 항목 | ✅ | 2026-07-04 |
| S5. 샘플·E2E 전수 전환 (`e2e`·`e2e-kotlin`·양쪽 샘플) — 소비자 이행, 구 표면 grep 0 | L18 + L/A~D 소비자 반영 | ✅ | 2026-07-04 |
| S6. contract 형태 고정 테스트 + grep 가드 | 공통 문서 6절 | ✅ | 2026-07-04 |
| G. 완료 게이트 — codex 리뷰(누락 0건) | 전체 | ✅ | 2026-07-04 (POSD 리뷰 2차 "이슈 없음" · contract 리뷰 2차 잔여 1건(backend SPI epoch)은 감독자 직권 개명·전체 그린 확인 후 마감. 1차 이슈 7건(Redis JSON 필드·epoch 표면·resolver 개명·SpotAddress 표면·ensure 의미론·directory DI 2건) 수정 완료. 단 S4k(kotlin suspend 동기화)는 별도 잔여 — 완료 후 최종 마감) |

## 2. 언어 매핑

| 계약 요소 | Java 표현 | Kotlin 표면 |
|-----------|-----------|-------------|
| 비동기 | 기존 core 관례(`ZLinkAwait`/`ZLinkSubmitStage` 등)를 S0에서 확인해 적용. call 터미널은 await 가능 단독(L13) — void submit 부재를 java 표면에도 그대로 강제 | suspend 함수 확장. Flow는 watch/event 표면에만 |
| 닫힌 합(L8 typed key) | sealed interface + record 구현 | 동일(공유) |
| record 모델 | java record | data class 별도 정의 금지 — java record 공유 |
| enum 명시 값 | enum + 명시 값 필드(공통 문서 5절 값 테이블 그대로) | 공유 |
| 타입 유도 lookup | 기존 결정 유지: `<TActor extends ZLinkActor>` **단일 제너릭**(2-인터페이스 분리 금지). actor client/directory의 타입 유도 overload는 이 관례로 | reified 확장 허용 |
| D3 등록 규약 | annotation이 .NET attribute 대응물 — annotation ↔ 등록 API 1:1 표를 S0에서 작성 | 공유 |
| C6 ownership | byte[]/ByteBuffer 복사 규칙 명문화 | 공유 |

## 3. Java/Kotlin 특이 사항

- **Kotlin은 java 런타임을 공유**한다(계약·spec 공유, guide만 kotlin-only). 이 문서 한 부로 두
  언어를 관리하고, S4k에서 suspend 표면만 별도 확인한다. kotlin 쪽에 독자 public 계약을 만들지
  않는다.
- A1(leave-actor 명명): camelCase가 언어 규약이므로 표기는 정상 — 개념 이름 일치만 확인.
- **오류 kind enum 부재(P0 조사-4 실측)**: java에는 `ZLinkFrameworkErrorKind` 대응 enum이 아예 없고
  `ZLinkFrameworkException`은 message/cause만 갖는다(worker 계열만 별도 subclass). S1에서 공통
  문서 5.2의 확정 이름 집합(기존 20 + 신설 2)과 kind→retriable 매핑으로 신설한다.
- **Role enum이 ordinal 기반(P0 조사-1 실측)**: `ZLinkLocationRole.java`에 명시 숫자가 없어 Redis
  row JSON의 숫자 매핑(`roleNumber(...)`)과 지식이 두 곳이다. S1에서 공통 값 테이블(0,2~6)로 명시
  값을 부여하고 매핑 함수 중복을 제거한다.
- `registry` 패키지 잔재가 core에 남아 있다 — 1차 이식의 정리 범위였는지 S0에서 확인하고, 남았으면
  A2(auto-connect enum 이중 정의)와 함께 처리한다.
- C5(set-only 속성): java에 해당 관례 없음 예상 — S0에서 setter-only 빌더 메서드 유무 확인.
- Spring Boot starter의 등록 표면(A4 per-role 등록 대응물 포함)이 core와 별도 모듈이므로 S2에서
  starter까지 함께 정리한다.

## 4. 사전 조사(S0) 체크리스트

- [x] 변경 목록 L1~L20, A1~D3 각각에 대해 java 현재 표면의 대응 심볼·파일을 표로 만든다
      (없음/이미 충족/변경 필요 3분류).
- [x] `systems.zlink.framework.registry` 잔재 목록과 처리 방침.
- [x] call 터미널 현황(`ZLinkSubmitStage`/await 시그니처) — C7 대조표의 java 열.
- [x] annotation ↔ 등록 API 대응표 — D3의 java 열.
- [x] kotlin 확장 표면 목록(suspend 대응 필요한 신설 API: directory/client/readiness/bind-or-get).

## 5. 완료 판정

- 상태 보드 전 단계 완료(코틀린 S4k 포함), 구 이름 잔존 grep 0.
- contract 형태 고정 테스트가 java·kotlin 양쪽 표면을 고정한다.
- 샘플·E2E(java + kotlin)가 L18 금지 규칙을 지킨다.
- codex 리뷰 게이트 `이슈 없음`.
