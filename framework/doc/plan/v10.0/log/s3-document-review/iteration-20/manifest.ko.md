# S3 문서 독립 리뷰 범위 — iteration 20

> **무효 iteration.** Codex 검토 중 동결 문서가 새로 정식화한 Actor join commit, STREAM Actor dispatch,
> durable Actor transfer, C++ STREAM mTLS, Actor transfer metric과 수신 content-type 계약이 현재 구현과
> 다른데 gap 문서가 이를 완료로 표시하거나 누락한 사실을 확인했다. Claude Sonnet 실행은 finding
> 반영 전에 중단했으며 verdict를 채택하지 않는다. 수정한 전체 범위는 다음 iteration에서 다시 동결한다.

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `20` |
| 동결 시각 | `2026-07-17T12:33:53,143237868+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `205` |
| 문서 집합 SHA-256 | `f5558168620eea61ae3438841126f49b2b5ed6a92df97060ff25e479e4995324` |
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

- route-mesh-v10-dotnet-contract-inventory.json: `21c1833b6f6b1a61e4d07ef94cce38800da23021906ec36092a800512c3d1aff`
- route-mesh-v10-contract-inventory.json: `e97f1f8d3fb0758264ec4270f214a4e575c85727f2f0af85a6b036d8b9c147d0`
- verify-framework-doc-contracts.sh: `d3d33d103233e3759401bffdc1cf4c530af620d919bb4fc5235caa923ab9150a`

## 4. iteration 19 이후 반영

- iteration 19는 동결 뒤 Java/Kotlin worker cancellation과 다섯 언어 Actor location Spot generation의
  목표 계약 대비 미구현 상태가 gap 문서에 정확히 기록되지 않은 사실을 확인하여 무효 처리했다.
- 공통 gap matrix·설명과 다섯 언어 gap 문서를 실제 구현 상태에 맞게 고쳤다.
- exact interface 문서에는 목표 계약 적용 추적 표를 추가하고 Java/Kotlin actor-session guide 예제를
  cancellation 인자 계약에 맞췄다.
- iteration 19에서 빠졌던 Kotlin gap 문서와 변경된 Java/Kotlin actor-session guide를 범위에 포함했다.
- exact contract, Markdown 링크, JSON parse와 whitespace 검사를 다시 통과한 뒤 이 revision을 동결했다.

## 5. 리뷰어 정책과 출력 계약

이 iteration은 문서 리뷰이므로 Codex agent와 Claude Sonnet만 사용한다. Claude Fable은 코드와 구현
결과를 검토하는 단계에서만 사용하며 이 문서 리뷰에는 사용하지 않는다.

1. 문서 원칙, 독자 책임, 현재 10.0.0 서술
2. 공통 의미, 다섯 언어 exact signature, Connector·HTTP error, Config 1~11, sample·guide·gap

이전 finding만 확인하지 말고 205개 전체를 처음부터 검토한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.

## 6. 리뷰 결과

| reviewer | 결과 | output | SHA-256 |
|---|---|---|---|
| Codex agent | finding — iteration 무효 | 별도 clean output 없음 | 해당 없음 |
| Claude Sonnet | 중단 — verdict 미채택 | session `14a76ec8-0493-475e-a590-bf380bd3f062` | 해당 없음 |
