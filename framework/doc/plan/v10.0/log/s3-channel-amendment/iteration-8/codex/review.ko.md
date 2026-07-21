# Iteration 8 Codex review

## 판정

`DOC REVIEW NOT CLEAN`

## 확인 결과

1. `framework/doc/framework/common/sample/tictactoe/README.ko.md`의 완료 기준에
   `수동 endpoint로 channel을 연결한다`는 문구가 남아 있었다. RouteMesh endpoint는 MeshNode peer
   pipe를 구성하며 ChannelName은 그 pipe 위의 업무 대상을 선택한다. 이 문구는 두 개념을 다시
   결합하므로 수정이 필요하다.
2. `scripts/verify-framework-doc-contracts.sh`의 `scenario_rows`는 실제 feature-map 행이 아니라 기대
   ID 개수를 합산했다. 또한 같은 scenario ID가 여러 번 나타나도 통과했으므로, 각 ID가 정확히 한 번
   나타나는지 검사하고 실제 행 수를 집계해야 한다.

시작·종료 파일 집합 hash와 파일 목록 hash는 manifest와 일치했다.
