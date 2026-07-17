# S3 문서 독립 리뷰 범위 — iteration 24

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `24` |
| 동결 시각 | `2026-07-17T14:29:40,135660667+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `205` |
| 문서 집합 SHA-256 | `517016d8ad95ca1c334b2ec747e3219864a4a5ee99ef5c971a32da4aec0ae4a9` |
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
- route-mesh-v10-contract-inventory.json: `c04bde64b2b2665b7735af05896eacd2638debc2a201c81daa6be501f96ec82b`
- verify-framework-doc-contracts.sh: `5d44fcdcc392c4b9cbbd2284e971be3e0d1e0745bdcc82005988e95d8e594fbb`

## 4. iteration 23 이후 반영

- C++ Channel guide에서 내부 ROUTER socket 배선을 제거하고 공개 설정 효과와 internals 링크만 남겼다.
- HTTP timeout의 언어별 공용 error mapping을 오류 계약과 맞췄다.
- connector 수신 한도를 wire payload와 압축 해제 결과에 각각 적용하고 오류별 operation·연결·종료
  사유·reconnect 행렬을 공통 spec에 고정했다.
- TypeScript exact interface에 누락된 call builder, models, enums, options, transport·codec extension을
  선언하고 source에 없는 `TlsValidationFailed`를 구현 gap으로 기록했다.
- Java/Kotlin SM-D10의 반대 queue admission 증거와 Kotlin Config 11의 Java host 대체 PASS를
  blocked/전환 대상으로 바로잡았다.
- Node TA-B1의 send local submit과 request 오류 의미를 분리했다.

## 5. 리뷰어 정책과 출력 계약

이 iteration은 문서 리뷰이므로 Codex agent와 Claude Sonnet만 사용한다. Claude Fable은 코드와 구현
결과를 검토하는 단계에서만 사용하며 이 문서 리뷰에는 사용하지 않는다.

이전 finding만 확인하지 말고 205개 전체를 처음부터 검토한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.

## 6. 리뷰 결과

| reviewer | 결과 | output | SHA-256 |
|---|---|---|---|
| Codex agent | finding 7건 — iteration 무효 | `codex-review.ko.md` | `da19cd842bcc014edc06b399aa4f7887618f8826b0b09cce971ca4bf59ad3c46` |
| Claude Sonnet | finding 4건 — iteration 무효 | `claude-sonnet-review.ko.md` | `73ec58d0528ae2dbd36352d8d68fedce000c57f7892a6ca8a7e5d2f78543806f` |

두 reviewer의 결과가 모두 clean이 아니므로 iteration 24는 S3 완료 증거로 채택하지 않는다. finding 11건을
수정한 새 동결 집합에서 독립 리뷰를 다시 수행한다.
