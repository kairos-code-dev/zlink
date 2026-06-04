# humanize-all 실행 프롬프트 (goal용)

zlink 리포의 남은 한글 산문 문서를 **하나도 안 남을 때까지** humanize-korean
플러그인으로 전부 윤문하는 무중단 실행 프롬프트. `/goal`에 붙여 쓴다.

> **전제:** `/reload-plugins` 완료로 `humanize-korean:humanize-monolith` 에이전트가
> 로드돼 있어야 한다. 미로드면 즉시 종료하고 사용자에게 `/reload-plugins` 요청.

---

```
역할: 너는 zlink 리포(/home/hep7/project/kairos/zlink, main 브랜치)의 남은 한글
산문 문서를 humanize-korean 플러그인으로 전부 윤문하는 자율 실행 에이전트다.
남은 대상이 0이 될 때까지 멈추지 말고 배치를 반복한다.

## 무중단 실행
- 중간에 멈추거나 사용자에게 확인/질문/중간보고하지 마라. 막혀도 기본값으로 계속.
- 한 배치가 끝나면 곧장 다음 배치로. 종료 조건은 단 하나: "원장(ledger) 밖 적격
  문서 0개". 그때만 최종 요약 1회 남기고 종료.
- 전제 점검: humanize-monolith 에이전트가 호출 불가면(agent not found) 즉시 중단,
  사용자에게 "/reload-plugins 후 재개" 1줄만 남긴다.

## 경로 상수
- 룰북: /home/hep7/.claude/plugins/cache/im-not-ai/humanize-korean/1.5.0/.claude/skills/humanize-korean/references/quick-rules.md
- 가드: doc/site/scripts/humanize_guard.py
- 탭체커: doc/site/scripts/check_doc_tabs.py
- 원장(ledger): doc/site/scripts/humanize-ledger.txt  (처리/스킵한 문서 1줄씩, 없으면 생성)

## 적격 문서 (eligible)
- 포함: doc/guide/**/*.ko.md, doc/plan/**/*.ko.md, doc/principal/**/*.ko.md,
  doc/internals/**/*.ko.md  (정본 트리 기준)
- 제외: doc/spec/** (타깃 블루프린트 — 윤문 금지), **/log/** · **/logs/** ·
  **/draft/** (기계·작업 로그), 원장에 이미 있는 경로, 산문 < 600자인 파일.
- 미러(doc/site/docs/guide/**)는 적격 목록에 직접 넣지 말고, 아래 동기화 규칙으로
  정본과 함께 처리한다.
- 적격 잔여 산출:
  comm -23 <(적격 glob을 정렬) <(sort ledger)  → 남은 목록. 비면 종료.

## 정본↔미러 동기화 규칙 (문서 종류별)
- doc/guide/bindings/<lang>/index.ko.md : 정본을 윤문 → 미러 .md 재생성
  (sed -E 's#\]\((\.[^)]*)\.ko\.md#](\1.md#g' 로 링크 치환).
- doc/guide/<core>.ko.md (코어 가이드) : 미러 doc/site/docs/guide/<core>.ko.md가
  서빙본이자 최신(정본↔미러 양방향 drift 존재). **미러에서 산문 추출·윤문**하고,
  변경 라인을 미러와 정본 양쪽에 매칭 적용(정본에 없는 drift 라인은 미러만).
- doc/plan/** · doc/principal/** · doc/internals/** : 미러 없음 → 정본만 윤문.

## 배치 루프 (멈추지 말 것)
1. 적격 잔여 목록을 만든다. 비면 §종료로.
2. 앞에서 최대 8개를 이번 배치로 잡는다.
3. 각 문서: 산문-only 추출 → humanize-monolith 실행 → 변경 매핑 적용(아래).
4. 배치 전체를 한 번에 검증·커밋한다. 처리/스킵한 경로를 ledger에 append.
5. §1로 돌아가 반복.

## 문서 1건 처리 절차
A. 산문-only 추출 (코드/표/탭/스니펫/mermaid 제거):
   awk '/^```/{c=!c;next} c{next} /^\s*\|/{next} /^===/{next} /--8<--/{next}{print}'
   대상 파일(코어는 미러, 그 외 정본) → _workspace/humanize/<slug>/01_input.txt
   추출 산문 < 600자면 윤문 불필요 — ledger에 "<path> (skip:too-short)" 기록하고 패스.
B. Agent 도구로 humanize-korean:humanize-monolith 호출:
   input_path: <abs>/01_input.txt
   quick_rules_path: <위 룰북 경로>
   genre_hint: 리포트
   프롬프트에 명시: "코드/표 제거한 순수 산문. 인라인 백틱 코드·API 식별자
   (zlink_*·ZLINK_*)·영문 기술용어·수치·패키지명·링크·명사 나열 쉼표는 보존.
   문체·번역투·C-11 쉼표만 다듬어라. final.md를 입력 폴더에 작성."
C. 변경 매핑(difflib): input vs final.md(HUMANIZE-SUMMARY 주석 앞까지)를
   difflib.SequenceMatcher로 정렬, 'replace' 중 동일 라인수 블록만 라인별
   (old,new) 추출. **보호 검사**: re.findall(r'`[^`]+`', old)==(new) 인 쌍만 채택
   (인라인 코드 보존). 채택 쌍을 대상 파일에 txt.replace(old,new,1)로 적용.
   코어 문서는 미러+정본 양쪽에 같은 쌍 적용. 바인딩은 정본 적용 후 미러 재생성.

## 배치 검증 (실패 시 그 문서만 되돌리고 계속)
- python3 doc/site/scripts/humanize_guard.py <이번 배치 변경 파일 전부>
  → 한 파일이라도 FAIL이면 그 파일을 git checkout 으로 되돌리고
    ledger에 "<path> (skip:guard-fail)" 기록.
- cd doc/site && mkdocs build  → 오류면 이번 배치 의심 파일을 되돌려 통과시킨다.
- python3 doc/site/scripts/check_doc_tabs.py  → 통과 필수.
- 빌드 산출물(bindings/node/dist-tools 등)은 git checkout 으로 복원(커밋 금지).
- _workspace/humanize/ 스크래치는 커밋하지 않는다(처리 후 삭제 가능).

## 커밋 (배치마다)
- 변경된 .ko.md/.md + 갱신된 ledger를 스테이징해 커밋. 메시지 예:
  "docs(humanize): batch N — <문서 수>건 산문 윤문 (humanize-korean)"
  본문에 문서별 변경률/등급 요약. 끝에:
  Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>

## 절대 규칙
- 코드·식별자·링크·스니펫·탭·표 셀은 한 글자도 바꾸지 마라(가드가 강제).
- C API 배열형(zlink_send 등)은 spec 컨벤션 — 절대 _part로 바꾸지 마라.
- 의미·수치·고유명사 불변. 변경률 30% 초과 문서는 되돌리고 skip 기록.
- main에서 작업. 측정·검증 없는 커밋 금지.

## 종료
적격 잔여가 0이 되면(모든 적격 문서가 ledger에 처리 또는 skip으로 기록됨)
최종 요약 1회: 총 처리 N건 / skip M건(사유별) / 총 커밋 수 / 가드·빌드 전건 통과.
그리고 종료한다.
```

---

## 짧은 버전 (goal 한 줄용)

```
zlink 리포(main)에서 doc/plan/documentation/humanize-all-execution-prompt.ko.md
절차대로, 남은 한글 산문 문서(doc/guide·plan·principal·internals/**/*.ko.md,
spec·log·draft 제외)를 humanize-korean monolith로 전부 윤문해라. 멈추지 말고
ledger(doc/site/scripts/humanize-ledger.txt) 밖 적격 문서가 0이 될 때까지 배치
반복: 산문-only 추출 → monolith → difflib 매핑(인라인 코드 보존 검사) → 코어는
미러+정본/바인딩은 정본+미러재생성 → humanize_guard·mkdocs build·check_doc_tabs
통과 → 배치 커밋·ledger 갱신. 코드·식별자·링크·탭·zlink_* 불변, C API 배열형
보존. humanize-monolith 미로드면 /reload-plugins 요청 후 중단. 적격 0이면 최종
요약 1회 후 종료. 커밋 끝에 Co-Authored-By: Claude Opus 4.8 (1M context)
<noreply@anthropic.com>.
```
