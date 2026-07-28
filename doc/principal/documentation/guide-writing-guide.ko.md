# 이해하기 쉬운 Framework guide 문서 작성 가이드

> 이 문서는 `framework/doc/framework/<language>/guide/` 아래의 도입 판단·사용법
> 문서를 작성하거나 수정하는 작업자가 따라야 하는 절차다. 주 독자는 문서 작업을
> 수행하는 AI다.
>
> [기술문서 작성 원칙](documentation-principles.ko.md)의 얇은 보충 문서다. 원칙
> 1~9는 그대로 적용한다. 이 문서의 §2·§3은 [스펙 작성 가이드](spec-writing-guide.ko.md)
> §2·§3과 같은 자리를 차지하지만, 이미 완성된 `dotnet/guide/` 14개 문서를 실제로
> 분석해서 뽑은 절차다 — 추측이 아니라 그 문서들이 실제로 따르고 있는 관행이다.

[기술문서 작성 원칙](documentation-principles.ko.md) ·
[스펙 작성 가이드](spec-writing-guide.ko.md) ·
[작성 예시: `dotnet/guide/`](../../../framework/doc/framework/dotnet/guide/01-overview.ko.md)

## 1. spec 문서와 다른 점

spec은 공개 계약을 **서술**한다. guide는 독자가 무엇을 왜 쓸지 **판단**하고 바로
따라 쓰게 돕는다. 정확한 계약과 guide의 설명이 어긋나면 spec이 이긴다 — 이
관계는 문서 도입부에서 명시한다.

> 모델 문장(`01-overview.ko.md`): "개념의 언어 중립 정식 정의는 공통 스펙 개요가,
> `.NET` public API의 정식 계약은 spec 문서가 다룬다. **두 표기가 어긋나면
> spec이 우선이다.**"

## 2. AI 작업 순서

### 2.1 입력 자료를 확인한다

1. 이 챕터가 다루는 기능의 공통 spec 챕터를 확인한다.
2. 언어별 exact interface 문서(`server/languages/<lang>/`)에서 정확한 public
   signature를 확인한다 — 코드 예제는 오늘 배포된 동작이 아니라 exact interface가
   정의하는 목표 계약을 따른다. 배포와 목표가 다르면 "구현 차이" 문서가 그 간극을
   소유하며, guide 코드는 목표 계약 쪽을 보여준다.
3. 가능하면 실행되는 샘플(`common/sample/`)과 코드 예제를 대응시킨다.
4. 자료가 충돌하면 spec과 exact interface를 우선한다.

챕터를 열 때 소유 spec 문서를 명시하고("정식 계약은 spec이 다루며, 이 챕터는
사용법을 다룬다"), 닫을 때 [13-interface-catalog](../../../framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md)의
해당 절 번호와 검증 클래스를 가리킨다. 개별 문장마다 출처를 달지 않는다 —
입력 확인과 대조는 챕터 시작과 끝, 두 지점에 구조적으로 둔다.

### 2.2 독자의 질문으로 목차를 만든다

기존 14개 챕터는 세 가지 모양으로 갈린다. 새 챕터를 쓸 때는 먼저 이 셋 중 어느
모양인지 정한다.

**모양 A — 도입 판단** (예: 01-overview, 04-feature-map, 14-alternative)

1. 한 줄 정의, 체감 난이도
2. 상황별 문제 → ZLink 기능 대응 표
3. 경계 — 이 문서가 다루지 않는 것
4. 선택을 돕는 flowchart나 기준 표
5. 실행 가능한 샘플 링크
6. 상세 비교(경쟁 기술 등, 참고용)

추천 결론을 낼 때는 적용 범위를 결론 문장 안에 넣는다("gRPC를 고민 중이라면
ZLink를 써라"가 아니라 "내부 서비스 통신을 gRPC로 고민 중이라면"처럼). 판단
기준은 기술명이 아니라 반복되는 문제 증상으로 준다. 경쟁 기술과 비교할 때
"없는 기능"과 "훅은 있는데 미리 구현된 도구가 없는 기능"을 섞지 않는다.

**모양 B — 레퍼런스/입문** (예: 02-getting-started, 03-concepts, 13-interface-catalog)

범위 선언 → 초반 용어/메타 절 → 개념 또는 interface 그룹별 본문(코드+표) →
"더 보기". 02는 실제 샘플 하나를 처음부터 끝까지 따라가는 walkthrough이고, 03은
전체 용어 지도, 13은 언어별 interface를 계약 순서로 색인한다 — 세 챕터 다 같은
템플릿을 억지로 따르지 않는다.

**모양 C — 기능 사용법** (05-channel-messaging ~ 12-operations, 가장 흔한 모양)

1. (선택) 익숙한 기술이나 "무엇을 해결해주는가" 문제로 방향 잡기
2. 핵심 개념 정의("SPOT은 ~다")
3. 등록/builder 코드
4. 핵심 사용법 — "종류 | 보내는 함수 | 받는 handler" 같은 한눈에 보기 표
5. 운영 변형(filter, scaling, 오류 종류 등)
6. 자주 막히는 곳 — 증상 → 원인 표
7. 더 보기 — spec, interface-catalog 절 번호, 검증 클래스, 샘플 링크

6번(자주 막히는 곳)은 모든 챕터에 있지 않다 — 대신 "오류 처리"나 "동작 방식"
절로 대체하는 챕터도 있다. 강제하지 않는다.

### 2.3 용어를 먼저 정리한다

guide는 spec의 공통 용어집(`01-glossary.ko.md`)을 직접 링크하지 않는다. 대신
[03-concepts §0](../../../framework/doc/framework/dotnet/guide/03-concepts.ko.md)에
~20개 핵심 용어를 한 줄 정의로 모아 둔 **guide 전용 로컬 용어집**을 쓴다. 이
로컬 용어집이 [기술문서 작성 원칙](documentation-principles.ko.md) 원칙 2
"용어는 문서 단위로 다시 소개한다"에서 말하는 정식 정의 자리다.

그 원칙을 그대로 적용한다: **이 챕터에서 그 용어가 처음 나오는 자리**에서는
한 문장으로 다시 풀고 링크한다. 같은 챕터 **안에서** 다시 나오면 링크 없이
이름만 쓴다.

```markdown
[actor](03-concepts.ko.md#actor)는 ID로 식별되는 상태 보유 객체다 — 이 챕터에서는
STREAM 연결과 actor 사이의 binding만 다룬다.
```

"용어가 낯설면 03-concepts §0을 보라"는 **링크만 거는** 포인터로는 부족하다 —
독자가 그 챕터만 열었을 수 있고, 링크를 안 따라가면 그 문장을 이해할 수 없다.

새 챕터에서 처음 쓰는 용어가 아직 03-concepts §0에 없으면 그 표에 항목을 먼저
추가한다. Framework 도메인 용어(SPOT, actor, Entry Spot, RoutingId 등)는 spec과
정확히 같은 이름을 쓰고 새로 짓지 않는다. 반대로 guide만의 서술 장치(체감
난이도, 자주 막히는 곳, 샘플에서 보기, 목표 계약)는 새로 만들어도 되지만, 이미
있는 표현을 재사용하고 비슷한 뜻의 이표현을 늘리지 않는다.

### 2.4 쉬운 설명부터 작성한다

절(`§1` 이하) 수준에서는 spec과 같은 규율을 쓴다 — 정의나 결과로 문장을 시작한다
("actor는 ID로 식별되는 상태 보유 객체다"). 반면 **챕터 진입부(`§0`)는 동기·문제
먼저**로 시작해도 된다 — 이게 spec과의 의도적인 차이다. 독자가 이미 아는 것(gRPC,
기존 코드)에서 출발해 새 개념으로 넘어가는 게 onboarding 문서의 역할이다.

```text
예(10-location.ko.md §0): "지금까지 챕터는 연결할 endpoint를 코드에 직접
적었다. location store를 등록하면 호출하는 쪽에서 remote endpoint 값이
사라진다." — 문제(before) → 해결(after) 순서.
```

한 문단(또는 표 한 칸)에 여러 판단을 담아도 되는 경우는 "자주 막히는 곳" 같은
Q→원인 표 안에서뿐이다 — 원인, 조건, 관련 코드, 주의사항을 한 칸에 압축해도
괜찮다. 표 밖의 일반 산문에서는 spec과 똑같이 한 문단에 주된 판단 하나만 둔다.

같은 단어가 문맥마다 다른 뜻이면 명시적으로 구분한다. 예: `06-spot.ko.md`는
"여기서 말하는 직렬화는 payload를 bytes로 바꾸는 codec 직렬화가 아니다. 메시지를
어떤 실행 줄에서 어떤 순서로 처리하는가를 뜻한다"처럼 짚고 넘어간다.

### 2.5 정확한 계약을 다시 대조한다

spec처럼 항목별 대조표를 절마다 쓰지 않는다. 대신 대조는
[13-interface-catalog](../../../framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md)
한 곳에 모은다 — 모든 interface 그룹이 그 문서에서 검증 클래스·테스트 메서드에
연결된다. 05~12 각 챕터는 닫는 절에서 그 절 번호와 검증 클래스를 가리키는 한
줄을 반드시 둔다.

```text
예(05-channel-messaging.ko.md §12): "이 챕터 계약의 실행 검증 예문은
[13-interface-catalog] §1 — 검증 클래스 `ChannelContracts`·`HandlerContracts`·
`CodecContracts`."
```

새 기능 챕터를 쓰거나 고칠 때는 이 포인터가 실제로 맞는 절 번호·클래스를
가리키는지 확인하는 게 guide의 "계약 대조" 단계다.

### 2.6 링크와 자동 검증을 확인한다

- 이 챕터에서 처음 쓰는 용어마다 한 문장 재설명 + 03-concepts §0 링크가 있는지
  확인한다(§2.3). 링크만 걸고 설명이 없는 자리가 없는지 다시 훑는다.
- guide는 dotnet에서 `Zlink.Framework.UnitTests/Documentation/Regression.cs`로
  구조적으로 검증된다.
  - 14개 파일이 실제로 존재하고 nav 마커·H1을 갖췄는지(내용 정확성은 아님).
  - 이름이 바뀌었거나 제거된 API를 걸러내는 금지 패턴 grep(예: 대체된
    `IZLinkSpotRefResolver` 계열, 폐기된 nested `Action<>` callback 모양을 문서에
    쓰지 않는지).
  - guide는 spec/exact-interface 문서에 강제하는 `## 회귀 테스트`
    섹션에서 명시적으로 **제외**된다 — "온보딩 산문이라 계약 문서가 아니다"가
    문서화된 정책이다.
- `scripts/verify-framework-doc-contracts.sh`는 guide를 검사하지 않는다 — spec
  전용이다. guide 코드 예제 검증은 위 Regression.cs의 금지 패턴 grep과, 가능하면
  실제 샘플과의 대조에 의존한다.
- Markdown 표·mermaid 문법을 확인하고 `git diff --check`를 실행한다.

## 3. 용어 작성 규칙

### 3.1 정의문은 로컬 용어집이 먼저 깔아둔 그림 위에 얹는다

spec은 "동작을 먼저 설명하고 이름을 나중에 붙인다"를 모든 용어에 강제하지만,
guide는 03-concepts §0가 이미 한 줄 그림을 심어 놨다는 전제로 `"X는 Y다"` 정의문을
바로 써도 된다 — 실제로 대부분의 §1이 이렇게 시작한다. 단, **아직
03-concepts §0에 없는 새 용어**나 **문맥마다 뜻이 겹치는 단어**는 spec 방식대로
동작을 먼저 풀고 이름을 붙인다(2.4의 "직렬화" 예).

### 3.2 로컬 용어집과 챕터별 첫 링크

새 챕터나 새 개념을 추가하면 03-concepts §0 표에 항목을 추가한다. 그 뒤 **이
용어를 쓰는 모든 챕터**에서, 각 챕터의 첫 등장 자리마다 §2.3처럼 한 문장으로
다시 풀고 링크한다 — 한 번 어딘가에 링크를 걸었다고 다른 챕터에서 생략하지
않는다. Spec의 공통 용어집을 직접 링크하지 않는다 — guide 독자에게는 로컬
용어집이 그 자리를 대신한다.

### 3.3 정식 식별자와 모호한 단어를 구분한다

Public type·method 이름은 그대로 쓴다. 특히 대소문자나 철자가 비슷해 헷갈리는
쌍은 명시적으로 구분한다.

> 예: `ZLink`(server framework 타입, 대문자 L)와 `Zlink`(Stream Connector client
> 타입, 소문자 l)를 혼동하지 않는다 — `01-overview` §8이 규칙을 밝히고,
> `05-channel-messaging`과 `09-stream`의 "자주 막히는 곳"에 같은 혼동이 다시
> 등장한다. `ZLinkStreamError`/`ZlinkStreamError`도 같은 패턴이다.

### 3.4 새 용어를 만들지 않는다 — 단, 서술 장치는 예외

Framework 도메인 개념은 spec/glossary 용어를 그대로 쓰고 절대 새 이름을 짓지
않는다. guide만의 온보딩 서술 장치(체감 난이도, 자주 막히는 곳, 샘플에서 보기,
목표 계약/목표 사용법)는 새로 만들어도 되는 예외다 — 다만 새 챕터를 쓸 때마다
비슷한 뜻의 새 표현을 또 만들지 않고, 이미 쓰이는 이름을 재사용한다.

## 4. 완료 점검표

- [ ] 챕터 여는 blockquote가 소유 spec 문서를 명시하고, 닫는 절이
      interface-catalog 절 번호·검증 클래스를 가리킨다(§2.1, §2.5).
- [ ] 챕터가 모양 A/B/C 중 어디에 해당하는지 정하고 그 템플릿을 따랐다 — 강제로
      맞추지 않아도 되는 절은 생략했다(§2.2).
- [ ] 새 용어를 03-concepts §0에 추가했고, 그 용어를 쓰는 각 챕터의 첫 등장
      자리마다 한 문장 재설명 + 링크를 달았다(§2.3, §3.2).
- [ ] `§1` 이하는 정의·결과 먼저, `§0` 진입부는 동기·문제 먼저로 썼다(§2.4).
- [ ] 표 밖 일반 산문은 한 문단에 판단 하나만 담았다(§2.4).
- [ ] 문맥마다 뜻이 겹치는 단어와 철자가 비슷한 public 식별자를 명시적으로
      구분했다(§3.1, §3.3).
- [ ] Framework 도메인 용어를 새로 짓지 않았다. 온보딩 서술 장치는 기존 표현을
      재사용했다(§3.4).
- [ ] `Zlink.Framework.UnitTests/Documentation/Regression.cs`의 금지 패턴에
      걸리는 API가 없는지 확인했다(§2.6).
- [ ] [기술문서 작성 원칙](documentation-principles.ko.md) 원칙 1~9, 특히 원칙 7
      한글 산문 문체와 원칙 6 렌더 확인을 확인했다.
