# S3 iteration 12 — Codex 독립 문서 리뷰

## 1. 실행 증거

| 항목 | 값 |
|---|---|
| reviewer | Codex agent `/root/s3_codex_review_i12` |
| 범위 | iteration 12 frozen scope 202개 전체 |
| 시작 hash | 202/202 일치, 실패 0 |
| 종료 hash | 202/202 일치, 실패 0 |
| aggregate | `9a41f1d2a3961d30dc68ee68039669b4ef2751ba3ce4684d1b993dad2baacb76` |
| file-list | `dfabc81f2c0ec7403683bc54ab3f350d9cf6565aac60a3baf9579192c513642f` |
| 종료 HEAD | `169c458ed238228d7a23cea089c8c467c96b953c` |
| 파일 수정 | 없음 |

## 2. Finding

1. `[1차소스][high]` `cpp/guide/04-di-container.ko.md:17` — application이 `create_scope(kind)`와
   `scope.kind()`를 사용할 수 있다고 설명하지만 exact 계약은 framework만 scope를 만든다.
2. `[1차소스][high]` `cpp/guide/04-di-container.ko.md:78` — 미선언 `add_factory(factory, lifetime)`
   overload를 사용한다.
3. `[1차소스][high]` `cpp/guide/13-interface-catalog.ko.md:89` — worker 구성, Spot publish와 destroy,
   session actor·manager 등 여러 표면이 exact C++ interface와 다르다.
4. `[1차소스][medium]` `cpp/guide/03-concepts.ko.md:117` — server STREAM에 reconnect·heartbeat 책임을
   부여하지만 공통 Connector 계약은 client connector 책임으로 고정한다.
5. `[1차소스][medium]` `cpp/guide/16-grpc-alternative.ko.md:60` — 다섯 정식 언어 계약과 다른 구현 상태·언어
   roadmap을 현재 가이드에 기록한다.
6. `[원칙][medium]` `cpp/guide/01-overview.ko.md:100` — 사용자 가이드 그림에 Core C API, ZMP,
   transport와 I/O thread 내부 구조가 들어 있다.
7. `[원칙][low]` `cpp/guide/16-grpc-alternative.ko.md:32` — 의인화·구어체와 작성 태도 자평이 문서 원칙에
   어긋난다.
8. `[원칙][low]` `cpp/guide/03-concepts.ko.md:5` — 여섯 개념을 다섯 개라고 설명한다.
9. `[원칙][low]` `cpp/guide/13-interface-catalog.ko.md:142` — STREAM 등록 문장이 열린 괄호에서 끊긴다.

Finding이 있으므로 `DOC REVIEW CLEAN`을 출력하지 않았다.
