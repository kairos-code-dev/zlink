# S3 문서 독립 리뷰 범위 — iteration 26

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `26` |
| 동결 시각 | `2026-07-17T15:39:13,995844944+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `205` |
| 문서 집합 SHA-256 | `f7d6e6c8b1d6ab2db6eb9a3001d503a988108b7079e3cfcb6bfb42853a0045d7` |
| 파일 목록 SHA-256 | `06f3695c8571e2c253c8f7af522dbe7d9fd54596c94eb5f724743c392087470d` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Reviewer는 시작과 종료 시 205개 파일별 hash와 aggregate를 다시 계산한다. 하나라도 다르면 결과를
채택하지 않는다. 이 manifest가 리뷰 중인 동안 다른 stage는 scope 파일을 수정하지 않는다.

## 3. 검토 범위와 기준

Framework 공통·server 정식 spec, 다섯 언어 exact interface, Connector, 관련 guide·gap, 공통·언어별
E2E·sample 문서 205개 전체다. Core 문서는 Framework finding 때문에 특정 Core 계약 확인이 필요할 때만
교차 확인하며 병렬 S4 구현은 계약 근거로 사용하지 않는다. 공통 E2E·sample은 새 public API 근거가 아니다.

반드시 루트 `AGENTS.md`, `doc/principal/documentation/documentation-principles.ko.md`,
`doc/principal/software-design-principles.md`, 이 manifest와 scope 전체를 읽는다.

검증 입력:

- route-mesh-v10-dotnet-contract-inventory.json: `6a343fae5574c154e8130b16cbfcfdeb1c55cc2887c88d74652372e7a2d240fe`
- route-mesh-v10-contract-inventory.json: `132e964e517eb5d9dfd18150fc038206661fc147fb9beff0a23d6ed796ce2752`
- verify-framework-doc-contracts.sh: `5d44fcdcc392c4b9cbbd2284e971be3e0d1e0745bdcc82005988e95d8e594fbb`

## 4. iteration 25 이후 반영

- C++ Spot context를 `spot_handle_t` 기반 outbound로 확정하고 옛 node RID+spot RID 목표 시그니처를
  제거했다. Source 적용은 `IMP-CP-03`으로 유지했다.
- C++ handler group builder와 Stream Connector `error_code_t` 닫힌 집합을 선언했다. Source의 계약 밖
  오류 값 3개는 `IMP-CP-49`로 열었다.
- Java manual RouteMesh 연결, actor factory·lifecycle, monitoring 예제를 exact interface에 맞췄다.
- Kotlin generic payload와 fanout 반환형, Node Nest STREAM actor dispatch, C++ HTTP, TypeScript wait predicate,
  Java·Kotlin·Node guide 호출 인자를 바로잡았다.
- .NET STREAM→Spot relay 예제를 `SpotHandle` resolve와 `SendToSpot` 경로로 교체하고 과거 재리뷰 문구가
  현재 gap을 부정하지 않도록 정리했다.

## 5. 리뷰어 정책과 출력 계약

이 iteration은 문서 리뷰이므로 Codex agent와 Claude Sonnet만 사용한다. Claude Fable은 코드와 구현
결과를 검토하는 단계에서만 사용하며 이 문서 리뷰에는 사용하지 않는다.

이전 finding만 확인하지 말고 205개 전체를 처음부터 검토한다. 자체 slug 추정으로 링크 finding을
확정하지 말고 실제 pymdownx renderer로 재현한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.

## 6. 리뷰 결과

| reviewer | 결과 | output | SHA-256 |
|---|---|---|---|
| Codex agent | finding 5건 | [`codex-review.ko.md`](./codex-review.ko.md) | `441243506dfb62024c9f1fd4c514c701f1d50d2b5132244cc272d9552a67c4cb` |
| Claude Sonnet | finding 1건 | [`claude-sonnet-review.ko.md`](./claude-sonnet-review.ko.md) | `b759b955e9243730851130d72160993caecda1456f52776ca67a720a6a768e11` |

두 reviewer가 모두 finding을 보고했으므로 iteration 26은 무효다. finding을 반영한 새 동결본을
iteration 27에서 처음부터 다시 검토한다.
