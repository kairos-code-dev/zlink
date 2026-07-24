# Node.js SpotActorTransfer E2E feature map

공통 정본은 [Config 10 — Spot·Actor relocation](../../../../doc/framework/common/e2e/config-10-spot-actor-relocation.ko.md)이다.
consumer는 Node HTTP client wrapper로 역할 서버 endpoint를 호출하고, bound session 검증에는 stream
connector를 사용한다. 아래 표는 정식 시나리오 ID를 한 행씩 기록한다.

| 시나리오 | 상태 | 검증 근거 |
|---|---|---|
| `ST-A1` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: local admission accept와 callback 순서. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-A2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: local admission reject의 무효과. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-A3` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: joined callback 완료 전 packet dispatch 차단. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-B1` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: remote transfer 성공과 state 복원. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-B2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: commit 뒤 source cleanup. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-B3` | 전환 필요 | 현재 runner는 transfer adapter가 없는 actor의 기본 빈 state transfer 성공과 target 기본 state를 확인한다. 그러나 `joined -> location_committed`를 성공 순서로 단언해, 공통 시나리오의 `location_committed -> joined` 순서와 다르다. 이 순서를 정렬하기 전에는 기존 Track A~E 로그를 완료 증거로 사용하지 않는다. |
| `ST-B4` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: custom empty state transfer. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-C1` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: commit 전 source 종료. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-C2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: commit 뒤 source 종료. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-C3` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: callback 단계별 failure 분류. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-D1` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: location commit 공개 시점. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-D2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: stale generation fencing. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-E1` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: transfer 성공 뒤 bound session push. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-E1A` | 부분 구현 | `internal route refresh preserves object generation while explicit bind can replace an incarnation`과 SM-D4A focused runner가 same-generation internal refresh, 새 generation explicit bind와 이전 token 격리를 검증한다. Ownership command가 durable `Completed` 뒤에만 발행되는 순서는 process relocation runner에서 추가 검증해야 한다. |
| `ST-E2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: transfer 실패 때 기존 session binding 유지. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-F1` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: moving backlog FIFO. Track F 로그: `log/20260710-200221-3864800`. |
| `ST-F2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: location publish 전 replay 순서. Track F 로그: `log/20260710-200221-3864800`. |
| `ST-F3` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: bound session의 cross-move FIFO. Track F 로그: `log/20260710-200221-3864800`. |
| `ST-F4` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: bounded straggler forwarding window. Track F 로그: `log/20260710-200221-3864800`. |
| `ST-F5` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: forwarding map 교체와 만료 뒤 축출. Track F 로그: `log/20260710-200221-3864800`. |
| `ST-F6` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: request correlation과 caller timeout 뒤 late reply 격리. Track F 로그: `log/20260710-200221-3864800`. |

## 증거 경계

- callback과 transfer 순서는 actor node가 실제 callback에서 남긴 evidence를 transfer id로 연결한다.
- location commit은 Redis key를 직접 읽지 않고 location monitoring event의 row update로 확인한다.
- bound session은 stream connector가 받은 push의 actor id, node rid와 state version으로 확인한다.
- Track F는 source backlog, target enqueue와 location commit 순서를 actor별 arrival index로 대조한다.
- request-correlation 시나리오는 request sequence와 flags를 보존하고, caller timeout 뒤 late reply가
  다음 request를 방해하지 않는지 확인한다.
