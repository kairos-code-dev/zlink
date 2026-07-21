# S3 RouteMesh amendment 통합 문서 review 범위 — iteration 14

## 1. 검토 질문

Framework RouteMesh 10.0.0의 Channel, classic fanout, MeshNode fixed drain, one-way submit과 Instance Spot
계약이 Core service 계약, Framework 공통 spec, 다섯 언어 exact interface, E2E·sample·fixture·verifier에서
모순 없이 이어지는지 검토한다.

이 검토는 공개 계약과 검증 기준만 판정한다. 아직 구현하지 않은 항목을 현재 구현처럼 설명하지 않으며 package
version이나 외부 배포 상태를 변경하지 않는다.

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3-CH`, `S3-FO`, `S3-DP`, `S3-SA`, `S3-IS` |
| iteration | `14` |
| 동결 시각 | `2026-07-21T14:53:19+09:00` |
| 기준 HEAD | `86258cb9a3ecbc7db2dfd86a8c18de45a562734a` |
| 검토 파일 수 | `113` |
| 파일 집합 SHA-256 | `4e015d1e7accc4f2cb8037bfdacb0ed8471722cee8ad128691f17ed757ed2e6a` |
| 파일 목록 SHA-256 | `0ad6551a53b4c0685591dbaead6a40556dfe83622dd96c6d4212276688600fb3` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 SHA-256 | [`scope-files.sha256`](./scope-files.sha256) |
| 공통 prompt | [`prompt.md`](./prompt.md) |

Reviewer는 시작과 종료 시 113개 파일 전체의 hash를 확인한다. 하나라도 다르면 finding을 판정하지 않고
`SNAPSHOT DRIFT`로 종료한다.

## 3. Iteration 13 finding 반영

- Instance location에서 제거한 `SpotKind`를 Redis fixture의 `Activating`·`Ready`·`Closing` row에서도
  제거했다.
- 정식 spec의 inline Ready JSON과 fixture bytes를 일치시키고 focused verifier가 둘을 직접 비교하게 했다.
- 세 Instance state가 같은 field schema를 사용하는지 verifier가 검사한다.
- 공통 E2E scenario 접두사 표에 Config 14의 `IS`를 추가했다.

이 확인은 이전 finding만 보는 delta review가 아니다. 두 reviewer는 전체 113개를 다시 검토한다.

## 4. 동결 검증

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-14/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-14/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-14/scope-files.txt
bash scripts/verify-framework-doc-contracts.sh
bash scripts/verify-framework-instance-spot-contracts.sh
bash scripts/verify-framework-submit-api.sh --contract
git diff --check
```

Finding이 없고 전체 scope를 읽었으며 시작·종료 hash가 같을 때만 마지막 줄을 정확히
`DOC REVIEW CLEAN`으로 기록한다.
