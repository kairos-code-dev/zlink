# S3 문서 독립 리뷰 범위 — iteration 2

## 1. 독자와 검토 질문

이 문서는 RouteMesh 10.0.0 계약을 승인하는 reviewer가 같은 문서 집합과 같은 판단 기준으로 독립
검토하는 데 필요한 입력을 고정한다. Reviewer는 다음 질문에 답한다.

> Core와 framework의 10.0.0 목표 계약, 다섯 언어 exact interface, E2E·sample 공개 문서가 문서 원칙을
> 지키면서 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `2` |
| 동결 시각 | `2026-07-17T02:16:59+09:00` |
| 기준 commit | `b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4` |
| 검토 문서 수 | `221` |
| 문서 집합 SHA-256 | `12d908d39baa0a6bbaaaddbdedc465f464dc9bc3e8dc1c03494b0be49b36d749` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Commit 뒤 문서 변경은 파일별 hash가 고정한다. 별도 commit을 만들지 않았으므로 reviewer는 시작과
종료 시 hash를 다시 계산한다. 값이 다르면 해당 결과를 채택하지 않고 새 iteration을 만든다.

## 3. 검토 범위

| 범위 | 파일 수 | 검토 내용 |
|---|---:|---|
| Core 정식 spec | 52 | 한국어·영문 공개 C ABI, 오류·소유권·lifecycle·동시성 계약 |
| framework 공통·server 정식 spec과 exact interface | 49 | 공통 의미와 .NET·C++·Java·Kotlin·Node.js 공개 signature |
| 공통 E2E | 12 | Config 1~11과 공통 검증 운영 계약 |
| 공통 sample | 11 | sample별 공개 사용 패턴과 계약 예제 |
| C++ 언어 E2E·sample 문서 | 19 | E2E 11개 lane, sample 6개 lane과 언어별 sample 루트 안내 |
| .NET 언어 E2E·sample 문서 | 20 | E2E 11개 lane, sample 7개 lane과 언어별 sample 루트 안내 |
| Java·Kotlin 언어 E2E·sample 문서 | 45 | 각 언어 E2E 11개 lane, sample 6개 lane과 공통 sample 루트 안내 |
| Node.js 언어 E2E·sample 문서 | 13 | E2E 11개 lane과 언어별 sample 루트 안내 |

`framework/doc/framework/spec/90-implementation-gap.ko.md`와 언어별 `gaps/` 문서는 현재 구현 차이를
추적하는 비계약 문서이므로 10.0.0 목표 계약 검토 범위에서 제외한다. 이 경계는 ledger S2-18과 exact
verifier가 고정한다. Porting inventory, sample porting inventory, 구현 source와 runner도 목표 문서
범위에 포함하지 않는다. S2 영향 inventory가 구현 파일과 후속 검증 stage를 고정한다.

Iteration 1의 217개 범위에는 언어별 sample 루트 안내 4개가 빠졌다. 이 iteration은 네 문서를 추가하고
공통 sample 안내와 ZoneWorld의 현재 10.0.0 서술을 함께 정리한 221개 전체 범위를 새 hash로 고정한다.

## 4. 계약 근거와 검증 자료

- 루트 `AGENTS.md`
- `doc/principal/documentation/documentation-principles.ko.md`
- `doc/principal/software-design-principles.md`
- `framework/doc/plan/v10.0/s1-core-public-api-inventory.ko.md`
- `framework/doc/plan/v10.0/s2-framework-contract-transition-inventory.ko.md`
- `framework/doc/plan/v10.0/s2-e2e-sample-runner-impact-inventory.ko.md`
- `framework/doc/plan/v10.0/route-mesh-10.0.0-execution-ledger.ko.md`
- `framework/doc/contract-inventory/route-mesh-v10-contract-inventory.json`
- `framework/doc/contract-inventory/route-mesh-v10-dotnet-contract-inventory.json`
- `scripts/verify-framework-doc-contracts.sh`
- Redis location fixture 3개

Core 정식 spec은 승인된 10.0.0 목표 계약이다. 병렬 S4가 수정 중인 현재 `core/include/`와 source는
S3 계약의 1차 소스로 사용하지 않는다. 기존 Core 공개 표면의 전수 분류에는 S1 inventory가 고정한
기준 commit을 사용한다. Framework 공통 spec은 언어별 exact interface의 1차 소스다. 공통 E2E·sample
문서는 새 public API의 근거가 아니라 계약 검증 요구와 누락을 찾는 입력이다.

## 5. 필수 리뷰 축

각 문서를 두 축으로 각각 판정한다.

1. **원칙:** 독자와 질문, spec·guide·internals 책임, 문서 작성 원칙 1~9, 한국어 문체와 현재 상태 규칙
2. **1차 소스:** Core–framework result·error·ownership·callback·metadata·backpressure 일치,
   공통 계약의 다섯 언어 exact signature 투영, E2E·sample 예제의 정식 계약 사용

Core 공개 identifier inventory, MeshNode lifecycle과 peer admission, node/channel/Spot direct,
one-shot reply, Logical Multicast의 6개 count·metadata snapshot·기본 NoDrop, Spot subscription,
Actor transfer와 STREAM session, timer backend, monitoring, Redis schema, 다섯 언어 exact signature,
Config 1~11, sample·runner 영향, 제거 surface, POSD·DDD 책임 경계를 모두 확인한다.

## 6. finding 출력 계약

Reviewer는 파일을 수정하지 않는다. Finding마다 다음 형식을 사용한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Severity는 `blocker`, `high`, `medium`, `low` 중 하나다. 실제 위치와 대조 근거가 없는 추상적인
우려는 finding으로 기록하지 않는다. Finding이 하나도 없을 때만 결과의 마지막 줄을 정확히 다음과
같이 쓴다.

```text
DOC REVIEW CLEAN
```

## 7. 독립 실행

| Reviewer | 수정 권한 | 실행 식별자 | 원본 출력 |
|---|---|---|---|
| Codex agent | 없음 | `/root/s3_codex_review_i2` | `codex-raw-output.txt` |
| Claude Sonnet | 없음 | 검토 시작 전 기록 | `claude-sonnet-raw-output.json` |

두 reviewer는 서로의 결과를 읽지 않은 상태에서 같은 scope를 검토한다. Coordinator는 두 결과를 모두
받은 뒤 finding을 1차 소스로 검증한다. 수정이 한 건이라도 발생하면 새 scope hash로 두 리뷰를 모두
처음부터 다시 실행한다.
