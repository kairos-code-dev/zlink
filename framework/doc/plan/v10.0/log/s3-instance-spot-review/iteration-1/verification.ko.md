# Instance Spot S3 검증 결과

## Main document verifier

```text
FRAMEWORK DOC CONTRACTS CLEAN languages=5 exact_documents=24 connector_exact=4 formal_documents=55 code_fixtures=20 declarations=1345 transition_owners=13 transition_members=187 feature_maps=59 scenario_rows=1097
```

## Instance Spot focused verifier

```text
FRAMEWORK INSTANCE SPOT CONTRACTS CLEAN languages=5 scenarios=76 core_pairs=3 redis_states=3 cas_operations=5 samples=2
```

## Snapshot과 format

Review 전·후 aggregate SHA-256은 모두
`eb48e5138d4608f7e2b0f8989ed007144dc09e4e6deb99557dfbf435266550bb`다.

대상 범위의 `git diff --check`도 통과했다.
