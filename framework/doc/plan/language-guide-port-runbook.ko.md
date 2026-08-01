# server 가이드 공통화 런북

> 지금 `framework/doc/framework/dotnet/guide/server/`에만 있는 server 가이드를 **공통 정본
> 한 벌 + 언어별 장**으로 재편하고, cpp · java · kotlin · node를 채우는 작업 지침이다.
> 여러 lane이 서로를 기다리지 않고 병렬로 진행하는 것이 목적이다.
>
> 작성 규약 자체는 [기술문서 작성 원칙](../../../doc/principal/documentation/documentation-principles.ko.md)과
> [사용자 가이드 문서 작성 가이드](../../../doc/principal/documentation/guide-writing-guide.ko.md)가
> 소유한다. 이 런북은 **무엇을 어디에 두고, 무엇을 근거로, 어느 순서로** 쓰는지만 정한다.

## 1. 공통화 근거

dotnet 가이드에서 언어 고유 표현(ASP.NET 타입, `IZLink*`, 패키지 이름 등)이 어디에 있는지
실측했다. 전체로는 코드블록 안 421회, 산문·표 246회다. 이 246회를 챕터별 산문 줄 수로
나누면 **대부분 챕터에서 0~10%**가 나온다. 개념과 동작 설명은 언어가 바뀌어도 그대로라는
뜻이다. 다섯 벌로 나눠 쓰면 같은 문장을 다섯 번 유지해야 하고, 한 곳을 고칠 때 나머지
넷이 어긋난다.

더 중요한 이유가 하나 있다. core 가이드는 코드를 문서에 적지 않고 **실제 샘플 파일에서
스니펫으로 끌어온다**. dotnet 가이드 검수에서 나온 결함 — 제거된 API를 챕터째 문서화,
join 호출 형태 오류, 새 terminal 미반영 — 은 전부 **복붙된 코드가 소스와 어긋난** 유형이다.
스니펫 방식은 이 계열을 구조적으로 막는다.

## 2. 공통과 언어별의 경계

산문 언어 의존도로 갈랐다.

**공통 정본 (12장)** — 산문 의존도 0~10%. 언어 차이는 코드 탭으로 흡수한다.

| 챕터 | 산문 의존도 |
| --- | --- |
| concepts · backpressure · samples · e2e-testing | 0~1% |
| actor-spot · actor-session · spot · location · alternative | 2~7% |
| channel-messaging · stream · operations | 8~10% |

**언어별 (5장)** — 내용 자체가 갈린다.

| 챕터 | 갈리는 이유 |
| --- | --- |
| overview | 산출물·패키지·호스팅 모델 서술이 언어마다 다르다 |
| getting-started | 설치가 NuGet · CMake/vcpkg · npm · Gradle로 절차가 다르다 |
| monitoring | telemetry 표면이 다르다(.NET `ActivitySource`·`Meter` 등). 산문 의존도 24% |
| interface-catalog | 언어별 타입 색인이 본질이다. 산문 의존도 41% |
| options | 옵션 이름과 기본값 표기가 언어 표면을 따른다 |

**cpp 전용 (3장)** — DI 컨테이너 · configuration · HTTP hosting. 다른 언어에 대응이 없다.

유지 대상이 `12 + 5×5 + 3 = 40장`이다. 전면 분리하면 `5×17 + 3 = 88장`이다.

## 3. 파일 배치와 이름

### 3.1 배치

`common/`에 정본을 두는 것은 이 저장소가 이미 쓰는 방식이다 — `common/spec`,
`common/sample`이 정본이고 언어별 문서는 실제 차이가 확인된 경우에만 둔다
([언어별 표현 기준](../framework/common/sample/languages/README.ko.md)).

```text
framework/doc/framework/
  common/guide/server/          공통 정본 12장 (언어 탭 포함)
  dotnet/guide/server/          언어별 5장 + 진입점 README
  cpp/guide/server/             언어별 5장 + cpp 전용 3장 + 진입점 README
  java/guide/server/            언어별 5장 + 진입점 README
  kotlin/guide/server/          언어별 5장 + 진입점 README
  node/guide/server/            언어별 5장 + 진입점 README
```

### 3.2 파일 이름 — 번호는 식별자다

현재 dotnet 가이드의 01~17 번호를 그대로 쓴다. **번호는 그 챕터를 가리키는 안정된
식별자이지 읽는 순서가 아니다.** 순서는 §4의 진입점이 정한다. 언어별 문서는 공통 축의
같은 번호를 쓰므로, 한 언어의 `server/` 디렉터리 안에서 번호가 겹치지 않는다.

| 번호 | 파일 | 위치 |
| --- | --- | --- |
| 01 | `01-overview.ko.md` | 언어별 |
| 02 | `02-getting-started.ko.md` | 언어별 |
| 03 | `03-concepts.ko.md` | **공통** |
| 04 | `04-backpressure.ko.md` | **공통** |
| 05 | `05-channel-messaging.ko.md` | **공통** |
| 06 | `06-spot.ko.md` | **공통** |
| 07 | `07-actor-spot.ko.md` | **공통** |
| 08 | `08-actor-session.ko.md` | **공통** |
| 09 | `09-stream.ko.md` | **공통** |
| 10 | `10-location.ko.md` | **공통** |
| 11 | `11-monitoring.ko.md` | 언어별 |
| 12 | `12-operations.ko.md` | **공통** |
| 13 | `13-interface-catalog.ko.md` | 언어별 |
| 14 | `14-samples.ko.md` | **공통** |
| 15 | `15-e2e-testing.ko.md` | **공통** |
| 16 | `16-options.ko.md` | 언어별 |
| 17 | `17-alternative.ko.md` | **공통** |

**cpp 전용 3장은 축 뒤 번호를 쓴다.** 지금 `04-di-container`·`05-configuration`·
`06-http-hosting`인데 그대로 두면 공통 축 04·05·06과 같은 번호가 두 뜻으로 쓰인다.
개명한다.

| 지금 | 바꿀 이름 |
| --- | --- |
| `04-di-container.ko.md` | `18-di-container.ko.md` |
| `05-configuration.ko.md` | `19-configuration.ko.md` |
| `06-http-hosting.ko.md` | `20-http-hosting.ko.md` |

번호가 뒤로 가도 cpp 독자가 뒤에 읽는다는 뜻은 아니다. cpp 진입점은 이 셋을 03 다음에
배치한다(§4).

## 4. 읽는 순서

공통 파일 하나를 다섯 언어가 공유하므로 파일 번호가 언어별 순서를 표현할 수 없다.
순서는 각 언어의 진입점 README와 사이트 nav가 제시한다.

java · kotlin · node 진입점은 01~17을 번호 순서 그대로 제시한다.

**cpp 진입점**은 DI·configuration·HTTP hosting이 기초에 해당하므로 앞으로 당긴다.

| cpp가 제시하는 순서 | 파일 |
| --- | --- |
| 1 · 2 | `01-overview` · `02-getting-started`(언어별) |
| 3 | `03-concepts`(공통) |
| **4 · 5 · 6** | **`18-di-container` · `19-configuration` · `20-http-hosting`(cpp 전용)** |
| 7 ~ 17 | `04-backpressure` ~ `17-alternative`(공통 + 언어별) |

**따라오는 결과 셋.**

- 공통 파일에는 `framework-adapter-nav` prev/next 마커를 둘 수 없다. 언어마다 앞뒤가
  다르기 때문이다. 순서 안내는 진입점 README와 사이트 nav가 맡는다. 언어별 장에는
  기존대로 nav를 유지한다.
- 문서 회귀 테스트(`Regression.cs`)가 검사하는 파일 목록과 nav 규칙을 새 구조에 맞게
  고쳐야 한다.
- **공통 정본은 언어별 문서를 링크하지 않는다.** 대상 언어가 정해지지 않아 상대 경로가
  성립하지 않기 때문이다. 아래 표기를 쓴다. 이 표기를 링크로 되돌리지 않는다.
  **생성 단계가 이 표기를 그 언어의 실제 링크로 바꾼다**(§4.1) — 소스에서만 표기로 두면
  되고, 독자는 링크를 본다.

| 가리키는 대상 | 공통 정본에서 쓰는 표기 |
| --- | --- |
| 언어별 장(01 · 02 · 11 · 13 · 16) | `` `11. Monitoring` 장 `` — 장 번호는 언어 공통 식별자다 |
| 언어별 공개 계약 spec | `common/spec/server/languages/README.ko.md` 하나로 모은다 |
| 언어별 샘플 디렉터리 | 링크 없이 이름만 적는다 |

이 규약은 `CommonGuideNarrative_DocumentsExist_AndCarryNoLanguageNav`가 강제한다.
탭 안 코드가 언어별 경로를 담는 것은 정상이므로 검사는 링크 대상만 본다.

### 4.1 공통 정본은 소스다 — 읽는 자리는 언어별 생성판

**탭을 읽는 자리로 두지 않는다.** 탭 방식은 한 파일에 다섯 언어를 담으므로 독자가 자기
언어와 무관한 코드를 **60~67% 함께 본다**. 장마다 탭 띠가 끼어들어 흐름도 끊긴다.

| 장 | 공통 소스 | Java 독자가 보는 무관 코드 |
| --- | ---: | ---: |
| 05 Channel Messaging | 2,642줄 | 1,604줄 (60%) |
| 06 Spot | 2,489줄 | 1,617줄 (64%) |
| 09 STREAM | 661줄 | 444줄 (67%) |

그래서 `common/guide/server/`는 **소스**로만 두고, 읽는 자리는
`<lang>/guide/server/`에 생성한다. `doc/site/scripts/generate_language_guides.py`가
그 언어의 탭만 남겨 만든다. 06장 기준 2,489줄 → **833줄**이고 탭이 없다.

생성이 하는 일 넷.

| 하는 일 | 왜 |
| --- | --- |
| 대상 언어 탭만 남기고 들여쓰기를 푼다 | 무관한 코드를 걷어낸다 |
| `` `11. Monitoring` 장 `` 표기를 실제 링크로 바꾼다 | 대상 언어가 정해졌으므로 링크가 성립한다. **소스로는 못 하는 일이다** |
| 그 언어의 읽는 순서로 앞뒤 nav를 붙인다 | 순서는 각 언어 README의 표가 소유한다(§4) |
| 생성 파일 표시를 머리에 붙인다 | 손으로 고치면 다음 생성에서 지워진다 |

**상대 링크는 손대지 않는다.** `common/guide/server/`와 `<lang>/guide/server/`는 깊이가
같아 `../../../common/spec/...`가 그대로 성립한다.

따라오는 규약 셋.

- **생성물을 저장소에 커밋한다.** 링크·앵커 게이트가 전부 "저장소에서 읽힌다"를 전제한다.
  CI가 `--check`로 소스와 일치하는지 확인한다.
- **사이트는 공통 가이드를 싣지 않는다.** `exclude_docs`에 `common/guide/`를 넣는다.
  nav는 언어별 완전판 다섯 벌이다.
- **생성판을 직접 고치지 않는다.** `DotNetGuideNarrative_DocumentsExist_AndAreWellFormed`가
  생성 표시를 확인한다.

## 5. 탭과 스니펫 규약

core 가이드와 같은 방식을 쓴다. 라벨은 framework가 지원하는 다섯 언어다.

````markdown
=== "C#/.NET"

    ```csharp
    --8<-- "framework/languages/dotnet/samples/TicTacToe/Server/Api/Handlers/CreateGameHttpHandler.cs:doc-create"
    ```

=== "C++"

    ```cpp
    --8<-- "framework/languages/cpp/samples/TicTacToe/server/api/create_game_handler.cpp:doc-create"
    ```
````

| 라벨 | 확장자 |
| --- | --- |
| `C#/.NET` | `.cs` |
| `C++` | `.cpp` |
| `Java` | `.java` |
| `Kotlin` | `.kt` |
| `Node/TypeScript` | `.ts` |

샘플 파일에는 발췌 구간을 마커로 심는다. 주석 문법만 언어를 따른다.

```csharp
// --8<-- [start:doc-create]
...가이드에 실릴 구간...
// --8<-- [end:doc-create]
```

**규칙 넷.**

1. **코드 블록은 두 종류다.** 아래 §5.1을 따른다.
2. **한 탭 블록에는 다섯 언어를 모두 담는다.** 예외는 §6의 미구현 샘플뿐이다.
3. **마커 이름은 용도별로 나눈다.** 한 파일에서 여러 곳을 인용하면 `:doc-register`,
   `:doc-handler`처럼 구분한다.
4. **언어별 설명은 코드 주석으로 붙인다.** 아래 §5.2를 따른다.

### 5.1 샘플 인용과 교육용 예제

core 샘플은 애초에 문서용 자립 예제라 모든 코드를 스니펫으로 끌어올 수 있다. framework
샘플은 실제 서비스 코드라 사정이 다르다. 공통 12장의 코드 블록 88개를 보면 상당수가
`GameRoom`·`PriceService`처럼 **개념 하나를 보이려고 만든 교육용 예제**이고, 샘플에 대응
코드가 없다. 그래서 두 종류를 나눈다.

| 종류 | 쓰는 자리 | 형태 | 검증 |
| --- | --- | --- | --- |
| **샘플 인용** | 실제로 도는 전체 흐름을 보일 때 | 샘플에 마커를 심고 스니펫으로 인용 | 경로·마커 존재(§11 2번) |
| **교육용 예제** | 개념 하나를 최소 코드로 보일 때 | 탭 안에 직접 적는다 | 식별자·호출 형태 대조(§11 5·6번) |

**둘 다 언어 탭을 쓴다.** 교육용 예제라고 한 언어만 두지 않는다.

**교육용 예제를 쓰는 조건**은 하나다 — 그 개념을 보이는 데 샘플 코드가 너무 크거나
곁가지가 많을 때. 샘플에 딱 맞는 구간이 있으면 스니펫을 쓴다. 판단이 갈리면 스니펫 쪽으로
기운다.

교육용 예제는 복붙이므로 소스와 어긋날 수 있다. 그래서 §11의 5·6번(식별자·호출 형태
대조)이 공통 정본에도 적용된다 — 원래는 언어별 장만 대상이었다.

**둘을 바꿔치기하지 않고 나란히 둔다.** 탭을 채우고 나서 확인한 것이다 — 교육용 예제를
샘플 인용으로 **대체하면 가르치던 내용이 사라진다.** 예를 들어 06-spot §3의 Create 예제는
생성 호출과 **거절 처리**를 함께 보이는데, 샘플의 마커 구간에는 거절 처리가 없다.

그래서 이렇게 한다.

| 자리 | 무엇을 두나 |
| --- | --- |
| 개념을 설명하는 본문 | 교육용 예제. 최소 코드로 그 개념만 |
| 그 아래 "샘플에서 보기" | 같은 호출을 실제 샘플에서 인용한 스니펫 |

읽는 쪽은 최소 예제로 개념을 잡고, 바로 아래에서 실제로 도는 코드를 본다. 스니펫 쪽은
경로·마커 존재를 체커가 검증하므로 소스와 어긋나면 CI가 잡는다.

**스니펫으로 바꿀 후보를 고르는 기준**은 하나다 — 다섯 언어 샘플에 **같은 호출이 다
있는가.** 하나라도 없으면 그 자리는 교육용 예제만 둔다. 마커는 언어별로 따로 심어야
하므로 한 언어만 빠져도 탭이 비게 된다.

### 5.2 산문은 공통, 언어별 설명은 코드 주석

역할 분담이 이 방식의 전부다.

| | 담는 것 | 담지 않는 것 |
| --- | --- | --- |
| 탭 밖 산문 | 개념, 동작 순서, 판단 기준, 제약 | 특정 언어의 타입·메서드 이름 |
| 스니펫 코드 | 그 언어의 실제 호출 | — |
| **스니펫 안 주석** | **코드만으로 안 보이는 그 언어의 제약과 이유** | 개념 설명, 다섯 언어 공통 규칙 |

산문에서 타입을 불러야 할 때는 **역할 이름**을 쓴다 — "등록 진입점", "요청 handler
interface", "비동기 완료 terminal". 정확한 이름은 코드와 언어별 장이 소유한다.

**언어별 단서는 주석으로 붙인다.** 스니펫이 샘플 소스에서 오므로 주석이 그대로 문서에
실린다. 설명이 해당 줄 옆에 붙고 언어별로 자동 분리되므로, 탭 안에 산문 문단을 따로 둘
필요가 없다.

```kotlin
// --8<-- [start:doc-create]
val created = spots.create(GAME_SPOT)
    .inMesh(MESH)
    .request(CreateGameReq(name))
    .await()          // 코루틴 안에서만 호출한다. 블로킹 문맥에서는 submit()을 쓴다.
// --8<-- [end:doc-create]
```

**따라오는 규칙 — 마커 구간의 주석은 문서다.** 샘플 주석을 유지보수 메모로 쓰던 관행이
그대로 노출된다. 마커 구간 안의 주석은 독자를 대상으로 쓰고, 문서 리뷰 대상에 포함한다.
구간 밖 주석은 지금처럼 자유롭게 쓴다.

**탭 안 산문은 예외로만 둔다.** 주석으로 붙일 자리가 없는 경우(예: 그 언어에서 이
호출을 아예 다른 방식으로 하는 경우)에만 한두 줄 허용한다. 그보다 길어지면 두 자리 중
하나로 보낸다.

| 성격 | 가는 곳 |
| --- | --- |
| 그 언어에서 한 번 이해하면 되는 것(코루틴 규칙, DI 주입 방식, 이벤트 루프) | 언어별 장(§2) |
| 다섯 언어에 공통인 규칙 | 탭 밖 산문 |

**cpp의 DI가 대표 사례다.** handler가 의존성을 어떻게 받는지는 cpp 전용 DI 장이 한 번
설명하고, 공통 정본의 탭에는 그 방식으로 작성된 코드와 짧은 주석만 실린다.

## 6. 공통 샘플

일곱 종이 공통 축이다. 공통 정본과 언어별 장 모두 이 일곱만 참조한다.

| 샘플 | dotnet | cpp | java | kotlin | node |
| --- | :-: | :-: | :-: | :-: | :-: |
| TicTacToe · Bingo · SupportChat · DeliveryDispatch · ShoppingMall · GameQuest | ✅ | ✅ | ✅ | ✅ | ✅ |
| ZoneWorld | ✅ | 구현 예정 | 구현 예정 | 구현 예정 | ✅ |

ZoneWorld는 전 언어에 추가한다. 브라우저 client는 `shared_sample/zoneworld`의 TypeScript
하나를 공유하므로 각 lane이 추가할 것은 server와 headless 시나리오 client다.

**미구현 샘플의 탭 예외.** 구현이 없는 언어의 탭은 비워 둘 수 없다 — 빈 탭은 독자에게
"이 언어에서는 안 된다"로 읽힌다. 그 자리에는 **구현 예정 안내 한 줄**을 넣고, §11의 탭
완전성 검사는 이 형태를 통과로 인정한다.

````markdown
=== "Java"

    ZoneWorld는 Java에 아직 구현되지 않았다. 시나리오는 위 설명을 따른다.
````

공통 sample 문서가 현재 ZoneWorld를 ".NET과 Node.js가 제공"으로 규정한다. 구현이 붙으면
**그 문서를 먼저 고치고** 가이드가 따라간다.

## 7. 사이트와 배포

### 7.1 도메인과 프로젝트

framework를 **최상위**로 올리고 core를 같은 도메인의 **`/core/` 구역**으로 내린다.
서브도메인을 쓰지 않으므로 GitHub Pages 하나로 둘 다 낼 수 있다(§7.3).

| | framework | core |
| --- | --- | --- |
| 주소 | **`zlink.systems`** | **`zlink.systems/core/`** |
| mkdocs 프로젝트 | `framework/doc/site/` | `doc/site/` |
| 담는 것 | 공통 12장 · 언어별 5장 · cpp 전용 4장 · 샘플 · 공통 스펙 | 소켓 패턴 · C API · binding · internals |
| 상대편 참조 | core 구역으로 절대 URL | framework로 절대 URL |

두 사이트는 각자 nav·검색 색인·i18n을 갖는다. 도메인만 공유하므로 독자에게는 상단
링크로 오가는 두 구역으로 보인다. framework 가이드가 transport·TLS·socket option을
설명할 때는 `https://kairos-code-dev.github.io/zlink/core/ko/guide/04-transports/`로 건다.

### 7.2 기존 링크 처리

지금 `zlink.systems/guide/*`·`/api/*`·`/internals/*`·`/ko/*`가 core를 가리킨다.
최상위를 framework로 바꾸면 이 경로가 전부 깨진다.

**옛 경로마다 stub을 깐다** — `doc/site/scripts/make_core_redirects.py`가 core 빌드
결과를 훑어 같은 경로에 `<meta http-equiv="refresh">` 문서를 만들고 `/core/`의 같은
자리로 보낸다. `<link rel="canonical">`을 함께 두어 크롤러가 새 주소를 잇는다.
GitHub Pages는 정적 호스팅이라 301을 낼 수 없다.

framework 최상위는 `dotnet/`·`cpp/`·`java/`·`kotlin/`·`node/`·`common/`이라 지금은
겹치지 않는다. 스크립트는 framework 페이지가 이미 있는 자리를 덮지 않는다.

### 7.3 배포 선택

**한 도메인, 두 구역으로 간다.** GitHub Pages는 저장소당 사이트 하나, 커스텀 도메인도
사이트당 하나다. 서브도메인을 쓰려면 저장소를 하나 더 두거나 호스팅을 옮겨야 했다.
구역으로 나누면 그 비용이 사라진다 — 사이트 하나에 두 빌드 결과를 겹쳐 올린다.

`docs.yml`이 이 순서로 짓는다.

1. framework 사이트를 `framework/doc/site/site`에 짓는다.
2. core 사이트를 `mkdocs build -d ../../framework/doc/site/site/core`로 그 안에 짓는다.
3. `make_core_redirects.py`가 옛 최상위 경로에 stub을 깐다(§7.2).
4. 합쳐진 `framework/doc/site/site`를 Pages에 올린다.

**순서가 뒤집히면 안 된다.** mkdocs는 `site_dir`를 비우고 시작하므로 framework를 나중에
지으면 core 결과가 지워진다.

포기한 것은 통합 검색과 통합 사이드바다. 두 구역은 각자 색인을 갖고, 서로는 상단
링크와 홈의 관련 문서 표로 오간다. 한 사이트로 병합하려면 `mkdocs-monorepo-plugin`과
i18n 설정 병합이 필요한데(core는 영어 기본, framework는 한국어 전용), 얻는 것에 비해
드는 위험이 크다.

### 7.4 nav와 미러

framework 사이트의 첫 화면은 "무엇을 만드나 → 시작하기 → 개념 → 샘플" 순서다. 공통 장은
한 벌이므로 nav에 다섯 번 등장시키지 않고 독자가 페이지 안에서 언어 탭으로 전환한다.
언어별 5장과 cpp 전용 장만 언어 그룹으로 묶고, 각 언어 그룹의 순서는 §4의 진입점이 정한
순서를 그대로 옮긴다. core로 가는 입구는 상단 링크 하나로 둔다.

**미러를 두지 않는다.** `docs_dir`을 정본 트리(`framework/doc/framework`)로 직접 잡는다.
core 사이트는 `doc/site/docs`로 복사하는 미러라 양방향 drift가 계속 문제였고, 여기서는
같은 구조를 만들지 않는다. 따라온 성질 셋.

- 문서끼리의 상대 링크가 repo에서든 사이트에서든 같은 대상을 가리킨다. 이번에 고친
  spec·sample 교차 링크가 사이트에서도 그대로 산다.
- 정본 트리 밖으로 나가는 링크(core guide, repo 루트 license)는 사이트 페이지가 없으므로
  **절대 URL로 적는다.** `https://kairos-code-dev.github.io/zlink/core/ko/guide/...` 형태다.
- 사이트에 올리지 않을 디렉터리는 `exclude_docs`로 뺀다. 현재 `perf/`가 그렇다.

i18n은 `mkdocs-static-i18n` suffix 구조를 쓰되 framework 가이드는 한국어만 있으므로
`ko` 하나를 default locale로 둔다. 이렇게 해야 `.ko` 없는 URL로 나온다.

`toc`에 unicode slugify가 반드시 있어야 한다. 기본 slug는 한글 제목에서 non-ASCII를
버려 문서 안 anchor 링크가 전부 깨진다.

**발행 범위는 `exclude_docs`가 정한다.** mkdocs는 nav에 없어도 `docs_dir` 아래 파일을
전부 빌드한다. §7.1이 정한 범위(가이드 · 스펙 · 샘플)만 남기려면 명시적으로 빼야 한다.
현재 제외 대상은 `perf/` · `*/internals/` · `common/e2e/`다.

**따라온 부채 — 정본 트리 밖을 가리키는 기존 링크.** spec 문서가 소스 파일(`.api.txt`,
schema JSON, 샘플 README)이나 repo 루트의 원칙 문서를 상대 경로로 가리키는 자리가 있다.
repo에서는 열리지만 사이트에는 대응 페이지가 없어 빌드 경고 27건으로 남는다. 가이드 20장과
언어 진입점은 이번에 전부 정리해 경고가 0이다. 나머지는 사이트를 공개하기 전에 절대 URL로
바꾸거나 nav에서 빼야 한다. 지금 배포하지 않으므로 급하지 않지만, 배포를 켜는 쪽이
떠안지 않도록 여기 적어 둔다.

`docs.yml`은 지금 `doc/site/**` 변경에만 반응한다. 두 사이트를 각각 빌드·배포하고 탭
체커를 양쪽에 돌리도록 고친다.

## 8. 샘플 마커와 체커

**8.1 샘플에 발췌 마커 심기.** core 샘플은 애초에 문서용 자립 예제지만, framework 샘플은
실제 서비스 코드라 "가이드에 실을 크기"의 구간이 없다. 공통 샘플 7종 × 5언어에 마커를
심어야 한다. 이 작업을 건너뛰면 결국 복붙으로 돌아간다.

**미리 다 심지 않는다.** 어떤 구간이 필요한지는 챕터를 쓰면서 정해진다. Phase 1이 dotnet
샘플에 심으면서 구간 목록을 만들고, Phase 2에서 각 언어 lane이 같은 목록을 자기 샘플에
심는다.

**8.2 탭 체커 확장.** `doc/site/scripts/check_doc_tabs.py`는 core용 9언어 라벨을 강제한다.
framework 5언어 집합을 추가하고, §6의 미구현 안내 탭을 통과로 인정하도록 고친다.

## 9. Phase와 lane

문서 공통화가 본체다. 사이트·체커·배포는 그 결과를 내보내는 트랙이며 문서 작업을 막지
않는다.

**Phase 0 — 스파이크 (반나절)**

12장을 다 바꾼 뒤에 렌더가 깨지면 되돌리기가 비싸다. **한 장으로 통하는지만 먼저
확인한다.**

1. 샘플 한 곳(예: TicTacToe dotnet의 Spot 생성 handler)에 `:doc-create` 마커를 심는다.
2. 공통 정본 한 장을 만들어 그 스니펫을 탭으로 인용한다.
3. `framework/doc/site/`에 최소 mkdocs 설정을 두고 로컬 빌드로 확인한다.

확인할 것은 셋이다 — 스니펫 경로가 repo 루트 기준으로 풀리는가, 탭이 언어별로 전환되는가,
마커 구간이 의도한 크기로 잘리는가.

**Phase 1 — 공통 정본 12장 (본체)**

기존 dotnet 문서를 `common/guide/server/`로 승격한다. 챕터마다 셋을 한다.

| 작업 | 내용 |
| --- | --- |
| 산문 중립화 | .NET 고유 이름을 역할 이름으로 바꾼다(§10). 이 단계의 핵심이다 |
| 스니펫 전환 | 코드블록을 샘플 마커 인용으로 바꾼다. 필요한 구간을 그때 정의하고 dotnet 샘플에 심는다 |
| 탭 골격 | 다섯 언어 탭을 만들고 dotnet만 채운다. 나머지 넷은 Phase 2에서 채워진다 |

스니펫 구간 목록은 이 과정의 **산출물**이지 선행 조건이 아니다. 챕터를 쓰면서 어떤 구간이
필요한지 정해지고, 그 목록이 Phase 2 lane들의 작업 지시가 된다.

**Phase 2 — 언어별 병렬**

| lane | 하는 일 | 시작 조건 |
| --- | --- | --- |
| **cpp** | 마커 심기 → cpp 탭 채우기 → 언어별 5장 + cpp 전용 3장 개명·작성 + 진입점 | Phase 1이 그 챕터를 끝낸 뒤 |
| **java** | 마커 → java 탭 → 언어별 5장 + 진입점 | 〃 |
| **node** | 마커 → node 탭 → 언어별 5장 + 진입점 | 〃 |
| **kotlin** | 마커 → kotlin 탭 → 언어별 5장 + 진입점 | **java lane이 그 챕터를 끝낸 뒤** |

**챕터 단위로 흐른다.** Phase 1이 한 챕터를 끝내면 cpp·java·node가 그 챕터의 탭을 동시에
채우고, kotlin은 java가 끝낸 챕터를 이어받는다. lane 전체가 끝나기를 기다리지 않는다.

탭 채우기는 서로 독립이라 병렬이다. 같은 파일의 다른 탭을 건드리므로 충돌 지점만
조율한다. 언어별 5장도 서로 독립이다. 각 lane이 §10 매핑표의 자기 칸을 이때 채운다.

**병행 트랙** — 사이트 골격(§7)은 Phase 1과 나란히, 체커 확장(§8.2)은 Phase 2 시작
전까지, core 분리·리다이렉트·CI는 배포 방식이 정해진 뒤. 셋 다 문서 작업을 막지 않는다.

### 9.1 진행 상태 (2026-07-31)

**Phase 0 완료.** 스니펫 경로·탭 전환·마커 구간을 로컬 빌드로 확인했다. 확인 결과는
사이트 설정에 반영했고 스파이크 페이지는 걷어냈다. 이때 잡은 것 하나 — 기본 slugify가
한글 제목을 버려 문서 안 anchor가 전부 깨진다. `toc`에 unicode slug가 필수다.

**Phase 1 완료.** 12장을 `common/guide/server/`로 승격하고 산문을 중립화했다. 코드
블록 92개가 다섯 언어 탭 안에 있고 dotnet 탭만 채워져 있다. 나머지 넷은 "준비 중"
탭이다. 진입점은 다섯 언어 모두 만들었다.

한 가지가 계획과 다르다 — **스니펫 전환은 하지 않았다.** §5.1이 정리한 대로 공통 12장의
코드 블록 대부분이 개념 하나를 보이려고 만든 교육용 예제라 샘플에 대응 코드가 없다.
지금은 전부 교육용 예제(탭 안 인라인)로 두었다. 샘플 인용으로 바꿀 구간은 Phase 2에서
각 lane이 자기 언어 코드를 채우면서 함께 정한다. 그때 §11의 5·6번(식별자·호출 형태
대조)이 공통 정본에도 걸린다.

**Phase 2 — 탭 채우기 완료, 언어별 장 남음.**

공통 12장의 탭 그룹 92개를 네 언어로 모두 채웠다. "준비 중" 탭은 하나도 남지 않았다.
예제는 각 언어의 실제 소스·샘플·공개 계약 spec에서 확인한 표면만 쓴다.

채우면서 세운 게이트가 하나 늘었다 — `doc/site/scripts/check_guide_identifiers.py`는
탭 코드의 호출 이름과 enum 상수를 그 언어의 실제 표면과 대조한다. 표기 관례가 언어마다
달라(C++ snake_case · Java SCREAMING_SNAKE · Node PascalCase) 옮겨 적을 때 틀리기 쉬운
자리를 잡는다. 이 게이트가 fanout subscriber endpoint 호출의 언어별 이름 불일치도
드러냈고, 그 계약은 `connect(endpoint)`로 통일했다.

**언어별 장도 완료했다.** 다섯 언어 진입점 README에 "준비 중" 표기가 하나도 없다.

- cpp — 01 · 02 · 11 · 13 · 16 + 전용 18 · 19 · 20 · 21
- java — 01 · 02 · 11 · 13 · 16
- kotlin — 01 · 02 + 차이만 쓰는 11 · 13. 16은 Java 장을 가리킨다
- node — 01 · 02 · 11 · 13 · 16

kotlin은 Java 런타임을 공유하므로 **다른 지점만 쓰고 나머지는 Java 문서를 가리킨다.**
같은 내용을 두 벌로 두면 한쪽이 반드시 낡는다.

쓰면서 확인한 spec↔구현 차이는
[가이드 집필 중 발견한 구현·샘플 갭](v11.0/guide-authoring-implementation-gaps.ko.md)에
쌓았다. 문서를 고치는 대신 계약을 기준으로 적고 갭으로 남기는 방식이다.

**스니펫 전환도 완료했다.** 다섯 언어 샘플 전부에 같은 호출이 있는 자리 9곳을 골라
마커를 심고 공통 정본에서 인용한다 — `doc-create` · `doc-timer-handler` ·
`doc-request-handler` · `doc-relocation-adapter` · `doc-entry-spot` ·
`doc-actor-packet-handler` · `doc-join-defer` · `doc-session` · `doc-session-auth`.

기준은 하나다 — **다섯 언어 샘플에 같은 호출이 다 있어야 후보다.** 한 언어라도 없으면
그 탭만 인라인으로 남아 탭 사이 모양이 어긋난다.

교육용 예제를 스니펫으로 **대체하지 않았다.** 샘플 코드는 실제 운영 코드라 가르치려는
개념 하나만 보이지 않는다. 예컨대 06장 §3은 Spot 생성 거절 처리를 가르치는데 샘플의
대응 구간에는 거절 경로가 없다. 그래서 샘플 인용을 위에 두고 "최소 형태로 보면 이렇다"로
교육용 예제를 이어 붙인다.

**게이트 3(산문 중립성)도 스크립트로 돌렸다.** 공통 12장의 탭 밖 산문 2,415줄에서 .NET
이름 14자리를 찾아 역할 이름으로 바꿨다 — `[ZLinkRequest]` 계열 attribute 표기,
`ZLinkConfigurationException`, `ZLinkTimerTick` · `ZLinkTimerOptions`, `ZLinkMessage`,
`ZLinkPeerStatus.State`, `ZLinkProtobufCodec`, `ZLinkFrameworkRuntimeState`다.

산문이 이런 이름을 부르면 나머지 네 언어 독자에게는 없는 이름이 된다. 특히 host
lifecycle 상태처럼 **값 이름의 표기까지 언어를 따르는 자리**(PascalCase ↔
SCREAMING_SNAKE)는 이름을 적는 대신 뜻을 적고 "표기는 언어를 따른다"를 덧붙였다.

**게이트 6도 끝냈다.** 두 축으로 돌렸다 — handler 시그니처 모양과 호출 arity다.

terminal 이탈은 **0건**이다. 다섯 언어 탭 어디에도 다른 언어의 terminal(`.Async` ·
`.submit` · `.await`)이 섞이지 않았다.

**arity 대조에서 Java · Kotlin의 등록 표면이 통째로 틀린 것이 드러났다.** 가이드는
`addPacket` · `addSubscribe` · `addActorPacket`을 registry 메서드로 적었는데,
`ZLinkSpotHandlerRegistry`에는 **`addHandler(Class<?>)` 하나뿐**이다. 무엇을 받는
handler인지는 구현한 interface와 annotation(`@ZLinkSpotSubscription` ·
`@ZLinkSpotActorSend` · `@ZLinkSpotActorRequest`)이 정한다. `HandlerContractTest`가
나머지 이름이 **없어야 한다고** 단언하고 있었다 — 즉 문서만 다른 언어 표면을 베껴 온
자리였다. 06장 표 두 개와 코드 두 개, 07장 코드 두 개를 고쳤다.

같은 대조에서 셋을 더 잡았다.

| 자리 | 무엇이 틀렸나 |
| --- | --- |
| Java · Kotlin `tryHandle` | 인자 둘로 적었다. 계약은 `(context, dispatch, payload)` 셋이다(08 · 09장 네 자리) |
| Java · Kotlin 구독 등록 | `addSubscribe(...)` 자체가 없다. annotation + `addHandler`다 |

**여기서 방향을 한 번 잘못 잡았다가 되돌렸다.** C++ `add_subscribe`가 topic 하나만 받길래
가이드를 구현에 맞췄는데, [C++ Spot 공개 계약](../framework/common/spec/server/languages/cpp/interfaces/04-spots.ko.md)은
`(channel_name, topic)` 둘을 선언한다. **계약이 기준이므로 가이드를 되돌리고 구현 갭으로
넘겼다**(handoff G10). 게이트 6은 구현이 아니라 **소유 spec**과 대조해야 한다 — 구현
불일치를 문서로 흡수하면 갭이 사라진 것처럼 보인다.

나머지 셋은 spec을 확인해 편집이 맞았다. Java `ZLinkSpotHandlerRegistry`는 spec도
`addHandler` 하나이고, Java `tryHandle`은 spec도 인자 셋이며, C++ typed session handler는
spec이 침묵해 구현이 유일한 근거다.

**게이트 6은 C++부터 돌렸다.** 방법은 이렇다 — 탭 코드에서 handler 선언과 `handle`
파라미터 수를 뽑아, 그 언어의 실제 계약(.NET·Java·Node는 interface 선언, C++은 concept과
`static_assert`)과 대조한다. `.NET` 8쌍, Java·Kotlin·Node 각 12쌍은 **전부 맞았다** —
`.NET`이 `CancellationToken` 하나를 더 받는 차이까지 규칙대로였다.

**C++에서 셋이 틀렸다.** 세 자리 다 이름은 실재하는데 **모양이 계약과 달라 컴파일되지
않는** 유형이다. 게이트 5(식별자 대조)가 잡지 못하는 자리가 정확히 여기다.

| 자리 | 무엇이 틀렸나 |
| --- | --- |
| 09장 typed session handler | `handle (stream, dispatch, payload)` 3개로 적었다. concept은 `handle (stream, payload)` 둘이고 dispatch context는 raw `on_packet` 경로에만 온다 |
| 06장 §4.2 per-call scope | Spot request를 handler class로 적었다. C++은 Spot member 함수이고 호출마다 여는 scope 표면도 없다 |
| 08장 actor packet handler | 자유 함수에 spot을 인자로 받았다. Spot member 함수라 spot은 `this`이고 인자는 셋이다 |

두 번째는 `cpp/guide/server/18-di-container` §3이 *"Spot packet과 Actor payload handler는
Spot member function이므로 DI handler class로 등록하지 않는다"* 고 이미 못박아 둔
내용이었다. **언어별 장은 맞게 썼는데 공통 정본이 어긋난** 경우다.

**게이트 4를 12장 전부에 돌렸다.** 결과는 아래에 챕터별로 적었다. **산문에는 틀린 서술이
하나도 없었고 전부 "빠졌다"였다** — 소유권을 잘못 가리킨 자리 하나(10장 lease 제약)만
예외다. 코드 쪽은 사정이 달랐다(게이트 6).
빠진 것들의 성격은 한 갈래로 모인다. **개념과 사용법은 잘 적혀 있는데, 그 계약이 어떤
조건에서 다르게 동작하는지가 없었다.** 실패 조건, 호출 규칙, 어느 경로에만 적용되는
예외가 그것이다.

**게이트 4의 전제도 갖췄다.** 공통 12장이 각자 계약을 소유하는 스펙 문서를 머리에
밝힌다. 그전에는 아홉 챕터가 언어별 spec **목차**를 가리켜서 대조할 상대가 없었다.

| 챕터 | 소유 스펙 |
| --- | --- |
| 03-concepts | `02-overview` · `03-interaction-model` |
| 04-backpressure | `05-async-execution-policy` |
| 05-channel-messaging | `08-channel-messaging` · `09-client-server-channel` |
| 06-spot | `11-spot-model` · `12-spot-messaging` |
| 07-actor-spot | `14-actor-model` · `15-spot-actor` |
| 08-actor-session | `20-session-actor-dispatch` |
| 09-stream | `19-stream-session` |
| 10-location | `21-location-runtime` · `22-location-store-redis` |
| 12-operations | `24-runtime-monitoring` · `25-runtime-metrics` · `28-graceful-drain-handoff` |
| 14 · 15 · 17 | 없음. 안내가 본질인 챕터라 없다고 밝힌다 |

**게이트 4를 08장부터 시작했다.** `20-session-actor-dispatch`와 대조해 산문이 빠뜨린
계약 넷을 채웠다 — session:Actor 개수 비대칭(session은 여럿, Actor는 하나), relay가
Location Store를 다시 조회하지 않는다는 점, disconnect 통지의 all-settled·dedupe 규칙,
닫힌 session의 늦은 reply 처리다. **넷 다 "빠졌다"이지 "틀렸다"가 아니었다.**

**09장도 마쳤다.** `19-stream-session`과 대조해 다섯을 채웠다 — startup에서 막는 등록
오류 여덟 가지, 오류가 어디로 가는지 가르는 네 갈래, **handler filter가 session
dispatch에는 적용되지 않는다**는 점, recv loop를 표면에 두지 않은 이유, 응답에 packet
이름이 실리지 않고 타입은 호출자가 정한다는 점이다.

filter 항목이 특히 값이 있다. 다른 dispatch에 filter를 걸어 둔 독자는 session 경로에도
걸릴 것으로 읽는데, 계약은 반대다. 문서에 없으면 인증을 filter에 두고 통과했다고 믿는
사고가 난다.

**10장도 마쳤다.** `21-location-runtime`과 대조해 넷을 채웠다 — 어느 Store가 언제
필수인지(Location은 object role이 있으면 필수, Relocation은 relocation policy나 Instance
Spot factory가 있으면 필수), lease 값 넷을 묶는 부등식과 위반 시 startup error,
`StoreFailureGrace`가 **owner 자격을 연장하지 않는다**는 점, lease가 끊긴 host가 새
작업을 받지 않는 범위다. 옵션 표에 빠져 있던 `OwnerLeaseRenewTimeout` ·
`OwnerLeaseFencingMargin`도 넣었다 — 부등식에 나오는 값이 표에 없었다.

여기서는 **소유권 오귀속**도 하나 고쳤다. lease 제약을 "언어별 계약을 따른다"고 넘겼는데
그 부등식은 `21-location-runtime` §4가 소유하는 공통 계약이다. 게이트 4가 아니면 드러나기
어려운 종류다.

**12장도 마쳤다.** `28-graceful-drain-handoff`와 대조해 둘을 채웠다. 둘 다 **배포
자동화가 반드시 알아야 하는데 문서에 없던** 것이다.

- **다시 부르거나 겹쳐 불렀을 때의 결과 일곱 가지.** 재시도가 의미 있는 결과는
  `Blocked` 하나뿐이고 `Relocated`에서 다시 부르면 최초 결과를 그대로 돌려준다.
  `Shutdown`은 target·capacity·Store 부재로 막히지 않으므로, 기다리던 `Relocate`가
  `Shutdown` 확정에 밀려 `Blocked`로 끝난다 — "먼저 옮기고 성공을 확인한 뒤 종료"를
  지켜야 하는 이유가 여기 있다.
- **전이 중에도 살아 있는 것.** `Relocating` · `Relocated` · `Draining`은 아무것도 안 받는
  상태가 아니다. 새로 시작하는 것만 막고 이미 수락한 request는 한 번만 끝난다.

**04장도 마쳤다.** `05-async-execution-policy`와 대조해 셋을 채웠다.

- **"보낼 자리를 기다리는 상한"이 전역 값 하나가 아니다.** 실제로 쓰이는 값은 그 호출이
  사용하는 socket이 소유한다. 특히 **STREAM reply는 호출자의 request timeout을 쓰지
  않는다.** 가이드는 루트 옵션 하나만 적어 두어 하나의 전역 값으로 읽혔다.
- **상한까지 기다리지 않고 즉시 실패하는 경우**가 있다. 대기 호출을 담아 두는 공간이
  다 찼을 때다. 1초로 설정했는데 즉시 실패하면 queue가 아니라 동시성을 봐야 한다.
- **기다리는 동안 target이 바뀔 수 있는 호출과 아닌 호출이 갈린다.** channel 이름 호출은
  자리 확보 전까지 후보를 다시 고르고, node·Spot·Actor 지정 호출은 대상을 유지한다.

셋 다 진단 방향을 바꾸는 종류다. 값 하나를 어디서 바꿔야 하는지, 즉시 실패가 무엇을
뜻하는지, 왜 어떤 호출만 다른 node로 흘러가는지가 문서에 없었다.

**07장도 마쳤다.** `14-actor-model` §3.1과 대조해 join 예약의 제약을 채웠다. 가이드는
`Defer()`가 무엇을 하는지와 왜 그 형태인지는 잘 설명했지만 **어디서 부를 수 있고 얼마나
부를 수 있는지**가 없었다.

- 부를 수 있는 자리와 없는 자리(factory · lifecycle callback · relocation adapter ·
  Instance Spot handler · 떼어낸 task는 `InvalidOperation`)
- 한도 — handler당 64개, request 1 MiB, 합계 8 MiB, reply 1 MiB, timeout 기본 5초.
  넘기면 그 자리에서 실패하고 **일부만 등록된 상태를 만들지 않는다**
- **예약한 Actor에 request를 보내면 순환 대기**가 되므로 제출 전에 거부한다
- 예약은 process 메모리에만 있어 crash 시 재생되지 않고, `Relocate` · `Shutdown`과
  겹치면 먼저 확정된 쪽을 따른다

순환 대기 항목이 특히 값이 있다. 예약과 request의 대상이 같은 Actor인지는 코드를 읽어서는
잘 안 보이는데, 문서에 없으면 그 오류를 만나고도 원인을 못 찾는다.

**03장도 마쳤다.** `02-overview` · `03-interaction-model`과 대조해 둘을 채웠다. 개념
설명은 충실했는데 **개념에서 코드로 넘어가는 다리**가 없었다.

- **무엇을 하려면 어디서 시작하나** — 여덟 갈래 시작 표면과 각각이 지정하는 대상.
  개념 장을 다 읽고도 "그래서 어느 객체를 잡느냐"에 답이 없었다.
- **framework가 맡는 것과 맡지 않는 것** — 주소 선택 · 재연결 · framing · codec ·
  reply correlation · backpressure queue는 안에서 처리하고, **인증 · quota · WAF ·
  공개 API 버전 관리 · 과금은 계약 범위가 아니다.** 도입 검토에서 반드시 나오는
  질문인데 어느 장에도 없었다.

**06장은 `11-spot-model` 쪽을 마쳤다.** 셋을 채웠다.

- **Spot 종류별 lifecycle callback 매트릭스.** 어느 Spot이 어느 callback을 받는지가
  없었다. 특히 **User Spot에 있던 Actor가 Entry Spot으로 돌아가도 Entry Spot의
  `OnCreateActor` · `OnActorJoin`은 불리지 않는다** — 복귀는 기본 membership이라 승인
  절차가 없다.
- **`OnClosing` 호출 조건.** 가이드에 한 줄뿐이었다. 세 이유가 Spot 종류마다 다 오지
  않고, **close가 실패하면 아예 불리지 않으며**, host shutdown에서는 member Actor가 아직
  살아 있는 상태로 실행된다.
- **Entry Spot 자체는 옮겨가지 않는다.** 옮겨지는 것은 거기 속한 Actor다. 그래서
  **Entry Spot에 담아 둔 상태는 host를 옮겨도 따라가지 않는다.**

**`12-spot-messaging` 쪽도 마쳤다.** 넷을 더 채웠다.

- **어느 queue에 무엇이 들어가나.** 특히 **Actor 앞 업무 message는 Spot queue를 거치지
  않고 Actor queue로 바로 간다.** Spot callback이 받아 넘겨주는 구조로 읽으면 설계가
  어긋난다. Instance Spot에 Actor membership이나 구독을 등록하려 하면 **등록 시점에**
  거부된다.
- **Spot handler에서 channel을 호출할 때의 범위.** 그 Spot을 소유한 MeshNode에 해당
  ChannelName이 없어도 **같은 process에 송신 경로가 있으면** 쓸 수 있고, 없으면
  `NotFound`다. 다른 process를 중계로 삼지 않는다. Spot 배치를 정할 때 함께 봐야 하는
  제약인데 문서에 없었다.
- **cold activation의 첫 message는 잃지 않는다.** durable하게 기록한 뒤 큐 맨 앞으로
  복원하며 별도 생성 request로 바뀌지 않는다.
- **실패해도 자동 재전송하지 않는다.** 다시 보내는 것은 새 operation이고 중복 실행
  처리는 보내는 쪽 책임이다.

**05장까지 마치면서 게이트 4가 끝났다.** `08-channel-messaging` · `09-client-server-channel`과
대조해 여섯을 채웠다.

- **두 경로에서 "자기 자신"의 취급이 다르다.** route mesh select-one은 보내는 node 자신을
  후보에서 뺀다 — channel 등록이 새 socket을 만들지 않고 기존 peer 연결을 쓰는데 MeshNode는
  자기 자신과 peer 연결을 맺지 않기 때문이다. **자기 자신만 Server인 node에서 부르면 대상
  없음으로 실패한다.** ClientServer는 반대로 같은 process의 Server도 똑같은 후보다.
- **후보가 없을 때의 대기도 갈린다.** route mesh는 즉시 실패, ClientServer는 호출 timeout과
  5초 중 짧은 쪽만큼 기다린다. 후보 없음의 뜻이 다르기 때문이다.
- **fanout channel은 손실을 허용한다.** 느린 구독자 몫을 버리고 발행은 성공으로 끝난다.
  Logical Multicast는 이 규칙의 대상이 아니다.
- **request 실패 뒤 자동 재전송이 없다**는 점과 **payload 내용으로 대상 종류가 바뀌지
  않는다**는 점.
- **store에서 찾았다고 바로 보내지 않는다.** 실제 연결에서 신원과 실행 세대를 다시 확인한다.
  server 재시작은 세대를 바꾸고, 늦은 reply는 원래 요청이 살아 있으면 그 결과가 되지만
  사라졌으면 버린다.
- **연결은 client가 시작하고 두 등록 정보를 서로 대신 쓰지 않는다.** 자동 탐색을 켰는데
  store가 없으면 bind 전에 시작이 실패한다.

이 대조에서 게이트 3의 구멍도 드러났다. 산문이 `BindAsync` · `OnCreateActorAsync` 같은
**`.NET` terminal이 붙은 operation 이름**을 부르고 있었는데 게이트 3이 타입 이름만 보고
있어 놓쳤다. 공통 12장에서 33자리다.

계약 문서가 이미 답을 정해 두었다 — `20-session-actor-dispatch` §4.1이 *"이 언어 중립
operation을 `NotifyDisconnected`라 하며 `.NET` exact interface에서는
`NotifyDisconnectedAsync(...)`로 표현한다"* 고 쓴다. **terminal을 뗀 이름이 언어 중립
이름이다.** 33자리를 그 규약으로 맞추고 게이트 3에 규칙을 넣었다.

| 남은 일 | 규모 |
| --- | --- |
| ZoneWorld 샘플 구현 | cpp · java · kotlin. 문서가 아니라 코드 작업이다 |

**§11의 게이트 여덟이 모두 통과했다.** 1 · 2 · 3 · 5 · 7 · 8은 스크립트로, 4 · 6은 사람이
전수로 돌렸다. 게이트 8은 렌더 HTML의 tab 그룹 수(101) · tab 수(505)를 소스와 대조하고 빈
code 블록이 없는지 확인했다 — 경고 수 세기가 아니다. 스크립트 셋은
`.github/workflows/docs.yml`에 걸려 있다. §12 완료 정의를 기준으로 **공통 정본과 다섯 언어 lane이
모두 완료**다. 문서 쪽에 남은 일은 없고, ZoneWorld는 샘플 코드 작업이라 이 런북의 범위
밖이다(handoff 문서 S1).

**병행 트랙 상태.** 사이트는 정본 트리를 docs root로 쓰도록 구성했고 가이드 장의 빌드
경고가 0이다. 체커는 정본 트리 전체를 보며 탭 언어 완전성을 검사한다. CI는 framework
사이트를 빌드해 확인만 하고 배포는 core 그대로다(§7.3).

**§7.4의 빌드 경고 27건은 판정을 끝냈다.** 전부 정본 트리 밖(`bindings/doc/` ·
`doc/principal/` · `framework/languages/`)을 가리키는 링크다. GitHub에서 문서를 읽을 때는
맞는 링크이고 사이트에 실을 대상이 아니라 mkdocs가 경고로 낸다.
`check_doc_links.py`가 저장소 기준으로 대조해 27건 전부 실재를 확인했다. 경고가 상수로
남으면 진짜 깨진 링크가 그 안에 묻히므로, 앞으로는 이 게이트가 둘을 가른다.

## 10. 표면 매핑표

**확정된 것** — [비동기 실행 정책 스펙](../framework/common/spec/05-async-execution-policy.ko.md)이
소유한다. 가이드는 인용만 한다.

| 의미 | .NET | Java · Node · C++ | Kotlin |
| --- | --- | --- | --- |
| 비동기 완료 terminal | `Async` | `submit` | `await` |
| 즉시 제출 | `Submit` | `submit` | `submit` |
| shared Spot gate 반납 | `Yield` | `yield` | `yield` |

**각 lane이 Phase 2에서 채울 것** — 자기 언어 칸을 채운 뒤 언어별 장을 쓴다. **비어 있는
칸을 지어내지 않는다.** 공통 정본 산문은 이 표가 채워지기 전에도 쓸 수 있다 — 타입 이름
대신 역할 이름을 쓰기 때문이다.

| dotnet 표면 | 확인할 것 |
| --- | --- |
| `builder.Services.AddZLinkFramework(...)` | 등록 진입점 |
| `IServiceCollection` / DI scope | 컨테이너 유무, 없으면 handler 의존성 전달 방식 |
| `IHostedService` / host lifecycle | 프로세스 수명에 붙이는 방법 |
| `ILogger` / `ActivitySource` / `Meter` | 로깅·trace·메트릭 표면 |
| `ValueTask` / `CancellationToken` | 비동기 타입과 취소 전달 |
| `IZLinkRequestHandler<TReq,TRes>` 등 | handler interface 이름과 시그니처 |
| attribute 기반 handler | 대응 기능 유무 |
| 패키지 구성과 버전 | 배포 단위 |

## 11. 검증 게이트

작성한 lane이 스스로 돌린다.

| # | 검사 | 대상 |
| --- | --- | --- |
| 0 | **생성 최신성** — 언어별 생성판이 공통 소스와 일치하는가 | 언어별 생성판 |
| 1 | **탭 완전성** — 다섯 언어가 모두 있고 라벨·확장자가 맞는가. §6 미구현 안내는 통과 | 공통 정본 |
| 2 | **스니펫 해석** — 모든 `--8<--` 경로와 마커가 존재하는가 | 공통 정본 |
| 3 | **산문 중립성** — 특정 언어 고유 이름이 남았는가(§10) | 공통 정본 |
| 4 | **산문·스펙 대조** — 개념과 동작 서술이 공통 스펙과 맞는가 | 공통 정본 |
| 5 | **식별자 대조** — 코드·산문 식별자가 그 언어 소스에 실재하는가 | 언어별 장 · 공통 정본의 교육용 예제 |
| 6 | **호출 형태 대조** — 예제를 대응 샘플과 1:1로 맞춘다 | 언어별 장 · 공통 정본의 교육용 예제 |
| 7 | **링크·앵커** — 공통↔언어별 상호 링크와 앵커가 실제 제목을 가리키는가 | 전체 |
| 8 | **렌더 확인** — 탭이 사이트에서 의도대로 전환되는가(원칙 6) | 전체 |

4번이 필요한 이유가 있다. 공통 정본은 코드가 스니펫이라 코드 층위는 자동으로 보장되지만
**산문은 아무도 대조하지 않는다.** dotnet 검수에서 나온 "제거된 API를 챕터째 문서화"가
정확히 산문 층위 결함이었다. 챕터마다 소유 스펙 문서를 지정하고 그것과 맞춘다.

**소유 문서는 챕터 머리에 밝힌다.** 대조할 상대가 문서에 적혀 있지 않으면 게이트 4는
매번 다시 찾는 일부터 해야 한다. 표기는 `**이 장의 계약 소유 문서** — …` 한 줄이고,
계약이 아니라 안내가 본질인 챕터(14 · 15 · 17)는 없다고 밝힌다. 이 규약은
`CommonGuideNarrative_DeclareTheSpecThatOwnsTheirContract`가 강제한다.

1·2·3·5·7은 스크립트로 돌린다.

| 게이트 | 스크립트 |
| --- | --- |
| 0 생성 최신성 | `doc/site/scripts/generate_language_guides.py --check` |
| 1 탭 완전성 · 2 스니펫 해석 | `doc/site/scripts/check_doc_tabs.py framework` |
| 3 산문 중립성 | `doc/site/scripts/check_prose_neutrality.py` |
| 5 식별자 대조 | `doc/site/scripts/check_guide_identifiers.py` |
| 7 링크·앵커 | `doc/site/scripts/check_doc_links.py` |

3번은 **탭 밖 산문만** 본다. 코드 펜스와 탭 블록은 언어별이므로 정상이고, 그 바깥에
`ZLink*` · `*_t` 같은 언어 고유 이름이 남았는지 찾는다. 다섯 언어 공통 규칙이라 산문에
나와도 되는 이름(§10의 terminal 셋 등)은 스크립트의 `ALLOWED`에 근거와 함께 등록한다.

2번은 경로와 마커 이름만 보다가 **구간이 온전한지**까지 본다. 마커가 있어도
end를 `) {}` 뒤에 두면 class가 열린 채 잘려 문법이 깨진 코드가 나간다. 줄 수와
중괄호 균형을 함께 확인한다.

7번은 파일 존재와 anchor 둘 다 본다. anchor는 사이트 두 곳이 쓰는 것과 같은
slugify(`case: lower, unicode: true`)로 계산하므로 한글 제목도 그대로 대조된다.
제목을 고치고 링크를 안 고친 자리가 이 방식으로 26건 나왔다.

4·6은 사람이 본다.

## 12. 완료 정의

**공통 정본** — 12장 모두 존재. 모든 코드 탭에 다섯 언어(미구현 샘플은 §6 안내 형태).
스니펫 전부 해석. §11의 1~4·7 통과.

**언어 lane** — 자기 탭 전부 채움. 언어별 5장 작성(cpp는 전용 3장 개명·작성 포함).
진입점 README가 읽는 순서 제시. §11의 5~8 통과. §10 매핑표의 자기 칸 채움.

## 13. 알려진 갭

| 갭 | 영향 | 처리 |
| --- | --- | --- |
| 공통 샘플에 발췌 마커가 없다 | 스니펫을 못 쓴다 | Phase 1이 구간을 정의하며 dotnet에 심고, Phase 2에서 lane별로 심는다(§8.1) |
| 교육용 예제는 샘플 대응이 없다 | 스니펫으로 못 바꾸고 복붙이 남는다 | 탭 안 인라인으로 두고 식별자·호출 형태 대조로 검증(§5.2) |
| framework 문서가 사이트에 없다 | 탭이 렌더되지 않는다 | `framework/doc/site/` 신설(§7.1) |
| GitHub Pages는 저장소당 사이트 하나 | 두 도메인을 지금 구성으로 낼 수 없다 | 두 번째 저장소 또는 호스팅 이전(§7.3) |
| 최상위가 core에서 framework로 바뀐다 | 기존 `zlink.systems/guide/*` 링크가 깨진다 | core 경로를 301 리다이렉트(§7.2) |
| `check_doc_tabs.py`가 core 9언어 전용 | framework 탭을 검사하지 못한다 | 5언어 집합과 미구현 안내 허용 추가(§8.2) |
| ZoneWorld가 cpp·java·kotlin에 없다 | 해당 탭을 코드로 채울 수 없다 | 구현 추가. 그 전까지 안내 탭(§6) |
| cpp 전용 3장 번호가 공통 축과 겹친다 | 같은 번호가 두 뜻으로 쓰인다 | 18~20으로 개명(§3.2) |
| 공통 파일에 nav prev/next를 둘 수 없다 | 기존 nav 규약·회귀 테스트와 충돌 | 순서를 진입점·사이트 nav로 이관하고 회귀 테스트 갱신(§4) |
| dotnet 가이드의 구조 편차(01장 §2 비대 등) | 공통 승격 시 함께 옮겨진다 | Phase 1에서 정리 |
