# S3 문서 독립 리뷰 범위 — iteration 23

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `23` |
| 동결 시각 | `2026-07-17T14:02:24,751486307+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `205` |
| 문서 집합 SHA-256 | `5379db55546bf16e9bee1d77ca12f7282c3c06c4892efa29c3fb7c2b966564c6` |
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

- route-mesh-v10-dotnet-contract-inventory.json: `6a343fae5574c154e8130b16cbfcfdeb1c55cc2887c88d74652372e7a2d240fe`
- route-mesh-v10-contract-inventory.json: `9afd2a609b17d5e7b1af27f2ec0c17f60cb3ed33e324957115cf5c2e3e2edff8`
- verify-framework-doc-contracts.sh: `5d44fcdcc392c4b9cbbd2284e971be3e0d1e0745bdcc82005988e95d8e594fbb`

## 4. iteration 22 이후 반영

- 전 언어 공통 gap 소유 범위를 §12.20 이후로 바로잡고, §12.21 해소 뒤 남은 §12.24와 sample 사용처의
  선행 관계만 유지했다.
- 중앙 IMP-X 표를 언어별 완료 증거에 맞춰 다시 계산하고 Java의 IMP-X3·IMP-X4 상태를 실제 해결 상태와
  일치시켰다.
- 공통 ActorRef를 `NodeRid`·`ActorId`·`Generation` 세 값으로 고정했다. .NET·Java exact 선언과
  snapshot을 보강하고, .NET의 계약 밖 `IsUnchecked`와 C++의 추가 `actor_type`은 §12.34 구현 gap으로
  분리했다.
- .NET STREAM guide의 send 한도 기준을 압축 사용 시 압축된 wire payload로 바로잡고 Kotlin guide의
  코드 블록 탭을 제거했다.
- exact fixture hash와 public declaration inventory를 변경된 정식 interface에 맞춰 갱신했다.

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
| Codex agent | finding 10건 — iteration 무효 | `codex-review.ko.md` | `54580d0ca123cd7495d30f8da78685c7d757402645efc5fd84f1f94bb0eaf9ef` |
| Claude Sonnet | `DOC REVIEW CLEAN` | `claude-sonnet-review.ko.md` | `bb2361518dd993752e83d18ba5143beac3c380b2e9bb5ed0401814ac940a5011` |

두 reviewer의 결과가 모두 clean이 아니므로 iteration 23은 S3 완료 증거로 채택하지 않는다. Codex finding을
수정한 새 동결 집합에서 독립 리뷰를 다시 수행한다.
