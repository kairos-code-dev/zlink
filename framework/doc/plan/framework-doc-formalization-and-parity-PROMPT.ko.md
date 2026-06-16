# 작업 프롬프트 — Framework 문서 정식화 + 기능 동등성(parity)

> 새 세션에 그대로 붙여 넣어 작업을 시작·완료시키는 지시문이다.
> 세부 절차·근거는 [framework-doc-formalization-and-parity-plan.ko.md](./framework-doc-formalization-and-parity-plan.ko.md)
> 에 있다. **시작 전에 계획서를 먼저 정독**하고, 어긋나면 계획서를 기준으로 한다.

---

`framework/` 문서를 코드 기준으로 최신화·정식화하고, `dotnet`을 기준 형태로 삼아
`java`/`kotlin`/`node` 가 동일한 기능·형태를 갖도록 맞춘다. 기능·샘플 갭은 codex
에이전트로 구현하고, 끝에서 임시 문서를 0개로 만든 뒤 codex로 최종 리뷰한다.

## 운영 규칙
- 진실 원천은 **코드**. 문서가 코드와 다르면 코드 기준으로 문서를 고친다.
- **엄격 순차, 병렬 금지.** 한 단계가 **그린(빌드+테스트 통과)** 이어야 다음으로 간다.
- 순서: **공통 문서 정리 → dotnet → (java, kotlin) → node → cpp → 최종 마감 → codex 리뷰.**
- **dotnet 은 코드 내용 최신화만**(형태·구조 변경 금지). dotnet 이 나머지의 기준 형태.
- **java/kotlin·node 는 dotnet 형태에 맞춰** 작성(장 구성·순서·역할 동일, 본문은 언어 특징만).
- **cpp 는 자기 형태 유지**, 별도 정리(case-study 복제·parity 순서에서 제외).
- 언어 고유 표현 차이(리플렉션/attribute/decorator/coroutine)는 갭이 아니다. **진짜 능력 갭만** 구현.
- 작업은 `main`. 조사는 read-only 에이전트, **구현·샘플·최종리뷰는 codex 에이전트**, 문서 편집은 직접.
- 문서 파일 추가/이동/삭제 시 회귀 테스트 하드코딩 목록을 **같은 변경에서** 갱신
  (dotnet `Documentation/Regression.cs`, cpp `test_cpp_framework_layout_contract.cpp`, 샘플 parity 게이트들).

## 확정 사항
- **공통 샘플 정본 6종**(`doc/spec/sample/`): Bingo, TicTacToe, SupportChat, DeliveryDispatch,
  ShoppingMallCheckout, GameQuest → 전 언어 동일 구현(코드 + `guide/samples/` 문서). 미구현분 codex로.
- **case-study(13–18) = 전량 복제**: dotnet `guide/case-studies/` 9개를 java/kotlin·node 에
  각 언어 문맥으로 전부 복제(코드 조각만 해당 언어). cpp 제외.

## 진행 절차 (각 단계 끝에 빌드+테스트 그린 확인 후 보고)
1. **공통 문서 정리(제일 먼저)**: `doc/spec/*` 코드 기준 점검, `spec/draft/`·`plan/` 임시 정리, README 갱신.
2. **dotnet**: 선결로 `DotNetDraftDocuments`를 실제 파일과 일치시켜 그린화 → 코드 대비 내용 최신화 →
   기능 체크리스트(feature-map+interface-catalog) 확정.
3. **java/kotlin**: 갭 분석(read-only) → **규모 보고·범위 합의 후** codex 기능 구현 →
   샘플 구현 → dotnet 형태로 문서 정렬·작성 + case-study 복제 → `draft/` 정리.
4. **node**: 갭 분석 → 보고·합의 → codex 구현 → 샘플 구현 → 문서 정렬·작성 + case-study 복제 →
   루트 plan·`draft/` 정리 → README 정식 승격.
5. **cpp**: 임시 문서 정리(contract 구조 유지/재편) → 코드 대비 최신화 → 샘플 보강.
6. **최종 마감**: README·링크 정합화 → 회귀 테스트 동기화 → **임시 문서 0개화**
   (`draft/`·`plan/`, IMPLEMENTATION-*, **이 프롬프트·계획 문서 포함**; 확정분은 정식 흡수, 이력만 `archive/`) → 전 테스트 그린.
7. **codex 최종 리뷰**: 문서↔코드 일치 / dotnet 형태 정합 / 샘플 6종 동등성 / case-study 복제 /
   기능 parity(갭 0) / 임시 문서 0개 점검 → 지적 수정·재검토 → 통과 시 완료.

## 주의
- 갭 분석 결과 구현이 클 수 있다. **무단 대형 구현 금지** — Phase 3·4 갭 보고 후 사용자와 범위 합의.
- cpp `implementation-plan`·`posd-log`는 contract test가 강제한다. 문서만 지우면 빌드 실패 →
  정식 internals로 재편하며 테스트를 함께 갱신할 것.
