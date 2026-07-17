# S3 문서 독립 리뷰 범위 — iteration 14

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `14` |
| 동결 시각 | `2026-07-17T11:20:58+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `202` |
| 문서 집합 SHA-256 | `595151344f36b8c5eaf76ee8b4bdfec9e9bbbb5fcc509f6b6f51c20d23bc1dd3` |
| 파일 목록 SHA-256 | `dfabc81f2c0ec7403683bc54ab3f350d9cf6565aac60a3baf9579192c513642f` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Reviewer는 시작과 종료 시 202개 파일별 hash와 aggregate를 다시 계산한다. 하나라도 다르면 결과를
채택하지 않는다.

## 3. 검토 범위와 기준

범위는 iteration 13과 같은 framework 공통·server 정식 spec, 다섯 언어 exact interface, Connector,
관련 guide·gap, 공통·언어별 E2E·sample 문서 202개다. Core 문서는 Framework finding 때문에 특정 Core
계약 확인이 필요할 때만 교차 확인하며 병렬 S4 구현은 계약 근거로 사용하지 않는다. 공통 E2E·sample은
새 public API의 근거가 아니다.

반드시 루트 `AGENTS.md`, `doc/principal/documentation/documentation-principles.ko.md`,
`doc/principal/software-design-principles.md`, 이 manifest와 scope 전체를 읽는다.

검증 입력:

- route-mesh-v10-dotnet-contract-inventory.json: `66d0fc2096b300614afa1c46bb1324fab8bb6db61ddee6840df210c028995541`
- route-mesh-v10-contract-inventory.json: `494e105ad6eec31351f4d8a0255c42f6e7f8dd593ff16b09edfd126d1989d944`
- verify-framework-doc-contracts.sh: `f62507a4970ea1cf8c066bf8970c7a419283c3bba3da4c73659fe2cda8882997`

## 4. iteration 13 finding 반영

- Actor join commit을 location CAS 뒤 source leave 순서로 정렬했다.
- Actor location에 Spot lifecycle generation을 추가하고 다섯 언어 exact·Redis fixture·inventory와
  verifier에서 강제했다.
- Redis transfer key를 length-prefix actor row key로 통일했다.
- server·HTTP host·Connector codec instance owner를 분리하고 explicit codec mismatch를
  `PayloadDecodeFailed`로 닫았다.
- transfer metric terminal을 activation과 실패 terminal로 통일했다.
- Kotlin TA-A3·A4와 Kotlin·Node ST-B3의 실제 구현 차이를 완료 증거에서 제외했고 Kotlin STREAM
  `close()`를 공개 동작에 맞췄다.
- 실제 pymdownx 렌더에서 발견한 anchor 오류 12건을 정정했다.

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
| Codex agent | 종료 hash 불일치, 참고 finding 7건 | [`codex-review.ko.md`](./codex-review.ko.md) | 검증 기록에 기재 |
| Claude Sonnet | scope drift로 중단 | [`claude-sonnet-review.ko.md`](./claude-sonnet-review.ko.md) | raw output hash는 검증 기록에 기재 |

두 reviewer 모두 종료 hash 조건을 충족하지 못했으므로 iteration 14는 clean 판정에 사용하지 않는다.
Codex의 시작 snapshot finding은 [`finding-ledger.ko.md`](./finding-ledger.ko.md)에 수정 입력으로 기록했다.
