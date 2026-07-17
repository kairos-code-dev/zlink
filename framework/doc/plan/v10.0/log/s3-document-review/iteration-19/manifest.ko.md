# S3 문서 독립 리뷰 범위 — iteration 19

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `19` |
| 동결 시각 | `2026-07-17T12:21:05,215624302+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `202` |
| 문서 집합 SHA-256 | `26b4763a83217a018071e9f050cbc5d68aa9e744544d182fe625b40e518d9cd3` |
| 파일 목록 SHA-256 | `dfabc81f2c0ec7403683bc54ab3f350d9cf6565aac60a3baf9579192c513642f` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Reviewer는 시작과 종료 시 202개 파일별 hash와 aggregate를 다시 계산한다. 하나라도 다르면 결과를
채택하지 않는다. 이 manifest가 리뷰 중인 동안 다른 stage는 scope 파일을 수정하지 않는다.

## 3. 검토 범위와 기준

Framework 공통·server 정식 spec, 다섯 언어 exact interface, Connector, 관련 guide·gap, 공통·언어별
E2E·sample 문서 202개 전체다. Core 문서는 Framework finding 때문에 특정 Core 계약 확인이 필요할 때만
교차 확인하며 병렬 S4 구현은 계약 근거로 사용하지 않는다. 공통 E2E·sample은 새 public API 근거가 아니다.

반드시 루트 `AGENTS.md`, `doc/principal/documentation/documentation-principles.ko.md`,
`doc/principal/software-design-principles.md`, 이 manifest와 scope 전체를 읽는다.

검증 입력:

- route-mesh-v10-dotnet-contract-inventory.json: `21c1833b6f6b1a61e4d07ef94cce38800da23021906ec36092a800512c3d1aff`
- route-mesh-v10-contract-inventory.json: `e97f1f8d3fb0758264ec4270f214a4e575c85727f2f0af85a6b036d8b9c147d0`
- verify-framework-doc-contracts.sh: `d3d33d103233e3759401bffdc1cf4c530af620d919bb4fc5235caa923ab9150a`

## 4. iteration 18 이후 반영

- iteration 18은 리뷰 도중 scope 파일 8개가 바뀌어 무효 처리했다.
- Sonnet이 참고용으로 보고한 Java interface catalog의 stale 이전·다음 navigation을 실제 문서 순서에
  맞게 수정했다.
- 변경 뒤 exact contract, 실제 Markdown render, JSON parse와 whitespace 검사를 다시 통과했다.

## 5. 리뷰어 정책과 출력 계약

이 iteration은 문서 리뷰이므로 Codex agent와 Claude Sonnet만 사용한다. Claude Fable은 코드와 구현
결과를 검토하는 단계에서만 사용하며 이 문서 리뷰에는 사용하지 않는다.

1. 문서 원칙, 독자 책임, 현재 10.0.0 서술
2. 공통 의미, 다섯 언어 exact signature, Connector·HTTP error, Config 1~11, sample·guide·gap

이전 finding만 확인하지 말고 202개 전체를 처음부터 검토한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.

## 6. 리뷰 결과

| reviewer | 결과 | output | SHA-256 |
|---|---|---|---|
| Codex agent | 무효 — 리뷰 도중 scope drift | [`codex-review.ko.md`](./codex-review.ko.md) | `a59dbeacbcce78adfe81e9f5f1f7d086d2adc677fb31e17a388837ec886877ba` |
| Claude Sonnet | 무효 — 리뷰 도중 scope drift로 중단 | [`claude-sonnet-review.ko.md`](./claude-sonnet-review.ko.md) | `9f4c752a3c673c9f00e0c6585bfcd13a7c82d6dfd9c7179081c0de63477f7836` |

iteration 19는 시작 hash가 일치했지만 리뷰 도중 implementation-gap 문서가 바뀌어 채택하지 않는다. 이
결과는 finding이나 clean 판정으로 사용하지 않는다.
