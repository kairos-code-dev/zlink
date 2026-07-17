# S5 iteration 1 finding ledger

R1(Codex) 9건 + R2(Claude Fable) 3건, 중복 없음 → 12건. editorial note 0건.
snapshot: `8206fd44dcd`, scope `281c02e4…` (625 files).

| ID | 출처 | 축 | 심각도 | 요지 | 수정 범위 | 상태 |
|---|---|---|---|---|---|---|
| F-I1-01 | R1 | I1 | high | NODROP remote 전송이 순차 send로 부분 전달 가능(전체 reserve→일괄 commit 아님) | code+test (`mesh_wire`) | open |
| F-I1-02 | R1 | I1 | high | Spot timer가 count만 증가·감소 경로 없음. logical Spot 수명(마지막 facade 해제 시 제거·재생성 generation 증가)·timer generation 격리·handler 상호배제 미구현 | code+test (`mesh_api`, `mesh_node_api`, runtime) | open |
| F-I1-03 | R1 | I1 | high | actor destroy가 즉시 삭제 — held claim·completion·bound session control을 deadline까지 drain하지 않음 | code+test (`mesh_actor_api`) | open |
| F-I1-04 | R1 | I1 | high | shutdown deadline에서 outstanding operation의 exactly-once `ESHUTDOWN` terminal completion 미생성, timeout 0 op 무기한 잔존, 재-shutdown `EDEADLK` | code+test (`mesh_node_api`) | open |
| F-I1-05 | R1 | I1 | medium | inbound peer의 관측 endpoint 미기록으로 manual/discovery MIXED 병합 도달 불가 | code+test (`mesh_wire`, descriptor) | open |
| F-I1-06 | R1 | I1 | medium | 상위 generation 교체 시 이전 generation DRAINING entry 미유지(관측 가능한 draining count 계약) | code+test (`mesh_wire`) | open |
| F-I1-07 | R1 | I1 | medium | peers/peer_channels/bindings query가 element 검증 실패 시 partial output 잔존 | code+test (`mesh_node_api`, `mesh_stream_session_api`) | open |
| F-I1-08 | R1 | I1 | medium | 공개 문자열 경로가 strict UTF-8 validator를 공유하지 않음 | code+test (name/topic 검증 경로) | open |
| F-I2-01 | R1 | I2 | medium | `mesh_wire`가 codec·admission state machine·service ingress router·transport를 한 파일에 결합 | code (모듈 분리, 공개 API 불변) | open |
| F1 | R2 | I1 | high | claim serial→owner 역해석 표가 전역인데 serial은 node별 1부터 — 다중 MeshNode에서 충돌 | code+test (`mesh_dispatch_api`) | open |
| F2 | R2 | I1 | medium | monitor handler 재진입 가드(`handler_active`)가 어디서도 set되지 않는 사문 | code+test (`mesh_runtime`, `mesh_monitor_api`) | open |
| F3 | R2 | I3 | low | `mesh_messaging_api.cpp:406-409` 무의미 삼항(양쪽 동일) | code | open |

## known risk 판정 합의

- TSAN 3계열: R1 정적 반박(part_helper)·불완전(auto-HWM·ypipe), R2 신규 코드 무관 판정 → finding 아님, 추적 유지.
- MIXED 도달성 → F-I1-05로 승격. peer DRAINING → F-I1-06으로 승격. 무기한 operation → F-I1-04로 승격.

## 처리 방침

12건 전부 이번 iteration에서 수정 후 전체 검증 재실행, iteration 2에서 두
리뷰어가 delta+직접 영향 범위를 재검토하고 마지막 전체 pass를 수행한다.
