# S3 문서 독립 리뷰 범위 — iteration 1

## 1. 독자와 검토 질문

이 문서는 RouteMesh 10.0.0 계약을 승인하는 reviewer가 같은 문서 집합과 같은 판단 기준으로 독립
검토하는 데 필요한 입력을 고정한다. reviewer가 답해야 하는 질문은 다음과 같다.

> Core와 framework의 10.0.0 목표 계약, 다섯 언어 exact interface, E2E·sample 공개 문서가 문서 원칙을
> 지키면서 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `1` |
| 동결 시각 | `2026-07-17T01:49:53+09:00` |
| 기준 commit | `b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4` |
| 검토 문서 수 | `217` |
| 문서 집합 SHA-256 | `d4c21b27cf4ca9f9084e1f42afd2c9a476adea159c97184624e8a94bfc54f2af` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

commit 이후의 문서 변경은 `scope-files.sha256`이 고정한다. 사용자가 commit을 요청하지 않았으므로
리뷰를 위해 별도 commit을 만들지 않는다. 리뷰가 끝날 때 같은 집합 hash를 다시 계산하며, 값이 다르면
해당 결과를 인정하지 않고 새 iteration을 만든다.

## 3. 검토 범위

| 범위 | 파일 수 | 검토 내용 |
|---|---:|---|
| Core 정식 spec | 52 | 한국어·영문 공개 C ABI, 오류·소유권·lifecycle·동시성 계약 |
| framework 공통·server 정식 spec과 exact interface | 49 | 공통 의미와 .NET·C++·Java·Kotlin·Node 공개 signature |
| 공통 E2E | 12 | Config 1~11과 공통 검증 운영 계약 |
| 공통 sample | 11 | 샘플별 공개 사용 패턴과 계약 예제 |
| C++ 언어 E2E·sample 문서 | 18 | 11개 E2E lane과 sample 공개 문서 |
| .NET 언어 E2E·sample 문서 | 19 | 11개 E2E lane과 sample 공개 문서 |
| Java·Kotlin 언어 E2E·sample 문서 | 44 | 각 언어 11개 E2E lane과 sample 공개 문서 |
| Node.js 언어 E2E 문서 | 12 | 11개 E2E lane과 공개 설명 |

`porting-inventory.ko.md`, `sample-porting-inventory.ko.md`, 구현 source와 runner는 목표 문서 범위에
포함하지 않는다. S2 영향 inventory가 해당 구현 파일과 S8·S9 검증 stage를 고정한다.

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
- `framework/testdata/location/redis/actor-location-v2.json`
- `framework/testdata/location/redis/mesh-node-descriptor-v1.json`
- `framework/testdata/location/redis/actor-transfer-v1.json`

Core 정식 spec은 승인된 10.0.0 목표 계약이다. 병렬 S4가 수정 중인 현재 `core/include/`와 source는
S3 계약의 1차 소스로 사용하지 않는다. 기존 Core 공개 표면의 전수 분류에는
`s1-core-public-api-inventory.ko.md`가 고정한 기준 commit을 사용한다. Framework 공통 spec은 언어별
exact interface의 1차 소스이고, 공통 E2E·sample 문서는 새 public API의 근거가 아니라 계약 검증
요구와 누락을 찾는 입력이다.

## 5. 필수 리뷰 축

각 문서를 다음 두 축으로 나누어 검토한다.

1. **원칙:** 문서의 독자와 질문이 분명한지, spec·guide·internals 책임이 섞이지 않는지,
   `documentation-principles.ko.md` 원칙 1~9와 한국어 문체·현재 상태 규칙을 지키는지 확인한다.
2. **1차 소스:** Core–framework 결과·오류·ownership·callback·metadata·backpressure 의미가 일치하는지,
   공통 계약이 다섯 언어 exact signature에 빠짐없이 투영되는지, E2E·sample 예제가 정식 계약만
   사용하는지 확인한다.

다음 항목을 반드시 포함한다.

- Core 공개 identifier inventory, 한국어·영문 signature와 result table
- MeshNode lifecycle, manual·location-store peer admission과 descriptor revision
- node/channel/Spot direct send·request와 one-shot reply token
- Logical Multicast metadata snapshot, remote·local 6개 count, 기본 NoDrop와 부분 전달 금지
- Spot local subscription, Actor transfer authority·64-byte token·membership epoch
- STREAM session과 Actor mailbox ownership, timer backend 경계
- runtime monitoring, message flow, graceful drain과 오류 mapping
- Redis descriptor·location·transfer schema와 byte fixture
- 다섯 언어 root·builder·handler·client·location·observation exact signature
- Config 1~11, sample과 package/runner deferred gate의 누락 여부
- 제거된 topology, bridge, SpotNode PUB/SUB plane와 reply metadata setter의 잔존 여부
- POSD의 깊은 모듈·정보 은닉과 DDD의 MeshNode·Spot·Actor·session 책임 경계

## 6. finding 출력 계약

reviewer는 파일을 수정하지 않는다. finding마다 다음 형식을 사용한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

`severity`는 `blocker`, `high`, `medium`, `low` 중 하나다. 추상적인 우려만 기록하지 않고 실제 문서
위치와 대조한 계약을 제시한다. finding이 하나도 없을 때만 결과의 마지막 줄을 정확히 다음과 같이 쓴다.

```text
DOC REVIEW CLEAN
```

## 7. 독립 실행

| Reviewer | 수정 권한 | 실행 식별자 | 원본 출력 |
|---|---|---|---|
| Codex agent | 없음 | `/root/s3_codex_review_i1` | `codex-raw-output.txt` |
| Claude Sonnet | 없음 | `claude-sonnet-s3-i1`, Claude Code `2.1.211`, `--model sonnet --effort high` | `claude-sonnet-raw-output.json` |

두 reviewer는 서로의 결과를 읽지 않은 상태에서 이 manifest와 같은 scope를 검토한다. 첫 결과를 모두
받은 뒤 coordinator가 finding을 1차 소스로 검증한다. 수정이 한 건이라도 발생하면 iteration 1의 clean
여부와 관계없이 새 scope hash로 두 리뷰를 모두 다시 실행한다.
