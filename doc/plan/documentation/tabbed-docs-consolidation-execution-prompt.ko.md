# tabbed-docs-consolidation 실행 프롬프트

[계획 런북](./tabbed-docs-consolidation-plan.ko.md)을 끝까지 완료시키기 위한
핸드오프 프롬프트. 새 세션/다른 에이전트에 그대로 붙여 넣는다. 현재 상태를
현물로 재점검하므로 plan이 이미 완료 표시여도 안전하다(검증만 하고 잔여만 마무리).

> **무중단 실행:** 이 프롬프트는 중간에 멈추거나 사용자에게 확인/보고하지 않고
> 모든 완료 기준이 충족될 때까지 한 번에 끝낸다.

---

## 전체 프롬프트

```
역할: 너는 zlink 리포(/home/hep7/project/kairos/zlink, 기본 브랜치 main)에서
"문서 탭 통합 개편(v2)" 계획을 끝까지 완료하는 자율 실행 에이전트다.

## 실행 원칙 (최우선)
- 중간에 멈추지 마라. 사용자에게 확인/질문/중간보고하지 마라. 모든 완료 기준이
  충족될 때까지 작업→검증→수정을 한 턴도 쉬지 않고 반복한다.
- 막히거나 트레이드오프가 생겨도 되묻지 말고 명백한 기본값으로 결정해 계속 간다.
- 한 단계가 끝나면 "다음 작업 진행할까요?" 같은 멈춤 없이 곧장 다음 단계로 넘어간다.
- 종료 조건은 단 하나: 계획 §4 완료 판정 7항목이 전부 검증으로 PASS. 그 전엔
  무슨 일이 있어도 멈추지 않는다. (도중의 보고/요약 출력은 금지 — 진행만 한다.)
- 마지막 PASS 후에만 §4 체크박스·§6 로그를 갱신하고 그때 1회 종료 요약을 남긴다.

## 입력 (source of truth — 반드시 먼저 읽어라)
- 계획 런북: doc/plan/documentation/tabbed-docs-consolidation-plan.ko.md
  (§2 로드맵 P1~P7, §4 완료 판정 기준, §6 완료 로그)
- 문서 원칙: doc/principal/documentation/documentation-principles.ko.md
- 샘플 정책: doc/spec/sample/SAMPLE_POLICY.md
- 탭 인프라: doc/site/mkdocs.yml (pymdownx.tabbed/snippets)
- 바인딩 스펙(타깃 블루프린트): doc/spec/bindings/<lang>/

## 진행 루프 (멈추지 말 것)
1. 계획을 읽고 P1~P7 각각의 "현재 실제 상태"를 코드/문서로 직접 확인한다
   (계획의 체크박스나 로그를 믿지 말고 리포 현물로 검증).
2. §4 7항목 중 미충족을 전부 추려, 의존 순서(P1→…→P7)로 끝까지 완료한다.
3. 각 단계는 "기능별로 9개 언어 전부" 만들고, 빌드+실행으로 검증한 뒤 커밋한다.
   측정/검증 없이 커밋하지 마라.
4. 단계가 끝나도 멈추지 말고 다음 단계로 즉시 진행. 모든 단계가 끝나면 §4 전 항목을
   재검증한다. 하나라도 실패하면 멈추지 말고 그 자리에서 고쳐 다시 검증한다(루프).
5. §4 전 항목 PASS가 확인되면 §4 체크박스와 §6 완료 로그를 갱신하고 종료한다.

## 대상 언어 (탭 9칸, 정확한 라벨)
C++ / C#/.NET / Java / Kotlin / Python / Node/TypeScript / JavaScript / Go / Rust
(소켓 패턴 문서는 C 탭을 추가해 최대 10칸. 서비스 문서는 C 샘플 없으면 C 제외.)

## 반드시 지킬 규약 (이걸 어기면 회귀한다)
A. 탭 코드는 절대 하드코딩하지 말고, 실제 빌드·실행 검증된 샘플 파일을
   pymdownx snippet `--8<--`로 임베드한다. 샘플이 단일 출처다.
B. 샘플은 "본문만" 노출한다. 각 샘플 파일에 섹션 마커를 넣고
   `--8<-- "경로:doc"`로 참조한다:
   - 마커: 주석으로 `// --8<-- [start:doc]` ~ `// --8<-- [end:doc]`
     (python은 `#`). main 진입 직후~종료 직전을 감싸 import/include·main
     시그니처·클래스 래퍼·헬퍼 스캐폴딩(unique_tcp/reservePort/wait_connected/
     must 등)을 숨긴다. dotnet 최상위문은 로컬 함수(static …) 다음부터 감싼다.
   - 마커는 주석이라 빌드/실행에 영향 없다.
C. 탭 마크업은 "펜스 형식"으로 통일한다(이게 핵심 — 안 그러면 dotnet처럼
   col 0에서 시작하는 샘플이 탭 들여쓰기를 벗어나 코드가 <p> 문단으로 깨진다):
       === "C#/.NET"

           ```csharp
           --8<-- "bindings/dotnet/samples/SpotChannelExample/Program.cs:doc"
           ```
   라벨 뒤 빈 줄 + 언어 펜스 + 들여쓴 snippet. bare `--8<--`(펜스 없음)는
   하이라이트가 안 되고 fragile하니 쓰지 마라.
D. doc/site/mkdocs.yml의 pymdownx.snippets에 `dedent_subsections: true`를 켜
   전 언어 본문을 col 0로 균일 정렬한다. (단 C의 펜스 정규화가 선행돼야 안전.
   비정규 구조에서 켜면 탭이 <p>로 깨진다.)
E. i18n/미러 구조:
   - 코어 가이드는 `<f>.md`(en 기본) + `<f>.ko.md`(ko) 쌍. nav는 .md 경로 참조,
     /ko는 .ko.md로 서빙. 바인딩 가이드는 `.md`-only(한글 내용), 내부 링크도 .md.
   - 정본은 doc/guide, 미러는 doc/site/docs/guide. **정본을 먼저 고치고** 미러로
     동기화한다. 바인딩 정본 .ko.md → 미러 .md 복사 시 상대링크 `.ko.md`→`.md`
     치환(절대 github URL의 .ko.md는 보존):
       sed -E 's#\]\((\.[^)]*)\.ko\.md#](\1.md#g'
   - 주의: 07-3-spot/07-4-actor는 정본↔미러 양방향 drift가 있다. 맹목 cp 금지
     (한쪽 콘텐츠 소실). 가법적 편집은 양 트리에 동일 연산으로 적용하라.
F. 새 canonical 샘플 등록 gotcha:
   rust=Cargo.toml [[example]], cpp=CMakeLists ZLINK_CPP_SAMPLE_SOURCES,
   java/kotlin=samples build.gradle의 run<Name>(kotlin 클래스=<File>Kt,
   java gradle `:kotlin-samples:run<Name>`로 실행), dotnet=독립 csproj
   (`dotnet run --project samples/<Pascal>`), go/python/node/js=디렉터리·파일 기반.
   Kotlin은 java 런타임(systems.zlink.*, Kotlin 2.1.0) 공유, JS는 node 런타임
   (@zlink-systems/zlink, 심링크) 공유 — 별도 네이티브 바인딩 만들지 마라.
G. 바인딩 가이드(P5)는 언어당 단일 index 1장으로: 설치·5분예제·핵심타입·
   소유권·에러·C API 대응표·네이티브/배포·샘플·더보기(코어 가이드 링크). 02~04 +
   05 일부 챕터는 삭제(삭제 전 회수 항목 옮겼는지 확인). Kotlin/JS는 별도 가이드가
   아니라 Java·Node 1장의 전용 절(### Kotlin / ### JavaScript) + README 표 행.
H. P6 CI 게이트: doc/site/scripts/check_doc_tabs.py가 (1)탭 9언어 누락/중복/이질,
   (2)`--8<--` 스니펫 경로 미해석(`:section` 접미 처리), (3)언어↔확장자 불일치를
   검출하고 .github/workflows/docs.yml의 mkdocs build 직전에 실행돼야 한다.

## 검증 (각 단계 + 최종, 멈추지 말고 자체 수행)
- 샘플: 언어별 빌드·실행으로 기대 출력 확인(go run / 직접 실행 /
  cargo run --example / cmake+make / gradlew run<Name> / dotnet run --project /
  node dist-tools/...). 실패하면 멈추지 말고 고쳐 재실행. 측정 없는 커밋 금지.
- 문서: `cd doc/site && mkdocs build` 무오류 + `python3 doc/site/scripts/
  check_doc_tabs.py` 통과. 렌더 감사: 빌드된 /ko HTML에서
  `<p>` 코드 깨짐 0, `language-<lang> highlight` 블록 수 = 탭셋×9,
  코드 첫 줄 leading-indent 전부 0인지 확인. 실패 시 그 자리에서 수정 후 재검증.
- 빌드 산출물(bindings/node/dist-tools 등)은 커밋하지 말고 HEAD로 복원해 트리를
  깨끗이 유지한다.

## 제약
- 기본 main에서 작업(브랜치는 명시 요청 시만). 기능/단계별로 검증 후 커밋.
- 코드를 spec에 맞춘다(spec이 타깃 블루프린트). spec을 코드에 맞춰 "고치지" 마라.
- 커밋 메시지 끝에:
  Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>

## 완료(종료) 기준 — 이게 전부 PASS여야 비로소 멈춘다
계획 §4의 7항목 + §6 로그 갱신:
1) 코어 패턴·서비스 문서가 9~10칸 언어 탭으로 통합.
2) 모든 탭 코드가 실제 샘플/contract와 일치(추측 0).
3) 네이티브 8언어 + Kotlin/JS 샘플이 canonical 시나리오·값을 동일 적용.
4) 바인딩 가이드 언어당 1장 축소(Kotlin/JS는 Java·Node 절).
5) 탭 누락·소실·API 부재를 CI가 검출.
6) mkdocs로 전 언어 탭 렌더 확인(깨짐 0, col 0 균일, 하이라이트).
7) mkdocs nav·미러·README가 새 구조 반영.
7항목 중 하나라도 미충족이면 멈추지 말고 계속 고친다. 전부 PASS가 확인된 그 순간
§4 체크박스·§6 로그를 갱신하고 종료한다(이때만 1회 최종 요약).
```

---

## 짧은 버전 (핸드오프/Codex용)

```
zlink 리포(main 브랜치)에서 doc/plan/documentation/tabbed-docs-consolidation-plan.ko.md
계획을 끝까지 완료해라. 중간에 멈추지 말고, 사용자에게 확인/중간보고 없이, 계획 §4
완료 판정 7항목이 전부 검증 PASS될 때까지 작업→검증→수정을 한 번에 반복한다. 막혀도
되묻지 말고 기본값으로 결정해 계속 간다. 전부 PASS인 순간에만 §4 체크박스·§6 로그를
갱신하고 종료한다(그때만 최종 요약 1회).

핵심 규약:
1. 탭 코드는 하드코딩 금지 — 빌드·실행 검증된 샘플을 `--8<-- "경로:doc"` 스니펫으로
   임베드. 섹션 마커 [start:doc]/[end:doc]를 main 본문만 감싸도록 넣어 import·헬퍼
   (unique_tcp/reservePort 등)·시그니처를 숨긴다.
2. 탭은 펜스 형식 필수: `=== "Lang"` + 빈 줄 + ```lang``` + 들여쓴 --8<--.
   (bare는 col 0 샘플이 <p>로 깨짐.) mkdocs.yml에 dedent_subsections:true로 col 0 정렬.
3. 9언어: C++/C#·.NET/Java/Kotlin/Python/Node·TS/JavaScript/Go/Rust. Kotlin=java런타임,
   JS=node런타임 공유(별도 바인딩 X). 바인딩 가이드는 언어당 1장, Kotlin/JS는 java/node 절.
4. 정본 doc/guide 먼저 → 미러 doc/site/docs/guide 동기화(.ko.md→.md 링크 치환).
   07-3-spot/07-4-actor 정본↔미러 양방향 drift 주의(맹목 cp 금지).
5. 검증: 샘플 언어별 빌드·실행 / `mkdocs build` 무오류 /
   `python3 doc/site/scripts/check_doc_tabs.py` 통과 / /ko HTML에서 <p>깨짐0·
   하이라이트·leading-indent 0. 실패 시 그 자리에서 고쳐 재검증. dist-tools 등 빌드
   산출물 커밋 금지.
6. spec이 타깃(코드를 spec에 맞춤). 커밋 끝에:
   Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```
