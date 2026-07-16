# S3 문서 독립 리뷰 범위 — iteration 3

## 1. 검토 질문

Reviewer는 아래 질문을 같은 동결 범위에서 독립적으로 판정한다.

> Core와 framework의 10.0.0 목표 계약, 다섯 언어 exact interface, E2E·sample 공개 문서가 문서 원칙을
> 지키면서 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `3` |
| 동결 시각 | `2026-07-17T03:07:26+09:00` |
| 기준 commit | `b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4` |
| 검토 문서 수 | `221` |
| 문서 집합 SHA-256 | `fa592ab14863f414080303881e1c3a31dcecdac8e2f2946ddee642c479c24f9a` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Commit 뒤 문서 변경은 파일별 hash가 고정한다. reviewer는 시작과 종료 시 hash를 다시 계산하며 값이
다르면 결과를 채택하지 않는다.

## 3. 검토 범위

| 범위 | 파일 수 | 검토 내용 |
|---|---:|---|
| Core 정식 spec | 52 | 한국어·영문 공개 C ABI, 오류·소유권·lifecycle·동시성 계약 |
| framework 공통·server 정식 spec과 exact interface | 49 | 공통 의미와 .NET·C++·Java·Kotlin·Node.js 공개 signature |
| 공통 E2E | 12 | Config 1~11과 공통 검증 운영 계약 |
| 공통 sample | 11 | sample별 공개 사용 패턴과 계약 예제 |
| C++ 언어 E2E·sample 문서 | 19 | E2E 11개 lane, sample 6개 lane과 sample 루트 안내 |
| .NET 언어 E2E·sample 문서 | 20 | E2E 11개 lane, sample 7개 lane과 sample 루트 안내 |
| Java·Kotlin 언어 E2E·sample 문서 | 45 | 각 언어 E2E 11개 lane, sample 6개 lane과 sample 루트 안내 |
| Node.js 언어 E2E·sample 문서 | 13 | E2E 11개 lane과 sample 루트 안내 |

`framework/doc/framework/spec/90-implementation-gap.ko.md`와 언어별 `gaps/` 문서는 현재 구현 차이를
추적하는 비계약 문서이므로 목표 계약 검토 범위에서 제외한다. 구현 source와 runner는 S2 영향
inventory가 후속 구현 stage의 입력으로 관리한다.

## 4. 계약 근거

- 루트 `AGENTS.md`
- `doc/principal/documentation/documentation-principles.ko.md`
- `doc/principal/software-design-principles.md`
- `framework/doc/plan/v10.0/s1-core-public-api-inventory.ko.md`
- `framework/doc/plan/v10.0/s2-framework-contract-transition-inventory.ko.md`
- `framework/doc/plan/v10.0/s2-e2e-sample-runner-impact-inventory.ko.md`
- `framework/doc/contract-inventory/route-mesh-v10-contract-inventory.json`
- `framework/doc/contract-inventory/route-mesh-v10-dotnet-contract-inventory.json`
- `scripts/verify-framework-doc-contracts.sh`
- Redis location fixture 3개

Core 정식 spec은 승인된 10.0.0 목표 계약이다. 병렬 S4가 수정 중인 `core/include/`와 source는 S3
계약의 1차 소스로 사용하지 않는다. 기존 Core 공개 표면의 전수 분류는 S1 inventory의 기준 commit을
사용한다. Framework 공통 spec은 언어별 exact interface의 1차 소스다. 공통 E2E·sample 문서는 새
public API의 근거가 아니라 계약 검증 요구와 누락을 찾는 입력이다.

## 5. iteration 2 수정 범위

- 원칙 축 finding 9건과 1차 소스 축 finding 14건을 모두 수정했다.
- C++ exact interface와 Embedded HTTP spec에서 내부 구현 배치와 단계별 계획을 제거했다.
- Config 8의 27개 ID와 Config 10의 20개 ID를 대상 언어 feature map에 각각 독립 행으로 기록했다.
- Config 1·2·6의 누락 ID를 보완했다.
- 55개 feature map의 canonical scenario 행을 자동 검사한다.

## 6. 필수 리뷰 축

각 문서를 다음 두 축으로 각각 판정한다.

1. **원칙:** 독자와 질문, spec·guide·internals 책임, 문서 작성 원칙 1~9, 한국어 문체와 현재 상태 규칙
2. **1차 소스:** Core–framework result·error·ownership·callback·metadata·backpressure 일치,
   공통 계약의 다섯 언어 exact signature 투영, E2E·sample 예제의 정식 계약 사용

Reviewer는 이전 finding의 수정만 보지 않고 221개 전체 범위를 처음부터 검토한다.

## 7. 출력 계약

Finding마다 다음 형식을 사용한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 결과 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. Reviewer는 문서를
수정하지 않는다.
