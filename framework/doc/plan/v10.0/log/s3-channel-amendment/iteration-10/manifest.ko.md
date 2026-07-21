# S3 Channel·fanout·fixed drain amendment 독립 리뷰 범위 — iteration 10

## 1. 검토 질문

> Framework RouteMesh 10.0.0의 Channel, classic fanout과 MeshNode fixed drain 계약이 Core service 계약,
> framework 공통 spec, 다섯 언어 exact interface, guide, E2E·sample·fixture·verifier에서 모순 없이
> 이어지는가?

Framework RouteMesh 10.0.0, Core 정식 spec 10.1.0과 현재 Core runtime bugfix version 10.6.0은 서로 다른
version 축이다. 이 검토는 package 배포나 version 변경을 수행하지 않는다.

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3-CH`, `S3-FO`, `S3-DP` |
| iteration | `10` |
| 동결 시각 | `2026-07-20T22:11:46+09:00` |
| 기준 HEAD | `86258cb9a3ecbc7db2dfd86a8c18de45a562734a` |
| 검토 파일 수 | `96` |
| 파일 집합 SHA-256 | `9da6b21db8bed816f9ae2b7bd7f61532d7c2ad6c1bf59049a1a0773563b768e0` |
| 파일 목록 SHA-256 | `7d428591a096c04333984c8dc75f75364fcb40f106546327f71d3a6e8de18390` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 SHA-256 | [`scope-files.sha256`](./scope-files.sha256) |
| 공통 prompt | [`prompt.md`](./prompt.md) |

Reviewer는 시작과 종료 시 96개 파일 전체를 확인한다. 하나라도 다르면 `SNAPSHOT DRIFT`로 종료한다.

## 3. Iteration 10 추가 확인

Iteration 9의 모든 finding을 반영했다. `.NET` guide의 Channel 호출은 ChannelName-only이며 MeshName을
업무 호출 인자로 사용하지 않는다. Classic fanout은 typed event와 전용 call·handler를 사용하고 transport
topic을 노출하지 않는다. MeshNode, fanout, STREAM과 Spot 예제는 exact builder와 manager 표면에 맞췄다.
Drain 사용 안내와 regression matrix는 MeshName별 `IZLinkRouteMeshRuntime`, `ZLinkMeshDrainResult`와
`ZLinkMeshRuntimeEvent`를 사용한다.

Config 3은 언어별 메서드 이름 대신 bounded submit terminal completion이라는 공통 의미를 사용한다. Verifier는
guide의 이전 MeshName Channel 호출, topic 기반 fanout, 이전 builder와 전역 drain 타입을 금지하고 현재
표면의 필수 fragment를 검사한다.

## 4. 동결 검증

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-10/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-10/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-10/scope-files.txt
bash scripts/verify-framework-doc-contracts.sh
git diff --check
```

Finding이 없고 전체 scope를 읽었으며 시작·종료 hash가 같을 때만 `DOC REVIEW CLEAN`으로 끝낸다. Reviewer는
파일을 수정하지 않는다.
