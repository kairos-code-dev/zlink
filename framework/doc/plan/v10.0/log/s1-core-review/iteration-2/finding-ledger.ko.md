# S1 Core 정식 스펙 finding ledger — iteration 2

## 1. 병합 결과

두 reviewer finding을 원인 단위로 합쳤다. 같은 socket bulk API 문제와 같은 한영 Utilities 문제는 한
수정 항목으로 관리한다.

| ID | 병합 finding | 출처 | 상태 |
|---|---|---|---|
| F2-01 | raw socket 문서를 retained `*_part` ABI로 통일하고 STREAM mode·framing 모순 제거 | C2-01·02·06·07, S2-01 | 완료 — bare bulk API no-hit, assigned signature `ZLINK_EXPORT`·header 일치, 한영 C block 일치 |
| F2-02 | Actor join·location·control에 Spot lifecycle generation 보존 | C2-03 | 완료 — join parameter와 location/control field 3개 추가, `EINVAL`·`ESTALE` 계약 및 reverse inventory hash 갱신 |
| F2-03 | Context에서 제거된 SpotNode 내부 topology 삭제 | C2-04 | 완료 — 제거 socket 이름과 bucket·queue·hysteresis 설명 no-hit |
| F2-04 | Utilities export, 한영 heading, Timer parameter·thread-safety parity 수정 | C2-05·09, S2-02 | 완료 — atomic export 6개와 Timer 계약 동기화, Utilities C block exact parity |
| F2-05 | public spec의 내부 owner·retry·sentinel·lock 구현 설명 제거 | C2-08, S2-03 | 완료 — 대상 내부 식별자와 구현 설명 scoped no-hit |
| F2-06 | BLOCKY 한영 의미·값 범위 통일 | S2-04 | 완료 — context default 1과 socket API `NOT_SUPPORTED`/`ENOTSUP` 한영 일치 |
| F2-07 | 번호가 붙은 실제 파일명과 link 표시명 통일 | C2-10, S2-05 | 완료 — 52개 frozen scope local link·fence 검사 통과 |
| F2-08 | MeshNode validation order 한영 parity 수정 | S2-06 | 완료 — argument, state, target lookup, backpressure 순서 일치 |

## 2. 재리뷰 조건

모든 항목을 수정한 뒤 정방향 inventory, 역방향 목표 inventory, 한영 C block 25쌍, 52개 local
link·fence, 번호, current-contract 표현, 제거 이름, plan 역참조와 `git diff --check`를 다시 통과했다.
새 iteration에서는 이전 finding만 확인하지 않고 같은 52개 정식 문서 전체를 Codex와 Claude Sonnet이
각각 다시 읽는다.
