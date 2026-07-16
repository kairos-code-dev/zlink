# S3 문서 독립 리뷰 범위 — iteration 7

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 server exact interface, 영향을 받은 Stream Connector 계약,
> E2E·sample 공개 문서가 문서 원칙을 지키면서 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `7` |
| 동결 시각 | `2026-07-17T06:08:16+09:00` |
| 기준 commit | `b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4` |
| 검토 문서 수 | `177` |
| 문서 집합 SHA-256 | `559380a8ca2234777982d7c705274db24bf33ee4b38500b5115205de32e63bdd` |
| 파일 목록 SHA-256 | `9f072f1af2b73fde7c08143da66cb651bae33074bfc915a45d18431ccf345f25` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Reviewer는 시작과 종료 시 `scope-files.sha256`의 177개 파일 hash와 aggregate hash를 다시 계산한다.
하나라도 다르면 결과를 채택하지 않는다.

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

Core 문서는 사용자 지시에 따라 이번 iteration의 전수 리뷰 범위에 포함하지 않는다. Framework 계약을
검토하다 Core 계약과의 불일치가 발견되면 필요한 연관 Core 계약만 교차 확인하고 finding에 근거를
기록한다. 현재 `core/include/`와 구현은 병렬 S4 변경물이므로 계약의 1차 소스로 사용하지 않는다.

`90-implementation-gap.ko.md`, 언어별 gaps, 구현 source와 runner는 목표 계약 리뷰 범위가 아니다.
공통 E2E·sample은 새 public API의 근거가 아니라 검증 요구와 누락을 찾는 입력이다.

## 4. 반드시 읽을 기준

- 저장소 루트 `AGENTS.md`
- `doc/principal/documentation/documentation-principles.ko.md`
- `doc/principal/software-design-principles.md`
- 이 manifest와 `scope-files.txt`
- framework 공통·server spec, 다섯 언어 server exact interface
- Stream Connector reconnect metric owner와 네 언어 투영
- Config 1~11 공통 E2E와 sample 공개 문서
- route-mesh contract inventory와 Redis fixture

## 5. iteration 6 이후 수정

- Codex finding 18건과 Claude Sonnet finding 4건을 병합했으며 Stream Connector finding 1건은 중복이었다.
- Spot·Actor handle과 관리·messaging 표면이 모든 언어에서 MeshName을 보존하도록 고쳤다.
- Actor payload와 Spot lifecycle에서 mutable Actor·Spot owner가 동시에 노출되지 않도록 분리했다.
- 다섯 언어의 Spot 생성·resolve, Node 오류 값과 MeshNode drain policy parity를 맞췄다.
- E2E·sample의 존재하지 않는 metric, 잘못된 근거, mutable owner 직접 호출과 stale route 의미를 고쳤다.
- Stream Connector에서 이전 버전 decoder 정책과 비계약 gap 문서 참조를 제거했다.

## 6. 리뷰 축

1. **원칙:** 독자와 질문, spec·guide·internals 책임, 문서 원칙 1~9, 현재 10.0.0 서술과 한국어 문체
2. **1차 소스:** framework 공통 의미와 다섯 언어 exact signature, Connector metric·배포 계약,
   Config 1~11, sample 공개 예제, Actor transfer·timer·location·metadata 의미

이전 finding의 수정 확인으로 범위를 줄이지 말고 177개 전체를 처음부터 검토한다.

## 7. 출력 계약

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.
