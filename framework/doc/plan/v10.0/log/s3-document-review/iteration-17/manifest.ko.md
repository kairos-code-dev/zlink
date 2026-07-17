# S3 문서 독립 리뷰 범위 — iteration 17

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `17` |
| 동결 시각 | `2026-07-17T11:58:15+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `202` |
| 문서 집합 SHA-256 | `dd8d4039c8a5397900a561776053d1f26803925a8b7ea3e961a28c62410c9371` |
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
- route-mesh-v10-contract-inventory.json: `c0e905d49e720b74905c04daf3f6916447c19a0de37930ae795f9c8bb8e33b90`
- verify-framework-doc-contracts.sh: `e06b228d48d017fc5d7d835f2fd72b797516cf695a2561ea3d5a968c56ff5cbd`

## 4. iteration 16 이후 반영

- iteration 16은 리뷰 중 7개 scope 파일이 바뀌어 무효 처리했다.
- 실제 Markdown heading ID와 어긋난 내부 link 12개를 수정했다.
- C++ RM-C2의 없는 RID 오류를 `RequestTargetNotFound` 전환 gap으로 되돌렸다.
- Java/Kotlin의 기존 ChannelName runtime options와 10.0.0 exact route-mesh runtime options 사이 차이를
  공통 gap, Java gap과 두 feature-map에 기록했다.
- 계약 검증, 실제 Markdown render, JSON parse와 whitespace 검사를 다시 통과했다.

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
| Codex agent | 무효 — 리뷰 도중 scope drift | [`codex-review.ko.md`](./codex-review.ko.md) | `98519f811f0e349d8c94d2af10b7d565308e0a96123da26b0622f55a3c4cc27b` |
| Claude Sonnet | 무효 — 리뷰 도중 scope drift로 중단 | [`claude-sonnet-review.ko.md`](./claude-sonnet-review.ko.md) | `5b7f8e1bc54d5940ac1c9052cfe46d41713256bc3a7720c003545d80f615441a` |

iteration 17은 시작 hash가 일치했지만 리뷰 도중 구현 차이 문서 4개가 바뀌어 채택하지 않는다. 이
iteration의 결과는 finding이나 clean 판정으로 사용하지 않는다.
