# Framework Guide cpp ↔ dotnet 정렬 + 기능 패리티 계획

> 상태: **계획(승인 대기)**. 작성일 2026-06-15.
> 목적: cpp / dotnet framework 사용자 가이드를 같은 구조·동등 깊이로 맞추고,
> 그 과정에서 드러나는 **언어별 기능 누락을 코드로 확인해 구현 대상으로 도출**한다.

## 0. 배경

`framework/languages/cpp/doc/guide` 와 `framework/languages/dotnet/doc/guide` 는
주제 커버리지는 거의 같지만 두 축에서 갈라져 있다.

- **구조/서술** — cpp 가 더 깔끔하고 예제 중심(TicTacToe·bingo·image.resize),
  개념 훅이 좋다(예: SPOT 직렬화 모델, dealer/route mesh 전용 섹션).
- **깊이** — dotnet 이 더 풍부하다(handler 작성 변형, 시작단계 검증, 연결 제어,
  codec, timer 정책, 자주 막히는 곳; interface-catalog 757줄 vs 80줄).

추가로, 한쪽 가이드에 있는 기능이 **다른 언어 framework 에는 구현이 없을 수 있다**.
문서만 복사하면 "있는 것처럼" 보이는 함정이 생기므로, 정렬 전 **코드로 기능 제공
여부를 확인**하고 누락은 구현 대상으로 분리한다.

## 1. 원칙

1. **cpp 골격을 베이스**로 삼고, dotnet 의 깊이를 흡수한다(양방향 보강).
2. **문서가 코드를 따라간다** — 코드에 없는 기능은 가이드에 "있는 것처럼" 쓰지
   않는다. 누락은 §6 gap 표로 빼서 구현 트랙으로 보낸다.
3. 각 언어 **idiom 유지**(snake_case vs PascalCase, `co_await` vs `async/await`).
   전역 챕터 번호는 그대로 둔다(cpp 는 di/config/http 3장을 더 가짐).
4. 중복은 정리하되 **정보는 잃지 않는다**.
5. 기존 제약 준수: 9개 언어 탭 규약·`check_doc_tabs.py`(있다면), doc/site 미러 drift,
   상대 링크 무결성.

## 2. 범위

**포함(정렬 대상 공유/부가 챕터):**

| 주제 | cpp | dotnet |
|------|-----|--------|
| Overview | 01 | 01 |
| Getting started | 02 | 02 |
| Concepts | 03 | 03 |
| Channel messaging | 07 | 04 |
| SPOT | 08 | 05 |
| Actor · Session | 09 | 06 |
| Stream | 10 | 07 |
| Registry | 11 | 08 |
| Monitoring | 12 | 09 |
| Interface catalog | 13 | 11 |
| Feature map | (없음→이식) | 10 |
| gRPC alternative / 사용처 | (없음→이식) | 12 |
| Samples map | 14 | samples/ + case-studies/ |

**제외:** cpp 전용 `04-di-container` · `05-configuration` · `06-http-hosting`
(언어 고유 호스팅 개념). 단 dotnet 의 동등 개념(ASP.NET Core DI/구성)은 dotnet
가이드에 이미 분산돼 있으므로 그대로 둔다. case-studies/samples **본문 재작성**은
범위 밖(진입/링크 정렬만).

## 3. 코드 위치 (기능 패리티 audit 기준)

- **dotnet**: `framework/languages/dotnet/src/Zlink.Framework/...`,
  `.../Systems.Zlink.Stream.Connector/...` (400+ .cs)
- **cpp**: `framework/languages/cpp/framework/include/zlink/framework/contracts/...`
  (public 계약), `framework/languages/cpp/framework/src/runtime/...`,
  `connector/`, `extensions/`, `http-client/`
- **공통 코어**: `core/src/...` (zlink core; 양 framework 공유)
- 참조 계약(target): `framework/languages/*/doc/spec/...`

## 4. 두 트랙

> **"기능"의 정의** — 여기서 audit 대상 기능은 **zlink framework 공유(cross-language)
> 기능 surface** 다: channel messaging(request/send/publish, dealer/route mesh),
> SPOT, actor·session, stream, registry/discovery, monitoring. 즉 cpp·dotnet·node·
> java 가 **모두 동등하게 제공해야 하는 framework 계약**(spec/bindings 가 정의하는
> 표면). **언어 호스팅 고유 기능은 제외** — cpp 의 DI 컨테이너·Configuration·HTTP
> Hosting, dotnet 의 ASP.NET Core DI/구성 통합 등은 의도적으로 언어별이라 패리티
> 대상이 아니다.

작업은 챕터마다 **(A) 기능 패리티 audit → (B) 문서 정렬** 순서로 돈다.

- **(A) audit**: 그 챕터가 다루는 기능 목록을 뽑아, cpp/dotnet **양쪽 코드**에서
  제공 여부를 확인 → §6 gap 표에 기록(present-both / cpp-only / dotnet-only /
  doc-only(코드 없음)).
- **(B) 문서 정렬**: present-both 만 양쪽 가이드에 동등 서술. one-only 는 "있는
  쪽만" 문서화하고 gap 표에 구현 과제로 표시. doc-only 는 문서에서 제거하거나
  spec-target 표기.

## 5. 챕터별 정렬 매핑

각 행: **cpp→dotnet 이식**(dotnet 에 없어 가져갈 cpp 섹션) /
**dotnet→cpp 이식**(cpp 에 없어 가져갈 dotnet 섹션) / **타깃 통합 구조**.

### 5.1 Channel messaging (cpp07 / dotnet04)
- dotnet→cpp: handler 작성 변형(interface + attribute/메서드), 노출 방법
  A(group)/B(typed) + **시작단계 검증**, 연결 제어(Discovery vs manual), codec 등록,
  통합 예제, "자주 막히는 곳".
- cpp→dotnet: dealer mesh / route mesh **전용 섹션**과 예제(dotnet 은 "2 channel
  종류"에 짧게 접혀 있음), 깔끔한 예제·mermaid.
- 통합 구조(안): 채널 종류 → 서버(handler group/typed, 시작검증) → 클라이언트(call
  표면, timeout 기본값) → filter → 연결 제어 → dealer mesh → fanout → route mesh →
  codec → 통합 예제 → 자주 막히는 곳 → 더 보기.

### 5.2 SPOT (cpp08 / dotnet05)
- cpp→dotnet: "room spot 직렬화 — 큐 하나, 한 번에 하나"(동시성 모델), entry spot
  매칭/룸 배정 예제.
- dotnet→cpp: timer 정책, 예외/종료, 인스턴스 생성/조회, outbound 3표면 상세,
  Stage wrapper, 자주 막히는 곳.
- audit 포인트: cpp 에 `IZLinkSpotPublisherClient` 대응(local spot 없는 노드 publish),
  Stage wrapper, timer 정책 옵션 존재 여부.

### 5.3 Actor · Session (cpp09 / dotnet06)
- dotnet→cpp: **session actor dispatch**(연결 서버/로직 서버 분리), resolver,
  오류 처리, 등록 골격(Session/Play 서버 전체 예제). cpp 가 가장 얇은 챕터.
- audit 포인트: cpp 의 actor gateway / bound session push / session-actor 바인딩
  코드 제공 여부 — **누락 가능성 높음**(우선 audit).

### 5.4 Stream (cpp10 / dotnet07)
- dotnet→cpp: heartbeat/reconnect, TLS, Unity 클라이언트, 오류 코드/결과,
  자주 막히는 곳.
- audit 포인트: cpp connector 의 heartbeat/reconnect/TLS 제공 여부(`connector/`).

### 5.5 Registry (cpp11 / dotnet08)
- dotnet→cpp: embedded/standalone 배포 모델, clustering, in-process vs 원격
  topology 조회, Registry 기반 route 기본 구현, lifecycle.
- audit 포인트: cpp 의 원격 topology query client, clustering 지원 여부.

### 5.6 Monitoring (cpp12 / dotnet09)
- dotnet→cpp: source 별(socket/registry/spot) event handler 예제, 자주 막히는 곳.
- cpp→dotnet: health/메트릭 섹션 정리(cpp 가 health·metric 분리).
- audit 포인트: cpp health/metric 표면 존재 여부.

### 5.7 Overview / Getting-started / Concepts
- Overview: dotnet 의 통합 4축·이름 표기 규칙·비목표·읽는 순서 ↔ cpp 의 토폴로지·
  소켓 패턴. 두 글을 같은 골격으로.
- Getting-started: dotnet "잘 안 될 때"(트러블슈팅) → cpp 에 추가.
- Concepts: cpp 의 app_t 수명주기·구성 표면 지도·module_t ↔ dotnet 의 "용어 빠르게
  잡기"·연결(Discovery vs manual)·send async. 양쪽 상호 보강.

## 6. 기능 패리티 audit 산출물 (gap 표)

각 기능을 아래 표로 분류해 `doc/plan/framework-guide-feature-parity.ko.md`(신규)에
누적한다. cpp-only/dotnet-only/doc-only 행이 **구현 또는 문서 수정 과제**다.

| 기능 | 챕터 | cpp 코드 | dotnet 코드 | 판정 | 조치 |
|------|------|---------|------------|------|------|
| (예) session actor dispatch | actor | ? | ✅ | dotnet-only? | cpp 구현 검토 |
| (예) stream TLS | stream | ? | ✅ | ? | audit |
| (예) 원격 topology query | registry | ? | ✅ | ? | audit |
| (예) request 기본 timeout 값 | channel | ? | 30s | 값 확인 | cpp 코드 확인 |

> ⚠️ 이미 cpp 가이드에 쓴 "request 기본 timeout 30초"는 dotnet 미러 가정이었다.
> cpp 코드(`framework/src/runtime/...`)에서 실제 기본값을 확인해 정정/확정한다.

## 7. 부가 챕터

- **interface-catalog 동등화**: dotnet 11(757줄, 10개 도메인 섹션)을 기준으로 cpp
  13(80줄)을 확장. 단 **코드 계약(contracts/ 헤더)에 실재하는 것만** 카탈로그에
  넣는다(audit 연동).
- **feature-map → cpp**: dotnet 10 을 cpp 로 이식(기능×난이도×언제, 빠른 선택).
- **gRPC alternative / 사용처 → cpp**: dotnet 12 를 cpp 로 이식(cross-language,
  배치 비교, 호출 경로). cpp 관점 예제로 치환.
- **samples-map 정렬**: cpp 14-samples-map ↔ dotnet samples/·case-studies/ 진입·
  링크 상호 참조 정리(본문 재작성 아님).

### 7.1 샘플 패리티 (cpp 가 dotnet 샘플 전부 동일 구현 예정)

현재 구현 상태:

| 샘플 앱 | dotnet | cpp |
|---------|--------|-----|
| TicTacToe | ✅ | ✅ |
| Bingo | ✅ | ✅ |
| DeliveryDispatch | ✅ | ❌ (예정) |
| GameQuest | ✅ | ❌ (예정) |
| ShoppingMall | ✅ | ❌ (예정) |
| SupportChat | ✅ | ❌ (예정) |

방침: **cpp 도 dotnet 과 동일한 샘플을 동일 구현으로 전부 제공**한다(현재 2/7 →
7/7 목표). 이는 코드 구현 트랙이라 본 계획서(문서 정렬 + 기능 audit) 범위 밖이지만,
두 가지로 직접 맞물린다.

1. **기능 audit 의 체크리스트** = dotnet 샘플 집합. 각 샘플이 쓰는 framework 공유
   기능을 cpp 가 제공하는지가 §6 gap 표의 1차 입력이다(샘플이 막히면 그 기능이
   누락이라는 신호).
2. **samples-map 문서**는 위 표처럼 구현/예정을 명시하고, cpp 샘플이 추가될 때마다
   갱신한다. cpp 도 dotnet 처럼 `guide/samples/` per-앱 문서 구조를 갖출지, 14-samples-map
   단일 장을 확장할지는 첫 샘플 추가 시점에 결정(기본: 단일 장 확장 후 분리).

## 8. 실행 순서(제안)

1. **기능 패리티 audit 먼저**(읽기 전용): 공유 7챕터 + interface-catalog 범위의
   기능을 cpp/dotnet 코드로 전수 확인 → §6 gap 표 완성.
2. gap 표 리뷰 → **구현 필요 항목 분리**(별도 구현 계획). 문서는 코드 현실에 맞춰.
3. 문서 정렬을 챕터 단위로: channel → spot → actor → stream → registry →
   monitoring → concepts/overview/getting-started → interface-catalog → 부가.
4. 각 챕터 적용 후 링크/탭/미러 검증.

## 9. 제약 / 리스크

- doc/site 미러(internals/guide verbatim, spec→api 재배치) drift — 가이드 변경 시
  미러 동기화 필요.
- 9개 언어 탭 규약·스니펫 경로(신규 샘플 등록 gotcha).
- 상대 링크 다수 — 챕터 재구성 시 깨지기 쉬움.
- **audit 가 구현 누락을 드러내면 범위가 문서→구현으로 확장**된다. 구현은 별도
  승인/계획으로 분리(이 계획서는 문서 정렬 + audit 까지).

## 10. 비목표

- case-studies/samples **본문** 재작성.
- cpp di/config/http 챕터 변경.
- 전역 챕터 번호 통일.
- spec/internals 문서 재작성(가이드 정렬에 필요한 최소 참조만).
