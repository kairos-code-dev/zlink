# S3 문서 독립 리뷰 범위 — iteration 18

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `18` |
| 동결 시각 | `2026-07-17T12:14:30+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `202` |
| 문서 집합 SHA-256 | `583b8bab212c653e65bbd564704f22c2bb9c27ed9543993a4da3c336088ab6ea` |
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
- verify-framework-doc-contracts.sh: `f62507a4970ea1cf8c066bf8970c7a419283c3bba3da4c73659fe2cda8882997`

## 4. iteration 17 이후 반영

- iteration 17은 리뷰 도중 공통 implementation-gap과 언어별 gap 문서가 바뀌어 무효 처리했다.
- 별도 구현 작업이 갱신한 현재 gap 내용을 유지했다.
- 실제 pymdownx heading ID와 어긋난 내부 link 13개를 복구했다.
- verifier의 slug 계산을 실제 pymdownx 규칙과 다시 맞췄다.
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
| Codex agent | 무효 — 리뷰 도중 scope drift | [`codex-review.ko.md`](./codex-review.ko.md) | `1cbc36b10b20a8741cecf1c3be911efeaf20a208c3e8b187b8ad7c83c2fd1942` |
| Claude Sonnet | 무효 — 리뷰 도중 scope drift로 중단 | [`claude-sonnet-review.ko.md`](./claude-sonnet-review.ko.md) | `daa58e7600cebfc492e14e7a01c1c4a84eecb7b0396a07cb9f5d29cddbba8878` |

iteration 18은 시작 hash가 일치했지만 리뷰 도중 Java exact interface가 바뀌었고, 이후 S2 완료 감사
finding도 반영했으므로 채택하지 않는다. 이 결과는 finding이나 clean 판정으로 사용하지 않는다.
