# Instance Spot S3 문서 검토 iteration 1 manifest

## 1. 목적

이 manifest는 `S3-IS-01`에서 Codex와 Claude Sonnet이 같은 Instance Spot 계약 snapshot을
검토했음을 확인하기 위한다. 원본 계약은 Core·Framework 정식 spec, 다섯 언어 exact
interface, Config 14, 두 reference sample, Redis fixture와 verifier를 포함한다.

## 2. Snapshot

Review 대상은 다음 범위를 파일 경로 순으로 SHA-256 한 결과로 고정했다.

- `core/doc/spec/core/` 아래의 모든 Markdown 파일
- `framework/doc/framework/spec/` 아래의 모든 Markdown 파일
- Config 14, GameQuest·ShoppingMall, topology fixture
- MeshNode descriptor·Instance location Redis fixture
- RouteMesh v10 contract inventory와 main·Instance verifier
- `instance-spot-draft.ko.md`

Aggregate SHA-256:

```text
eb48e5138d4608f7e2b0f8989ed007144dc09e4e6deb99557dfbf435266550bb
```

Review 전·두 reviewer 완료 후에 같은 값을 재확인했다. 이 값은 Git object ID가 아니라
작업 tree 문서 집합의 aggregate hash다.

## 3. 완료 판정

- Codex: `OPEN FINDINGS 0`
- Claude Sonnet 2.1.215: `OPEN FINDINGS 0`
- Main document verifier: clean
- Instance Spot focused verifier: clean
- 대상 범위 `git diff --check`: clean

Claude의 첫 실행은 읽기 turn 상한으로 전체 범위를 검토하지 못해 완료 증거로 사용하지
않았다. 상한을 늘려 누락 범위를 모두 읽은 두 번째 실행 결과만 채택했다.
