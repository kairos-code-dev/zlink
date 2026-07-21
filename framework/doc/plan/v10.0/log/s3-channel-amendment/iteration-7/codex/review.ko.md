# Codex 독립 리뷰 결과

- 시작·종료 HEAD: `86258cb9a3ecbc7db2dfd86a8c18de45a562734a`
- 검토 파일: 71/71
- 파일 집합 SHA-256: `0f58a4cc8368b9aad99cc69213c2f3bf99facc3a3587f482847c86ac7ebab608`
- 파일 목록 SHA-256: `00bba02f27b40d650b02d659bcd613d4e69097220565dd42419fa2a2937f7818`
- verifier: `FRAMEWORK DOC CONTRACTS CLEAN`

Iteration 6의 Node runtime null 가능성, sample topology와 classic fanout topic finding은 지정된 범위에서
수정됐다. 다음 의미 불일치가 남아 있다.

## Finding

[계약][high] `framework/doc/framework/spec/server/51-runtime-metrics.ko.md:100` — location record와 owner
lease metric이 모든 descriptor를 집계하면서 label을 `mesh_name`으로 고정한다 — ClientServer server와
fanout publisher descriptor는 정식 location 계약상 MeshName이 없고 ChannelName으로 식별되므로 두
topology의 record·lease를 허위 MeshName 없이 표현할 수 없다 — topology별 metric을 분리하거나
record·owner kind별로 정확한 scope label 계약을 정하고 verifier가 descriptor kind와 label scope를
교차 검사해야 한다.

[계약][high] `framework/doc/framework/common/sample/tictactoe/README.ko.md:26` — sample 문서가 Channel별
endpoint가 있는 것처럼 물리 연결을 설명하고 Bingo 책임 표도 `Api`와 `Play`를 ChannelName으로 남긴다 —
RouteMesh 계약은 ChannelName마다 endpoint를 만들지 않으며 canonical fixture는 TicTacToe MeshNode pipe와
`bingo.api`·`bingo.room`만 정의한다 — TicTacToe를 MeshNode ROUTER peer endpoint 표현으로 고치고 Bingo의
비정식 ChannelName을 canonical 이름으로 통일하며 verifier에 해당 금지 표현을 추가해야 한다.

[계약][medium] `framework/languages/java/e2e-kotlin/PubSub/feature-map.ko.md:13` — PS-B2 목표 증거가 등록
topic을 유지하는 publisher restart 동작으로 정의되어 있다 — 10.0.0 classic fanout exact interface는
topic을 받지 않고 typed handler와 Framework packet name 등록을 유지하므로 제거 대상인 topic subscription을
회귀 조건으로 보존하게 된다 — 등록한 typed handler와 subscriber process를 유지한다고 수정하고 verifier가
다섯 PubSub feature map의 classic fanout topic 표현을 금지해야 한다.

종료 시 71개 파일별 hash와 두 aggregate hash가 모두 일치했고 문서 계약 verifier가 통과했다.

DOC REVIEW NOT CLEAN
