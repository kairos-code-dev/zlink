# S3 문서 독립 리뷰 범위 — iteration 5

## 1. 검토 질문

> Core와 framework의 10.0.0 목표 계약, 다섯 언어 server exact interface, 영향을 받은 Stream Connector
> 계약, E2E·sample 공개 문서가 문서 원칙을 지키면서 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `5` |
| 동결 시각 | `2026-07-17T04:35:00+09:00` |
| 기준 commit | `b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4` |
| 검토 문서 수 | `227` |
| 문서 집합 SHA-256 | `f1d9cdc5c2e18d79dbac1a68d2a501d66b39a920f731033b6b71f88091954d1c` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Reviewer는 시작과 종료 시 `scope-files.sha256`의 227개 파일 hash와 aggregate hash를 다시 계산한다.
하나라도 다르면 결과를 채택하지 않는다.

## 3. 검토 범위

| 범위 | 파일 수 |
|---|---:|
| Core 정식 spec | 52 |
| framework 공통·server 정식 spec과 server exact interface | 50 |
| 영향을 받은 Stream Connector 공통·언어별 exact 계약 | 5 |
| 공통 E2E | 12 |
| 공통 sample | 10 |
| C++ 언어 E2E·sample 문서 | 19 |
| .NET 언어 E2E·sample 문서 | 20 |
| Java·Kotlin 언어 E2E·sample 문서 | 46 |
| Node.js 언어 E2E·sample 문서 | 13 |

`90-implementation-gap.ko.md`, 언어별 gaps, 구현 source와 runner는 목표 계약 리뷰 범위가 아니다.
S2 영향 inventory가 구현 파일과 후속 검증 stage를 고정한다.

## 4. 반드시 읽을 기준

- 저장소 루트 `AGENTS.md`
- `doc/principal/documentation/documentation-principles.ko.md`
- `doc/principal/software-design-principles.md`
- 이 manifest와 `scope-files.txt`
- S1 Core public API inventory와 승인된 Core 정식 spec
- framework 공통·server spec, 다섯 언어 server exact interface
- Stream Connector reconnect metric owner와 네 언어 투영
- Config 1~11 공통 E2E와 sample 공개 문서
- route-mesh contract inventory와 Redis fixture

병렬 S4의 현재 `core/include/`와 구현은 S3 계약의 1차 소스가 아니다. 기존 Core 표면의 분류에는 S1
기준 commit을 사용한다. 공통 E2E·sample은 새 public API의 근거가 아니라 검증 요구와 누락을 찾는
입력이다.

## 5. iteration 4 이후 수정

- Codex 29건과 Claude Sonnet 71건을 모두 owner와 red gate에 연결하고 closure audit로 해소를 확인했다.
- Core metadata·monitoring·errno, framework worker·selection·location·Actor transfer·multicast 계약을
  정합화했다.
- 다섯 언어 server exact interface의 누락, 중복 owner와 completion 의미를 수정했다.
- 공통·언어별 E2E와 sample의 stale API, 상태 모순, 이력 서술, 잘못된 링크와 diagram을 정리했다.
- Connector reconnect metric을 server session 계약에서 Stream Connector 계약으로 옮기고 네 언어의
  public provider·sink 투영을 명시했다.
- Actor destroy의 Entry Spot 제한과 `OnLeaveActor` 재호출 금지를 공통·언어별 계약에 고정했다.

## 6. 리뷰 축

1. **원칙:** 독자와 질문, spec·guide·internals 책임, 문서 원칙 1~9, 현재 10.0.0 서술과 한국어 문체
2. **1차 소스:** Core–framework result·error·ownership·callback·metadata·NoDrop 일치, 다섯 언어 exact
   signature, Connector metric owner·reader, Config 1~11, sample 공개 예제, Actor transfer·timer·location 의미

이전 finding의 수정 확인으로 범위를 줄이지 말고 227개 전체를 처음부터 검토한다.

## 7. 출력 계약

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.
