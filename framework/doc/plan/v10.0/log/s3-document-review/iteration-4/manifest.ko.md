# S3 문서 독립 리뷰 범위 — iteration 4

## 1. 검토 질문

> Core와 framework의 10.0.0 목표 계약, 다섯 언어 exact interface, E2E·sample 공개 문서가 문서 원칙을
> 지키면서 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `4` |
| 동결 시각 | `2026-07-17T03:17:57+09:00` |
| 기준 commit | `b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4` |
| 검토 문서 수 | `221` |
| 문서 집합 SHA-256 | `f55b2bffe92f9576d5f330c62696a44c0ab277d0ff5a7421fbadf460852b9306` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Reviewer는 시작과 종료 시 같은 방식으로 scope hash를 계산한다. 값이 다르면 결과를 채택하지 않는다.

## 3. 검토 범위

| 범위 | 파일 수 |
|---|---:|
| Core 정식 spec | 52 |
| framework 공통·server 정식 spec과 exact interface | 49 |
| 공통 E2E | 12 |
| 공통 sample | 11 |
| C++ 언어 E2E·sample 문서 | 19 |
| .NET 언어 E2E·sample 문서 | 20 |
| Java·Kotlin 언어 E2E·sample 문서 | 45 |
| Node.js 언어 E2E·sample 문서 | 13 |

`90-implementation-gap.ko.md`, 언어별 gaps, 구현 source와 runner는 목표 계약 리뷰 범위가 아니다.
S2 영향 inventory가 구현 파일과 후속 검증 stage를 고정한다.

## 4. 계약 근거

- `AGENTS.md`
- `doc/principal/documentation/documentation-principles.ko.md`
- `doc/principal/software-design-principles.md`
- S1 Core public API inventory와 승인된 Core 정식 spec
- framework 공통·server spec과 다섯 언어 exact interface
- Config 1~11 공통 E2E와 sample 공개 문서
- route-mesh contract inventory와 Redis fixture
- `scripts/verify-framework-doc-contracts.sh`

병렬 S4의 현재 `core/include/`와 구현은 S3 계약의 1차 소스가 아니다. 기존 Core 표면의 분류에는 S1
기준 commit을 사용한다. 공통 E2E·sample은 새 public API의 근거가 아니라 검증 요구와 누락을 찾는
입력이다.

## 5. 이전 iteration 수정

- iteration 2의 원칙 9건과 1차 소스 14건을 수정했다.
- iteration 3의 coordinator finding 4건을 수정했다.
- C++ formal spec에서 내부 source 경로, 구현 library·worker·runtime type과 application sample을 제거했다.
- C++ 목차 번호와 교차 참조, Java·Node package 계약 제목을 정리했다.
- 55개 feature map에서 공통 Config의 scenario ID마다 독립 행이 존재하는지 자동 검사한다.

## 6. 리뷰 축

1. **원칙:** 독자와 질문, spec·guide·internals 책임, 문서 원칙 1~9, 현재 10.0.0 서술과 한국어 문체
2. **1차 소스:** Core–framework result·error·ownership·callback·metadata·NoDrop 일치, 다섯 언어 exact
   signature, Config 1~11, sample 공개 예제, Actor transfer·timer·location 의미

이전 finding의 수정만 확인하지 말고 221개 전체를 처음부터 검토한다.

## 7. 출력 계약

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.
