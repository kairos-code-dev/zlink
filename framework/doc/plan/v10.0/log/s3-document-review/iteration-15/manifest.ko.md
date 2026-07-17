# S3 문서 독립 리뷰 범위 — iteration 15

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `15` |
| 동결 시각 | `2026-07-17T11:46:45+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `202` |
| 문서 집합 SHA-256 | `7d1e933b2a5b8ce3e13b72a605e7f055abd5656647a1c7aea9786fc3443b5f84` |
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

## 4. iteration 14 finding 반영

- durable Actor location row commit과 ready route 공개 시점을 분리했다.
- transfer duration terminal을 activation 또는 실패 terminal로 정렬했다.
- C++ channel request 예제의 MeshName을 보완하고 HTTP C++ timeout을 공개 `code()`로 표현했다.
- C++ transfer gap의 폐기 순서와 message-flow stale reason/action 값을 정식 계약에 맞췄다.
- C++·Java·Kotlin guide의 금지 문체와 구어체를 정리했다.
- 실제 Markdown 렌더 결과를 기준으로 내부 heading link 12개와 검증기의 연속 공백 slug 처리를 고쳤다.
- iteration 14 review 중 반영된 18개 동시 변경도 이 scope에 포함해 처음부터 다시 검토한다.

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
| Codex agent | 시작 hash 불일치로 무효 | [`codex-review.ko.md`](./codex-review.ko.md) | 검증 기록에 기재 |
| Claude Sonnet | scope drift 확인 뒤 중단 | [`claude-sonnet-review.ko.md`](./claude-sonnet-review.ko.md) | raw output hash는 검증 기록에 기재 |

두 reviewer 모두 clean 판정에 사용하지 않는다. 수정 작업자가 모두 종료한 뒤 안정 hash로 새 iteration을
동결한다.
