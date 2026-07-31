# server 가이드 공통화 런북

> 지금 `framework/doc/framework/dotnet/guide/server/`에만 있는 server 가이드를 **공통 정본
> 한 벌 + 언어별 장**으로 재편하고, cpp · java · kotlin · node를 채우는 작업 지침이다.
> 여러 lane이 서로를 기다리지 않고 병렬로 진행하는 것이 목적이다.
>
> 작성 규약 자체는 [기술문서 작성 원칙](../../../doc/principal/documentation/documentation-principles.ko.md)과
> [사용자 가이드 문서 작성 가이드](../../../doc/principal/documentation/guide-writing-guide.ko.md)가
> 소유한다. 이 런북은 **무엇을 어디에 두고, 무엇을 근거로, 어느 순서로** 쓰는지만 정한다.

## 1. 왜 공통화인가

dotnet 가이드에서 언어 고유 표현이 어디에 있는지 실측한 결과다.

| 위치 | 언어 고유 표현 |
| --- | --- |
| 코드블록 안 | 421회 |
| 산문·표 | 246회 |

챕터별로 보면 **산문의 언어 의존도가 대부분 0~10%**다. 개념과 동작 설명은 언어가 바뀌어도
그대로다. 다섯 벌로 나눠 쓰면 같은 문장을 다섯 번 유지해야 하고, 한 곳을 고칠 때 나머지
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

## 3. 파일 배치

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

## 4. 읽는 순서는 언어별 진입점이 정한다

공통 파일 하나를 다섯 언어가 공유하므로 **파일 이름의 번호로 순서를 표현할 수 없다.**
순서는 각 언어의 진입점 README와 사이트 nav가 정한다.

이 결정이 앞서 정한 cpp 배치를 그대로 살린다. cpp 독자는 DI·configuration·HTTP hosting을
concepts 다음에 읽고, 그 뒤로 공통 장을 잇는다. 파일을 새 번호로 복제하지 않는다.

| cpp 진입점이 제시하는 순서 | 실제 파일 |
| --- | --- |
| 1 · 2 | cpp overview · getting-started(언어별) |
| 3 | 공통 concepts |
| **4 · 5 · 6** | **cpp DI · configuration · HTTP hosting(cpp 전용)** |
| 7 ~ | 공통 backpressure ~ alternative + 언어별 monitoring·interface-catalog·options |

java · kotlin · node 진입점은 공통 축 순서를 그대로 제시한다.

**따라오는 결과 둘.**

- 공통 파일에는 `framework-adapter-nav` prev/next 마커를 둘 수 없다. 언어마다 앞뒤가
  다르기 때문이다. 순서 안내는 진입점 README와 사이트 nav가 맡는다. 언어별 장에는
  기존대로 nav를 유지한다.
- 문서 회귀 테스트(`Regression.cs`)가 검사하는 파일 목록과 nav 규칙을 새 구조에 맞게
  고쳐야 한다.

## 5. 탭과 스니펫 규약

core 가이드와 같은 방식을 쓴다. 라벨은 framework가 지원하는 다섯 언어다.

````markdown
=== "C#/.NET"

    ```csharp
    --8<-- "framework/languages/dotnet/samples/TicTacToe/Server/Api/Handlers/CreateGameHttpHandler.cs:doc"
    ```

=== "C++"

    ```cpp
    --8<-- "framework/languages/cpp/samples/TicTacToe/server/api/create_game_handler.cpp:doc"
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
// --8<-- [start:doc]
...가이드에 실릴 구간...
// --8<-- [end:doc]
```

**규칙 셋.**

1. **코드를 문서에 적지 않는다.** 공통 정본의 모든 언어 코드 블록은 스니펫이다. 손으로
   적은 코드는 소스와 어긋나기 시작한다.
2. **한 탭 블록에는 다섯 언어를 모두 담는다.** 빠진 언어가 있으면 그 챕터는 미완이다.
3. **마커 이름은 용도별로 나눈다.** 한 파일에서 여러 곳을 인용하면 `:doc-register`,
   `:doc-handler`처럼 구분한다.

## 6. 딸려 오는 작업

**6.1 샘플에 발췌 마커 심기.** core 샘플은 애초에 문서용 자립 예제지만, framework 샘플은
실제 서비스 코드라 "가이드에 실을 크기"의 구간이 없다. 공통 샘플 7종 × 5언어에 마커를
심어야 한다. 이 작업을 건너뛰면 결국 복붙으로 돌아간다.

**미리 다 심지 않는다.** 어떤 구간이 필요한지는 챕터를 쓰면서 정해진다. Phase 1이 dotnet
샘플에 심으면서 구간 목록을 만들고, Phase 2에서 각 언어 lane이 같은 목록을 자기 샘플에
심는다.

**6.2 사이트 분리 — framework가 최상위, core는 서브도메인.** framework 문서는 아직
사이트에 없다. `doc/site/`가 유일한 mkdocs 프로젝트이고 core만 담아 `zlink.systems`로
배포된다. framework를 **최상위 도메인의 메인 사이트**로 올리고 core를 서브도메인으로
내린다.

| | framework | core |
| --- | --- | --- |
| 도메인 | **`zlink.systems`**(메인) · `framework.zlink.systems`(별칭) | **`core.zlink.systems`** |
| mkdocs 프로젝트 | **신규** — `framework/doc/site/` | `doc/site/`(현행 이동) |
| 담는 것 | 공통 정본 12장 · 언어별 5장 · cpp 전용 3장 · 샘플 · 공통 스펙 | 소켓 패턴 · C API · binding · internals |
| 상대편 참조 | core 사이트로 절대 URL | framework 사이트로 절대 URL |

두 사이트는 각자 nav·검색 색인·i18n을 갖는다. 서로를 가리키는 링크는 절대 URL이다 —
framework 가이드가 transport·TLS·socket option을 설명할 때
`https://core.zlink.systems/guide/04-transports/`로 건다.

**기존 링크 처리.** 지금 `zlink.systems/guide/*`·`/api/*`·`/internals/*`가 core를 가리키고
있다. 최상위를 framework로 바꾸면 이 경로가 전부 깨진다. **core 경로를
`core.zlink.systems`의 같은 경로로 301 리다이렉트**한다. 외부에서 걸린 링크와 검색
색인이 살아 있으므로 이 작업을 빼면 안 된다.

**배포 제약과 선택.** GitHub Pages는 저장소당 사이트 하나, 커스텀 도메인도 사이트당
하나다. 사이트가 둘이 되므로 지금처럼 이 저장소 Pages 하나로는 못 낸다.

| 안 | 방식 | 비용 |
| --- | --- | --- |
| **A. 두 번째 저장소** | 소스는 이 저장소에 두고, CI가 core 사이트 빌드 결과를 `zlink-systems/core-docs` 같은 저장소로 push해 `core.zlink.systems` CNAME을 붙인다. 이 저장소 Pages는 framework(최상위)를 낸다 | 저장소 하나 추가와 배포 토큰. 현 구성 변경이 가장 작다 |
| **B. 호스팅 이전** | Cloudflare Pages·Netlify로 옮겨 한 저장소에서 프로젝트 둘을 각 도메인으로 배포. 리다이렉트 규칙도 호스팅이 제공한다 | 호스팅 이전 결정이 필요. 대신 301 리다이렉트·PR 프리뷰·다중 사이트가 모두 자연스러워진다 |

리다이렉트가 필요해진 만큼 B의 이점이 커졌다. **어느 쪽이든 문서 작업은 영향받지
않는다** — 정해지기 전에도 `framework/doc/site/`를 만들고 로컬 빌드로 진행할 수 있다.

**CI.** `docs.yml`은 지금 `doc/site/**` 변경에만 반응한다. 두 사이트를 각각 빌드·배포하고
탭 체커를 양쪽에 돌리도록 고친다.

**Home과 nav.** framework 사이트의 첫 화면은 "무엇을 만드나 → 시작하기 → 개념 → 샘플"
순서다. 공통 장은 한 벌이므로 nav에 다섯 번 등장시키지 않고 독자가 페이지 안에서 언어
탭으로 전환한다. 언어별 5장과 cpp 전용 3장만 언어 그룹으로 묶고, 각 언어 그룹의 순서는
§4의 진입점이 정한 순서를 그대로 옮긴다. core로 가는 입구는 상단 링크 하나로 둔다.

**미러 규칙.** core 사이트가 쓰는 방식을 따른다 — guide·internals는 verbatim, 스펙은
재배치. i18n은 `mkdocs-static-i18n` suffix 구조(`.md`/`.ko.md`)를 그대로 쓴다.

**6.3 탭 체커 확장.** `doc/site/scripts/check_doc_tabs.py`는 core용 9언어 라벨을
강제한다. framework 5언어 라벨을 별도 집합으로 받도록 확장한다.

## 7. 공통 샘플

일곱 종이 공통 축이다. 공통 정본과 언어별 장 모두 이 일곱만 참조한다.

| 샘플 | dotnet | cpp | java | kotlin | node |
| --- | :-: | :-: | :-: | :-: | :-: |
| TicTacToe · Bingo · SupportChat · DeliveryDispatch · ShoppingMall · GameQuest | ✅ | ✅ | ✅ | ✅ | ✅ |
| ZoneWorld | ✅ | 구현 예정 | 구현 예정 | 구현 예정 | ✅ |

ZoneWorld는 전 언어에 추가한다. 브라우저 client는 `shared_sample/zoneworld`의 TypeScript
하나를 공유하므로 각 lane이 추가할 것은 server와 headless 시나리오 client다.

**탭은 다섯 언어를 모두 요구하므로 ZoneWorld 스니펫이 필요한 자리는 구현이 붙을 때까지
공통 정본에 넣을 수 없다.** 그 전까지는 시나리오 설명만 쓰고 코드 탭은 비워 둔다.

공통 sample 문서가 현재 ZoneWorld를 ".NET과 Node.js가 제공"으로 규정한다. 구현이 붙으면
**그 문서를 먼저 고치고** 가이드가 따라간다.

## 8. Phase와 lane

문서 공통화가 본체다. 사이트·체커·배포는 그 결과를 내보내는 트랙이며 문서 작업을
막지 않는다.

**Phase 0 — 스파이크 (반나절)**

12장을 다 바꾼 뒤에 렌더가 깨지면 되돌리기가 비싸다. **한 장으로 통하는지만 먼저
확인한다.**

1. 샘플 한 곳(예: TicTacToe dotnet의 Spot 생성 handler)에 `:doc` 마커를 심는다.
2. 공통 정본 한 장을 만들어 그 스니펫을 탭으로 인용한다.
3. `framework/doc/site/`에 최소 mkdocs 설정을 두고 로컬 빌드로 탭 전환과 스니펫 삽입을
   눈으로 확인한다.

여기서 확인할 것은 셋이다 — 스니펫 경로가 repo 루트 기준으로 풀리는가, 탭이 언어별로
전환되는가, 마커 구간이 의도한 크기로 잘리는가.

**Phase 1 — 공통 정본 12장 (본체)**

기존 dotnet 문서를 `common/guide/server/`로 승격한다. 챕터마다 세 가지를 한다.

| 작업 | 내용 |
| --- | --- |
| 산문 중립화 | .NET 고유 이름을 역할 이름으로 바꾼다(§9). 이 단계의 핵심이다 |
| 스니펫 전환 | 코드블록을 샘플 마커 인용으로 바꾼다. 필요한 구간을 그때 정의하고 dotnet 샘플에 심는다 |
| 탭 골격 | 다섯 언어 탭을 만들고 dotnet만 채운다. 나머지 넷은 Phase 2에서 채워진다 |

스니펫 구간 목록은 이 과정의 **산출물**이지 선행 조건이 아니다. 챕터를 쓰면서 어떤
구간이 필요한지 정해지고, 그 목록이 Phase 2 lane들의 작업 지시가 된다.

**Phase 2 — 언어별 병렬**

| lane | 하는 일 |
| --- | --- |
| **cpp** | Phase 1이 정한 구간을 자기 샘플에 심기 → 공통 정본에 cpp 탭 채우기 → 언어별 5장 + cpp 전용 3장 + 진입점 |
| **java** | 마커 → java 탭 → 언어별 5장 + 진입점 |
| **node** | 마커 → node 탭 → 언어별 5장 + 진입점 |
| **kotlin** | 마커 → kotlin 탭 → 언어별 5장 + 진입점. java lane 뒤를 따른다 |

**탭 채우기는 서로 독립이라 완전 병렬이다.** 같은 파일의 다른 탭을 건드리므로 충돌
지점만 조율한다. 언어별 5장도 서로 독립이다. 각 lane이 §9 매핑표의 자기 칸을 이때 채운다.

kotlin은 java 런타임을 공유하므로 java lane이 한 챕터를 끝낼 때마다 미러링한다.

**병행 트랙 — 사이트·체커·배포**

Phase 1과 나란히 진행한다. 문서가 없어도 골격은 세울 수 있고, 문서가 늘어날수록 채워진다.

| 항목 | 언제 |
| --- | --- |
| `framework/doc/site/` nav·Home 구성 | Phase 1과 병행 |
| `check_doc_tabs.py` 5언어 집합 추가 | Phase 2 시작 전까지. 탭이 다섯 개가 되는 시점부터 의미가 생긴다 |
| core 사이트 분리·301 리다이렉트·CI | 배포 방식(§6.2 A·B)이 정해진 뒤. 문서 작업과 무관 |

## 9. 표면 매핑표

**확정된 것** — [비동기 실행 정책 스펙](../framework/common/spec/05-async-execution-policy.ko.md)이
소유한다. 가이드는 인용만 한다.

| 의미 | .NET | Java · Node · C++ | Kotlin |
| --- | --- | --- | --- |
| 비동기 완료 terminal | `Async` | `submit` | `await` |
| 즉시 제출 | `Submit` | `submit` | `submit` |
| shared Spot gate 반납 | `Yield` | `yield` | `yield` |

**각 lane이 Phase 2에서 채울 것** — 자기 언어 칸을 채운 뒤 언어별 장을 쓴다. **비어 있는
칸을 지어내지 않는다.** 공통 정본 산문은 이 표가 채워지기 전에도 쓸 수 있다 — 타입
이름 대신 역할 이름을 쓰기 때문이다.

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

산문이 특정 언어 타입을 그대로 부르면 공통화가 깨진다. **역할 이름으로 쓰고 정확한 타입은
코드 탭에 맡긴다** — 예: "등록 진입점", "요청 handler interface".

## 10. 근거 규칙

**그 언어의 코드에서 가져온다.** 공통 정본의 산문은 개념을 서술하고, 코드는 스니펫이
가져온다. 어느 쪽도 dotnet 코드를 문법만 바꿔 옮기지 않는다.

| 무엇을 쓸 때 | 근거 |
| --- | --- |
| 코드 탭 | `framework/languages/<lang>/samples/`의 마커 구간 |
| interface 시그니처(언어별 장) | 그 언어의 public 헤더·소스 |
| 검증 클래스 이름 | 그 언어의 contract test |
| 시나리오·검증 기준 | `common/sample/`의 시나리오 문서 |

소스에 없는 이름을 쓰지 않는다.

## 11. 검증 게이트

작성한 lane이 스스로 돌린다.

1. **탭 완전성** — 탭 블록마다 다섯 언어가 모두 있고 라벨·확장자가 맞는가(체커).
2. **스니펫 해석** — 모든 `--8<--` 경로와 마커가 실제로 존재하는가(체커).
3. **식별자 대조** — 언어별 장의 코드·산문 식별자가 그 언어 소스에 실재하는가.
4. **호출 형태 대조** — 언어별 장의 예제를 대응 샘플과 1:1로 맞춰 본다. 이름만 맞고 호출
   형태가 틀리는 결함은 식별자 대조로 잡히지 않는다.
5. **링크·앵커** — 공통↔언어별 상호 링크와 앵커가 실제 제목을 가리키는가.
6. **산문 중립성** — 공통 정본 산문에 특정 언어 고유 이름이 남았는가(§9).
7. **렌더 확인** — 탭이 사이트에서 의도대로 전환되는가(원칙 6).

1·2·5·6은 스크립트로 돌린다.

## 12. 완료 정의

**공통 정본** — 12장 모두 존재, 모든 코드 탭에 다섯 언어, 스니펫 전부 해석, 산문 중립성
통과.

**언어 lane** — 자기 탭 전부 채움, 언어별 5장 작성, 진입점 README가 읽는 순서 제시,
§11 검증 통과, §9 매핑표의 자기 칸 채움.

## 13. 알려진 갭

| 갭 | 영향 | 처리 |
| --- | --- | --- |
| 공통 샘플에 발췌 마커가 없다 | 스니펫을 못 쓴다 | Phase 0에서 구간 정의, Phase 2에서 lane별로 심기(§6.1) |
| framework 문서가 사이트에 없다 | 탭이 렌더되지 않는다 | `framework/doc/site/`에 별도 사이트 신설(§6.2) |
| GitHub Pages는 저장소당 사이트 하나 | 두 도메인을 지금 구성으로 낼 수 없다 | 두 번째 저장소로 publish 또는 호스팅 이전(§6.2 A·B) |
| 최상위가 core에서 framework로 바뀐다 | 기존 `zlink.systems/guide/*` 링크가 깨진다 | core 경로를 `core.zlink.systems`로 301 리다이렉트(§6.2) |
| `check_doc_tabs.py`가 core 9언어 전용 | framework 탭을 검사하지 못한다 | 5언어 집합 추가(§6.3) |
| ZoneWorld가 cpp·java·kotlin에 없다 | 해당 스니펫 자리를 채울 수 없다 | 구현 추가. 그 전까지 코드 탭 보류(§7) |
| 공통 파일에 nav prev/next를 둘 수 없다 | 기존 nav 규약·회귀 테스트와 충돌 | 순서를 진입점·사이트 nav로 이관하고 회귀 테스트 갱신(§4) |
| dotnet 가이드의 구조 편차(01장 §2 비대 등) | 공통 승격 시 함께 옮겨진다 | Phase 1에서 정리 |
