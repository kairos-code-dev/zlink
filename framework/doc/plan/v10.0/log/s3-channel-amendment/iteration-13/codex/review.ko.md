# Codex 독립 문서 review — iteration 13

113개 scope의 시작·종료 hash가 일치했고 모든 verifier와 `git diff --check`가 통과했다. Snapshot drift는
없다. 다음 계약 충돌 때문에 clean으로 판정하지 않는다.

[계약][high] `framework/doc/framework/spec/server/41-location-store-redis.ko.md:164` — Instance row의
canonical JSON이 byte-for-byte fixture와 충돌한다 — 정식 spec과 다섯 언어 exact interface는 Instance
location에서 `SpotKind`를 제거했지만 `framework/testdata/location/redis/instance-spot-location-v1.json:27`은
`SpotKind:"Instance"`를 포함한다. 공식 Redis extension이 spec을 따르면 fixture와 다른 bytes를 만든다 —
`SpotKind`를 fixture에서 제거하고 세 상태의 canonical JSON을 정식 spec 및 exact interface와 일치시켜야 한다.

[계약][medium] `scripts/verify-framework-instance-spot-contracts.sh:505` — Instance fixture verifier가 정식
canonical JSON과의 일치를 검사하지 않아 위 충돌을 clean으로 통과시킨다 — verifier는 spec의 fixture link만
확인한 뒤 자체 field 목록에 `SpotKind`를 하드코딩하며, 정식 spec의 JSON field 순서와 bytes를 읽어 비교하지
않는다 — 정식 spec의 canonical JSON code block을 추출해 fixture의 `Ready` row와 byte-for-byte 비교하고, 세
상태도 같은 field schema를 사용하는지 검증해야 한다.

`DOC REVIEW CLEAN` 아님.
