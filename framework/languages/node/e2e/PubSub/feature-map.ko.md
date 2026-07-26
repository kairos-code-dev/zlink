# Node.js PubSub E2E feature map

| Scenario | 상태 | Node.js 검증 파일 | 비고 |
|----------|------|-------------------|------|
| `PS-A1` | 구현 | `Client/Scenarios/ps-a1-fanout-basic-delivery-scenario.ts` | warm-up barrier 뒤 모든 subscriber가 공유하는 연속 sequence marker를 실제 subscriber 역할 server의 `/evidence/wait`로 확인한다. |
| `PS-A2` | 10.0.0 전환 대상 | 기존 `PS-A2` scenario | 현재 transport filter 시나리오를 서로 다른 packet name의 typed handler가 자기 event만 정확히 한 번 처리하는 시나리오로 교체해야 한다. |
| `PS-A3` | 구현 | `Client/Scenarios/ps-a3-late-subscriber-scenario.ts` | 차단된 transport에서 ready 전 event를 한 번 발행하고, subscriber의 실제 `ConnectionReady` 뒤 첫 event 수신과 이전 event replay 부재를 확인한다. |
| `PS-A4` | 구현 | `Client/Scenarios/ps-a4-subscriber-reconnect-scenario.ts` | subscriber process를 유지한 채 transport를 끊고 복구해 기존 subscription 자동 재적용, disconnect 구간 non-replay, fast subscriber 지속 수신을 확인한다. |
| `PS-B1` | 구현 | `Client/Scenarios/ps-b1-slow-subscriber-scenario.ts` | slow subscriber delay evidence와 fast subscriber tail event marker를 실제 subscriber 역할 server evidence로 확인한다. |
| `PS-B2` | 구현 | `Client/Scenarios/ps-b2-publisher-restart-scenario.ts` | publisher를 같은 endpoint로 재시작한 뒤 기존 subscriber의 `ConnectionReady`와 첫 event 전달을 확인한다. |
| `PS-C1` | 구현 | `Client/Scenarios/ps-c1-missing-message-name-scenario.ts` | subscriber dispatch drop evidence와 이후 정상 publish delivery marker를 실제 subscriber 역할 server evidence로 확인한다. |

## 검증 경로 판정

현재 source와 runner는 Redis를 등록하지 않고 manual endpoint를 사용한다. 아래 PS-A1~C1은 manual
회귀 증거이며 automatic discovery 완료 증거가 아니다.

| Scenario | 상태 | Node.js 목표 증거 |
|---|---|---|
| `PS-D1` | 미구현 | 전용 publisher descriptor·Publisher RID·endpoint 없는 subscriber actual port 연결 |
| `PS-D2` | 미구현 | public `ZLinkFanoutRuntime` snapshot의 publisher identity·connection intent와 `excluded_draining` discriminated event entry로 다른 ChannelName·descriptor 종류·drain 중 publisher를 제외하고, actual native SUB ready publisher만 handler에 도달 |
| `PS-D3` | 미구현 | public fanout snapshot의 `connectionIntentCount=2`·`readyConnectionCount=2`와 실제 native disconnect 후 publisher changed discriminated event의 `disconnected` entry로 publisher 추가·정상 제거 수렴 |
| `PS-D4` | 미구현 | public fanout event의 기존 identity `disconnected`, 새 identity `reconnecting`·actual native `ready`, `excluded_stale` discriminated entry와 최신 snapshot으로 lease 만료·재등록·낮은 generation/revision 거부 확인 |
| `PS-D5` | 미구현 | public location changed discriminated event의 `degraded`·`ready` Location snapshot, publisher changed `reconnecting`·actual native `ready`·`excluded_stale` entry와 current connection intent snapshot으로 fail-static·복구 수렴 확인 |
| `PS-D6` | 미구현 | port 0 재시작과 advertised endpoint 갱신 |
| `PS-D7` | 미구현 | capacity 1 observer의 bounded coalescing·sequence gap 후 public snapshot resync, `AbortSignal` 격리, 정상 observer·dispatch 지속과 manual endpoint mutation의 automatic snapshot·event 격리 |
| `PS-E1` | 구현 | 현재 manual runner를 store 없는 별도 회귀로 유지 |
| `PS-E2` | 미구현 | automatic subscriber store 누락, automatic/manual mode 혼합, 고정 Publisher RID와 자동 할당 둘 다 누락, fixed/allocated RID 동시 설정의 typed startup 오류와 store 없는 manual 조합 성공 |

Classic fanout의 수신자는 client stream session이 아니라 subscriber 역할 server다. 공통 E2E README는
이 경우 subscriber handler가 남긴 bounded `/evidence/wait` marker를 성공 기준으로 사용할 수 있다고
정리한다. 따라서 이 feature map은 별도 client stream connector observer를 요구하지 않는다.

## 검증

- `timeout 420s framework/languages/node/e2e/PubSub/run_e2e.sh`
  - 결과: `pubsub e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260715-074718-2239660`
  - 통과 scenario: `PS-A1`, `PS-A2`, `PS-A3`, `PS-A4`, `PS-B1`, `PS-B2`, `PS-C1`

이 로그에서 `PS-A2`라는 이름으로 통과한 항목은 기존 transport filter 시나리오다. Packet name별 typed
handler dispatch로 교체하기 전에는 공통 `PS-A2` 완료 증거로 사용하지 않는다.
