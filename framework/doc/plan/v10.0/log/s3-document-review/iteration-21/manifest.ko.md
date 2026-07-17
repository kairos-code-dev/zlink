# S3 문서 독립 리뷰 범위 — iteration 21

> **무효 iteration.** 두 reviewer가 동결 범위에서 finding을 보고했으므로 clean verdict로 채택하지 않는다.
> 수정은 이 동결본이 아니라 다음 iteration에 반영한다.

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `21` |
| 동결 시각 | `2026-07-17T13:04:25,356091303+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `205` |
| 문서 집합 SHA-256 | `a66bb7c5066a5ae20d05123072eb9515491f22bbb82ea593816032b592234e14` |
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

- route-mesh-v10-dotnet-contract-inventory.json: `21c1833b6f6b1a61e4d07ef94cce38800da23021906ec36092a800512c3d1aff`
- route-mesh-v10-contract-inventory.json: `829fd58a14c94112763a446c55de5e2ba1c50dcc4921170907f0dac0707a8f73`
- verify-framework-doc-contracts.sh: `d3d33d103233e3759401bffdc1cf4c530af620d919bb4fc5235caa923ab9150a`

## 4. iteration 20 이후 반영

- iteration 20은 Codex 검토에서 새 정식 계약과 현재 구현의 차이가 gap 문서에 누락된 사실을 확인해
  무효 처리했다. 당시 Sonnet 실행은 중단했으며 verdict를 채택하지 않는다.
- 전 언어 Actor join CAS commit 순서, Actor location Spot generation, STREAM Actor dispatch MeshName,
  durable Actor transfer store와 Actor transfer metric label·terminal 차이를 gap으로 고정했다.
- C++ STREAM TLS의 client 인증서 요구 설정 누락을 별도 gap으로 기록했다.
- 수신 envelope의 알 수 없는 non-JSON content-type을 JSON·기본 serializer·raw payload로 처리하는
  전 언어 결함을 §12.32로 기록했다.
- `.NET` STREAM connector의 handler-bound send가 bounded 수신 queue admission을 우회하는 결함을
  §12.25에 추가했다.
- 정식 exact interface와 gap 추적 표, Redis key fixture, Config 4·8·10·11 evidence를 같은 목표 계약으로
  정렬하고 forbidden 표현 한 건을 중립적인 기술 표현으로 고쳤다.

## 5. 리뷰어 정책과 출력 계약

이 iteration은 문서 리뷰이므로 Codex agent와 Claude Sonnet만 사용한다. Claude Fable은 코드와 구현
결과를 검토하는 단계에서만 사용하며 이 문서 리뷰에는 사용하지 않는다.

1. 문서 원칙, 독자 책임, 현재 10.0.0 서술
2. 공통 의미, 다섯 언어 exact signature, Connector·HTTP error, Config 1~11, sample·guide·gap

이전 finding만 확인하지 말고 205개 전체를 처음부터 검토한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.

## 6. 리뷰 결과

| reviewer | 결과 | output | SHA-256 |
|---|---|---|---|
| Codex agent | finding — 무효 | `codex-review.ko.md` | `3137126dee49512f256cdf37900f4ae5f91976ff10c533a1aaaef7506adc0223` |
| Claude Sonnet | finding — 무효 | `claude-sonnet-review.ko.md` | `96fcea44df5136cc73039ab7809a29533e796d316eafe49bb82b06b05d05ecd2` |
