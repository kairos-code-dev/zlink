# S1 Core 정식 스펙 finding ledger — iteration 4

| ID | 병합 finding | 출처 | 상태 |
|---|---|---|---|
| F4-01 | request result enum에 `BACKPRESSURED=113`을 공통 반영 | C4-01 | 수정 완료 — socket 공통과 errors exact enum 일치 |
| F4-02 | MeshName conflict와 trust/auth failure 분리 | C4-02 | 수정 완료 — MeshName·RID/generation conflict와 trust/auth result 분리 |
| F4-03 | MeshNode `MAXMSGSIZE` option 지원 표 통일 | C4-03 | 수정 완료 — socket 공통과 MeshNode 지원 표 일치 |
| F4-04 | caller output capacity 부족을 `BUFFER_TOO_SMALL/ENOBUFS`로 통일 | C4-04 | 수정 완료 — channel·subscription query no-partial 재시도 계약 통일 |
| F4-05 | Core 소유 Actor transfer data plane·ACK·ordering·failure 계약 완결 | C4-05 | 수정 완료 — private allowance, 자동 전송, high-water ACK와 rollback 명시 |
| F4-06 | part submit 실패 시 전체 staging abort·sequence 종료·안전한 새 시작 명시 | C4-06 | 수정 완료 — PAIR·PUB·XPUB·DEALER·ROUTER 공통 계약, STREAM 비적용 명시 |
| F4-07 | claim recv의 중복 `domains` argument 제거 | C4-07 | 수정 완료 — 4-argument exact signature로 단순화 |
| F4-08 | public peer snapshot과 숨겨진 raw/internal peer state 경계 명시 | C4-08 | 수정 완료 — service README 경계 보정 |
| F4-09 | formal multicast에서 storage·encoding 구현 전략 제거 | C4-09 | 수정 완료 — observable ownership·delivery·result만 유지 |
| F4-10 | 항상 실패하는 `zlink_msg_gets()`를 10.0.0 공개 계약에서 제거 | C4-10 | 수정 완료 — formal no-hit, inventory 제거와 CI-16 추적 |
| F4-11 | Message formal spec의 internals 링크 제거 | S4-01 | 수정 완료 — formal owner link만 유지 |
| F4-12 | TLS `See also`·`참고` link target 한·영 일치 | S4-02 | 수정 완료 — link target exact parity |

정방향·역방향 inventory, 한·영 C block 25쌍, 52개 link·fence, 금지 문구와 `git diff --check`가
통과했다. iteration 5에서 새 동결본 전체를 Codex와 Claude Sonnet이 다시 독립 리뷰한다.
