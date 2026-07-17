# S3 문서 독립 리뷰 범위 — iteration 13

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `13` |
| 동결 시각 | `2026-07-17T10:24:28+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `202` |
| 문서 집합 SHA-256 | `6ffed18e000bdb2df033499ec16eb4544e47cc183dfc18d84b29220898888e31` |
| 파일 목록 SHA-256 | `dfabc81f2c0ec7403683bc54ab3f350d9cf6565aac60a3baf9579192c513642f` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 hash | [`scope-files.sha256`](./scope-files.sha256) |

Reviewer는 시작과 종료 시 202개 파일별 hash와 aggregate를 다시 계산한다. 하나라도 다르면 결과를
채택하지 않는다.

## 3. 검토 범위

| 범위 | 파일 수 |
|---|---:|
| framework 공통·server 정식 spec과 server exact interface | 50 |
| Stream Connector 공통·언어별 exact 계약 | 5 |
| 관련 framework common·gap·HTTP error·C++/.NET/JVM/Node guide | 25 |
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
- scope 202개 전체
- `framework/doc/contract-inventory/route-mesh-v10-dotnet-contract-inventory.json`
  SHA-256 `c96334c9015a56426fa1f7bd0560aeb175e05a6533028c06d37d6fa4510667b2`
- `framework/doc/contract-inventory/route-mesh-v10-contract-inventory.json`
  SHA-256 `b39387255e798a2785fb76ae642e98df3cc8dafbdf3d6cf5f69b8ab518b4a504`
- `scripts/verify-framework-doc-contracts.sh`
  SHA-256 `f85020b56d339db16b8d0373237f62e7c4d2b2abfd31d4162294ac64c1a8aaae`

## 5. iteration 12 finding 반영

- C++ DI scope owner와 factory lifetime 계약을 정렬하고 application이 scope를 직접 만들지 않게 했다.
- C++ worker·Spot·Actor·STREAM catalog를 exact interface와 맞추고 끊긴 문장과 미선언 표면을 제거했다.
- C++ STREAM은 저수준 `stream(name)`과 framework options-level `add_stream_node(name)`를
  분리하고 typed session 등록과 Actor dispatch MeshName 선택을 명시했다.
- Connector가 소유하는 reconnect·heartbeat 책임과 framework server의 session dispatch 책임을 분리했다.
- C++ overview의 내부 배선과 구현 설명, 지원 언어 roadmap 및 금지 문체를 제거했다.
- Node STREAM guide를 실제 `streamPayloadCodec`, `addStreamCodec(...)`과 MessagePack·Protobuf
  extension 계약에 맞췄다.

## 6. 리뷰어 정책

이 iteration은 문서 리뷰이므로 Codex agent와 Claude Sonnet만 사용한다. Claude Fable은 코드와 구현
결과를 검토하는 단계에서만 사용하며 이 문서 리뷰에는 사용하지 않는다.

## 7. 리뷰 축과 출력 계약

1. 문서 원칙, 독자 책임, 현재 10.0.0 서술
2. 공통 의미, 다섯 언어 exact signature, Connector·HTTP error, Config 1~11, sample·guide·gap

이전 finding 수정 확인으로 범위를 줄이지 말고 202개 전체를 처음부터 검토한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 파일은 수정하지 않는다.

## 8. 리뷰 결과

| reviewer | 결과 | output | SHA-256 |
|---|---|---|---|
| Codex agent | finding 9건 | [`codex-review.ko.md`](./codex-review.ko.md) | `c2028addd55267ab8e444d182c355dcb90fd25d9322f56144d4ca9a88d43c421` |
| Claude Sonnet | scope drift로 무효 | [`claude-sonnet-review.ko.md`](./claude-sonnet-review.ko.md) | raw output hash는 검증 기록에 기재 |

Codex finding은 [`finding-ledger.ko.md`](./finding-ledger.ko.md)에 병합했다. Sonnet 실행은 종료 hash
불일치로 채택하지 않으며 새 iteration에서 같은 전체 범위를 다시 검토한다.
