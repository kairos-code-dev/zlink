# S3 문서 독립 리뷰 범위 — iteration 22

> **무효 iteration.** 두 reviewer가 동결 범위에서 finding을 보고했으므로 clean verdict로 채택하지 않는다.
> 수정은 이 동결본이 아니라 다음 iteration에 반영한다.

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `22` |
| 동결 시각 | `2026-07-17T13:34:02,210498681+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `205` |
| 문서 집합 SHA-256 | `3e890ba18a9e9bab6a4b116128e1ba6bc1ce219da5e4ab1c1d3b08c5015ac9b4` |
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

## 4. iteration 21 이후 반영

- 통합 RouteMesh·MeshNode exact interface와 현재 source·package·sample·E2E의 차이를 §12.33과 다섯 언어
  고유 ID로 기록했다. 실행 상태와 gate 증거는 execution ledger만 소유한다.
- C++ TLS gap은 node options builder의 client 인증서 요구 설정과 low-level builder의 계약 밖 TLS 메서드
  제거를 구분해 exact interface와 일치시켰다.
- 수신 content-type 차이를 미구현이 아니라 결함으로 분류하고, 공통 계약에 없는 raw/binary 예외를
  해결안에서 제거했다.
- C++ §12.21은 해소로, §12.23은 callback `std::stop_token` 전달만 남은 상태로 정정했다.
- 언어별 gap 수치와 중앙 인덱스를 일치시키고 정식 spec에 없던 store read별 5초 상한 요구는 감사 근거
  오류로 해소했다. `storeFailureGrace` 근거는 실제 §2.4로 고쳤다.
- Java guide의 잘못된 장 번호 표시와 Kotlin actor-session 예제의 잘못된 dispatcher 호출을 고쳤다.

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
| Codex agent | finding — 무효 | `codex-review.ko.md` | `38519c3a72833408bdb4298b1cfca1d6802bb1716a0f7e525021011990790590` |
| Claude Sonnet | finding — 무효 | `claude-sonnet-review.ko.md` | `d48616e3835b8dd4230f86a79e66cf92a2e47ee20a31dc0a0ace0b649bfcefab` |
