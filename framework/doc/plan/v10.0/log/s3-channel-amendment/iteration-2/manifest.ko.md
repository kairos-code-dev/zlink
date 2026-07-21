# S3 Channel·fanout amendment 독립 리뷰 범위 — iteration 2

## 1. 검토 질문

> Iteration 1의 모든 finding을 수정한 Channel 계약과 location store 기반 fanout 자동 발견 계약이 Core,
> framework 공통 spec, 다섯 언어 exact interface, E2E·sample·fixture·verifier에서 하나의 모순 없는
> 10.0.0 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3-CH`, `S3-FO` |
| iteration | `2` |
| 동결 시각 | `2026-07-20T18:29:26+09:00` |
| 기준 HEAD | `86258cb9a3ecbc7db2dfd86a8c18de45a562734a` |
| 검토 파일 수 | `70` |
| 파일 집합 SHA-256 | `72431e8feef1b758a6879fc2f8e866db935f038d32f547cc978ebb1ea633f76f` |
| 파일 목록 SHA-256 | `0f1172592c455d39fd208e01517133c2b583989ad8deff6e4d66644be71d8208` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 SHA-256 | [`scope-files.sha256`](./scope-files.sha256) |
| 공통 prompt | [`prompt.md`](./prompt.md) |

기준 HEAD는 저장소 기준점이며 파일별 SHA-256은 그 위의 현재 작업 트리를 동결한다. Reviewer는 시작과
종료 시 70개 파일을 다시 확인한다. 하나라도 다르면 결과를 채택하지 않고 `SNAPSHOT DRIFT`로 종료한다.

## 3. Iteration 2 추가 범위

Iteration 1의 57개 파일을 모두 다시 검토한다. 여기에 Config 3, 다섯 PubSub feature map, fanout metric,
세 언어의 routing ID allocation exact 문서, ClientServer·fanout Redis fixture와 E2E runner inventory를
추가했다. 따라서 iteration 1의 sample·Channel finding 수정과 fanout 자동 발견 계약을 같은 snapshot에서
함께 검토한다.

## 4. 리뷰 기준과 경계

- 루트 `AGENTS.md`, 문서 원칙과 POSD 원칙을 먼저 읽는다.
- 정식 spec과 언어별 exact interface가 공개 계약의 근거다. Gap과 E2E는 목표 계약을 만들지 않는다.
- Fanout automatic discovery는 store 등록 publisher의 전용 descriptor를 사용한다. Store 없는 고정 endpoint
  publisher와 manual subscriber는 기존 동작을 유지한다.
- Fanout descriptor는 MeshNode, ClientServer와 generic peer record를 재사용하지 않는다.
- Config 3은 descriptor kind·ChannelName 격리, publisher 추가·제거·lease 만료·재등록·port 0·store 장애와
  manual 회귀를 실제 상태로 검증해야 한다.
- Core bridge·relay, MeshName 재노출, weight 0 client, timeout 증가와 언어별 private helper는 허용된 해결책이
  아니다.

## 5. 동결 검증

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-2/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-2/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-2/scope-files.txt
```

## 6. Reviewer 출력

Finding은 다음 형식을 사용한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[계약][severity] file:line — 문제 — 근거 — 제안
```

Finding이 없고 시작·종료 hash가 같을 때만 마지막 줄을 `DOC REVIEW CLEAN`으로 쓴다. Reviewer는 파일을
수정하지 않는다.
