# S3 Channel·fanout amendment 독립 문서 리뷰 — iteration 3

## Snapshot 확인

- 시작 시 `scope-files.sha256` 70개 항목이 모두 `OK`였다.
- `scope-files.sha256` SHA-256은 `2c475789ba810679447659a40b42c8908fed2b83cd2d53cf882fe1fe7d4ec9b2`였다.
- `scope-files.txt` SHA-256은 `0f1172592c455d39fd208e01517133c2b583989ad8deff6e4d66644be71d8208`였다.
- 기준 HEAD `86258cb9a3ecbc7db2dfd86a8c18de45a562734a`를 확인했다.
- 루트 `AGENTS.md`, `doc/principal/documentation/documentation-principles.ko.md`,
  `doc/principal/software-design-principles.md`, iteration 3 `manifest.ko.md`와 scope 70개 파일 전체를
  읽었다.
- `scripts/verify-framework-doc-contracts.sh`는 `FRAMEWORK DOC CONTRACTS CLEAN`으로 종료했다.
- iteration 2 codex 리뷰의 6개 finding을 모두 재검증했다. Logical Multicast MeshName 제거, .NET·C++
  fanout manual subscriber runtime handle, PS-A2 packet-name feature map 정정, PS-D2 subscriber
  descriptor 제거, TicTacToe API-A↔API-B 연결, C++ HTTP hosting signature는 모두 해소를 확인했다.
  아래 finding 하나만 iteration 2와 동일한 위치에 그대로 남아 있다.

## Findings

[계약][medium] framework/doc/framework/spec/server/languages/java/01-system-structure.ko.md:118 —
"manual peer는 remote `RoutingId`를 따로 받지 않는다"는 문구가 같은 문서 205~213행의
`peerConnections().connect(RoutingId.from("play-peer"), endpoint)` 예제 및 "Remote RID까지 미리
검증해야 하면 별도 overload인 `connect(expectedRoutingId, endpoint)`를 사용한다"는 설명과 정면으로
모순된다 — iteration 2 리뷰(`iteration-2/codex/review.ko.md:25`)가 이미 같은 줄 번호에서 지적했으나
이번 amendment에서 반영되지 않고 원문 그대로 남았다. 같은 문서 안에서 "받지 않는다"(부재)와
"expected RID overload 사용"(존재)이 공존하므로 독자가 어느 쪽이 정식 계약인지 판단할 수 없다 —
118행을 "manual peer는 remote `RoutingId`를 선택적으로 받을 수 있으며, 지정하면 admission에서
endpoint identity를 확인한다"는 뜻으로 고쳐 205~213행의 두 overload(단순 `connect(endpoint)`,
expected RID 포함 `connect(expectedRoutingId, endpoint)`)와 일치시킨다.

## 종료 확인

- 종료 hash 검사에서 scope 70개가 모두 `OK`이고 두 목록 hash가 시작 값과 같음을 다시 확인했다.

DOC REVIEW NOT CLEAN
