# S3 Channel·fanout amendment 독립 리뷰 범위 — iteration 8

## 1. 검토 질문

> Framework RouteMesh 10.0.0의 Channel·classic fanout 계약이 현재 Core service 계약, framework 공통
> spec, 다섯 언어 exact interface, E2E·sample·fixture·verifier에서 모순 없이 이어지는가?

Framework RouteMesh 10.0.0, Core 정식 spec 10.1.0과 현재 Core runtime bugfix version 10.6.0은 서로 다른
version 축이다. 이 검토는 package 배포나 version 변경을 수행하지 않는다.

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3-CH`, `S3-FO` |
| iteration | `8` |
| 동결 시각 | `2026-07-20T21:17:13+09:00` |
| 기준 HEAD | `86258cb9a3ecbc7db2dfd86a8c18de45a562734a` |
| 검토 파일 수 | `71` |
| 파일 집합 SHA-256 | `9417ae73c562ecaf6cccc423cde6b98fbd2a7b5915b21e51f722455c23588d9f` |
| 파일 목록 SHA-256 | `00bba02f27b40d650b02d659bcd613d4e69097220565dd42419fa2a2937f7818` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 SHA-256 | [`scope-files.sha256`](./scope-files.sha256) |
| 공통 prompt | [`prompt.md`](./prompt.md) |

Reviewer는 시작과 종료 시 71개 파일을 확인한다. 하나라도 다르면 `SNAPSHOT DRIFT`로 종료한다.

## 3. Iteration 8 추가 확인

Iteration 7 Codex finding을 반영했다. Location record·owner lease metric은 MeshName이 없는 ClientServer와
fanout descriptor를 `scope_kind=channel`, `scope_name=ChannelName`으로 표현한다. TicTacToe와 Bingo의
남은 비정식 Channel endpoint·ChannelName 표현을 제거했다. 다섯 PubSub feature map은 classic fanout의
typed handler·packet 의미만 사용하며 transport topic을 회귀 조건으로 두지 않는다. Verifier는 이 세
의미를 구조적으로 검사한다.

## 4. 동결 검증

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-8/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-8/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-8/scope-files.txt
```

Finding이 없고 시작·종료 hash가 같을 때만 `DOC REVIEW CLEAN`으로 끝낸다. Reviewer는 파일을 수정하지
않는다.
