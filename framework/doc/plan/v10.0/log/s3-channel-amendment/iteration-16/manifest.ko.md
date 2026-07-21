# S3 RouteMesh amendment 통합 문서 review 범위 — iteration 16

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
| iteration | `16` |
| 동결 시각 | `2026-07-21T15:49:02+09:00` |
| 기준 HEAD | `86258cb9a3ecbc7db2dfd86a8c18de45a562734a` |
| 검토 파일 수 | `114` |
| 파일 집합 SHA-256 | `cb5a8c064a8f2b6bc4cd70255a1e0af11584335abb42ba244e8f955d7d749aae` |
| 파일 목록 SHA-256 | `1ea9b8188de0d3b4ab282f90236d9641e6e19deffa4e4d25cd0315988d1cbec3` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 SHA-256 | [`scope-files.sha256`](./scope-files.sha256) |
| 공통 prompt | [`prompt.md`](./prompt.md) |

Reviewer는 시작과 종료 시 114개 파일 전체의 hash를 확인한다. 하나라도 다르면 finding을 판정하지 않고
`SNAPSHOT DRIFT`로 종료한다.

## 3. Iteration 15 이후 변경

- Framework public contract governance에서 특정 Core version을 반복해 고정하지 않고 Core 정식 spec을
  계약 근거로 참조하도록 수정했다.
- Java Instance address send 예제가 target activation을 기다린다고 오해할 수 있던 주석을 source local
  outbound admission 완료 의미로 수정했다.
- Java Instance Spot feature map을 추가하고 Config 14의 `IS-C`, `IS-B`, `IS-F`, `IS-REG`, `IS-E2E` 76개 ID를
  모두 명시했다.
- 일반 `spot.h`와 Framework 전용 `instance_spot_driver.h` 경계, driver record에서 제거한 다섯 필드의 범위,
  cold placement 두 함수와 activation·admission 전이 여섯 함수의 구성을 명확히 했다.
- Instance verifier가 Core 한글·영문 spec뿐 아니라 실제 두 service header와 root `zlink.h`를 읽어 fixed record
  field, 정확한 여덟 함수·export와 일반 header 비노출을 검사하도록 보강했다.

이 확인은 변경 부분만 보는 검토가 아니다. 두 reviewer는 전체 114개를 다시 검토한다.

## 4. 동결 검증

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-16/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-16/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-16/scope-files.txt
bash scripts/verify-framework-doc-contracts.sh
bash scripts/verify-framework-instance-spot-contracts.sh
bash scripts/verify-framework-submit-api.sh --contract
git diff --check
```

Finding이 없고 전체 scope를 읽었으며 시작·종료 hash가 같을 때만 마지막 줄을 정확히
`DOC REVIEW CLEAN`으로 기록한다.
