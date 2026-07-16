# S3 문서 독립 리뷰 범위 — iteration 8

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 server exact interface, 영향을 받은 Stream Connector 계약,
> E2E·sample 공개 문서가 문서 원칙을 지키면서 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `8` |
| 동결 시각 | `2026-07-17T06:40:45+09:00` |
| 기준 commit | `b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4` |
| 검토 문서 수 | `177` |
| 문서 집합 SHA-256 | `6c495cd54c4e69f84c5a809badb65d177cbd177d5ad9b3809db12f14d2138498` |
| 파일 목록 SHA-256 | `9f072f1af2b73fde7c08143da66cb651bae33074bfc915a45d18431ccf345f25` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Reviewer는 시작과 종료 시 177개 파일별 hash와 aggregate를 다시 계산한다. 하나라도 다르면 결과를
채택하지 않는다.

## 3. 검토 범위

| 범위 | 파일 수 |
|---|---:|
| framework 공통·server 정식 spec과 server exact interface | 50 |
| 영향을 받은 Stream Connector 공통·언어별 exact 계약 | 5 |
| 공통 E2E | 12 |
| 공통 sample | 10 |
| C++ 언어 E2E·sample 문서 | 19 |
| .NET 언어 E2E·sample 문서 | 20 |
| Java·Kotlin 언어 E2E·sample·porting inventory | 47 |
| Node.js 언어 E2E·sample·porting inventory | 14 |

Core 문서는 사용자 지시에 따라 전수 리뷰하지 않는다. Framework finding 때문에 특정 Core 계약 확인이
필요할 때만 관련 Core 정식 문서를 교차 확인한다. 현재 Core header와 구현은 병렬 S4 변경물이므로 계약
근거로 사용하지 않는다. 공통 E2E·sample은 새 public API의 근거가 아니다. 구현 source가 target
10.0.0 exact API에 아직 도달하지 않았다는 사실만으로 정식 target 계약을 이전 API로 되돌리지 않는다.

## 4. 반드시 읽을 기준

- 루트 `AGENTS.md`
- `doc/principal/documentation/documentation-principles.ko.md`
- `doc/principal/software-design-principles.md`
- 이 manifest와 `scope-files.txt`
- scope의 framework 공통·server spec, 다섯 언어 exact interface, E2E·sample 문서 전체
- route-mesh contract inventory와 Redis fixture

## 5. iteration 7 이후 수정

- `Yield`를 owner turn 반납과 새 turn continuation 의미로 단일화하고 Config 8을 operation 의미 기준으로 고쳤다.
- Actor turn과 Spot control claim을 분리하고 transfer commit·activation·route publish 순서를 정합화했다.
- 정상 handoff와 takeover, MeshNode drain policy와 application evidence를 구분했다.
- Java·Node Actor send의 metadata와 admission result, transfer adapter context parity를 보완했다.
- C++·.NET timer·Spot·Entry Spot parity와 exact 예제·현재 계약 표현을 보완했다.
- 언어별 feature map의 실제 status 모순과 stale API를 고쳤으며 target exact에 근거한 API는 유지했다.

## 6. 리뷰 축과 출력 계약

1. 문서 원칙, 독자 책임, 현재 10.0.0 서술
2. 공통 의미, 다섯 언어 exact signature, Connector, Config 1~11, sample, transfer·timer·location·metadata·drain

이전 finding 수정 확인으로 범위를 줄이지 말고 177개 전체를 처음부터 검토한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.
