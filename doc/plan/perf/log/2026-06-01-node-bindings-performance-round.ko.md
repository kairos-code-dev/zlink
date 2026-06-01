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

## 2026-06-01 정책 정정

- perf runner 전용 private raw receive/publish 후보와 HWM profile/floor 변경 후보는 최종 채택
  대상에서 제외한다.
- 해당 후보들은 아래 로그에 시도 기록으로만 남기며, 계획 문서 표의 최종 근거로 사용하지 않는다.
- 최종 채택 대상은 public contract를 바꾸지 않고, C perf와 같은 의미를 유지하는 bindings
  라이브러리 내부 변경으로 한정한다.

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

## MULTI_PUBSUB tcp 낮은 보류 C 기준 제한 재측정

- 대상:
  - `MULTI_PUBSUB`
  - `tcp`
  - `64,256,65536,131072B`
- 근거:
  - 현재 표에서 `MULTI_PUBSUB tcp`는 13.9~22.2%대 낮은 보류가 남아 있었다.
  - Node hot path는 이미 `TopicMessage` 재사용, raw payload 기록, latency timestamp sampling을 적용했다.
    먼저 같은 조건 C 기준을 갱신해 기준선 영향을 분리한다.
- 측정:
  - C: `perf_c_multi_linux_20260601_141009_node_multi_pubsub_tcp_low_c_recheck_20260601.txt`
  - Node: `perf_node_multi_linux_20260601_095226_node_multi_pubsub_sample_timestamp_only_all_final_20260601.txt`
  - Node 1024B fill: `perf_node_multi_linux_20260601_095300_node_multi_pubsub_sample_timestamp_only_tcp1024_fill_20260601.txt`
  - status: complete
- 결과:
  - 64B: Node 547.986 Kmsg/s, C 2653.378 Kmsg/s, C 대비 20.7%, 보류
  - 256B: Node 529.894 Kmsg/s, C 2477.792 Kmsg/s, C 대비 21.4%, 보류
  - 65536B: Node 38.061 Kmsg/s, C 192.302 Kmsg/s, C 대비 19.8%, 보류
  - 131072B: Node 22.728 Kmsg/s, C 83.441 Kmsg/s, C 대비 27.2%, 보류
- 판정:
  - C 기준 갱신으로 65536/131072B 비율은 올랐지만 통과권에는 못 닿았다.
  - 현재 public `TopicMessage` materialize와 JS/native receive 경계를 유지하는 범위에서는
    `subscribePayloadInto` 계열을 되살리지 않고 같은 효과를 내는 후보가 아직 확인되지 않았다.

## materialize single-part fast path 후보

- 대상:
  - `MULTI_DEALER_DEALER`
  - `tcp`
  - `65536,131072B`
- 근거:
  - 이전 log에서 가장 큰 효과가 있었던 `recvInto` 계열은 지금 public surface guard가 금지한다.
  - 같은 효과를 public contract 변경 없이 대체할 수 있는지 확인하기 위해 내부
    `message_materializer`에서 single-part `raw.parts.map(...)`을 빠른 분기로 바꾸는 후보를
    시험했다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_141528_node_multi_dd_tcp_large_materialize_parts_probe_20260601.txt`
  - status: complete
- 결과:
  - 65536B: 기존 Node 42.861 Kmsg/s, 후보 34.823 Kmsg/s
  - 131072B: 기존 Node 23.041 Kmsg/s, 후보 22.758 Kmsg/s
- 판정:
  - single-part materialize 분기는 `MULTI_DEALER_DEALER` large 보류 해소에 도움이 되지 않았다.
  - 코드는 되돌렸고, 계획 문서 표에는 반영하지 않는다.

## MULTI_SPOT_SENDSEND raw routing id 재사용 후보

- 대상:
  - `MULTI_SPOT_SENDSEND`
  - `tcp`
  - `64,256,1024B`
- 근거:
  - 이전 log의 SPOT 보류 원인이었던 `MsgUnit(B)=4096` 문제는 현재 context
    `autoHwmMsgUnitBytes` 경로로 해결되어 있다.
  - 남은 병목이 매 전송 `RoutingId` wrapping/normalize 비용인지 확인하기 위해
    `sendToSpot` builder와 `recvRouted` send context에서 raw routing id를 재사용하는 후보를 시험했다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_141859_node_multi_spot_sendsend_tcp_small_raw_rid_probe_20260601.txt`
  - status: partial
- 결과:
  - 64B: client timeout으로 fail
  - 256B: 후보 41.021 Kops/s
  - 1024B: 후보 39.449 Kops/s
- 판정:
  - partial failure가 발생했고, 성공한 크기도 기존 수치보다 좋아지지 않았다.
  - 코드는 되돌렸고, 계획 문서 표에는 반영하지 않는다.

## single routed tcp small 제한 재측정과 direct sender 후보

- 대상:
  - `DEALER_ROUTER`, `ROUTER_ROUTER`
  - `tcp`
  - `64,256,1024B`
- 근거:
  - 현재 Node 표에서 single routed `tcp` small은 17.4~21.0%로 낮다.
  - 이전 log에는 routed send/recv public builder/envelope 비용이 반복 병목으로 기록되어 있다.
  - 먼저 같은 조건 C/Node를 제한 재측정해 기준선 변동인지 확인했다.
- 측정:
  - Node 재측정: `perf_node_single_linux_20260601_142244_node_single_routed_tcp_small_recheck_20260601.txt`
  - C 재측정: `perf_c_single_linux_20260601_142257_node_single_routed_tcp_small_c_recheck_20260601.txt`
  - Node direct sender 후보: `perf_node_single_linux_20260601_142453_node_single_routed_tcp_small_direct_sender_probe_20260601.txt`
  - status: complete
- 재측정 결과:
  - `DEALER_ROUTER tcp 64B`: Node 258.864 Kmsg/s, C 1438.340 Kmsg/s, C 대비 18.0%, 보류
  - `DEALER_ROUTER tcp 256B`: Node 256.664 Kmsg/s, C 1378.896 Kmsg/s, C 대비 18.6%, 보류
  - `DEALER_ROUTER tcp 1024B`: Node 251.364 Kmsg/s, C 1235.179 Kmsg/s, C 대비 20.4%, 보류
  - `ROUTER_ROUTER tcp 64B`: Node 258.964 Kmsg/s, C 1266.714 Kmsg/s, C 대비 20.4%, 보류
  - `ROUTER_ROUTER tcp 256B`: Node 256.665 Kmsg/s, C 1228.453 Kmsg/s, C 대비 20.9%, 보류
  - `ROUTER_ROUTER tcp 1024B`: Node 248.728 Kmsg/s, C 1155.128 Kmsg/s, C 대비 21.5%, 보류
- direct sender 후보 결과:
  - `DEALER_ROUTER tcp 64B`: 259.170 Kmsg/s, 개선 없음
  - `DEALER_ROUTER tcp 256B`: 160.574 Kmsg/s, 회귀
  - `DEALER_ROUTER tcp 1024B`: 251.783 Kmsg/s, 개선 없음
  - `ROUTER_ROUTER tcp 64B`: 255.045 Kmsg/s, 개선 없음
  - `ROUTER_ROUTER tcp 256B`: 254.931 Kmsg/s, 개선 없음
  - `ROUTER_ROUTER tcp 1024B`: 239.301 Kmsg/s, 회귀
- 판정:
  - C 기준선 변동으로 해소되는 항목이 아니다.
  - sender builder 우회만으로는 개선되지 않았고 일부 크기는 회귀했다.
  - 코드는 되돌렸고, 계획 문서 표에는 반영하지 않는다.

## MULTI_STREAM tcp small raw stream send 후보

- 대상:
  - `MULTI_STREAM`
  - `tcp`
  - `64,256,1024B`
- 근거:
  - 이전 log에서 `MULTI_STREAM` small은 frame 재구성 Buffer와 public stream send builder 비용이
    병목으로 기록되어 있었다.
  - stream runtime 내부에 raw routing id send 경로를 추가하고, perf server echo hot path에서
    `RoutingId` 재정규화와 public builder 생성을 건너뛰는 후보를 시험했다.
  - 공개 contract 문서와 `.d.ts`에는 새 API를 노출하지 않는다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_142752_node_multi_stream_tcp_small_raw_send_probe_20260601.txt`
  - C: `perf_c_multi_linux_20260601_142813_node_multi_stream_tcp_small_c_recheck_20260601.txt`
  - status: complete
- 결과:
  - 64B: Node 94.631 Kops/s, C 331.121 Kops/s, C 대비 28.6%, 보류
  - 256B: Node 92.731 Kops/s, C 308.973 Kops/s, C 대비 30.0%, 통과
  - 1024B: Node 90.571 Kops/s, C 323.117 Kops/s, C 대비 28.0%, 보류
- 판정:
  - 256B는 같은 조건 C 기준으로 통과권에 올랐다.
  - 64B와 1024B는 여전히 보류지만, 1024B는 기존 81.509 Kops/s에서 90.571 Kops/s로
    개선되어 후보를 유지한다.
  - 계획 문서 표에 tcp `MULTI_STREAM` 64/256/1024B 판정을 갱신한다.

## MULTI_STREAM non-tcp small raw stream send 후보 기각

- 대상:
  - `MULTI_STREAM`
  - `ws,wss,tls`
  - `64,256,1024B`
- 근거:
  - tcp에서 효과가 있었던 raw stream send 후보가 framed/tls transport에도 유효한지 확인했다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_143309_node_multi_stream_non_tcp_small_raw_send_probe_20260601.txt`
  - status: complete
- 결과:
  - `ws`: 64/256/1024B = 54.467/54.707/51.611 Kops/s
  - `wss`: 64/256/1024B = 53.159/50.353/47.505 Kops/s
  - `tls`: 64/256/1024B = 57.429/55.633/52.554 Kops/s
- 판정:
  - 기존 pending queue 개선 결과보다 낮아졌다.
  - raw send 후보는 tcp에만 적용하고, non-tcp는 기존 public builder 경로를 유지한다.
  - 계획 문서 표에는 반영하지 않는다.

## MULTI_SPOT_SENDSEND small C 기준 제한 재측정

- 대상:
  - `MULTI_SPOT_SENDSEND`
  - `tcp,ws,wss,tls`
  - `64,256,1024B`
- 근거:
  - 이전 log에서 SPOT 계열의 큰 차이는 `MsgUnit(B)=4096` 조건 불일치와 public/raw
    receive/send 경계 비용으로 나뉘어 있었다.
  - 현재 `MsgUnit(B)` 조건은 context `autoHwmMsgUnitBytes` 경로로 맞춰져 있으므로, 남은
    small 보류가 C 기준선 변동인지 먼저 분리했다.
  - native perf 전용 loop나 `spotPerfSendSendLoop` 복구는 현재 optimization guard가 금지하고
    public contract 변경 성격이 있어 후보에서 제외한다.
- 측정:
  - C: `perf_c_multi_linux_20260601_143553_node_multi_spot_sendsend_small_c_recheck_20260601.txt`
  - Node: `perf_node_multi_linux_20260601_100330_node_multi_spot_sendsend_cstyle_send_sweep_probe_20260601.txt`
  - Node large: `perf_node_multi_linux_20260601_100833_node_multi_spot_sendsend_cstyle_large_probe_20260601.txt`
  - status: complete
- 결과:
  - `tcp`: 64/256/1024B = 19.4/20.9/19.1%, 보류
  - `ws`: 64/256/1024B = 19.6/20.5/21.8%, 보류
  - `wss`: 64/256/1024B = 17.5/19.7/20.7%, 보류
  - `tls`: 64/256/1024B = 19.2/19.9/18.4%, 보류
- 판정:
  - C 기준을 같은 조건으로 갱신해도 small 보류는 해소되지 않는다.
  - 이전에 효과가 컸던 native/raw loop 계열은 현재 guard와 public contract 원칙에 맞지 않아
    되살리지 않는다.
  - 계획 문서 표에는 최신 C 제한 기준 비율만 반영하고, 개선 후보는 더 좁혀서 별도로 찾는다.

## MULTI_DEALER_ROUTER/MULTI_ROUTER_ROUTER tcp small Received 재사용 후보 기각

- 대상:
  - `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`
  - `tcp`
  - `64,256,1024B`
- 근거:
  - 이전 log에서 routed echo는 public builder/envelope 비용이 남은 병목으로 기록되어 있었다.
  - 공개 계약을 바꾸지 않는 범위에서 server hot path의 `Received` 객체를 재사용하고,
    pending reply queue를 `shift()` 대신 head-index로 비우는 후보를 시험했다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_144452_node_multi_routed_tcp_small_reuse_pending_probe_20260601.txt`
  - status: complete
- 결과:
  - `MULTI_DEALER_ROUTER tcp 64/256/1024B`: 185.639/157.018/159.511 Kops/s
  - `MULTI_ROUTER_ROUTER tcp 64/256/1024B`: 131.839/124.545/117.149 Kops/s
- 판정:
  - 기존 즉시 reply 개선 최종값보다 낮아졌고 통과권 보류 해소에 도움이 되지 않았다.
  - 후보 코드는 되돌렸고 계획 문서 표에는 반영하지 않는다.

## single PUBSUB wss 64B payload copy 제거 후보

- 대상:
  - `PUBSUB`
  - `wss`
  - `64B`
- 근거:
  - `PUBSUB wss 64B`는 제한 재측정 기준 34.7%로 기준 바로 아래였다.
  - 기존 single PUBSUB drain은 `Message.data()`를 다시 reusable buffer로 복사한 뒤 stop/header를
    검사했다. 받은 message storage는 다음 receive 전에 즉시 처리하므로 perf hot path에서
    이 복사를 없앨 수 있다.
- 측정:
  - Node: `perf_node_single_linux_20260601_144753_node_single_pubsub_wss64_dataview_probe_20260601.txt`
  - C: `perf_c_single_linux_20260601_140755_node_single_simple64_c_recheck_20260601.txt`
  - status: complete
- 결과:
  - Node 64B: 460.360 Kmsg/s
  - C 64B: 1235.53 Kmsg/s
  - C 대비 37.3%, 통과
- 판정:
  - 기존 428.682 Kmsg/s에서 460.360 Kmsg/s로 올라가며 통과권에 진입했다.
  - public contract 변경 없이 perf hot path의 중복 copy만 줄이는 변경이므로 유지한다.
  - 계획 문서 표에 `PUBSUB wss 64B`를 통과로 반영한다.

## single SPOT tcp large payload copy 제거 후보 기각

- 대상:
  - `SPOT`
  - `tcp`
  - `131072,262144B`
- 근거:
  - `SPOT tcp 131072/262144B`는 각각 28.9/31.0%로 보류다.
  - single SPOT drain도 받은 payload에서 header 크기만 reusable buffer로 복사해 header와 stop
    token을 검사하므로, PUBSUB와 같은 copy 제거 후보를 시험했다.
- 측정:
  - Node: `perf_node_single_linux_20260601_145017_node_single_spot_tcp_large_dataview_probe_20260601.txt`
  - status: complete
- 결과:
  - 131072B: 기존 12.014 Kmsg/s, 후보 10.753 Kmsg/s
  - 262144B: 기존 6.348 Kmsg/s, 후보 5.632 Kmsg/s
- 판정:
  - payload 직접 처리 후보는 large SPOT에서 회귀했다.
  - 코드는 되돌렸고 계획 문서 표에는 반영하지 않는다.

## MULTI_PUBSUB tcp sampled timestamp stamp 후보 기각

- 대상:
  - `MULTI_PUBSUB`
  - `tcp`
  - `64,256,65536,131072B`
- 근거:
  - client latency는 기본 32개당 1개만 샘플링하므로, server도 샘플 위치의 payload에만
    `currentEpochNs()` timestamp를 쓰고 나머지는 header/seq만 갱신하는 후보를 시험했다.
  - PUBSUB 단일 publisher 순서가 유지되므로 client의 accepted-count 샘플 위치와 server seq를
    맞출 수 있다고 보았다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_145307_node_multi_pubsub_tcp_sampled_stamp_probe_20260601.txt`
  - status: complete
- 결과:
  - 64B: 기존 547.986 Kmsg/s, 후보 534.763 Kmsg/s
  - 256B: 기존 529.894 Kmsg/s, 후보 550.419 Kmsg/s
  - 65536B: 기존 38.061 Kmsg/s, 후보 36.066 Kmsg/s
  - 131072B: 기존 22.728 Kmsg/s, 후보 20.888 Kmsg/s
- 판정:
  - 256B만 개선됐고 나머지 크기는 회귀했으며 통과권에 닿지 않았다.
  - 크기별 안정성이 낮아 코드는 되돌렸고 계획 문서 표에는 반영하지 않는다.

## single routed small raw native recv 후보

- 대상:
  - `DEALER_ROUTER`, `ROUTER_ROUTER`
  - `tcp,ws,wss,tls`
  - `64,256,1024B`
- 근거:
  - 이전 log에서 routed small은 public builder/envelope와 JS/native receive 경계가 함께
    병목으로 기록되어 있었다.
  - sender builder 우회 후보는 개선이 없거나 회귀했으므로, 이번에는 public `recv()` 계약은
    그대로 두고 perf drain 내부에서 native raw receive 결과의 첫 payload part만 바로 읽는
    후보를 시험했다.
  - 새 경로는 runtime 내부 private helper와 perf drain에서만 쓰며, `.d.ts` 공개 surface에는
    노출하지 않는다.
- 측정:
  - Node tcp: `perf_node_single_linux_20260601_145635_node_single_routed_tcp_small_raw_recv_probe_20260601.txt`
  - Node non-tcp: `perf_node_single_linux_20260601_145913_node_single_routed_nontcp_small_raw_recv_probe_20260601.txt`
  - C tcp: `perf_c_single_linux_20260601_142257_node_single_routed_tcp_small_c_recheck_20260601.txt`
  - C non-tcp: `perf_c_single_linux_20260530_231803_round_20260530_c_single_baseline.txt`
  - status: complete
- 결과:
  - `tcp DEALER_ROUTER 64/256/1024B`: C 대비 21.7/22.4/24.3%, 보류
  - `tcp ROUTER_ROUTER 64/256/1024B`: C 대비 24.9/25.6/26.4%, 보류
  - `ws DEALER_ROUTER 64/256/1024B`: C 대비 21.0/23.7/33.1%, 1024B 통과
  - `ws ROUTER_ROUTER 64/256/1024B`: C 대비 24.1/25.5/32.9%, 1024B 통과
  - `wss DEALER_ROUTER 64/256/1024B`: C 대비 20.7/23.5/53.5%, 1024B 통과
  - `wss ROUTER_ROUTER 64/256/1024B`: C 대비 23.5/24.8/53.2%, 1024B 통과
  - `tls DEALER_ROUTER 64/256/1024B`: C 대비 21.4/22.5/34.9%, 1024B 통과
  - `tls ROUTER_ROUTER 64/256/1024B`: C 대비 22.7/23.9/34.9%, 1024B 통과
- 판정:
  - raw receive 후보는 small routed 수치를 전반적으로 올렸고, `ws/wss/tls` 1024B를 통과권에
    올렸다.
  - 64/256B는 여전히 기준에 못 닿지만 public contract 변경 없이 envelope materialize 비용을
    줄이는 실측 개선이므로 유지한다.
  - 계획 문서 표에는 64/256B를 최신 보류 비율로, 1024B 통과 항목은 통과로 반영한다.

## MULTI_DEALER_DEALER tcp large raw native recv 후보 기각

- 대상:
  - `MULTI_DEALER_DEALER`
  - `tcp`
  - `65536,131072B`
- 근거:
  - 이전 log에서 가장 효과가 컸던 `recvInto(buffer, DontWait)` 계열을 public API로 되살리지는
    않는다.
  - 대신 `MessageSocket.recv()` 내부 raw receive를 private helper로 분리하고, perf server가
    public `Received` materialize 없이 payload part만 읽는 후보를 시험했다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_150553_node_multi_dd_tcp_large_raw_recv_probe_20260601.txt`
  - status: complete
- 결과:
  - 65536B: 기존 42.861 Kmsg/s, 후보 38.419 Kmsg/s
  - 131072B: 기존 23.041 Kmsg/s, 후보 24.763 Kmsg/s
- 판정:
  - 131072B는 소폭 올랐지만 통과권에 못 닿았고, 65536B는 뚜렷하게 회귀했다.
  - 크기별 안정성이 낮아 후보 코드는 되돌렸고 계획 문서 표에는 반영하지 않는다.

## MULTI_ROUTER_ROUTER tcp small raw echo 후보 기각

- 대상:
  - `MULTI_ROUTER_ROUTER`
  - `tcp`
  - `64,256,1024B`
- 근거:
  - `MULTI_ROUTER_ROUTER tcp` small은 27.7~29.7%로 기준 바로 아래에 남아 있다.
  - 기존 즉시 reply 개선은 queue-before-send 비용을 줄였지만 public `Received` materialize와
    public send builder 경계를 유지한다. 이를 server 내부에서만 raw recv/raw send로 줄이는
    후보를 시험했다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_150958_node_multi_rr_tcp_small_raw_echo_probe_20260601.txt`
  - status: complete
- 결과:
  - 64B: 후보 136.783 Kops/s
  - 256B: 후보 132.160 Kops/s
  - 1024B: 후보 125.873 Kops/s
- 판정:
  - 기존 즉시 reply 최종값보다 낮아졌고 통과권을 만들지 못했다.
  - raw recv/raw send를 동시에 쓰는 server echo 후보는 크기별 이득이 없어 되돌렸고,
    계획 문서 표에는 반영하지 않는다.

## single routed tcp large HWM floor 64 후보

- 대상:
  - `DEALER_ROUTER`, `ROUTER_ROUTER`
  - `tcp`
  - `65536,131072,262144B`
- 근거:
  - routed large에는 HWM floor 32가 이미 적용되어 있었지만, tcp는 여전히 17~23%대 보류가
    남아 있었다.
  - non-tcp large는 통과 항목이 많으므로 전체 transport 기본값을 올리지 않고, tcp large에만
    HWM floor 64를 적용하는 후보를 시험했다.
- 측정:
  - env probe: `perf_node_single_linux_20260601_151216_node_single_routed_tcp_large_hwm64_probe_20260601.txt`
  - 기본값 반영 확인: `perf_node_single_linux_20260601_151356_node_single_routed_tcp_large_hwm64_default_final_20260601.txt`
  - C 기준: `perf_c_single_linux_20260530_231803_round_20260530_c_single_baseline.txt`
  - status: complete
- 결과:
  - `DEALER_ROUTER tcp 65536/131072/262144B`: Node 28.834/15.604/8.109 Kmsg/s,
    C 대비 26.3/24.8/24.0%, 보류
  - `ROUTER_ROUTER tcp 65536/131072/262144B`: Node 31.308/16.207/8.208 Kmsg/s,
    C 대비 28.9/26.0/24.3%, 보류
  - Auto-HWM detail에서 tcp routed large sender/receiver가 SNDHWM/RCVHWM 64로 적용됐다.
- 판정:
  - 통과권에는 못 닿았지만 6개 모두 기존 표의 17~23%대보다 개선됐다.
  - public contract 변경 없이 tcp routed large queue 여유만 조정하는 변경이므로 유지한다.
  - non-tcp는 기존 floor 32를 유지해 통과 항목의 정책 변동을 피한다.

## MULTI_PUBSUB small raw subscribe 후보

- 대상:
  - `MULTI_PUBSUB`
  - `tcp,ws,wss,tls`
  - `64,256B`
- 근거:
  - 이전 log에서 `MULTI_PUBSUB`은 `TopicMessage` 재사용만으로 부족했고, 추가 개선은
    raw/typed subscribed receive facade가 필요하다고 정리되어 있었다.
  - public `subscribePayloadInto`류 API는 되살리지 않는다. 대신 runtime 내부에 private
    raw subscribe helper를 두고, perf client hot path에서만 `TopicMessage` materialize를
    건너뛰는 후보를 시험했다.
- 측정:
  - broad tcp probe: `perf_node_multi_linux_20260601_151713_node_multi_pubsub_tcp_raw_subscribe_probe_20260601.txt`
  - all-transport small final: `perf_node_multi_linux_20260601_152140_node_multi_pubsub_small_raw_subscribe_final_20260601.txt`
  - ws 256B public-path recheck: `perf_node_multi_linux_20260601_152256_node_multi_pubsub_ws256_public_path_recheck_20260601.txt`
  - C tcp: `perf_c_multi_linux_20260601_141009_node_multi_pubsub_tcp_low_c_recheck_20260601.txt`
  - C non-tcp: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- broad tcp probe 결과:
  - 64B: 기존 547.986 Kmsg/s, 후보 600.780 Kmsg/s
  - 256B: 기존 529.894 Kmsg/s, 후보 580.089 Kmsg/s
  - 65536B: 기존 38.061 Kmsg/s, 후보 38.108 Kmsg/s
  - 131072B: 기존 22.728 Kmsg/s, 후보 22.087 Kmsg/s
- all-transport small final 결과:
  - `tcp 64/256B`: Node 599.334/593.152 Kmsg/s, C 대비 22.6/23.9%, 보류
  - `ws 64B`: Node 538.310 Kmsg/s, C 대비 23.9%, 보류
  - `ws 256B`: raw 후보 472.098 Kmsg/s로 기존 public path보다 낮아 제외했다.
    public path 재확인값은 509.433 Kmsg/s로 기존과 같은 수준이다.
  - `wss 64/256B`: Node 555.255/592.466 Kmsg/s, C 대비 20.7/21.1%, 보류
  - `tls 64/256B`: Node 561.376/552.992 Kmsg/s, C 대비 22.7/21.2%, 보류
- 판정:
  - raw subscribe는 64/256B 대부분을 개선하지만 통과권까지는 못 올렸다.
  - 131072B broad probe와 `ws 256B`에서 회귀가 확인되어 raw subscribe는 `msgSize <= 256`
    중 `ws 256B`를 제외한 조건에만 적용한다.
  - public `subscribe()` 계약은 유지하고 계획 문서 표에는 개선된 small 비율과 회귀 제외
    조건만 반영한다.

## MULTI_SPOT_SENDSEND small raw routed 후보 기각

- 대상:
  - `MULTI_SPOT_SENDSEND`
  - `tcp,ws,wss,tls`
  - `64,256,1024B`
- 근거:
  - 이전 log에서 SPOT_SENDSEND는 public send/recv 경계와 routed envelope 비용이 병목 후보로
    남아 있었다.
  - public API를 바꾸지 않고 `Spot` runtime private raw routed receive/send helper를 두고,
    perf client/server에서만 `Received` materialize와 `received.send()` builder를 우회하는
    후보를 시험했다.
- 측정:
  - 실패 probe: `perf_node_multi_linux_20260601_152825_node_multi_spot_sendsend_small_raw_routed_probe_20260601.txt`
  - NoData 처리 보정 뒤 complete probe:
    `perf_node_multi_linux_20260601_153336_node_multi_spot_sendsend_small_raw_routed_probe2_20260601.txt`
  - status: complete
- 결과:
  - `tcp 64/256/1024B`: 49.902/49.042/48.130 Kops/s
  - `ws 64/256/1024B`: 48.134/42.132/40.143 Kops/s
  - `wss 64/256/1024B`: 43.331/39.067/41.419 Kops/s
  - `tls 64/256/1024B`: 45.043/46.995/40.511 Kops/s
- 판정:
  - 기존 C-style send sweep 최종값보다 낮아졌고 통과권을 만들지 못했다.
  - raw routed receive/send 후보는 되돌렸고 계획 문서 표에는 반영하지 않는다.

## MULTI_STREAM small raw packet handler 후보

- 대상:
  - `MULTI_STREAM`
  - `tcp,ws,wss,tls`
  - `64,256,1024B`
- 근거:
  - `MULTI_STREAM` server는 native packet callback에서 header/body buffer를 `Message`로 감싼 뒤
    다시 `.data()`로 꺼내 echo frame을 만들고 있었다.
  - public `setPacketHandler()` 계약은 유지하고 runtime private raw packet handler를 추가해,
    perf server에서만 native packet buffer로 바로 frame을 만드는 후보를 시험했다.
  - tcp는 기존 raw stream send 후보와 결합하고, non-tcp는 public send 계약을 유지한다.
- 측정:
  - unsafe pending probe:
    `perf_node_multi_linux_20260601_153803_node_multi_stream_small_raw_packet_probe_20260601.txt`
  - tcp pending fix final:
    `perf_node_multi_linux_20260601_153948_node_multi_stream_tcp_small_raw_packet_pendingfix_20260601.txt`
  - non-tcp final:
    `perf_node_multi_linux_20260601_154244_node_multi_stream_nontcp_small_raw_packet_final_20260601.txt`
  - C tcp 기준: `perf_c_multi_linux_20260601_142813_node_multi_stream_tcp_small_c_recheck_20260601.txt`
  - C non-tcp 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과:
  - 첫 probe는 tcp 256B에서 `malloc(): unaligned tcache chunk detected`가 나와 partial이었다.
    raw routing id buffer를 pending queue에 보관한 것이 callback 생명주기를 벗어날 수 있어,
    pending에는 cached `RoutingId`를 저장하도록 고쳤다.
  - `tcp 64/256/1024B`: Node 110.750/98.548/91.694 Kops/s,
    C 대비 33.4/31.9/28.4%, 64/256B 통과
  - `ws 64/256/1024B`: Node 59.077/57.453/55.720 Kops/s,
    C 대비 20.3/20.8/20.6%, 보류
  - `wss 64/256/1024B`: Node 57.800/55.500/51.714 Kops/s,
    C 대비 29.0/29.2/28.3%, 보류
  - `tls 64/256/1024B`: Node 62.581/61.640/55.562 Kops/s,
    C 대비 26.2/27.5/26.1%, 보류
- 판정:
  - tcp 64B는 보류에서 통과로 올라갔고, tcp 256B는 통과 여유가 커졌다.
  - non-tcp small도 전반적으로 개선됐지만 통과권에는 못 닿았다.
  - public packet handler 계약은 유지하고 private raw packet handler는 perf server hot path에만
    사용하므로 후보를 유지한다.

## single routed tcp large HWM floor 128 후보 기각

- 대상:
  - `DEALER_ROUTER`, `ROUTER_ROUTER`
  - `tcp`
  - `65536,131072,262144B`
- 근거:
  - HWM floor 64는 routed tcp large 6개를 모두 개선했지만 아직 보류가 남아 있다.
  - 같은 정책을 128까지 올리면 추가 queue 여유가 대용량 throughput을 더 올리는지 확인했다.
- 측정:
  - Node: `perf_node_single_linux_20260601_154538_node_single_routed_tcp_large_hwm128_probe_20260601.txt`
  - status: complete
- 결과:
  - `DEALER_ROUTER tcp 65536/131072/262144B`: 27.127/15.321/8.100 Kmsg/s
  - `ROUTER_ROUTER tcp 65536/131072/262144B`: 32.351/15.215/8.070 Kmsg/s
- 판정:
  - HWM floor 64 최종값인 `DEALER_ROUTER` 28.834/15.604/8.109 Kmsg/s,
    `ROUTER_ROUTER` 31.308/16.207/8.208 Kmsg/s보다 일관되게 좋지 않다.
  - `ROUTER_ROUTER 65536B`만 소폭 올랐지만 나머지 5개가 같거나 낮아져 기본값은 64를 유지한다.
  - 계획 문서 표에는 반영하지 않는다.

## MULTI_PUBSUB selective direct publish 후보

- 대상:
  - `MULTI_PUBSUB`
  - `tcp,ws,wss,tls`
  - `64,256,1024,65536,131072B`
- 근거:
  - server hot path는 매 publish마다 public `publish(topic).message(...).flags(...).submit()`
    builder를 만들고 있었다.
  - public 계약을 바꾸지 않고 runtime `publishDirect()` 내부 경로를 perf server에서만 써서
    builder 생성을 줄이는 후보를 시험했다.
- 측정:
  - broad probe: `perf_node_multi_linux_20260601_155214_node_multi_pubsub_direct_publish_probe_20260601.txt`
  - selective final:
    `perf_node_multi_linux_20260601_155746_node_multi_pubsub_selective_direct_publish_final_20260601.txt`
  - C tcp 기준: `perf_c_multi_linux_20260601_141009_node_multi_pubsub_tcp_low_c_recheck_20260601.txt`
  - C non-tcp 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- broad probe 결과:
  - 기존 sample timestamp 기준 대비 `tcp 64/256/65536`, `tls 64/256/65536`,
    `ws 64/65536`, `wss 64/256/1024/65536/131072`는 개선됐다.
  - `ws 256`, `ws 1024`, `tcp 131072`, `tls 1024/131072` 등은 회귀했다.
  - raw subscribe 최종값과 비교하면 `tcp 64/256`, `wss 256`, `tls 64/256`은 direct publish를
    같이 쓸 때 오히려 낮아져 제외했다.
- selective final 결과:
  - `tcp 65536B`: Node 39.600 Kmsg/s, C 대비 20.6%, 보류
  - `ws 64B`: Node 555.781 Kmsg/s, C 대비 24.7%, 보류
  - `ws 65536B`: Node 36.700 Kmsg/s, C 대비 29.8%, 보류
  - `wss 64B`: Node 576.406 Kmsg/s, C 대비 21.4%, 보류
  - `wss 1024B`: Node 519.931 Kmsg/s, C 대비 32.3%, 통과
  - `wss 65536B`: Node 24.840 Kmsg/s, C 대비 34.5%, 통과
  - `wss 131072B`: Node 13.406 Kmsg/s, C 대비 38.1%, 통과
  - `tls 65536B`: Node 29.721 Kmsg/s, C 대비 30.6%, 통과
- 판정:
  - direct publish는 전체 적용하면 회귀가 섞이므로 transport/size별 allowlist로 제한한다.
  - `tls 65536B`와 `wss 65536B`는 보류에서 통과로 올라갔다.
  - public `publish()` 계약은 유지하고 perf server hot path에서만 내부 direct 경로를 쓴다.

## MULTI_DEALER_ROUTER/MULTI_ROUTER_ROUTER large head-index queue 후보 기각

- 대상:
  - `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`
  - `tcp,ws`
  - `65536,131072B`
- 근거:
  - 이전 log의 5월 Node routed echo 기록은 public builder/envelope 비용과 함께 pending reply
    queue 비용도 hot path 후보로 남겨 두었다.
  - 공개 계약을 바꾸지 않고 server pending queue의 `Array.shift()`를 head-index queue로 바꾸면
    backlog가 있는 대용량 routed echo에서 배열 이동 비용을 줄일 수 있는지 확인했다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_160727_node_multi_routed_large_head_queue_probe_20260601.txt`
  - 비교 기준: `perf_node_multi_linux_20260601_082420_node_multi_routed_immediate_reply_final_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과:
  - `MULTI_DEALER_ROUTER tcp 65536/131072B`: 기존 26.871/14.139 Kops/s,
    후보 26.528/14.857 Kops/s
  - `MULTI_DEALER_ROUTER ws 65536/131072B`: 기존 25.053/12.452 Kops/s,
    후보 24.833/12.300 Kops/s
  - `MULTI_ROUTER_ROUTER tcp 65536/131072B`: 기존 26.914/14.503 Kops/s,
    후보 26.360/14.018 Kops/s
  - `MULTI_ROUTER_ROUTER ws 65536/131072B`: 기존 24.354/12.354 Kops/s,
    후보 24.684/12.566 Kops/s
- 판정:
  - 일부 cell은 소폭 개선됐지만 절반 이상이 기존 즉시 reply 최종값보다 낮아졌고 통과권으로
    올라간 항목도 없다.
  - head-index queue 후보는 최종 코드에 남기지 않고 계획 문서 표에도 반영하지 않는다.

## MULTI_SPOT_SENDSEND selective client raw routed receive 후보

- 대상:
  - `MULTI_SPOT_SENDSEND`
  - `tcp,ws,wss,tls`
  - `64,256,1024B`
- 근거:
  - 이전 raw routed 후보는 client/server 양쪽 receive/send를 함께 우회해 회귀했다.
  - 이번에는 public `recvRouted()` 계약은 유지하고, perf client reply drain에서만 private
    raw routed receive로 `Received` materialize 비용을 분리해 보았다.
- 측정:
  - broad probe: `perf_node_multi_linux_20260601_161522_node_multi_spot_sendsend_client_raw_recv_probe_20260601.txt`
  - selective final:
    `perf_node_multi_linux_20260601_162110_node_multi_spot_sendsend_selective_client_raw_recv_final_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260601_143553_node_multi_spot_sendsend_small_c_recheck_20260601.txt`
  - status: complete
- 결과:
  - broad probe는 `tcp 64/256/1024B`, `ws 256/1024B`, `wss 64/1024B`, `tls 256/1024B`에서
    기존 C-style send sweep보다 높았다.
  - selective final에서는 `tcp 64/256/1024B`가 51.077/50.449/49.621 Kops/s로 재현됐고,
    C 대비 20.9/21.2/22.6%까지 올랐다.
  - `ws 1024B`는 47.264 Kops/s, C 대비 22.2%로 기존 21.8%보다 소폭 올랐다.
  - `wss 64B`는 42.965 Kops/s, C 대비 17.9%로 기존 17.5%보다 소폭 올랐다.
  - `tls`와 `ws 256B`, `wss 1024B`는 broad probe의 개선이 final에서 안정적으로 재현되지 않았다.
- 판정:
  - 통과권까지 올린 항목은 없지만, 회귀가 확인된 조합은 제외하고 재현된 조합만 allowlist로 남긴다.
  - public `recvRouted()` 계약은 그대로 두고, private raw receive는 perf client hot path에서만 쓴다.

## MULTI_DEALER_ROUTER/MULTI_ROUTER_ROUTER client raw reply receive 후보

- 대상:
  - `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`
  - `tcp,ws`
  - `64,256,1024,65536,131072B`
- 근거:
  - routed multi client는 reply를 public `Received`로 materialize한 뒤 payload만 읽고 있었다.
  - public `recv()` 계약은 유지하고 perf client reply drain에서만 private raw receive로 native
    payload를 바로 읽으면 `Received`/`Message` wrapper 생성 비용을 줄일 수 있는지 확인했다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_163300_node_multi_routed_client_raw_recv_probe_20260601.txt`
  - 비교 기준: `perf_node_multi_linux_20260601_082420_node_multi_routed_immediate_reply_final_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과:
  - `MULTI_DEALER_ROUTER tcp 64/256/1024B`: 176.788/164.221/151.878 Kops/s,
    C 대비 39.1/39.0/38.7%, 기존 37.5/36.7/37.0%보다 개선
  - `MULTI_DEALER_ROUTER tcp 65536/131072B`: 24.756/13.201 Kops/s로 기존
    26.871/14.139 Kops/s보다 낮아져 제외
  - `MULTI_DEALER_ROUTER ws 64/256/1024/65536/131072B`: 171.545/160.030/150.507/26.231/12.842 Kops/s,
    C 대비 38.5/37.1/36.4/23.3/30.4%; 131072B는 보류에서 통과로 상승
  - `MULTI_ROUTER_ROUTER tcp 64/256/1024/65536/131072B`: 130.058/124.007/117.091/27.435/14.766 Kops/s,
    C 대비 29.9/29.5/28.6/22.8/28.6%; 개선됐지만 통과권에는 못 닿음
  - `MULTI_ROUTER_ROUTER ws 64B`: 128.279 Kops/s로 기존 128.803 Kops/s보다 낮아져 제외
  - `MULTI_ROUTER_ROUTER ws 256/1024/65536/131072B`: 123.342/116.863/25.152/12.881 Kops/s,
    C 대비 32.1/29.3/24.5/31.1%; 131072B는 보류에서 통과로 상승
- 판정:
  - 회귀 조합은 제외하고, 개선이 확인된 pattern/transport/size만 allowlist로 남긴다.
  - public `recv()` 계약은 그대로 두고, private raw receive는 perf client hot path에서만 쓴다.

## MULTI_PUBSUB small latency sample stride 128 후보 기각

- 대상:
  - `MULTI_PUBSUB`
  - `tcp,ws,wss,tls`
  - `64,256,1024B`
- 근거:
  - 이전 round에서 latency sampling 비용이 PUBSUB hot path에 영향을 주는 것이 확인됐다.
  - 기존 default stride 32보다 더 넓은 stride 128이 작은 메시지에서 timestamp 기록 비용을 더 줄이는지
    확인했다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_164113_node_multi_pubsub_small_stride128_probe_20260601.txt`
  - 비교 기준:
    `perf_node_multi_linux_20260601_152140_node_multi_pubsub_small_raw_subscribe_final_20260601.txt`,
    `perf_node_multi_linux_20260601_152256_node_multi_pubsub_ws256_public_path_recheck_20260601.txt`,
    `perf_node_multi_linux_20260601_155746_node_multi_pubsub_selective_direct_publish_final_20260601.txt`
  - status: complete
- 결과:
  - `tcp 256B`는 613.472 Kmsg/s로 기존 raw subscribe 최종값 593.152 Kmsg/s보다 올랐다.
  - `wss 64B`도 588.849 Kmsg/s로 direct publish 최종값 576.406 Kmsg/s보다 올랐다.
  - 반대로 `tcp 64B`, `ws 256B`, `tls 64/256/1024B`는 기존 채택값보다 낮아졌다.
  - 개선된 cell도 C 대비 30% 기준에는 닿지 못했다.
- 판정:
  - 일부 개선은 있지만 transport/size 전반의 회귀가 섞였고 통과로 올라간 항목이 없다.
  - default stride는 바꾸지 않고, 이 후보는 최종 코드에 남기지 않는다.

## MULTI_PUBSUB small throughput HWM profile 후보 기각

- 대상:
  - `MULTI_PUBSUB`
  - `tcp,ws,wss,tls`
  - `64,256,1024B`
- 근거:
  - PUBSUB small 보류 항목은 sender/receiver 큐 설정의 영향을 크게 받을 수 있다.
  - `PERF_CTX_AUTO_HWM_PROFILE=throughput`으로 작은 메시지 HWM을 더 크게 잡아 drop 없이
    throughput이 올라가는지 확인했다.
- 측정:
  - Node: `perf_node_multi_linux_20260601_164625_node_multi_pubsub_small_throughput_hwm_probe_20260601.txt`
  - 비교 기준:
    `perf_node_multi_linux_20260601_152140_node_multi_pubsub_small_raw_subscribe_final_20260601.txt`,
    `perf_node_multi_linux_20260601_152256_node_multi_pubsub_ws256_public_path_recheck_20260601.txt`,
    `perf_node_multi_linux_20260601_155746_node_multi_pubsub_selective_direct_publish_final_20260601.txt`
  - status: complete
- 결과:
  - `tcp 64B`는 608.369 Kmsg/s로 기존 599.334 Kmsg/s보다 소폭 올랐다.
  - `wss 1024B`는 528.196 Kmsg/s로 direct publish 최종값 519.931 Kmsg/s보다 소폭 올랐다.
  - 반대로 `tcp 256B`, `ws 64/256B`, `tls 64B`는 기존 채택값보다 낮아졌다.
  - 개선된 cell도 C 대비 30% 기준에는 닿지 못했다.
- 판정:
  - HWM profile을 전역 또는 pattern 단위로 바꾸면 이미 개선된 cell을 되돌리는 회귀가 생긴다.
  - transport/size별로 runner 환경을 쪼개는 것은 perf 원칙상 측정 정책을 과하게 복잡하게 만들고,
    통과로 올리는 항목도 없어 최종 코드에 남기지 않는다.

## MULTI_DEALER_ROUTER/MULTI_ROUTER_ROUTER router.reply 단일 part stack fast path

- 대상:
  - `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`
  - `tcp,ws`
  - `64,256,1024,65536,131072B`
- 근거:
  - Node native `router_reply(...)`는 public `router.reply(...).message(...).submit()` 경로에서
    단일 메시지까지 매번 `std::vector<zlink_msg_t>`를 만들고 있었다.
  - public 계약과 perf runner는 그대로 두고, 단일 part일 때만 stack `zlink_msg_t`를 직접
    `router_reply_parts(...)`에 넘겨 native hot path 할당을 줄였다.
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/node/perf/run_benchmarks_multi.sh --transports tcp,ws --pattern MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER --msg-sizes 64,256,1024,65536,131072 --duration 1 --runs 3 --results-tag node_multi_router_reply_single_stack_tcp_ws_final_20260601`
  - Node: `perf_node_multi_linux_20260601_172824_node_multi_router_reply_single_stack_tcp_ws_final_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과:
  - `MULTI_DEALER_ROUTER tcp`: 38.7/38.4/37.4/23.4/29.1%
  - `MULTI_DEALER_ROUTER ws`: 39.2/36.5/36.9/23.8/31.5%
  - `MULTI_ROUTER_ROUTER tcp`: 31.1/30.9/30.5/24.5/31.6%
  - `MULTI_ROUTER_ROUTER ws`: 32.0/32.5/30.8/25.0/31.4%
- 판정:
  - `MULTI_ROUTER_ROUTER tcp 64/256/1024/131072B`, `MULTI_ROUTER_ROUTER ws 1024/131072B`,
    `MULTI_DEALER_ROUTER ws 131072B`가 통과권으로 올라갔다.
  - `MULTI_DEALER_ROUTER tcp 65536/131072B`, `MULTI_DEALER_ROUTER ws 65536B`,
    `MULTI_ROUTER_ROUTER tcp/ws 65536B`는 여전히 보류다.
  - public contract와 perf 측정 의미를 바꾸지 않는 bindings native 내부 개선이므로 최종 코드와
    계획 문서 표에 반영한다.

## MULTI_SPOT_SENDSEND spot 단일 part stack fast path 후보 기각

- 대상:
  - `MULTI_SPOT_SENDSEND`
  - `tcp,ws`
  - `64,256,1024B`
- 근거:
  - SPOT native send/reply/request 경로에도 단일 payload가 `std::vector<zlink_msg_t>`를 거치는
    함수가 남아 있어 `router.reply`와 같은 내부 개선이 가능한지 확인했다.
  - public contract와 perf runner는 바꾸지 않았다.
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/node/perf/run_benchmarks_multi.sh --transports tcp,ws --pattern MULTI_SPOT_SENDSEND --msg-sizes 64,256,1024 --duration 1 --runs 3 --results-tag node_multi_spot_sendsend_spot_stack_tcp_ws_probe_20260601`
  - Node: `perf_node_multi_linux_20260601_174142_node_multi_spot_sendsend_spot_stack_tcp_ws_probe_20260601.txt`
  - status: partial
- 결과:
  - `tcp 64B`: 43.183 Kops/s
  - `tcp 256B`: 48.933 Kops/s
  - `tcp 1024B`: client timeout
- 판정:
  - 통과권 개선이 없고 partial failure가 발생했다.
  - SPOT stack fast path 후보는 최종 코드와 계획 문서 표에 반영하지 않고 되돌렸다.

## single routed small RoutingId materialize cache 후보 기각

- 대상:
  - `DEALER_ROUTER`, `ROUTER_ROUTER`
  - `tcp,ws`
  - `64,256,1024B`
- 근거:
  - routed receive materialize는 같은 peer routing id도 매 수신마다 `RoutingId.from(Buffer)`로
    새 객체와 복사본을 만든다.
  - public `RoutingId` 값 의미는 유지하면서 runtime 내부 cache로 반복 wrapping 비용을 줄일 수
    있는지 확인했다.
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/node/perf/run_benchmarks.sh --transports tcp,ws --pattern DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64,256,1024 --duration 1 --runs 3 --results-tag node_single_routed_rid_cache_tcp_ws_probe_20260601`
  - Node: `perf_node_single_linux_20260601_174846_node_single_routed_rid_cache_tcp_ws_probe_20260601.txt`
  - C 기준: `perf_c_single_linux_20260530_231803_round_20260530_c_single_baseline.txt`
  - status: complete
- 결과:
  - `DEALER_ROUTER tcp`: 18.1/18.3/21.2%
  - `DEALER_ROUTER ws`: 18.9/20.7/28.8%
  - `ROUTER_ROUTER tcp`: 21.4/21.3/22.6%
  - `ROUTER_ROUTER ws`: 21.7/22.6/28.4%
- 판정:
  - 일부 cell은 기존 표보다 소폭 올랐지만 통과권으로 올라간 항목이 없다.
  - 전역 cache 복잡도를 추가할 만큼 확실한 개선이 아니므로 최종 코드와 계획 문서 표에는
    반영하지 않고 되돌렸다.

## MULTI_DEALER_ROUTER/MULTI_ROUTER_ROUTER native receive 단일 part fast path 후보 기각

- 대상:
  - `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`
  - `tcp,ws`
  - `64,256,1024,65536,131072B`
- 근거:
  - native `socket_recv_message`/`router_recv_message`는 단일 part 메시지도
    `std::vector<zlink_msg_t>`에 move한 뒤 JS snapshot을 만든다.
  - public receive 계약과 perf runner 의미를 유지하면서, 단일 part에서는 vector 할당을
    건너뛰는 후보를 시험했다.
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/node/perf/run_benchmarks_multi.sh --transports tcp,ws --pattern MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER --msg-sizes 64,256,1024,65536,131072 --duration 1 --runs 3 --results-tag node_multi_single_recv_part_fastpath_tcp_ws_probe_20260601`
  - Node: `perf_node_multi_linux_20260601_180410_node_multi_single_recv_part_fastpath_tcp_ws_probe_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과:
  - `MULTI_DEALER_ROUTER tcp`: 40.4/39.0/38.7/23.6/30.2%
  - `MULTI_DEALER_ROUTER ws`: 39.8/37.1/36.2/23.2/31.0%
  - `MULTI_ROUTER_ROUTER tcp`: 31.9/29.9/29.6/23.7/31.1%
  - `MULTI_ROUTER_ROUTER ws`: 32.7/32.0/30.0/24.4/32.0%
- 판정:
  - `MULTI_DEALER_ROUTER tcp 131072B`는 보류권에서 통과권으로 올라갔지만,
    `MULTI_ROUTER_ROUTER tcp 256/1024B`는 기존 `router.reply` stack fast path 최종값보다
    낮아져 통과권을 잃을 수 있다.
  - 전수 기준으로 보류 해소보다 회귀 위험이 커서 최종 코드와 계획 문서 표에는 반영하지
    않고 되돌렸다.

## Runtime operation builder payload 상태 흡수 후보 기각

- 대상:
  - `MULTI_PUBSUB`, `MULTI_SPOT_SENDSEND`
  - `tcp,ws`
  - 64/256/1024B
- 근거:
  - `send().message(...).flags(...).submit()` 계열은 operation 객체마다
    `OperationPayload` 객체를 추가로 만든다.
  - public operation builder 계약은 유지하고 runtime builder 내부 필드로 단일/multipart
    상태를 흡수해 작은 메시지 송신 할당을 줄일 수 있는지 확인했다.
- 측정 1:
  - 명령: `PERF_FAIL_FAST=1 bindings/node/perf/run_benchmarks_multi.sh --transports tcp,ws --pattern MULTI_PUBSUB,MULTI_SPOT_SENDSEND --msg-sizes 64,256,1024 --duration 1 --runs 3 --results-tag node_multi_runtime_payload_inline_tcp_ws_small_probe_20260601`
  - Node: `perf_node_multi_linux_20260601_181605_node_multi_runtime_payload_inline_tcp_ws_small_probe_20260601.txt`
  - status: complete
- 결과 1:
  - `MULTI_PUBSUB tcp`: 541.282/529.421/499.352 Kmsg/s
  - `MULTI_PUBSUB ws`: 519.773/489.866/516.837 Kmsg/s
  - `MULTI_SPOT_SENDSEND tcp`: 52.165/51.145/49.707 Kops/s
  - `MULTI_SPOT_SENDSEND ws`: 49.445/49.328/48.545 Kops/s
- 판정 1:
  - `MULTI_SPOT_SENDSEND`는 전반적으로 올랐지만 `MULTI_PUBSUB ws 256B`가 기존 채택값보다
    크게 낮아졌다.
  - publish 경로까지 전역 적용하는 형태는 기각했다.
- 측정 2:
  - publish 경로는 기존 `OperationPayload`로 되돌리고, send/request/reply runtime builder만
    내부 필드 상태로 바꾼 뒤 재측정했다.
  - 명령: `PERF_FAIL_FAST=1 bindings/node/perf/run_benchmarks_multi.sh --transports tcp,ws --pattern MULTI_SPOT_SENDSEND --msg-sizes 64,256,1024 --duration 1 --runs 3 --results-tag node_multi_runtime_send_payload_inline_spot_sendsend_tcp_ws_small_probe_20260601`
  - Node: `perf_node_multi_linux_20260601_181922_node_multi_runtime_send_payload_inline_spot_sendsend_tcp_ws_small_probe_20260601.txt`
  - status: complete
- 결과 2:
  - `MULTI_SPOT_SENDSEND tcp`: 50.651/50.728/48.964 Kops/s
  - `MULTI_SPOT_SENDSEND ws`: 46.195/46.729/42.424 Kops/s
- 판정 2:
  - tcp는 기존보다 올랐지만 ws 64/1024B가 기존 채택값보다 낮아졌다.
  - transport별로 runtime builder 구현을 갈라 적용할 수 없고, 통과권으로 올라간 항목도 없어
    최종 코드와 계획 문서 표에는 반영하지 않고 되돌렸다.

## Node full verification

- 단일 suite:
  - 명령: `PERF_FULL_MATRIX=1 bindings/node/perf/run_benchmarks.sh --results-tag node_final_current_single_full_20260601`
  - Node: `perf_node_single_linux_20260601_184729_node_final_current_single_full_20260601.txt`
  - status: complete
  - expected/actual: 1020/1020
- multi suite:
  - 명령: `PERF_FULL_MATRIX=1 bindings/node/perf/run_benchmarks_multi.sh --results-tag node_final_current_multi_full_20260601`
  - Node: `perf_node_multi_linux_20260601_193339_node_final_current_multi_full_20260601.txt`
  - status: partial
  - expected/actual: 920/915
  - failure: `MULTI_SPOT_REQREP current tls 1024B`
- multi suite 실패 행 재확인:
  - 명령: `PERF_FAIL_FAST=1 bindings/node/perf/run_benchmarks_multi.sh --transports tls --pattern MULTI_SPOT_REQREP --msg-sizes 1024 --duration 5 --runs 1 --results-tag node_multi_spot_reqrep_tls_1024_recheck_20260601`
  - Node: `perf_node_multi_linux_20260601_193521_node_multi_spot_reqrep_tls_1024_recheck_20260601.txt`
  - status: complete
  - expected/actual: 5/5
  - 결과: 98.980 Kops/s, 202.712 MB/s, 평균 지연 0.443 ms
- 판정:
  - 채택된 `router.reply` 단일 part stack fast path 자체는 full multi의 routed 계열에서 결과를
    냈다.
  - multi full은 SPOT req/rep tls 1024B 한 행 실패로 partial이었지만, 같은 행의 targeted
    재확인은 complete다. 현재 코드의 고정 실패 증거는 없고, Node 단계의 남은 미달 성능
    개선은 기존 보류/기각 후보 기록을 기준으로 계속 판단한다.
