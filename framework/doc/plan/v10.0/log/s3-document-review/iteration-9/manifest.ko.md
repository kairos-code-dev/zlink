# S3 문서 독립 리뷰 범위 — iteration 9

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 server exact interface, 영향을 받은 Stream Connector 계약,
> E2E·sample 공개 문서가 문서 원칙을 지키면서 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `9` |
| 동결 시각 | `2026-07-17T07:23:52+09:00` |
| 기준 commit | `b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4` |
| 검토 문서 수 | `177` |
| 문서 집합 SHA-256 | `c1cd9d0068931a5d13b31c49f18da8b5eef9cb0bdb2521896abff9f58fd30d5e` |
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

## 5. iteration 8 이후 수정

- Async/Yield, C++ request·Connector, monitoring enum, flow correlation과 unsigned integer 계약을 다섯
  언어 exact interface와 공통 계약에서 정렬했다.
- Config 7을 MeshNode snapshot·typed event·readiness·Logical Multicast backpressure/drop 계약으로
  다시 구성하고 다섯 언어 feature map을 같은 canonical 시나리오에 맞췄다.
- Config 2의 SM-F6, Config 3 packet-name fanout dispatch, Config 9 actor direct 비오염 근거를 정식
  framework 계약에 맞췄다.
- 공통 E2E·sample의 죽은 절 인용, stale API, 현재 구현 상태 모순과 문서 원칙 위반 표현을 정리했다.

## 6. 리뷰 축과 출력 계약

1. 문서 원칙, 독자 책임, 현재 10.0.0 서술
2. 공통 의미, 다섯 언어 exact signature, Connector, Config 1~11, sample, transfer·timer·location·metadata·drain

이전 finding 수정 확인으로 범위를 줄이지 말고 177개 전체를 처음부터 검토한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.
