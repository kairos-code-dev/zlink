# S3 Channel·fanout·fixed drain amendment 독립 리뷰 범위 — iteration 9

## 1. 검토 질문

> Framework RouteMesh 10.0.0의 Channel, classic fanout과 MeshNode fixed drain 계약이 Core service 계약,
> framework 공통 spec, 다섯 언어 exact interface, E2E·sample·fixture·verifier에서 모순 없이 이어지는가?

Framework RouteMesh 10.0.0, Core 정식 spec 10.1.0과 현재 Core runtime bugfix version 10.6.0은 서로 다른
version 축이다. 이 검토는 package 배포나 version 변경을 수행하지 않는다.

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3-CH`, `S3-FO`, `S3-DP` |
| iteration | `9` |
| 동결 시각 | `2026-07-20T21:40:57+09:00` |
| 기준 HEAD | `86258cb9a3ecbc7db2dfd86a8c18de45a562734a` |
| 검토 파일 수 | `96` |
| 파일 집합 SHA-256 | `0c61c2f62a71f4f8f2f96c29e80aacd0679369453104d0674e6969e8cd302350` |
| 파일 목록 SHA-256 | `7d428591a096c04333984c8dc75f75364fcb40f106546327f71d3a6e8de18390` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 SHA-256 | [`scope-files.sha256`](./scope-files.sha256) |
| 공통 prompt | [`prompt.md`](./prompt.md) |

Reviewer는 시작과 종료 시 96개 파일을 확인한다. 하나라도 다르면 `SNAPSHOT DRIFT`로 종료한다.

## 3. Iteration 9 추가 확인

Iteration 8의 두 finding을 반영했다. TicTacToe는 manual endpoint가 MeshNode peer pipe를 구성한다는 점과
ChannelName이 그 pipe 위의 업무 대상을 선택한다는 점을 구분한다. Feature-map verifier는 canonical
scenario ID가 정확히 한 번 선언되는지 검사하고 실제 행 1,001개를 집계한다. 이 수는 canonical scenario
1,000개와 명시적으로 분리된 보충 행 `SM-Q9` 한 개다.

MeshNode drain은 policy enum과 builder option을 제거하고 하나의 고정 순서를 사용한다. Spot create와
`GetOrCreate`는 local-only이며 remote resolve·messaging은 기존 owner row만 사용한다. Drain은 admission
seal, accepted work, Actor handoff, STREAM barrier, local Spot close와 owner cleanup 순서다. Row 제거 뒤
stale SpotHandle은 remote create를 시작하지 않으며 explicit local `GetOrCreate`만 새 generation을 만든다.

## 4. 동결 검증

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-9/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-9/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-9/scope-files.txt
bash scripts/verify-framework-doc-contracts.sh
git diff --check
```

Finding이 없고 시작·종료 hash가 같을 때만 `DOC REVIEW CLEAN`으로 끝낸다. Reviewer는 파일을 수정하지
않는다.
