# S3 amendment iteration 13 finding ledger

| ID | reviewer | severity | finding | 처리 |
|---|---|---|---|---|
| A13-01 | Codex·Claude Sonnet | high | Instance Redis inline canonical JSON과 byte-for-byte fixture의 `SpotKind` field 집합이 다르고 verifier가 불일치를 허용함 | Instance location에서 제거한 `SpotKind`를 fixture의 세 상태에서 제거했다. Inline Ready JSON의 timestamp를 fixture와 맞추고 focused verifier가 inline JSON과 Ready fixture bytes를 직접 비교하며 세 상태의 동일 field schema를 검사하게 했다 |
| A13-02 | Claude Sonnet | low | 공통 E2E 시나리오 ID 표에 Config 14의 `IS` 접두사가 없음 | `IS` — Instance Spot 행을 추가했다 |

새 계약은 Instance location에서 항상 같은 값이던 `SpotKind`를 되살리지 않는다. Location 종류는 record kind와
전용 Store capability가 이미 확정하므로 fixture를 정식 spec에 맞추는 방향을 선택했다.

두 finding을 반영한 뒤 세 contract verifier와 scoped `git diff --check`가 통과했다. 다음 review는 새 hash의
iteration 14 전체 scope에서 처음부터 수행한다.
