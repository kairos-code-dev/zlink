# S3 RouteMesh amendment 통합 문서 review 범위 — iteration 15

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
| iteration | `15` |
| 동결 시각 | `2026-07-21T14:59:02+09:00` |
| 기준 HEAD | `86258cb9a3ecbc7db2dfd86a8c18de45a562734a` |
| 검토 파일 수 | `113` |
| 파일 집합 SHA-256 | `a50e5de86d1cc3da63e5914265b1e3a2417f0eebd615b53472d40c5b9f10e149` |
| 파일 목록 SHA-256 | `0ad6551a53b4c0685591dbaead6a40556dfe83622dd96c6d4212276688600fb3` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 SHA-256 | [`scope-files.sha256`](./scope-files.sha256) |
| 공통 prompt | [`prompt.md`](./prompt.md) |

Reviewer는 시작과 종료 시 113개 파일 전체의 hash를 확인한다. 하나라도 다르면 finding을 판정하지 않고
`SNAPSHOT DRIFT`로 종료한다.

## 3. Iteration 14 이후 변경

- Instance placement와 activation data에서 제거한 다섯 필드와 제거 이유를 명시했다.
- owner claim 뒤에만 확정되는 `leader_spot_generation`은 claim 결과에 유지하고, cold placement와 activation
  data에는 넣지 않는다는 경계를 분명히 했다.

이 확인은 변경 부분만 보는 검토가 아니다. 두 reviewer는 전체 113개를 다시 검토한다.

## 4. 동결 검증

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-15/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-15/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-15/scope-files.txt
bash scripts/verify-framework-doc-contracts.sh
bash scripts/verify-framework-instance-spot-contracts.sh
bash scripts/verify-framework-submit-api.sh --contract
git diff --check
```

Finding이 없고 전체 scope를 읽었으며 시작·종료 hash가 같을 때만 마지막 줄을 정확히
`DOC REVIEW CLEAN`으로 기록한다.
