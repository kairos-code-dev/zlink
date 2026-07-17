# S3 문서 독립 리뷰 범위 — iteration 12

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `12` |
| 동결 시각 | `2026-07-17T09:45:02+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `202` |
| 문서 집합 SHA-256 | `9a41f1d2a3961d30dc68ee68039669b4ef2751ba3ce4684d1b993dad2baacb76` |
| 파일 목록 SHA-256 | `dfabc81f2c0ec7403683bc54ab3f350d9cf6565aac60a3baf9579192c513642f` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Reviewer는 시작과 종료 시 202개 파일별 hash와 aggregate를 다시 계산한다. 하나라도 다르면 결과를
채택하지 않는다.

## 3. 검토 범위

| 범위 | 파일 수 |
|---|---:|
| framework 공통·server 정식 spec과 server exact interface | 50 |
| Stream Connector 공통·언어별 exact 계약 | 5 |
| 관련 framework common·gap·HTTP error·C++/.NET/JVM/Node guide | 25 |
| 공통 E2E | 12 |
| 공통 sample | 10 |
| C++ 언어 E2E·sample 문서 | 19 |
| .NET 언어 E2E·sample 문서 | 20 |
| Java·Kotlin 언어 E2E·sample·porting inventory | 47 |
| Node.js 언어 E2E·sample·porting inventory | 14 |

Core 문서는 사용자 지시에 따라 전수 리뷰하지 않는다. Framework finding 때문에 특정 Core 계약 확인이
필요할 때만 관련 Core 정식 문서를 교차 확인한다. 현재 Core 구현은 병렬 S4 변경물이므로 계약 근거로
사용하지 않는다. 공통 E2E·sample은 새 public API의 근거가 아니다.

## 4. 반드시 읽을 기준과 검증 입력

- 루트 `AGENTS.md`
- `doc/principal/documentation/documentation-principles.ko.md`
- `doc/principal/software-design-principles.md`
- 이 manifest와 `scope-files.txt`
- scope 202개 전체
- `framework/doc/contract-inventory/route-mesh-v10-dotnet-contract-inventory.json`
  SHA-256 `c96334c9015a56426fa1f7bd0560aeb175e05a6533028c06d37d6fa4510667b2`
- `framework/doc/contract-inventory/route-mesh-v10-contract-inventory.json`
  SHA-256 `56de69bf785778c10a8c7dfbf315ea2433dac8937cd5e7c2c6bbc8667bd6ca59`
- `scripts/verify-framework-doc-contracts.sh`
  SHA-256 `f85020b56d339db16b8d0373237f62e7c4d2b2abfd31d4162294ac64c1a8aaae`

## 5. iteration 11 finding 반영

- common README의 target-first 규칙과 Actor lifecycle owner를 정렬하고 .NET·Node channel guide를
  RouteMesh API로 전환했다.
- C++ messaging facade와 Java·Kotlin·C++ worker cancellation exact interface를 완결하고 관련 C++
  가이드 7개를 동결 범위에 추가했다.
- 추가 C++ 가이드의 제거된 client/server·SpotMesh builder, 자동 bridge·구형 Registry 설명과 내부 socket
  배선을 RouteMesh·MeshNode·Redis location store의 사용자 관점 흐름으로 전환했다.
- HTTP client timeout·closed 오류 경계와 정식 계약에서 구현 이력을 분리했다.
- Config 1~3 dispatch reason/action과 C++·.NET·Java gap 및 Java exact 문서의 실제 anchor를 정정했다.
- 계약 inventory, code fixture와 실제 Markdown anchor 검증을 변경 계약에 맞춰 갱신했다.

## 6. 리뷰 축과 출력 계약

1. 문서 원칙, 독자 책임, 현재 10.0.0 서술
2. 공통 의미, 다섯 언어 exact signature, Connector·HTTP error, Config 1~11, sample·guide·gap

이전 finding 수정 확인으로 범위를 줄이지 말고 202개 전체를 처음부터 검토한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.

## 7. 리뷰 결과

| reviewer | 결과 | output | SHA-256 |
|---|---|---|---|
| Codex agent | finding 9건 | [`codex-review.ko.md`](./codex-review.ko.md) | `0c5188d307aa04e8e9ea3038e32e4eb324b034a32e79634f2ecc621cc434ec21` |
| Claude Sonnet | finding 2건 | [`claude-sonnet-raw-output.json`](./claude-sonnet-raw-output.json) | `8c0b3266e84ea6210ab4be5b0a6ff945d9e2a41c1ea8748c4ff980345618d784` |

두 reviewer 모두 finding을 보고했으므로 iteration 12는 `DOC REVIEW CLEAN`이 아니다. 병합 결과와 red
gate는 [`finding-ledger.ko.md`](./finding-ledger.ko.md)에 기록했다.
