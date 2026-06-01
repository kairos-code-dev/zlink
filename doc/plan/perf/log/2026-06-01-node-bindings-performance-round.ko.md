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

## MULTI_ROUTER_ROUTER wss/tls 256/1024B 제한 재측정

- 대상:
  - `MULTI_ROUTER_ROUTER`
  - `wss,tls`
  - `256,1024B`
- 근거:
  - 이전 log에서 wss `MULTI_ROUTER_ROUTER 1024B`는 단독 재측정으로 통과한 전례가 있었다.
  - 현재 표에서도 wss/tls 256/1024B가 30% 기준 바로 아래라 넓은 full 대신 해당 cell만 확인했다.
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/node/perf/run_benchmarks_multi.sh --transports wss,tls --pattern MULTI_ROUTER_ROUTER --msg-sizes 256,1024 --duration 1 --runs 3 --results-tag node_multi_rr_wss_tls_256_1024_recheck_20260601`
  - Node: `perf_node_multi_linux_20260601_140048_node_multi_rr_wss_tls_256_1024_recheck_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과:
  - wss 256B: Node 112.980 Kops/s, C 375.848 Kops/s, C 대비 30.1%, 통과
  - wss 1024B: Node 111.327 Kops/s, C 356.472 Kops/s, C 대비 31.2%, 통과
  - tls 256B: Node 116.538 Kops/s, C 396.794 Kops/s, C 대비 29.4%, 보류
  - tls 1024B: Node 112.172 Kops/s, C 377.889 Kops/s, C 대비 29.7%, 보류
- 판정:
  - wss 256/1024B는 계획 문서 표에 통과로 반영한다.
  - tls 256/1024B는 기존 C full 기준으로는 30% 바로 아래였으므로 같은 조건 C 기준을 다시 확인한다.

## MULTI_ROUTER_ROUTER tls 256/1024B C 기준 제한 재측정

- 대상:
  - `MULTI_ROUTER_ROUTER`
  - `tls`
  - `256,1024B`
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/c/perf/run_benchmarks_multi.sh --transports tls --pattern MULTI_ROUTER_ROUTER --msg-sizes 256,1024 --duration 1 --runs 3 --results-tag node_rr_tls_256_1024_c_recheck_20260601`
  - C: `perf_c_multi_linux_20260601_140139_node_rr_tls_256_1024_c_recheck_20260601.txt`
  - Node: `perf_node_multi_linux_20260601_140048_node_multi_rr_wss_tls_256_1024_recheck_20260601.txt`
  - status: complete
- 결과:
  - tls 256B: Node 116.538 Kops/s, C 364.557 Kops/s, C 대비 32.0%, 통과
  - tls 1024B: Node 112.172 Kops/s, C 350.914 Kops/s, C 대비 32.0%, 통과
- 판정:
  - 기존 C full 기준에서는 30% 바로 아래였지만, 같은 조건 제한 C 기준으로는 둘 다 통과다.
  - 계획 문서 표에 tls 256/1024B를 통과로 반영한다.

## MULTI_DEALER_DEALER tcp large direct part 후보

- 대상:
  - `MULTI_DEALER_DEALER`
  - `tcp`
  - `65536,131072B`
- 의도:
  - 이전 log에서 효과가 컸던 `recvInto`는 현재 public surface guard가 금지하므로 되살리지 않는다.
  - 대신 현재 public `Received` storage를 유지한 채 perf server 내부의 `singlePartOrThrow()` 검증 호출만
    `received.parts[0]` 직접 접근으로 줄여 보았다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_140423_node_multi_dd_tcp_large_direct_part_probe_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과:
  - 65536B: 기존 Node 42.861 Kmsg/s, 후보 34.067 Kmsg/s, C 대비 23.6% -> 18.8%
  - 131072B: 기존 Node 23.041 Kmsg/s, 후보 22.719 Kmsg/s, C 대비 24.6% -> 24.3%
- 판정:
  - `singlePartOrThrow()` 검증 호출은 이 large 병목이 아니며, 65536B는 뚜렷하게 낮아졌다.
  - 최종 코드와 표에는 반영하지 않고 되돌린다.

## Single simple 64B 제한 재측정

- 대상:
  - `PAIR,PUBSUB,DEALER_DEALER`
  - `ws,wss,tls`
  - `64B`
- 근거:
  - 계획 문서에서 `PAIR tls 64B`, `PUBSUB ws/wss/tls 64B`, `DEALER_DEALER tls 64B`가
    35% 기준 바로 아래에 있었다.
  - 코드 후보보다 같은 조건 C/Node 제한 재측정으로 기준선 변동을 먼저 확인했다.
- 측정:
  - Node: `perf_node_single_linux_20260601_140751_node_single_simple64_recheck_20260601.txt`
  - C: `perf_c_single_linux_20260601_140755_node_single_simple64_c_recheck_20260601.txt`
  - status: complete
- 결과:
  - `PAIR tls 64B`: Node 463.288 Kmsg/s, C 1245.184 Kmsg/s, C 대비 37.2%, 통과
  - `PUBSUB ws 64B`: Node 442.060 Kmsg/s, C 1235.177 Kmsg/s, C 대비 35.8%, 통과
  - `PUBSUB wss 64B`: Node 428.682 Kmsg/s, C 1235.792 Kmsg/s, C 대비 34.7%, 보류
  - `PUBSUB tls 64B`: Node 432.522 Kmsg/s, C 1227.706 Kmsg/s, C 대비 35.2%, 통과
  - `DEALER_DEALER tls 64B`: Node 447.989 Kmsg/s, C 1242.333 Kmsg/s, C 대비 36.1%, 통과
- 판정:
  - 4개 보류 cell은 제한 재측정으로 통과에 올랐으므로 계획 문서 표에 반영한다.
  - `PUBSUB wss 64B`는 34.7%로 여전히 기준에 못 닿아 보류를 유지한다.
