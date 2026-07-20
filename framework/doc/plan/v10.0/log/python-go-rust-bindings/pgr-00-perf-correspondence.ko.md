# PGR-00 C perf 대응표

## 1. 읽는 방법

이 표는 2026-07-20 작업 snapshot의 runner와 case source를 연결한다. `축약`은 source 하나가 C의 서로 다른
측정 의미를 분리하지 않은 상태, `누락`은 해당 case source와 runner 선택값이 없는 상태다. 각 언어 lane은
자기 행의 차이를 고치고 C와 같은 process 역할, phase, handshake, timestamp와 결과 확정 지점을 검토한다.

## 2. Single

C 기준 source root는 `bindings/c/perf/single/src/`, 공통 phase·측정 helper는
`bindings/c/perf/single/common/`이다.

| C pattern/source | Python | Go | Rust | 현재 판정 |
|---|---|---|---|---|
| `PAIR` / `perf_pair.cpp` | `single/perf_pair.py` | `single/perf_pair.go` | `single/src/perf_pair.rs` | source 대응 |
| `PUBSUB` / `perf_pubsub.cpp` | `single/perf_pubsub.py` | `single/perf_pubsub.go` | `single/src/perf_pubsub.rs` | source 대응 |
| `DEALER_DEALER` / `perf_dealer_dealer.cpp` | `single/perf_dealer_dealer.py` | `single/perf_dealer_dealer.go` | `single/src/perf_dealer_dealer.rs` | source 대응 |
| `DEALER_ROUTER` / `perf_dealer_router.cpp` | `single/perf_dealer_router.py` | `single/perf_dealer_router.go` | `single/src/perf_dealer_router.rs` | source 대응 |
| `DEALER_ROUTER_REQREP` / `perf_dealer_router_reqrep.cpp` | 누락 | 누락 | 누락 | red |
| `ROUTER_ROUTER` / `perf_router_router.cpp` | `single/perf_router_router.py` | `single/perf_router_router.go` | `single/src/perf_router_router.rs` | source 대응 |
| `ROUTER_ROUTER_REQREP` / `perf_router_router_reqrep.cpp` | 누락 | 누락 | 누락 | red |
| `SPOT_PUBSUB` / `perf_spot_pubsub.cpp` | `single/perf_spot.py`의 `SPOT` | `single/perf_spot.go`의 `SPOT` | `single/src/perf_spot.rs`의 `SPOT` | 이름·공개 API 전환 red |

세 runner는 C의 두 req/rep pattern을 선택할 수 없으며 `SPOT_PUBSUB`도 `SPOT`으로 축약한다. 따라서 현재
single 대응 차이는 언어마다 3건이다.

## 3. Multi

C 기준 case는 `bindings/c/perf/multi/src/perf_multi_*`와 같은 디렉터리의 client/server helper다.
Python root는 `bindings/python/perf/multi/`, Go root는 `bindings/go/perf/multi/`, Rust root는
`bindings/rust/perf/multi/src/`다.

| C pattern | Python source | Go source | Rust source | 현재 판정 |
|---|---|---|---|---|
| `DEALER_DEALER` | `perf_multi_dealer_dealer_{client,server}.py` | `perf_multi_dealer_dealer.go` | `perf_multi_dealer_dealer_{client,server}.rs` | source 대응 |
| `DEALER_ROUTER_SENDSEND` | `perf_multi_dealer_router_{client,server}.py` | `perf_multi_dealer_router.go` | `perf_multi_dealer_router_{client,server}.rs` | `DEALER_ROUTER`로 축약 |
| `ROUTER_ROUTER_SENDSEND` | `perf_multi_router_router_{client,server}.py` | `perf_multi_router_router.go` | `perf_multi_router_router_{client,server}.rs` | `ROUTER_ROUTER`로 축약 |
| `DEALER_ROUTER_REQREP` | 누락 | 누락 | 누락 | red |
| `ROUTER_ROUTER_REQREP` | 누락 | 누락 | 누락 | red |
| `ROUTER_ROUTER_ONEWAY` | 누락 | 누락 | 누락 | red |
| `PUBSUB` | `perf_multi_pubsub_{client,server}.py` | `perf_multi_pubsub.go` | `perf_multi_pubsub_{client,server}.rs` | source 대응 |
| `SPOT_PUBSUB` | `perf_multi_spot_{client,server}.py` | `perf_multi_spot.go` | `perf_multi_spot_{client,server}.rs` | `SPOT`으로 축약·공개 API 전환 red |
| `SPOT_REQREP` | `perf_multi_spot_reqrep_{client,server}.py` | `perf_multi_spot_reqrep.go` | `perf_multi_spot_reqrep_{client,server}.rs` | 공개 API 전환 red |
| `SPOT_SENDSEND` | `perf_multi_spot_sendsend_{client,server}.py` | `perf_multi_spot_sendsend.go` | `perf_multi_spot_sendsend_{client,server}.rs` | 공개 API 전환 red |
| `STREAM` | shared C client + `perf_multi_stream_server.py` | `perf_multi_stream.go` | shared C client + `perf_multi_stream_server.rs` | shared client 정책 확인 필요 |

세 runner는 C의 send/send, req/rep와 one-way 축을 같은 이름으로 모두 구분하지 않는다. 현재 multi 대응
차이는 언어마다 축약 3건, 누락 3건이며 Spot 3건은 `SpotNode` 제거 뒤 MeshNode pull dispatch로 바꿔야 한다.

## 4. 의미 대조 기준

각 pattern의 source가 생기거나 이름만 바뀌었다고 완료하지 않는다. 다음 항목을 C source와 언어 source의
구체 위치로 연결해 차이 0개를 확인한다.

| 항목 | C 기준 위치 | 언어별 확인 위치 |
|---|---|---|
| process 역할 | multi client/server source와 runner spawn 분기 | 각 언어 runner의 case command와 client/server source |
| ready·active phase | `perf_single_phase.hpp`, multi control helper | 각 언어 common/control helper와 case 진입점 |
| handshake·stop token | single pattern source, multi control frame | 각 언어 case의 ready/stop 송수신 지점 |
| timestamp·유효 수신 | `perf_single_latency.hpp`, `perf_multi_metrics.hpp` | 각 언어 metric helper와 수신 loop |
| throughput·latency 확정 | C case의 마지막 `RESULT` 입력 | 각 언어 case의 metric snapshot과 `RESULT` 출력 직전 |
| runner 책임 | C `run_benchmarks*.sh` | 언어별 `run_benchmarks*.sh` 또는 Python runner |

언어 lane review에는 위 여섯 항목의 source line을 채운 새 snapshot 표를 남긴다. 여기의 현재 표는 구현 전
red inventory이며 smoke나 성능 판정 증거가 아니다.
