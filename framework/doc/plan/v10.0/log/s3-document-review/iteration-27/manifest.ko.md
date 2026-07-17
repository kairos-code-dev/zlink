# S3 문서 독립 리뷰 범위 — iteration 27

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `27` |
| 동결 시각 | `2026-07-17T16:16:58,709032986+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `205` |
| 문서 집합 SHA-256 | `363f3945c8178b28e1b86ba87c69c4d3ae60ad740c551f1c788dca4e9b65f07f` |
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

## 4. iteration 26 이후 반영

- Entry Spot에서 user Spot으로 이동할 때도 target join, location commit, source leave, target joined 순서가
  적용되도록 execution-turn E2E를 정정했다.
- Stream Connector의 대기 표면은 두 dispatch mode 모두 아직 소비하지 않은 수신 큐를 직접 소비하고
  Manual callback pump와 독립적이라는 공통 의미를 고정했다. packet 이름의 정확한 인자와 overload는
  언어별 문서가 소유하도록 정리했다.
- HTTP codec registry와 Redis 장애 의미가 실제 계약 소유 문서를 가리키도록 잘못된 참조를 고쳤다.
- C++ classic fanout guide에서 topic을 transport 구독 필터처럼 표현한 그림을 제거하고 모든 subscriber가
  수신한 뒤 handler가 topic을 확인한다는 공개 동작으로 맞췄다.

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
| Codex agent | 실패 — finding 10개 | `codex-review.ko.md` | `e1bd5d33f193d8ccc39ffb6deb1f9090e8a742118efc8fd87c2762318df14a16` |
| Claude Sonnet | 실패 — finding 8개, 임시 파일 생성 | `claude-sonnet-review.ko.md` | `b562b0a0974eaba0914cf31509d0152c36526af5a5cf1e4f51ea5a4b75a9c036` |

두 reviewer 모두 시작·종료 동결 hash를 확인했지만 finding이 존재한다. Sonnet reviewer는 저장소와
동결 범위를 수정하지 않았으나 `/tmp`에 읽기용 임시 파일을 만들었으므로 파일 생성 금지 조건도 충족하지
못했다. iteration 27은 채택하지 않고 모든 finding을 수정한 뒤 새 동결본을 검토한다.
