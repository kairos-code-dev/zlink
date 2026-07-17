# S3 문서 독립 리뷰 범위 — iteration 25

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `25` |
| 동결 시각 | `2026-07-17T15:03:34,164943286+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `205` |
| 문서 집합 SHA-256 | `531e3ad5f8252ea95f8f77054080e399153134c10c1a2c73e76fd4c0ab2a7097` |
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
- route-mesh-v10-contract-inventory.json: `5574c118fd89cfa8d6e113ab2ff5af149bd668a96f2809219200e9a66be9fc38`
- verify-framework-doc-contracts.sh: `5d44fcdcc392c4b9cbbd2284e971be3e0d1e0745bdcc82005988e95d8e594fbb`

## 4. iteration 24 이후 반영

- `RemoteError`를 유효한 Error payload의 공통 오류로 정의하고 matching request와 error event 전달을
  분리했다. Error JSON payload decode 실패는 request/event만 실패시키고 연결을 유지하도록 오류 행렬을
  세분했다.
- Java connector의 계약 밖 `flowFrom(...)` 선언을 제거하고 callback 실행 범위의 내부 flow context
  전파를 정식화했다.
- Node HTTP timeout의 안정적인 `cause.name == "TimeoutError"` 식별 계약과
  `ZLinkStreamSessionError.Internal` 목표 선언을 추가하고 source 차이를 `IMP-ND-39/40`으로 열었다.
- Node SpotService의 `SM-D10`, `SM-F3`, `SM-F5`와 묶음 판정을 실제 10.0.0 검증 상태에 맞췄다.
- C++ guide의 Kotlin 자동 등록 비교와 Java·Node exact 문서의 절 번호를 바로잡았다.
- Codex가 제안한 C++ `#상세-1` anchor는 실제 pymdownx render에서 오류 2건을 만들었다.
  기존 `#상세_1`이 실제 생성 anchor임을 확인해 변경하지 않았다.

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
| Codex agent | finding 17건 | [`codex-review.ko.md`](./codex-review.ko.md) | `61a31f79710f49aa80b7cf5c78918754d7e56120e0b21a6217426ac0120fe03b` |
| Claude Sonnet | finding 1건 | [`claude-sonnet-review.ko.md`](./claude-sonnet-review.ko.md) | `855dba55833a657f08fb544b22cc5e785c9d0b0529574094bebe460145507738` |

두 reviewer의 시작·종료 HEAD, scope 목록과 파일별 hash는 모두 동결값과 일치했다. finding 18건 때문에
iteration 25는 무효다. 수정한 뒤 새 iteration에서 205개 전체 독립 리뷰를 다시 시작한다.
