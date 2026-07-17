# S3 문서 독립 리뷰 범위 — iteration 28

## 1. 검토 질문

> Framework 10.0.0 공통 계약, 다섯 언어 exact interface, Connector·HTTP error 계약, 관련 guide·gap,
> E2E·sample 문서가 서로 모순 없이 구현 가능한 하나의 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3` |
| iteration | `28` |
| 동결 시각 | `2026-07-17T17:03:32,640966393+09:00` |
| 기준 commit | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 검토 문서 수 | `205` |
| 문서 집합 SHA-256 | `1468e3db03054ed7ac757bf7b7532bc4b6fe7e44b01b304943f41bdafd34f8ab` |
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
- route-mesh-v10-contract-inventory.json: `042a1dffaa1edacc8aa82456abb89996f8ccecce27f9414deeb505db3f95d47c`
- verify-framework-doc-contracts.sh: `5d44fcdcc392c4b9cbbd2284e971be3e0d1e0745bdcc82005988e95d8e594fbb`

## 4. iteration 27 이후 반영

- Java와 Kotlin actor/session guide를 `addRouteMesh`·`routeMesh`, 실제 join result와 nullable
  `spotRid()` 계약에 맞추고, 같은 MeshNode join과 다른 MeshNode transfer의 actor 수명을 분리했다.
- Java와 Kotlin guide에서 내부 dispatch·gateway 배선 설명을 제거하고 공개 등록 절차만 남겼다.
- Java exact system 구조의 Spot direct 예제를 `SpotHandleResolver`와 `SpotHandle` 계약에 맞췄다.
- Java/Kotlin route guide와 C++ Spot guide의 존재하지 않는 호출 이름과 인자를 exact interface에 맞췄다.
- connector 상태와 무관한 범용 assertion helper를 production connector 계약에서 제거하고 E2E
  `Client/Support` 책임으로 고정했다. connector에는 push 관측을 위한 wait 계열만 남겼다.
- 이미 만들어진 C++ connector exact 문서를 미구현으로 표시하던 오래된 gap 행과 TicTacToe의 오래된
  remote-ref 용어를 제거했다.

## 5. 리뷰어 정책과 출력 계약

이 iteration은 문서 리뷰이므로 Codex agent와 Claude Sonnet만 사용한다. Claude Fable은 코드와 구현
결과를 검토하는 단계에서만 사용하며 이 문서 리뷰에는 사용하지 않는다.

이전 finding만 확인하지 말고 205개 전체를 처음부터 검토한다. 자체 slug 추정으로 링크 finding을
확정하지 말고 실제 pymdownx renderer로 재현한다. 저장소 안팎 어디에도 파일을 만들거나 수정하지 않는다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[1차소스][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다.

## 6. 리뷰 결과

| reviewer | 결과 | output | SHA-256 |
|---|---|---|---|
| Codex agent | 실패 — finding 6개 | `codex-review.ko.md` | `29366573866bbaf5f36a520c3298f8db9bdbafc625028e07ac7402ccc3f24195` |
| Claude Sonnet | 실패 — finding 4개 | `claude-sonnet-review.ko.md` | `cfebef46d495258216ce7938db8b1b0066de4c6afaa9a02d4e873759f72cb843` |

두 reviewer 모두 시작·종료 동결 hash를 확인했고 저장소 안팎에 파일을 만들지 않았다. finding이
존재하므로 iteration 28은 채택하지 않고 수정 후 새 동결본을 검토한다.
