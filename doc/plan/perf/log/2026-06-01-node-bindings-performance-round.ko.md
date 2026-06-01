# 2026-06-01 Node bindings 성능 작업 로그

이 문서는 `bindings-library-performance-improvement-plan-2026-05-30.ko.md`에서 분리한
Node 측정과 후보 검토 기록이다. 계획 문서 본문에는 최종 상태 표와 간단한 판정만 남긴다.

## 진행 원칙

- 후보 개발 중에는 남은 보류 항목의 pattern, transport, msg-size만 좁혀 측정한다.
- 후보가 표에 반영될 만큼 개선되면 같은 failset 또는 all-transport 묶음으로 다시 확인한다.
- 다른 언어로 넘어가기 전에는 Node 전체 또는 문서 failset을 넓게 재확인한다.
- `bindings/node/dist/index.d.ts`와 `bindings/node/src/zlink/contracts/**` 공개 계약은 바꾸지 않는다.
- `recvPayloadInto`, `subscribePayloadInto`, `publishFrom`, `sendFrom`, borrowed send helper 계열은
  현재 public surface guard가 금지하므로 되살리지 않는다.

## 현재 문서 반영 방식

- 계획 문서 본문에는 결과 표, 기준 파일, 최종 판정만 둔다.
- 후보별 상세 측정, 실패 이유, 원복 근거는 이 log 파일에 이어서 남긴다.
- 이전에 계획 문서 `## 10. 라운드 진행 로그`에 들어 있던 시간순 로그는 본문에서 제거한다.
  필요한 세부 근거는 각 perf 결과 파일과 이 log 파일에 남긴다.

## 이전 log에서 재사용할 판단 기준

- 이전 log `2026-05-18-bindings-performance-round.ko.md`에서 Node에 효과가 컸던 항목은
  `MULTI_DEALER_DEALER`의 `sendFrom(..., DontWait)`와 `recvInto(buffer, DontWait)` 계열이었다.
  당시 tcp 전체가 통과권으로 올랐지만, 현재 public surface guard는 borrowed/raw payload helper
  계열을 금지하므로 그대로 되살리지 않는다.
- 같은 log에서 `MULTI_PUBSUB`은 `TopicMessage` 재사용만으로는 부족했고, 추가 개선은
  raw/typed subscribed receive facade가 필요하다고 정리되어 있었다. 현재 코드는
  `TopicMessage` 재사용과 latency sampling까지 반영되어 있으므로, 같은 재사용 후보를 반복하지 않는다.
- SPOT 계열의 과거 `MsgUnit(B)=4096` 문제는 context `autoHwmMsgUnitBytes` 공개 옵션으로
  해결된 상태다. 현재 perf 결과에서도 `MsgUnit(B)`가 msg size와 맞는지 먼저 확인하고,
  이 문제가 아닌 경우에는 send/recv hot path 병목으로 본다.
- `MULTI_STREAM` small은 과거에도 public stream send builder와 frame 재구성 비용이 병목으로
  기록됐다. Buffer 수명 안전성이 불명확한 scratch frame 재사용은 이번 라운드에서도 heap
  corruption을 만들었으므로 다시 시도하지 않는다.

## MULTI_SPOT_SENDSEND small active slot 32 후보

- 대상:
  - `MULTI_SPOT_SENDSEND`
  - `tcp,ws,wss,tls`
  - `64,256,1024B`
- 의도:
  - 100개 spot 전체를 active로 순회하는 비용을 줄이기 위해 small size active spot 수를 32로 제한한다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_135317_node_multi_spot_sendsend_small_active32_probe_20260601.txt`
  - status: complete
- 결과:
  - tcp 64/256/1024B: 32.142/36.066/29.075 Kops/s
  - tls 64/256/1024B: 31.786/32.085/24.265 Kops/s
  - ws 64/256/1024B: 35.248/23.292/28.920 Kops/s
  - wss 64/256/1024B: 21.766/26.160/21.986 Kops/s
- 판정:
  - 기존 최종값의 47~50 Kops/s대보다 낮은 cell이 많다.
  - active slot 축소는 순회 비용보다 round-trip concurrency 감소 효과가 더 컸다.
  - 보류 해소에 도움이 되지 않아 최종 코드와 표에는 반영하지 않는다.
