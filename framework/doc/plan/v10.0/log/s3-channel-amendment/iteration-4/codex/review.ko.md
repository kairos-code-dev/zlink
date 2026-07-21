# S3-CH·S3-FO iteration 4 Codex 독립 문서 리뷰

## Snapshot 검증

- 시작 시 `scope-files.sha256`의 71개 파일이 모두 일치했다.
- 파일별 hash 목록 SHA-256은 `c6c88d4db2ee18b87f99eec7c5cd80d6e1f6715c3324b2249c1996bb20c846da`였다.
- scope 파일 목록 SHA-256은 `00bba02f27b40d650b02d659bcd613d4e69097220565dd42419fa2a2937f7818`였다.
- `scope-files.txt`에 고정된 71개 파일 전체를 검토했다.
- `scripts/verify-framework-doc-contracts.sh`는 `FRAMEWORK DOC CONTRACTS CLEAN`으로 끝났으며 scenario row는 995개였다.
- 종료 시에도 71개 파일과 두 SHA-256 값이 시작 시점과 일치했다.

## Findings

[계약][high] framework/doc/framework/common/e2e/config-3-pubsub.ko.md:22 — automatic fanout subscriber의 실제 연결 집합과 연결 lifecycle을 공개 evidence로 요구하지만 이를 읽는 공개 관찰 계약이 없다 — PS-D2~PS-D5는 제외 endpoint의 `ConnectionReady` 부재, endpoint별 연결·해제와 stale endpoint 제거를 검증하도록 요구한다. 그러나 `spec/server/50-runtime-monitoring.ko.md:8`의 공개 monitoring 범위와 snapshot·event는 RouteMesh와 ClientServer만 포함하고, `spec/server/40-location-runtime.ko.md:268`은 location 상태를 MeshNode snapshot으로 관찰하도록 한정한다. 또한 `spec/05-framework-api.ko.md:258`의 endpoint 연결 handle은 manual subscriber의 목록만 소유하며 automatic discovery 결과를 관찰하거나 수정하는 표면이 아니므로, fanout-only automatic subscriber는 private runtime 또는 raw socket hook 없이 이 E2E evidence를 만들 수 없다 — automatic fanout subscriber용 read-only snapshot과 lifecycle event를 공통 monitoring 계약과 다섯 언어 exact interface에 추가해 endpoint 또는 descriptor identity, generation·revision, ready·disconnected 상태를 관찰하게 하고 Config 3·inventory·verifier를 그 표면에 맞춘다. Manual endpoint 연결 handle의 mutation 책임은 현재처럼 manual mode에만 둔다.

DOC REVIEW NOT CLEAN
