# S3 문서 독립 리뷰 범위 — iteration 11

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `11` |
| 동결 시각 | `2026-07-17T08:46:13+09:00` |
| 기준 commit | `b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4` |
| 검토 문서 수 | `195` |
| 문서 집합 SHA-256 | `8d5851fd02395f8d80924a7feca67769d8bef121ca538e5471ed0a8976361023` |
| 파일 목록 SHA-256 | `ba3393d0b5d5516c83c26de6fb2255830db17aa38801c7a45be2d8c54600946a` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Reviewer는 시작과 종료 시 195개 파일별 hash와 aggregate를 다시 계산한다. 하나라도 다르면 결과를
채택하지 않는다.

## 3. 검토 범위

| 범위 | 파일 수 |
|---|---:|
| framework 공통·server 정식 spec과 server exact interface | 50 |
| Stream Connector 공통·언어별 exact 계약 | 5 |
| 관련 framework common·gap·HTTP error·C++/.NET/JVM/Node guide | 18 |
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
- scope 195개 전체
- `framework/doc/contract-inventory/route-mesh-v10-dotnet-contract-inventory.json`
  SHA-256 `c96334c9015a56426fa1f7bd0560aeb175e05a6533028c06d37d6fa4510667b2`
- `framework/doc/contract-inventory/route-mesh-v10-contract-inventory.json`
  SHA-256 `76eec47c0e6b13d8a7cc721fc31177371a10757d831947c6710e25e9a8c7b62c`
- `scripts/verify-framework-doc-contracts.sh`
  SHA-256 `0c333c128ddbb0b8a3c5318321e534874dfc1add63b4750dacb8764b6a3052a2`

## 5. iteration 10 finding 반영

- framework API의 handler filter·codec·dispatch action owner와 관련 절 링크를 정리하고 C++ guide를
  RouteMesh·MeshNode topology로 전환했다.
- 공통 message-flow observer와 runtime-error sink 계약을 하나의 owner에 고정하고 다섯 언어 exact
  interface, Config 5와 언어별 evidence를 같은 닫힌 값으로 정렬했다.
- Java/Kotlin drain과 timeout owner, Config 1 decode reason, Spot control claim을 정렬했다.
- 관련 guide·gap·HTTP error 문서, machine inventory와 verifier를 같은 계약으로 갱신했다.

## 6. 리뷰 축과 출력 계약

1. 문서 원칙, 독자 책임, 현재 10.0.0 서술
2. 공통 의미, 다섯 언어 exact signature, Connector·HTTP error, Config 1~11, sample·guide·gap

이전 finding 수정 확인으로 범위를 줄이지 말고 195개 전체를 처음부터 검토한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.
