[스펙 목차](../README.ko.md)

# Draft -- auto-HWM 개선 정책: context 메모리 산정과 단위 예산

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`와 정식 spec 문서에 없는
> API, 상수, 기본 동작을 보장하지 않는다.
> 구현과 공개 헤더, 관련 테스트, 정식 문서가 확정되면 적절한 spec 문서로
> 나누어 반영한다.

## 1. 목적

이 초안은 기존 auto-HWM 계산을 보완 옵션으로 남기는 문서가 아니라, 기존
계산 정책을 개선된 정책으로 대체하기 위한 기준을 정리한다. 사용자가 context
메모리 값을 어떻게 정해야 하는지, 그리고 라이브러리가 그 값을 HWM으로 어떻게
바꿔야 하는지를 정의한다.

이 문서의 핵심 방향은 아래와 같다.

- context 메모리는 HWM을 무조건 크게 만드는 값이 아니라, 전체 큐 메모리 상한이다.
- 사용자는 소켓별 HWM 숫자를 직접 계산하지 않고, client 수와 spot 수 기준으로
  context 메모리를 정한다.
- 라이브러리는 소켓 역할, client 수, spot 수, `MsgUnit(B)`을 기준으로 적절한
  HWM을 계산한다.
- 큰 메시지 one-way fanout에서 큐가 깊어져 latency가 초 단위로 커지는 상황을
  기본 정책에서 막는다.

## 2. 배경

기존 auto-HWM은 context 메모리 예산을 큐 슬롯으로 바꾸는 성격이 강했다.
이 방식은 메모리 사용량을 설명하기는 쉽지만, 작은 메시지에서는 HWM이 너무 크게
나오고 큰 메시지 fanout에서는 큐 체류 시간이 커질 수 있다.

따라서 이 초안의 방향은 기존 정책 위에 또 다른 예외 경로를 추가하는 것이
아니다. 기존 정책의 문제를 고쳐, 기본 auto-HWM 계산 자체가 역할, 연결 수,
spot 수, `MsgUnit(B)`에 맞게 동작하도록 바꾸는 것이다.

특히 one-way `PUB/SUB`와 `SPOT`은 요청/응답처럼 자연스러운 in-flight 제한이
없다. publish 쪽이 계속 밀어 넣으면 subscriber 쪽 큐가 깊어지고, payload에
찍힌 송신 시각과 수신 시각 차이는 큐 체류 시간을 포함한다. 따라서 throughput
테스트에서는 latency가 크게 보일 수 있다.

이 초안은 측정값 기반 adaptive 정책을 기본으로 쓰지 않는다. 평균 메시지 크기나
consumer 처리량을 계속 측정해서 HWM을 바꾸면 정책이 흔들리고 디버깅이 어려워질
수 있기 때문이다. 대신 사용자가 쉽게 예측할 수 있는 정적 단위 예산을 사용한다.

## 3. 사용자 메모리 산정 공식

이 초안의 첫 구현은 아래 결정을 기준으로 한다.

- `profile`은 공개 context 옵션으로 추가한다. 옵션 이름은
  `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`로 둔다.
- profile 값은 새 enum `zlink_auto_hwm_profile_t`로 정의한다.
  `ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY`, `ZLINK_AUTO_HWM_PROFILE_BALANCED`,
  `ZLINK_AUTO_HWM_PROFILE_THROUGHPUT` 세 값을 둔다.
- 기본값은 `ZLINK_AUTO_HWM_PROFILE_BALANCED`이다.
- profile 옵션에 알 수 없는 값을 넣으면 `EINVAL`을 반환한다.
- 이 초안에서는 별도 `publish_fanout_count` 공개 옵션을 추가하지 않는다.
  SpotNode의 기본 동시 publish 대상 수는 기존
  `ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP` 값을 fanout limit으로 재사용한다.

기본 `balanced` profile에서 context 메모리 권장값은 아래 공식으로 계산한다.

```text
recommended_context_memory =
  64 MiB
  + pub_fanout_budget(pub_client_count)
  + spotnode_budget(total_spot_count,
                    publish_fanout_count,
                    spotnode_peer_count)
  + routed_budget(routed_client_count)
  + stream_budget(stream_client_count)
```

각 budget은 `balanced` profile에서 아래 공식을 사용한다.

```text
pub_fanout_budget(n) =
  n * 2 MiB

spotnode_budget(total_spots, publish_fanout, peers) =
  spot_metadata_budget(total_spots)
  + publish_fanout * 2 MiB
  + peers * 2 MiB

spot_metadata_budget(n) =
  min(n * 64 KiB, 128 MiB + n * 16 KiB)

routed_budget(n) =
  min(n * 512 KiB, 256 MiB + n * 64 KiB)

stream_budget(n) =
  min(n * 256 KiB, 512 MiB + n * 32 KiB)
```

`publish_fanout_count`는 전체 spot 수가 아니라, 한 번의 publish에서 동시에 받는
spot 수이다. 이 값을 모르면
`min(total_spot_count, ZLINK_CTX_AUTO_HWM_SPOT_BOOTSTRAP_DFLT)`를 시작값으로
사용한다. 기본값 기준으로는 `min(total_spot_count, 500)`이다.

계산 결과는 64 MiB 단위로 올림한다. 최소 권장값은 128 MiB이다.

각 항목의 뜻은 아래와 같다.

| 항목 | 의미 |
|---|---|
| `64 MiB` | context runtime, 내부 상태, transport 여유분을 위한 기본 예산 |
| `pub_client_count` | 일반 `PUB` / `XPUB` 소켓이 fanout하는 subscriber 수 |
| `total_spot_count` | 한 SpotNode 안에 만들어진 전체 spot handle 수 |
| `publish_fanout_count` | 한 번의 publish에서 동시에 받는 spot 수 |
| `spotnode_peer_count` | SpotNode가 mesh나 외부 fanout으로 연결하는 peer/client 수 |
| `routed_client_count` | `ROUTER`, `DEALER` 등 routed request/reply 연결 수 |
| `stream_client_count` | `STREAM` 연결 수 |
| `spot_metadata_budget(n)` | 전체 spot 수에 따라 늘어나는 낮은 수준의 관리 예산 |
| `routed_budget(n)` | 작은 규모에서는 client당 512 KiB를 쓰고, 큰 규모에서는 client당 증가폭을 64 KiB 수준으로 낮추는 예산 |
| `stream_budget(n)` | 대규모 stream 연결에서 client 수 증가 속도를 더 낮게 잡는 예산 |

fanout 예산은 실제 동시에 fanout되는 대상 수에 비례해 잡지만, routed와 stream
예산은 client 수가 커질수록 증가 속도를 낮춘다. `ROUTER` / `DEALER` 계열과
`STREAM`은 요청/응답 흐름이나 연결별 backpressure가 자연스러운 in-flight 제한을
만들기 때문에, 10,000 clients 같은 규모에서 client당 큰 예산을 끝까지 곱하면
예산이 과하게 커진다.

SPOT에서는 전체 spot 수와 동시 publish fanout 수를 분리해야 한다. 예를 들어
spot이 10,000개 있어도 하나의 publish가 매번 10,000개 전체로 나가는 경우는
일반적인 운영 가정이 아니다. 전체 spot 수는 낮은 metadata budget에 반영하고,
queue budget은 실제 동시 publish 대상 수를 기준으로 잡는다.

이 공식은 정확한 RSS 예측식이 아니다. 사용자가 context budget을 정할 때 쓰는
운영 가이드이다. 실제 HWM은 이 budget 안에서 다시 소켓 역할과 `MsgUnit(B)`,
profile별 size cap에 따라 낮아질 수 있다.

## 4. Profile별 단위 예산

profile은 공개 context 옵션으로 노출한다. 최종 구현에서는 공개 헤더, 정식 spec,
모든 binding이 같은 enum 값을 제공해야 한다.

| Profile | PUB fanout | SPOT fanout | SPOT meta small | SPOT meta large | routed small | routed large | stream small | stream large |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `low_latency` | 512 KiB | 512 KiB | 32 KiB | 8 KiB | 256 KiB | 32 KiB | 128 KiB | 16 KiB |
| `balanced` | 2 MiB | 2 MiB | 64 KiB | 16 KiB | 512 KiB | 64 KiB | 256 KiB | 32 KiB |
| `throughput` | 4 MiB | 4 MiB | 128 KiB | 32 KiB | 1 MiB | 128 KiB | 512 KiB | 64 KiB |

planner가 HWM을 계산할 때 쓰는 per-connection unit budget은 아래처럼 둔다.

| Policy class | `low_latency` | `balanced` | `throughput` |
|---|---:|---:|---:|
| `fanout` | 512 KiB | 2 MiB | 4 MiB |
| `spot_data` | 512 KiB | 2 MiB | 4 MiB |
| `routed` | 256 KiB | 512 KiB | 1 MiB |
| `peer_queue` | 256 KiB | 512 KiB | 1 MiB |
| `stream` | 128 KiB | 256 KiB | 512 KiB |
| `recv_ingress` | 128 KiB | 256 KiB | 512 KiB |
| `control` | 64 KiB | 64 KiB | 128 KiB |

`profile.spot_publish_fanout_bytes`와 `profile.spot_peer_bytes`는 같은 profile의
`spot_data` unit budget과 같은 값을 사용한다. `DEALER`는 context memory
산정에서는 `routed_client_count`에 포함하고, planner의 HWM cap 선택에서는
`peer_queue` class를 사용한다.

각 profile의 의도는 아래와 같다.

- `low_latency`
  큐를 짧게 유지한다. backpressure가 더 자주 걸릴 수 있다.
- `balanced`
  기본 운영값이다. 큰 메시지 one-way fanout에서 초 단위 latency를 피하는 쪽을
  우선한다.
- `throughput`
  큐 여유를 크게 둔다. latency가 커질 수 있으므로 perf throughput 또는 별도
  튜닝이 필요한 환경에서만 사용한다.

## 5. 예시

### 5.1 PUB 100 clients

```text
64 MiB + 100 * 2 MiB = 264 MiB
```

권장 설정값은 320 MiB이다. 메모리를 더 보수적으로 잡고 싶으면 384 MiB 또는
512 MiB를 선택할 수 있다.

### 5.2 SpotNode 1개, 전체 spot 2개, 동시 publish 대상 2개, peer 100개

```text
spot_metadata_budget(2)
= min(2 * 64 KiB, 128 MiB + 2 * 16 KiB)
= 128 KiB

publish fanout budget = 2 * 2 MiB = 4 MiB
peer budget = 100 * 2 MiB = 200 MiB

64 MiB + 128 KiB + 4 MiB + 200 MiB
= 약 268 MiB
```

권장 설정값은 320 MiB이다.

### 5.3 SpotNode 1개, 전체 spot 10,000개, 동시 publish 대상 500개

```text
spot_metadata_budget(10000)
= min(10000 * 64 KiB, 128 MiB + 10000 * 16 KiB)
= min(625 MiB, 약 284 MiB)
= 약 284 MiB

publish fanout budget = 500 * 2 MiB = 1000 MiB

64 MiB + 284 MiB + 1000 MiB = 약 1348 MiB
```

권장 설정값은 1408 MiB 또는 1536 MiB이다. 전체 spot이 10,000개여도 publish가
한 번에 500개 정도의 대상에게만 나간다면, 10,000개 전체를 fanout queue 예산으로
계산하지 않는다.

### 5.4 ROUTER 100 clients

```text
routed_budget(100)
= min(100 * 512 KiB, 256 MiB + 100 * 64 KiB)
= min(50 MiB, 약 262 MiB)
= 50 MiB

64 MiB + 50 MiB = 114 MiB
```

권장 설정값은 128 MiB이다.

### 5.5 ROUTER 10,000 clients

```text
routed_budget(10000)
= min(10000 * 512 KiB, 256 MiB + 10000 * 64 KiB)
= min(5000 MiB, 약 881 MiB)
= 약 881 MiB

64 MiB + 881 MiB = 약 945 MiB
```

권장 설정값은 1024 MiB이다. 선형 공식만 쓰면 약 5 GiB가 필요해 보이지만,
routed 패턴은 fanout처럼 client마다 깊은 one-way queue를 기본으로 잡지 않는다.

### 5.6 STREAM 10,000 clients

```text
stream_budget(10000)
= min(10000 * 256 KiB, 512 MiB + 10000 * 32 KiB)
= min(2500 MiB, 약 825 MiB)
= 약 825 MiB

64 MiB + 825 MiB = 약 889 MiB
```

권장 설정값은 960 MiB 또는 1024 MiB이다. STREAM은 10,000 clients 측정에서도
HWM 16, 64, 256 사이 throughput과 latency 차이가 크지 않았으므로, routed보다
조금 더 낮은 증가 속도 제한 공식을 사용한다.

### 5.7 256 KiB fanout에서 HWM 감각

`balanced` profile에서 PUB client 단위 예산은 2 MiB이다.

```text
2 MiB / 256 KiB = 8 messages
```

따라서 256 KiB 메시지를 100 clients로 fanout하는 기본 HWM은 client당 몇 개
수준으로 낮아진다. 이 값은 throughput을 무조건 최대로 밀기 위한 값이 아니라,
큰 메시지가 큐에 오래 쌓이는 상황을 피하기 위한 기본값이다.

## 6. multi perf 측정 근거

아래 값은 `bindings/c/perf` multi runner로 `tcp`, 100 clients, 2초 duration,
수동 HWM sweep을 실행해 얻은 기준값이다. 목적은 현재 구현을 그대로 문서화하는
것이 아니라, 개선 정책의 size cap과 단위 예산을 정하는 것이다.

실행 조건은 아래와 같다.

```bash
cmake --build core/build
PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES=1 \
PERF_MULTI_RUN_COOLDOWN_MS=100 \
./bindings/c/perf/run_benchmarks_multi.sh \
  --reuse-build \
  --pattern PUBSUB,SPOT \
  --transports tcp \
  --msg-sizes 64,1024,65536,131072,262144 \
  --duration 2 \
  --clients 100 \
  --hwm <N> \
  --transport-transition-ms 100 \
  --pattern-transition-ms 100
```

대표 결과는 아래와 같다.

| Pattern | Size | HWM | Throughput | P99 latency | 판단 |
|---|---:|---:|---:|---:|---|
| `MULTI_PUBSUB` | 64 B | 64 | 3.478 Mmsg/s | 18.365 ms | balanced 상한 후보 |
| `MULTI_PUBSUB` | 64 B | 128 | 3.913 Mmsg/s | 227.091 ms | throughput 전용 |
| `MULTI_PUBSUB` | 64 KiB | 32 | 203.908 Kmsg/s | 20.787 ms | balanced 상한 후보 |
| `MULTI_PUBSUB` | 64 KiB | 128 | 217.544 Kmsg/s | 63.674 ms | throughput 전용 |
| `MULTI_PUBSUB` | 256 KiB | 8 | 45.124 Kmsg/s | 24.752 ms | balanced 상한 후보 |
| `MULTI_PUBSUB` | 256 KiB | 64 | 45.025 Kmsg/s | 144.528 ms | 이득 대비 latency 증가 |
| `MULTI_SPOT` | 64 KiB | 32 | 156.958 Kmsg/s | 3.972 ms | balanced 상한 후보 |
| `MULTI_SPOT` | 128 KiB | 8 | 79.718 Kmsg/s | 94.470 ms | balanced 상한 후보 |
| `MULTI_SPOT` | 128 KiB | 16 | 79.864 Kmsg/s | 255.827 ms | cap 초과 |
| `MULTI_SPOT` | 256 KiB | 8 | 41.811 Kmsg/s | 1219.236 ms | 발행률 제한 필요 |

`DEALER_ROUTER`는 같은 조건에서 HWM 4, 16, 64 모두 256 KiB p99 latency가
5 ms 미만이었다. 요청/응답 계열은 자연스러운 in-flight 제한이 있으므로
one-way fanout과 같은 큰 cap 조정이 필요하지 않다.

추가로 `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `STREAM`을 측정했다.
100 clients, `tcp`, 2초 duration 기준 대표값은 아래와 같다.

| Pattern | Size | HWM | Throughput | P99 latency | 판단 |
|---|---:|---:|---:|---:|---|
| `MULTI_DEALER_ROUTER` | 64 KiB | 16 | 170.297 Kmsg/s | 0.621 ms | HWM 민감도 낮음 |
| `MULTI_DEALER_ROUTER` | 64 KiB | 256 | 159.480 Kmsg/s | 0.691 ms | 큰 HWM도 latency 안정 |
| `MULTI_ROUTER_ROUTER` | 64 KiB | 16 | 153.531 Kmsg/s | 0.753 ms | HWM 민감도 낮음 |
| `MULTI_ROUTER_ROUTER` | 64 KiB | 256 | 162.914 Kmsg/s | 0.642 ms | 큰 HWM도 latency 안정 |
| `MULTI_DEALER_DEALER` | 64 KiB | 16 | 167.219 Kmsg/s | 14.170 ms | balanced 상한 후보 |
| `MULTI_DEALER_DEALER` | 64 KiB | 256 | 157.951 Kmsg/s | 253.725 ms | cap 초과 |
| `MULTI_STREAM` | 64 KiB | 16 | 116.623 Kmsg/s | 1.452 ms | HWM 민감도 낮음 |
| `MULTI_STREAM` | 64 KiB | 256 | 116.737 Kmsg/s | 1.502 ms | 큰 HWM도 latency 안정 |

대규모 client 수에서는 HWM보다 연결 수와 payload 크기가 latency를 더 크게
좌우했다. `tcp`, 1~2초 duration, 수동 HWM sweep 기준 대표값은 아래와 같다.

| Pattern | Clients | Size | HWM | Throughput | P99 latency |
|---|---:|---:|---:|---:|---:|
| `MULTI_DEALER_ROUTER` | 1000 | 64 KiB | 16 | 72.897 Kmsg/s | 23.407 ms |
| `MULTI_DEALER_ROUTER` | 5000 | 64 KiB | 16 | 37.907 Kmsg/s | 110.550 ms |
| `MULTI_DEALER_ROUTER` | 10000 | 64 KiB | 16 | 18.933 Kmsg/s | 252.358 ms |
| `MULTI_ROUTER_ROUTER` | 1000 | 64 KiB | 16 | 70.686 Kmsg/s | 22.766 ms |
| `MULTI_ROUTER_ROUTER` | 5000 | 64 KiB | 16 | 37.694 Kmsg/s | 123.444 ms |
| `MULTI_ROUTER_ROUTER` | 10000 | 64 KiB | 64 | 24.702 Kmsg/s | 238.325 ms |
| `MULTI_STREAM` | 1000 | 64 KiB | 16 | 72.193 Kmsg/s | 18.667 ms |
| `MULTI_STREAM` | 5000 | 64 KiB | 64 | 57.735 Kmsg/s | 120.851 ms |
| `MULTI_STREAM` | 10000 | 64 KiB | 16 | 53.250 Kmsg/s | 254.934 ms |

이 결과 때문에 `routed_budget(n)`과 `stream_budget(n)`은 client 수 전체에 큰
선형 계수를 곱하지 않는다. 특히 `STREAM`은 10,000 clients에서 HWM 16, 64, 256
차이가 작았으므로 `stream_budget(n)`의 large 계수를 routed보다 낮게 둔다.

`SPOT` 256 KiB는 HWM만 낮춰도 1 ms latency probe에서는 p99가 1초대로 남았다.
같은 HWM 8에서 `PERF_MULTI_SPOT_LATENCY_ONLY_INTERVAL_US=5000`이면 p99가
7.255 ms, `10000`이면 6.225 ms였다. 따라서 큰 메시지 SPOT latency는 HWM
공식만으로 판단하지 않고, 발행 간격이나 애플리케이션 발행률도 함께
제한해야 한다.

SPOT publish fanout은 전체 spot 수가 아니라 한 번의 publish 대상 수를 기준으로
잡는다. 10,000 spot 전체에 동시에 publish하는 테스트는 saturation worst-case로는
볼 수 있지만 일반 운영 가이드의 기본값으로 쓰지 않는다. 이번 초안에서는 동시에
publish되는 spot 수를 500까지 측정한 값을 기본 근거로 삼는다.

| Pattern | Publish targets | Size | HWM | Throughput | P99 latency |
|---|---:|---:|---:|---:|---:|
| `MULTI_SPOT` | 500 | 64 B | 8 | 2.100 Mmsg/s | 5.640 ms |
| `MULTI_SPOT` | 500 | 1 KiB | 8 | 1.483 Mmsg/s | 5.302 ms |
| `MULTI_SPOT` | 500 | 64 KiB | 8 | 92.963 Kmsg/s | 34.894 ms |

참고로 1000개 이상 전체 publish도 일부 확인했지만, 이는 기본 산정 기준에서
제외한다. 1000 targets에서 64 B는 p99 8.735 ms였으나, 1 KiB와 64 KiB는
clean latency pass가 수백 ms 이상으로 커졌다. 5000 targets 64 B도 p99가 약
1초로 증가했다. 이 값들은 "전체 spot 수를 publish fanout 수로 오해하면 안 된다"는
근거로만 사용한다.

## 7. 라이브러리 내부 HWM 계산식

라이브러리는 사용자가 설정한 context budget을 절대 상한으로 보고, 아래 순서로
HWM을 계산한다.

구현은 공개 socket type을 직접 정책 키로 쓰지 않고, 내부
`auto_hwm_policy_class`로 한 번 변환한다. 이 분류는 공개 API가 아니라 planner
내부 구현 세부 사항이다. 공개 monitor에는 숫자 값만 노출할 수 있으나, 정식 spec에
노출하려면 enum 값을 공개 헤더에 추가해야 한다.

| Policy class | 적용 대상 |
|---|---|
| `fanout` | `PUB`, `XPUB` |
| `spot_data` | SpotNode `local-pub`, `mesh-pub`, spot pub send 방향 |
| `recv_ingress` | `SUB`, `XSUB`, SpotNode `ingress-sub`, `mesh-xsub`, spot sub recv 방향 |
| `routed` | `ROUTER`, SpotNode internal/external router |
| `peer_queue` | `DEALER`, `PAIR`처럼 broker 없이 peer queue가 깊어질 수 있는 방향 |
| `stream` | `STREAM` |
| `control` | SpotNode control pub/sub 등 데이터 payload가 아닌 제어 경로 |

`DEALER`는 실제 상대가 `ROUTER`인지 다른 `DEALER`인지 항상 알 수 없으므로 첫
구현에서는 `peer_queue` cap을 적용한다. 이 값은 `DEALER_ROUTER`에도 안전하게
낮은 HWM을 주며, 측정상 64 KiB payload에서 throughput 손실 없이 latency가
안정적이었다.

```text
context_budget = user configured context memory budget
queue_budget = context_budget - reserve_budget

socket_class = auto_hwm_policy_class(socket)
socket_unit_budget = profile_unit_budget(socket_class)
socket_count = max(1, class_count(socket))

socket_budget_cap = socket_unit_budget * socket_count
socket_budget = min(socket_budget_cap, fair_share(queue_budget, socket))

per_connection_budget = socket_budget / socket_count
memory_hwm = per_connection_budget / MsgUnit(B)
size_cap = profile_size_class_cap(profile, socket_class, MsgUnit(B))

effective_hwm =
  clamp(
    min(memory_hwm, size_cap),
    floor(socket_class, MsgUnit(B)),
    size_cap
  )
```

auto-HWM v2의 floor는 모든 data class에서 1이다. control class도 floor 1을 쓴다.
최종 상한은 별도 hard cap이 아니라 `size_cap`이다. 따라서 기존 구현의
base-floor 중심 계산은 이 공식으로 대체되어야 한다.

`fair_share(queue_budget, socket)`은 같은 context 안의 auto-HWM 대상 소켓들이
전체 budget을 초과하지 않도록 나누어 주는 값이다. 구현은 단순하게 소켓 역할별
weight와 count를 사용한다.

```text
socket_weight = class_weight(socket_class) * socket_count
fair_share = queue_budget * socket_weight / total_weight
```

초기 weight는 아래처럼 둔다.

| Policy class | Weight |
|---|---:|
| `fanout` | 2 |
| `spot_data` | 2 |
| `recv_ingress` | 1 |
| `routed` | 2 |
| `peer_queue` | 1 |
| `stream` | 1 |
| `control` | 0.25 |

이 weight는 "메모리를 더 달라"는 의미가 아니라, 같은 context 안에서 어느 역할이
큐 pressure를 더 크게 만들 수 있는지를 나타내는 분배 기준이다.

단일 socket에서 auto-HWM을 즉시 다시 계산하는 경로도 이 공식의 예외가 되면 안
된다. 연결 수, scope count, msg unit, buffer 크기가 바뀌면 해당 socket만 독립
계산하지 않고 context 전체 auto-HWM 재계산을 예약한다. 즉 최종 HWM은 항상 같은
context 안의 auto-HWM 대상 소켓 집합을 기준으로 계산해야 한다.

## 8. MsgUnit별 HWM 상한

메모리 예산만으로 HWM을 계산하면 작은 메시지에서 값이 너무 커질 수 있다.
따라서 `MsgUnit(B)` 구간별 상한을 둔다. 아래 표는 `balanced` profile의 기본
상한이다.

| `MsgUnit(B)` | 일반 fanout cap | SPOT data-plane cap | routed cap | peer queue cap | stream cap |
|---:|---:|---:|---:|---:|---:|
| `<= 1 KiB` | 64 | 64 | 64 | 64 | 64 |
| `<= 4 KiB` | 64 | 64 | 64 | 64 | 64 |
| `<= 16 KiB` | 64 | 64 | 64 | 64 | 64 |
| `<= 64 KiB` | 32 | 32 | 64 | 16 | 64 |
| `<= 128 KiB` | 8 | 8 | 64 | 8 | 64 |
| `<= 256 KiB` | 8 | 8 | 64 | 8 | 64 |
| `> 256 KiB` | 4 | 4 | 32 | 4 | 32 |

최종 HWM은 `memory_hwm`과 profile별 `size_cap` 중 작은 값을 사용한다.
따라서 context 메모리를 크게 잡아도 큰 메시지 큐가 무한히 깊어지지 않는다.

profile별 cap은 아래 비율로 조정한다.

| Profile | Cap 조정 |
|---|---|
| `low_latency` | `balanced` cap의 1/2, 최소 1 |
| `balanced` | 위 표 그대로 |
| `throughput` | `balanced` cap의 2배. 단, `> 256 KiB`는 8을 넘기지 않는다 |

`control` class는 payload 크기 기준 cap을 쓰지 않는다. `low_latency`와
`balanced`에서는 cap 64, `throughput`에서는 cap 128을 사용한다.

SpotNode 내부 routed socket은 요청/응답형 경로의 burst를 흡수해야 하므로
`MsgUnit(B) <= 16 KiB`에서 별도 보정 cap을 둔다. `balanced`와 `throughput`은
`128`, `low_latency`는 `64`를 사용한다. 이 보정은 SpotNode의
`internal-router`와 `external-router` 같은 routed 내부 socket에만 적용하고,
one-way publish/fanout socket의 `spot_data` cap은 위 표를 그대로 따른다.

이 cap은 "항상 가장 빠른 throughput"을 목표로 하지 않는다. sweep 결과처럼 HWM을
더 키워도 throughput 이득이 작고 p99 latency만 크게 늘어나는 구간이 있기 때문에,
기본값은 latency 폭증을 피하는 쪽에 둔다.

`peer queue cap`은 `DEALER_DEALER`처럼 broker 없이 양쪽 peer queue가 직접
깊어질 수 있는 패턴에 쓴다. `ROUTER`가 중간에서 흐름을 제한하는
`DEALER_ROUTER`와 `ROUTER_ROUTER`는 같은 64 KiB payload에서도 HWM 256까지
latency가 안정적이었지만, `DEALER_DEALER`는 64 KiB에서 HWM 256일 때 p99가
253.725 ms까지 증가했다.

## 9. SpotNode 적용 방식

SPOT은 외부 spot handle만 계산하면 안 된다. SpotNode 내부 소켓도 같은 정책으로
계산해야 한다.

| SPOT 대상 | Policy class | Count 기준 | HWM 방향 |
|---|---|---|---|
| spot pub handle | `spot_data` | effective publish fanout 또는 peer 수 | `SNDHWM` |
| spot sub handle | `recv_ingress` | local publisher 수 또는 peer 수 | `RCVHWM` |
| `local-pub` | `spot_data` | effective publish fanout | `SNDHWM` |
| `mesh-pub` | `spot_data` | active peer 수 | `SNDHWM` |
| `ingress-sub` | `recv_ingress` | local pub spot 수 | `RCVHWM` |
| `mesh-xsub` | `recv_ingress` | active peer 수 | `RCVHWM` |
| `internal-router` | `routed` | routed spot 수 | `SNDHWM`, `RCVHWM` |
| `external-router` | `routed` | active peer 수 | `SNDHWM`, `RCVHWM` |
| `peer_ctrl_pub/sub` | `control` | active peer 수 | `SNDHWM`, `RCVHWM` |

SpotNode 메모리 산정 가이드는 아래 항목을 쓴다.

```text
spot_metadata_budget(total_spot_count)
+ publish_fanout_count * profile.spot_publish_fanout_bytes
+ spotnode_peer_count * profile.spot_peer_bytes
```

`total_spot_count`는 내부 metadata와 낮은 수준의 관리 예산에만 사용한다.
`publish_fanout_count`는 전체 spot 수가 아니라 한 번의 publish가 실제로 동시에
보내는 대상 수이다. 운영자가 값을 모르면 `min(total_spot_count, 500)`을 기본
시작값으로 둔다.

core 첫 구현에서는 effective publish fanout을 아래처럼 계산한다.

```text
publish_fanout_limit =
  max(1, ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP)

candidate_publish_targets =
  max(local_sub_spot_count, active_peer_count, observed_scope_count)

effective_publish_fanout =
  max(1, min(candidate_publish_targets, publish_fanout_limit))
```

`observed_scope_count`를 알 수 없는 경로에서는 0으로 본다. 이 계산은 전체 spot
수를 fanout queue 예산으로 쓰지 않기 위한 기본 정책이다. 사용자가 실제로 전체
spot에 동시에 publish하는 saturation workload를 운영 기준으로 삼아야 한다면,
`ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP`을 그 fanout 수 이상으로 올려야 한다.

수동 override는 아래 기존 SpotNode 옵션을 그대로 존중한다.

```c
ZLINK_SPOT_NODE_OPT_PUB_HWM
ZLINK_SPOT_NODE_OPT_SUB_HWM
ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM
ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM
```

auto-HWM v2에서는 local fanout과 mesh fanout을 같은 숫자로 뭉개지 않고, 각 내부
소켓의 count 기준으로 따로 계산한다. 자동 계산값은 public setter를 우회하는
내부 apply 경로로 반영해야 하며, 수동 override 플래그를 켜면 안 된다.

## 10. 큰 메시지 SPOT 발행률 제한

SPOT data-plane은 내부 fanout과 routing 경로를 함께 지나므로, 128 KiB 이상
one-way payload에서는 HWM만으로 latency를 설명할 수 없다. 특히 256 KiB 메시지를
100 clients로 1 ms 간격 fanout하면 이론상 약 25 GiB/s의 payload를 계속 밀어 넣는
것과 같다. 이 조건은 일반 latency probe가 아니라 saturation test에 가깝다.

따라서 auto-HWM 정책은 아래 원칙을 함께 가져야 한다.

- HWM은 queue residency를 제한한다.
- 큰 메시지 one-way SPOT latency는 발행률 제한과 함께 판단한다.
- `MsgUnit(B) >= 128 KiB`인 SPOT data-plane은 기본 profile에서 HWM cap 8을 넘기지
  않는다.
- `MsgUnit(B) >= 256 KiB`이고 동시 publish fanout 대상이 100개 이상이면, latency
  측정이나 기본 운영 가이드는 최소 5 ms 이상의 publish interval을 권장한다.
- 전체 spot이 1000개 이상이어도 한 번의 publish 대상이 작으면 전체 spot 수를
  fanout queue 예산으로 계산하지 않는다.
- throughput profile에서 이 제한을 완화할 수 있지만, p99 latency가 크게 증가할 수
  있음을 명시한다.

## 11. 수동 설정 우선순위

사용자가 HWM을 수동으로 설정한 방향은 auto-HWM이 덮어쓰지 않는다.

우선순위는 아래와 같다.

1. 개별 socket 또는 spot handle의 수동 HWM
2. SpotNode 옵션으로 설정한 수동 HWM
3. auto-HWM v2 계산값
4. 기존 socket type 기본값

perf runner에서 `--hwm`, `--send-hwm`, `--recv-hwm`을 주는 경우도 수동 설정으로
본다. 이 값은 외부 spot handle뿐 아니라 SpotNode 내부 데이터 소켓에도 전달되어야
한다.

## 12. Context budget이 권장값보다 작거나 클 때

권장 공식은 사용자가 처음 값을 고르는 가이드이다. 실제 설정값이 권장보다 작거나
커도 동작은 아래처럼 명확해야 한다.

- 권장값보다 작으면 HWM이 더 낮아지고, backpressure가 더 빨리 걸릴 수 있다.
- 권장값보다 크면 HWM이 어느 정도 커질 수 있지만, `size_cap`이 상한을 막는다.
- `throughput` profile을 명시하지 않는 한, 큰 context budget만으로 큰 메시지
  큐가 깊어지면 안 된다.

즉 context budget은 전체 안전 상한이고, HWM의 기본 성격은 role, count,
`MsgUnit(B)`가 정한다.

## 13. 변경되는 API와 enum

구현 시 공개 계약은 `core/include/zlink.h`와 `core/include/zlink_enum.h`를
기준으로 아래 항목을 추가하거나 갱신한다. 이 섹션에 없는 새 공개 API는
auto-HWM v2 첫 구현 범위에 넣지 않는다.

### 13.1 변경 요약

| 구분 | 이름 | 변경 내용 |
|---|---|---|
| 새 enum type | `zlink_auto_hwm_profile_t` | auto-HWM profile 값을 정의한다 |
| 새 enum value | `ZLINK_CTX_OPT_AUTO_HWM_PROFILE` | context profile option을 추가한다 |
| 새 macro | `ZLINK_CTX_AUTO_HWM_PROFILE_DFLT` | 기본 profile 값을 정의한다 |
| 기존 API | `zlink_ctx_set()` | 새 context option을 받을 수 있게 한다 |
| 기존 API | `zlink_ctx_get()` | 새 context option을 조회할 수 있게 한다 |
| 기존 struct | `zlink_monitor_snapshot_t` | auto-HWM v2 디버깅 필드를 추가한다 |

아래 항목은 추가하지 않는다.

| 이름 | 이유 |
|---|---|
| `ZLINK_CTX_OPT_AUTO_HWM_PUBLISH_FANOUT` | 기존 `ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP`을 fanout limit으로 재사용한다 |
| 새 SpotNode HWM option | 기존 `ZLINK_SPOT_NODE_OPT_*_HWM` 수동 override 계약을 유지한다 |
| 새 socket HWM option | 기존 `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_RCVHWM`, `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`를 유지한다 |

### 13.2 새 enum type

```c
typedef enum zlink_auto_hwm_profile_t
{
    ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY = 1,
    ZLINK_AUTO_HWM_PROFILE_BALANCED = 2,
    ZLINK_AUTO_HWM_PROFILE_THROUGHPUT = 3
} zlink_auto_hwm_profile_t;
```

이 enum은 `core/include/zlink_enum.h`에 추가한다.

| 값 | 의미 |
|---|---|
| `ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY` | latency를 우선하고 HWM을 낮게 잡는다 |
| `ZLINK_AUTO_HWM_PROFILE_BALANCED` | 기본 운영 profile이다 |
| `ZLINK_AUTO_HWM_PROFILE_THROUGHPUT` | queue 여유를 더 주고 throughput을 우선한다 |

### 13.3 새 context option enum value

```c
ZLINK_CTX_OPT_AUTO_HWM_PROFILE
```

- `zlink_ctx_option_t`의 다음 미사용 값에 추가한다. 현재 공개 헤더 기준으로는
  `ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP = 16` 뒤의 `17`을 사용한다.
- `zlink_ctx_set()`은 위 enum 값을 정수로 받는다.
- `zlink_ctx_get()`은 현재 profile 값을 같은 enum 값으로 돌려준다.
- 기본값은 `ZLINK_AUTO_HWM_PROFILE_BALANCED`이다.
- 알 수 없는 값은 `EINVAL`로 실패한다.

### 13.4 기본값 macro

```c
#define ZLINK_CTX_AUTO_HWM_PROFILE_DFLT ZLINK_AUTO_HWM_PROFILE_BALANCED
```

이 macro는 `core/include/zlink.h`의 context 기본값 영역에 추가한다.

### 13.5 기존 API 동작 변경

`zlink_ctx_set()`과 `zlink_ctx_get()`의 함수 시그니처는 바꾸지 않는다. 새 option
값을 기존 option dispatch에 추가한다.

```c
zlink_ctx_set(ctx, ZLINK_CTX_OPT_AUTO_HWM_PROFILE, profile_value);
zlink_ctx_get(ctx, ZLINK_CTX_OPT_AUTO_HWM_PROFILE);
```

`profile_value`는 `zlink_auto_hwm_profile_t` 값이어야 한다. 다른 값은 `EINVAL`로
실패한다. profile이 바뀌면 context 전체 auto-HWM 재계산을 예약한다.

### 13.6 monitor snapshot 갱신

정식 구현에서는 auto-HWM 결과를 디버깅할 수 있도록 monitor snapshot에 아래 값을
추가한다.

| 필드 | 의미 |
|---|---|
| `auto_hwm_profile` | 적용된 profile |
| `auto_hwm_policy_class` | 내부 planner가 사용한 policy class |
| `auto_hwm_unit_budget_bytes` | profile과 class로 정해진 per-connection budget |
| `auto_hwm_size_cap` | `MsgUnit(B)`와 profile로 정해진 HWM 상한 |
| `auto_hwm_effective_publish_fanout` | SPOT fanout 계산에 사용한 최종 count |

이 필드들은 기존 `auto_hwm_socket_queue_share_bytes`,
`auto_hwm_effective_message_bytes`, `auto_hwm_socket_message_slots`와 함께 보면
최종 HWM이 왜 그 값이 되었는지 추적할 수 있어야 한다.

## 14. 구현 체크리스트

- [ ] context budget 문서와 기본값을 `balanced` 기준으로 정리한다.
- [ ] `zlink_auto_hwm_profile_t`와 `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`을 공개 헤더에
  추가한다.
- [ ] auto-HWM 계산에서 context memory를 전체 cap으로만 사용한다.
- [ ] auto-HWM planner에 내부 `auto_hwm_policy_class`를 추가한다.
- [ ] PUB, SUB, ROUTER, STREAM 등 일반 소켓 role mapping을 정리한다.
- [ ] SpotNode 내부 소켓별 role과 count 기준을 분리한다.
- [ ] SpotNode effective publish fanout은 `ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP`
  기반으로 계산한다.
- [ ] profile별 `MsgUnit(B)` 구간 cap을 추가한다.
- [ ] 단일 socket refresh 경로가 context 전체 재계산을 우회하지 않게 한다.
- [ ] monitor snapshot에 profile, policy class, unit budget, size cap,
  effective publish fanout을 추가한다.
- [ ] 수동 HWM 설정 방향은 auto 재계산이 덮어쓰지 않게 한다.
- [ ] perf runner의 SpotNode 수동 HWM 전달 경로를 회귀 테스트에 넣는다.
- [ ] one-way large message fanout perf에서 latency 해석을 throughput 모드와
  latency 모드로 분리한다.
- [ ] `core/include` 또는 `core/src` 변경 뒤 `cmake --build core/build`로 runtime을
  다시 만든다.
- [ ] `bindings/c/perf`가 `core/build`의 `libzlink`를 쓰는지 확인한다.
- [ ] 공개 헤더 변경을 각 binding의 native header/library에 동기화한다.
- [ ] profile 옵션과 monitor snapshot 필드를 각 binding에 반영한다.
- [ ] 구현 완료 뒤 정식 `doc/spec`, `doc/guide`, `doc/internals`,
  `doc/spec/bindings/언어별/`에 나누어 반영한다.

## 15. 회귀 테스트 항목

구현 완료 조건에는 아래 회귀 테스트를 포함한다. core나 binding 구현을 바꾼 뒤에는
각 항목을 실제 명령으로 확인하고, 실패하면 원인을 고친 뒤 같은 항목을 다시 실행한다.

### 15.1 core build와 단위 테스트

- [ ] `cmake --build core/build`
- [ ] core 전체 테스트
- [ ] `zlink_ctx_set()` / `zlink_ctx_get()`의
  `ZLINK_CTX_OPT_AUTO_HWM_PROFILE` 기본값, 설정값, `EINVAL` 실패 경로
- [ ] `ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB`가 작을 때 HWM이
  `size_cap` 아래에서 낮아지는 경로
- [ ] `ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB`가 클 때 HWM이
  `size_cap`을 넘지 않는 경로
- [ ] `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`가 `MsgUnit(B)` cap 선택에 반영되는 경로

### 15.2 auto-HWM planner 회귀 테스트

- [ ] `low_latency`, `balanced`, `throughput` profile별 unit budget 선택
- [ ] `fanout`, `spot_data`, `routed`, `peer_queue`, `stream`, `recv_ingress`,
  `control` policy class mapping
- [ ] `MsgUnit(B)` 구간별 `size_cap` 선택
- [ ] `DEALER`가 `peer_queue` cap을 사용하는 경로
- [ ] `STREAM`이 `stream` class budget과 cap을 사용하는 경로
- [ ] 단일 socket refresh가 context 전체 auto-HWM 재계산을 예약하는 경로
- [ ] 수동 `SNDHWM` 또는 `RCVHWM` 설정 방향을 auto-HWM이 덮어쓰지 않는 경로
- [ ] 낮아진 HWM 적용이 pending queue 때문에 지연되는 deferred HWM 경로

### 15.3 SpotNode 회귀 테스트

- [ ] SpotNode 내부 `local-pub`, `mesh-pub`, `ingress-sub`, `mesh-xsub`,
  `internal-router`, `external-router`, `peer_ctrl_pub/sub` policy class mapping
- [ ] `effective_publish_fanout =
  min(max(local_sub_spot_count, active_peer_count, observed_scope_count),
  ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP)` 계산 경로
- [ ] `total_spot_count`가 fanout queue budget이 아니라 metadata budget에만
  반영되는 경로
- [ ] `ZLINK_SPOT_NODE_OPT_PUB_HWM`, `ZLINK_SPOT_NODE_OPT_SUB_HWM`,
  `ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM`,
  `ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM` 수동 override 우선순위
- [ ] SpotNode monitor snapshot에서 profile, policy class, unit budget, size cap,
  effective publish fanout을 확인하는 경로

### 15.4 C sample과 perf smoke

- [ ] `bindings/c/samples` smoke
- [ ] `bindings/c/perf` runner가 실행 전에 실제 `core/build/lib/libzlink.so` 경로를
  출력하는지 확인
- [ ] `bindings/c/perf` runner가 `core/build` runtime이 source보다 오래되면
  실패하는지 확인
- [ ] `bindings/c/perf` single 전체 패턴 smoke
- [ ] `bindings/c/perf` multi 전체 패턴 smoke
- [ ] `MULTI_PUBSUB` 256 KiB, balanced profile에서 HWM이 8 근처 cap으로 제한되는지
  확인
- [ ] `MULTI_SPOT` 256 KiB, balanced profile에서 HWM cap 8과 5 ms 이상 latency
  interval 가이드가 적용되는지 확인
- [ ] `MULTI_DEALER_DEALER` 64 KiB에서 `peer_queue` cap이 적용되는지 확인
- [ ] `MULTI_STREAM` 1000, 5000, 10000 clients smoke
- [ ] `MULTI_SPOT` publish targets 500 smoke

### 15.5 binding 회귀 테스트

- [ ] 각 binding native header/library가 최신 `core/include`와 `core/build`
  runtime 기준으로 동기화되었는지 확인
- [ ] 각 binding에서 auto-HWM profile enum과 context option을 설정하고 조회하는
  테스트
- [ ] 각 binding에서 monitor snapshot의 새 auto-HWM 필드를 읽는 테스트
- [ ] 각 binding 전체 테스트
- [ ] sample 디렉터리가 있는 binding은 sample smoke
- [ ] perf 디렉터리가 있는 binding은 실제 core runtime을 사용하는 perf smoke
