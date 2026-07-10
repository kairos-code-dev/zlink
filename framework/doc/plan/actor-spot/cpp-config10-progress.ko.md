# C++ config-10 Spot Actor Transfer — 진행확인표

> 목표: cpp framework/샘플을 .NET 레퍼런스와 동등하게, config-10 Spot Actor
> Transfer E2E를 **근본수정**으로 통과.
> 정본 스펙: [common/spec/spot-actor.ko.md](../../framework/common/spec/spot-actor.ko.md)
> (§9 bound session transfer, §10 in-flight handoff),
> 공통 검증표: [common/e2e/config-10-spot-actor-transfer.ko.md](../../framework/common/e2e/config-10-spot-actor-transfer.ko.md).
> 상세 작업기록: [cpp-worker.ko.md](cpp-worker.ko.md), [in-flight-handoff/README.ko.md](in-flight-handoff/README.ko.md).
> 최종 갱신 2026-07-11. 현재 **16/19 실측 통과**.

## 1. 시나리오 진행표 (19개)

| ID | 시나리오 | 상태 | 근거 / 잔여 원인 |
|----|----------|:----:|------------------|
| ST-A1 | local join accept | ✅ | 실측 통과 |
| ST-A2 | local join reject | ✅ | 실측 통과 |
| ST-A3 | local moving-dispatch 차단 | ✅ | 실측 통과 (delay-joined gate 동안 packet 보류) |
| ST-B1 | remote stateful transfer | ✅ | 실측 통과 |
| ST-B2 | source cleanup 실패 후 성공 | ✅ | 단독 실측 통과 |
| ST-B3 | remote missing adapter | ✅ | 실측 통과 (기본 빈 state transfer) |
| ST-B4 | remote empty-state transfer | ✅ | 실측 통과 |
| ST-C1 | source down before commit | ✅ | 단독 실측 통과 |
| ST-C2 | source down after target commit | ❌ | **ST-F6** — bound session이 node-b bind인데 actor는 node-a → cross-node push `request_timeout` |
| ST-C3 | callback 실패 분류 (4종) | ✅ | 실측 통과 |
| ST-D1 | location commit timing | ✅ | 실측 통과 |
| ST-D2 | stale source release fencing | ✅ | 실측 통과 |
| ST-E1 | bound push after remote transfer | ✅ | 2026-07-11 근본해결 (DTO stream_payload ADL 브리지) |
| ST-E2 | bound session rebind isolation | ✅ | 2026-07-11 근본해결 (동상) |
| ST-F1 | in-flight handoff 순서 | ✅ | 실측 통과 (backlog 보존 순서) |
| ST-F2 | direct 추월 방지 | ✅ | 실측 통과 |
| ST-F3 | bound session cross-move 순서 | ✅ | 2026-07-11 근본해결 (동상) |
| ST-F4 | straggler forward then fail-fast | ❌ | **ST-F6** — window 후 stale ref 요청이 source에서 fail-fast stale 안 됨 (actor_id 재해결로 라이브 추종) |
| ST-F5 | forwarding mapping eviction | ❌ | **ST-F6** — 동상 (chained hop) |

**요약: 16 ✅ / 3 ❌ (C2·F4·F5, 전부 ST-F6 의존)**

## 2. 잔여 작업표

| # | 작업 | 계층 | 우선순위 | 상태 | 의존/비고 |
|---|------|------|:--------:|:----:|-----------|
| 1 | **ST-F6**: §10.5 request-reply forwarding + §9 bound session transfer | framework | **P0** | ⬜ | dotnet `ZLinkActorSessionForwarder` 이식. C2·F4·F5 해제 |
| 2 | §11-12 계약: commit 전 성공 노출 없음 + 실패 시 route 비오염 evidence | framework | P0 | 🟡 | 1 완료 시 config-10 ST-C로 마감 |
| 3 | config-10 3-pass 전체 runner 그린 | e2e | P0 | 🟡 | 1 완료 후 러너 최종 확인 |
| 4 | `backlog_enqueued` marker 타이밍 정합 | framework | P1 | ⬜ | 순서는 보존(F1/F2 통과), marker 발화 시점만 잔여 |
| 5 | H6: in-flight handoff POSD/DDD 루프 | doc/refactor | P1 | ⬜ | 전 언어 transverse |
| 6 | P5: codex POSD/DDD 리팩토링 루프 CONVERGED | refactor | P1 | 🟡 | 회귀 그린 유지 |
| 7 | public source interface ↔ contract test 정본화 | framework | P2 | ⬜ | `session_actor_manager_t`→`actor_ref_t` 원복됨, 계약 결정 필요 |
| 8 | 샘플 join/transfer 순서 코드 검토 | sample | P2 | 🟡 | run_sample.sh 전부 통과, 코드 레벨 검토만 |

범례: ✅ 완료 · 🟡 부분/검토잔여 · ⬜ 미착수 · ❌ 실패

## 3. ST-F6 세부 분해 (작업 #1)

| 하위 | 내용 | 스펙 | 해제 시나리오 |
|------|------|------|---------------|
| F6-a | 요청을 ref 노드로 제출(actor_id 재해결 대신), source 노드가 window 내 forward | §10.2-6, §10.5 | F4 |
| F6-b | chained hop forwarding mapping + eviction 후 fail-fast stale | §10.4-3 | F5 |
| F6-c | request-reply 상관관계를 forward 경로에서 유지 (reply 채널 복귀) | §10.5 | F4/F5 |
| F6-d | bound session route를 commit 시 target으로 rebind + source 전달 | §9 | C2 |
| F6-e | source node 사망 시 bound session 정리/재바인드 | §9 | C2 |

**계층 확인**: core 전송 primitive(`request_to_spot`, mesh, STREAM)는 이미 존재.
ST-F6는 그 위에서 actor-level 오케스트레이션을 하는 **framework 작업** (core 변경 아님).
단, 모든 샘플이 공유하는 actor 요청/세션 경로를 건드리므로 착수 시 샘플 전체 회귀 검증 동반.

## 4. 이번 세션(2026-07-11) 확정·커밋 내역

| 항목 | 커밋 |
|------|------|
| config-10 e2e + §10 handoff + detached-close + connector + boost ODR (39파일) | `244074ccf` |
| config-10 상태 문서 갱신 (16/19) | `53320238a` |

E1/E2/F3 근본해결: connector 응답 디코드(`apply_packet_payload`)에 `from_json`
fallback이 없어 no-op으로 빈 메시지를 남기던 갭 → e2e DTO에 `to_stream_payload`/
`from_stream_payload` ADL 브리지 추가. B2/C1 단독 통과 확인.
