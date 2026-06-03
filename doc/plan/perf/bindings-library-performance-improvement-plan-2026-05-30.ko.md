# bindings 라이브러리 성능 재측정 라운드 (2026-05-30)

> 이 문서는 [`bindings-library-performance-improvement-plan.ko.md`](bindings-library-performance-improvement-plan.ko.md)
> 의 측정 상태표를 처음부터 다시 채우기 위한 워크북이다. 정책, 목표 비율, 판정 기준,
> public API 가이드라인은 원본 계획 문서를 그대로 따른다. 결과는 매 측정 라운드마다 같은
> 칸에 상태와 C 대비 비율을 함께 적는다.

## 0. 표기와 범위 규칙

상태 칸에는 `미측정`, `통과(비율%)`, `미달(비율%)`, `해당 없음` 네 가지
형식만 쓴다. 판정 기준은 원본 계획 문서의 §1, §2, §3, §4, §5를 그대로 따른다.

- Single suite size set: `64, 256, 1024, 65536, 131072, 262144`
- Multi suite size set: `64, 256, 1024, 4096, 65536, 131072` (기존 `262144`는 새 판정에 쓰지 않는다)
- `MULTI_STREAM`의 측정 대상 size는 `64, 256, 1024, 65536`이고, `4096`, `131072`는 `해당 없음`이다.
- C++/.NET single suite의 `inproc | SPOT`, `ipc | SPOT`은 full single에서 결과가 없으므로 `해당 없음`이다.
- Java, Node, Go, Rust, Python single suite는 `tcp, ws, wss, tls` transport만 다룬다.
- Multi suite는 모든 언어에서 `tcp, ws, wss, tls` transport만 다룬다.

각 행 마지막의 `결과 파일 / 메모` 칸에는 같은 조건의 C 기준 결과 파일과 대상 binding
결과 파일, 그리고 필요한 근거를 적는다. 같은 transport 안에서 size별로 결과 파일이
다르면 행을 size별로 쪼개도 되지만, 쪼갠 뒤에도 transport/pattern/size 조합이 빠지면
안 된다.

## 1. 언어 진행 상태

| 순서 | 언어 | perf 경로 | Single 상태 | Multi 상태 | 다음 작업 |
|---|---|---|---|---|---|
| 1 | C++ | `bindings/cpp/perf` | `미달 없음` | `미달 없음` | Multi는 full+제한 재측정으로 통과. Single은 1개 잔여 항목을 제한 재측정과 수정 실험으로 확인한 뒤 미달로 정리했다. |
| 2 | .NET | `bindings/dotnet/perf` | `미달 없음` | `미달 없음` | Single은 routed active recv 정렬로 `ROUTER_ROUTER inproc 262144B`를 통과로 올렸고, 나머지 7개는 public API 기준에서 추가 개선 후보가 확인되지 않아 미달했다. Multi는 full+4096B 보강+제한 재측정 뒤 잔여 11개를 미달했다. |
| 3 | Java | `bindings/java/perf` | `미달 없음` | `미달 없음` | Single은 SPOT 대용량 9개를 재검토하고 wrapper 재사용 실험까지 확인한 뒤 미달했다. Multi는 `MULTI_DEALER_DEALER ws 131072B`와 `MULTI_SPOT wss` 5개를 미달했다. |
| 4 | Node | `bindings/node/perf` | `미달 6/144 (4.2%)` | `미달 21/156 (13.5%)` | Multi가 10% gate를 초과하지만, fastpath 변경, single routed 단일 payload native 수신, `sendToSpot` DontWait result-code 경로, `MULTI_PUBSUB` 단일 payload 내부 수신 경로를 적용했고, 추가 public contract-safe 후보는 log에 기각 근거를 남겼다. 평균 성능 비율을 계산한 뒤 다음 언어로 넘어간다. |
| 5 | Go | `bindings/go/perf` | `미달 6/144 (4.2%)` | `미달 20/192 (10.4%)` | `MULTI_SPOT_REQREP tcp 4096B`는 request message 누수 수정으로 통과했고, 같은 complete 검증에서 `ws 131072B`도 통과로 회복했다. 잔여 routed echo, one-way 64B, builder hot path 후보를 추가로 시험했지만 새 통과를 만들지 못해 Go는 추가 후보 소진으로 정리하고 Rust로 넘어간다. |
| 6 | Rust | `bindings/rust/perf` | `미달 25/144 (17.4%)` | `미달 11/192 (5.7%)` | Single 공통 송신 loop를 public `Message::with_size(...).data_mut()` 직접 작성으로 바꿔 `PUBSUB wss 64B`를 통과로 올렸다. 이후 table transport full 확인에서 routed 대용량과 `SPOT tcp/ws/tls 1024B`는 기준에 못 닿았고, public recv envelope를 우회하지 않는 추가 후보는 log에 기각 근거를 남겼다. |
| 7 | Python | `bindings/python/perf` | `미측정` | `미측정` | 2026-06-03에 public socket contract와 perf 의미를 복구하면서 perf script의 private native active-loop 직접 호출을 제거했다. `PAIR tcp 64B` public API probe는 C 대비 23.5%로 one-way 최소 기준 30%에 못 닿았다. 아래 Python 표의 이전 통과 수치는 현재 코드 기준 판정으로 쓰지 않고, public Python API 경로로 다시 측정해야 한다. |

### 1.1 언어별 평균 성능

아래 지표는 현재 측정값이 있는 C++, .NET, Java, Node, Go, Rust, Python을 계산한다.
각 언어의 Single/Multi 상태표에서 `통과(비율%)`, `미달(비율%)` 형식의
측정 셀을 모두 모아 C 대비 throughput 비율을 계산한다. `해당 없음`과 `미측정`은
제외한다.

단순 평균은 높은 outlier에 쉽게 끌려간다. 그래서 중앙값, p10, 최저 10% 평균을 함께 본다.
p10은 하위 10% 경계값이고, 최저 10% 평균은 가장 느린 구간의 체감 위험을 보기 위한
보조 지표다.

| 언어 | 측정 셀 수 | 평균 | 중앙값 | p10 | 최저 10% 평균 | Single 평균 | Multi 평균 |
|---|---:|---:|---:|---:|---:|---:|---:|
| C++ | 389 | 119.6% | 99.6% | 91.5% | 87.1% | 136.4% | 101.1% |
| .NET | 388 | 87.6% | 92.8% | 60.8% | 49.4% | 93.3% | 81.3% |
| Java | 328 | 92.1% | 89.2% | 59.2% | 47.9% | 103.9% | 82.9% |
| Node | 300 | 60.9% | 41.7% | 28.0% | 23.0% | 70.5% | 52.1% |
| Go | 335 | 73.8% | 68.2% | 45.0% | 38.2% | 81.8% | 67.7% |
| Rust | 336 | 94.8% | 95.5% | 76.3% | 48.2% | 95.4% | 94.4% |
| Python | 328 | 80.5% | 82.2% | 46.5% | 38.3% | 85.4% | 76.7% |

Python 행은 2026-06-03 public contract 복구 전의 과거 측정값이다. 현재 코드의
성능 판정에는 쓰지 않고, public Python API 경로로 다시 측정한 complete report가
나온 뒤 새 값으로 교체한다.


## 2. 기준 파일 메모

새 라운드에서 확보한 C 기준 파일은 아래 표에 기록한다. 같은 라운드 안에서는 동일
기준 파일을 모든 언어 비교에 재사용한다.


| 항목 | 기준 파일 | 비고 |
|---|---|---|
| C single full | `perf_c_single_linux_20260530_231803_round_20260530_c_single_baseline.txt` | core 6.0.4, default sizes. status=complete (1020/1020). 22m 15s. baseline 사본 보관됨. |
| C multi full | `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt` | core 6.0.4, msg-sizes 64,256,1024,4096,65536,131072. status=complete (960/960, 192/192 success). 34m 5s. baseline 사본 보관됨. |
| C single 제한 재측정 | `perf_c_single_linux_20260531_083553_round_20260530_c_single_dr_inproc_large_recheck.txt`, `perf_c_single_linux_20260531_083624_round_20260530_c_single_rr_inproc_large_recheck.txt`, `perf_c_single_linux_20260531_084303_round_20260530_c_single_rr_inproc_262144_runs3.txt`, `perf_c_single_linux_20260531_084343_round_20260530_c_single_dr_inproc_131072_runs3.txt`, `perf_c_single_linux_20260531_085029_round_20260530_c_single_dr_inproc_131072_reconfirm.txt`, `perf_c_single_linux_20260531_104549_round_20260530_c_single_java_spot_large_recheck.txt`, `perf_c_single_linux_20260531_135902_round_20260530_c_single_go_failset_recheck.txt` | C++ single inproc routed large 미달, Java single SPOT 대용량 미달, Go single failset 확인용 제한 재측정. |
| C multi 제한 재측정 | `perf_c_multi_linux_20260531_083459_round_20260530_c_multi_pubsub_tls_65536_recheck.txt`, `perf_c_multi_linux_20260531_100611_round_20260530_c_multi_dotnet_routed_failset_recheck.txt`, `perf_c_multi_linux_20260531_101006_round_20260530_c_multi_dotnet_spot_failset_recheck.txt`, `perf_c_multi_linux_20260531_112539_round_20260530_c_multi_java_failset_recheck.txt` | C++ multi `MULTI_PUBSUB tls 65536B` 측정 오차, .NET multi 잔류 미달, Java multi failset 확인용 제한 재측정. |


## 3. C++ 상태

perf 경로: `bindings/cpp/perf`. 기준 파일과 보강 파일은 측정 시점에 결과 파일/메모 칸에 직접 적는다.


### 3.1 Single suite

2026-05-30 round v4 결과. C 기준 파일 `perf_c_single_linux_20260530_231803_round_20260530_c_single_baseline.txt`,
C++ 결과 파일 `perf_cpp_single_linux_20260531_072511_round_20260530_cpp_single_full_v4_pool.txt`.
주요 binding 개선: send_operation_t/request/reply chain의 중복 heap alloc 제거 (private unique_ptr ctor 추가),
spot_operation_state_t thread-local pool로 per-send alloc 제거, recv_part_out_guard_t의 empty-msg 빠른 경로.
제한 재측정 보강 파일: C `perf_c_single_linux_20260531_083553_round_20260530_c_single_dr_inproc_large_recheck.txt`,
C++ `perf_cpp_single_linux_20260531_083607_round_20260530_cpp_single_dr_inproc_large_recheck.txt`,
C `perf_c_single_linux_20260531_083624_round_20260530_c_single_rr_inproc_large_recheck.txt`,
C++ `perf_cpp_single_linux_20260531_083633_round_20260530_cpp_single_rr_inproc_large_recheck.txt`,
C `perf_c_single_linux_20260531_084303_round_20260530_c_single_rr_inproc_262144_runs3.txt`,
C++ `perf_cpp_single_linux_20260531_084322_round_20260530_cpp_single_rr_inproc_262144_runs3.txt`,
C `perf_c_single_linux_20260531_084343_round_20260530_c_single_dr_inproc_131072_runs3.txt`,
C++ `perf_cpp_single_linux_20260531_084403_round_20260530_cpp_single_dr_inproc_131072_runs3.txt`,
C `perf_c_single_linux_20260531_085029_round_20260530_c_single_dr_inproc_131072_reconfirm.txt`,
C++ `perf_cpp_single_linux_20260531_085235_round_20260530_cpp_single_dr_inproc_131072_final_residual.txt`.
미달 cell (1개): `DEALER_ROUTER inproc 131072B`는 runs=3 제한 재측정 median에서도
65.1%로 최소 기준 70%에 못 닿았다. `DEALER_ROUTER inproc 262144B`는 제한 재측정에서
172.9%로 회복됐고, `ROUTER_ROUTER inproc 262144B`는 단발 제한 재측정에서는 40.5%였지만
runs=3 median에서 88.7%로 회복되어 측정 변동으로 판정했다.
추가 원인 확인으로 C++ perf adapter를 거치지 않고 public `dealer_socket_t`/`router_socket_t`를
직접 쓰는 실험과, routed recv의 RID assign 비용을 줄이는 실험을 각각 수행했지만
`perf_cpp_single_linux_20260531_084842_round_20260530_cpp_single_dr_inproc_131072_direct_socket_recheck.txt`,
`perf_cpp_single_linux_20260531_085121_round_20260530_cpp_single_dr_inproc_131072_rid_assign_recheck.txt`
모두 개선을 만들지 못했다. 두 실험 변경은 최종 코드에 남기지 않았다. 남은 차이는
inproc routed 대용량 단일 조건에서만 반복되는 국소 항목이다. 현재 perf driver는
이미 public `router_socket_t::recv(routing_id_t&, message_t&, ...)` 단일 part 경로를
사용하고 있어 `received_t` materialize 비용도 피한다. RID assign 실험은 median
232.52K msg/s로 최종 잔류 측정 224.33K msg/s와 같은 변동 범위였고, C 기준
344.75K msg/s의 70% 기준에는 못 닿았다. public API를 유지한 상태에서 더 줄일 수 있는
C++ binding 내부 후보가 확인되지 않아 미달로 둔다.

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `통과(98.5%)` | `통과(100.5%)` | `통과(95.1%)` | `통과(99.7%)` | `통과(99.2%)` | `통과(99.1%)` | C full/C++ full. |
| `tcp` | `PUBSUB` | `통과(107.3%)` | `통과(117.7%)` | `통과(132.2%)` | `통과(510.3%)` | `통과(578.5%)` | `통과(622.9%)` | C full/C++ full. |
| `tcp` | `DEALER_DEALER` | `통과(100.2%)` | `통과(99.4%)` | `통과(99.7%)` | `통과(99.0%)` | `통과(99.0%)` | `통과(101.2%)` | C full/C++ full. |
| `tcp` | `DEALER_ROUTER` | `통과(89.9%)` | `통과(89.2%)` | `통과(100.7%)` | `통과(99.1%)` | `통과(97.8%)` | `통과(96.0%)` | C full/C++ full. |
| `tcp` | `ROUTER_ROUTER` | `통과(97.9%)` | `통과(97.7%)` | `통과(101.8%)` | `통과(100.9%)` | `통과(102.9%)` | `통과(99.0%)` | C full/C++ full. |
| `tcp` | `SPOT` | `통과(105.5%)` | `통과(102.0%)` | `통과(98.4%)` | `통과(88.0%)` | `통과(85.5%)` | `통과(98.0%)` | C full/C++ full. |
| `ws` | `PAIR` | `통과(100.6%)` | `통과(99.7%)` | `통과(93.8%)` | `통과(99.0%)` | `통과(98.8%)` | `통과(99.9%)` | C full/C++ full. |
| `ws` | `PUBSUB` | `통과(97.3%)` | `통과(103.5%)` | `통과(117.0%)` | `통과(236.1%)` | `통과(303.3%)` | `통과(448.7%)` | C full/C++ full. |
| `ws` | `DEALER_DEALER` | `통과(99.3%)` | `통과(99.1%)` | `통과(89.9%)` | `통과(100.1%)` | `통과(100.6%)` | `통과(99.7%)` | C full/C++ full. |
| `ws` | `DEALER_ROUTER` | `통과(96.5%)` | `통과(93.7%)` | `통과(96.4%)` | `통과(95.2%)` | `통과(98.7%)` | `통과(98.7%)` | C full/C++ full. |
| `ws` | `ROUTER_ROUTER` | `통과(112.5%)` | `통과(98.6%)` | `통과(96.2%)` | `통과(103.4%)` | `통과(101.0%)` | `통과(103.1%)` | C full/C++ full. |
| `ws` | `SPOT` | `통과(99.6%)` | `통과(101.3%)` | `통과(101.7%)` | `통과(91.9%)` | `통과(103.9%)` | `통과(98.2%)` | C full/C++ full. |
| `wss` | `PAIR` | `통과(100.1%)` | `통과(99.1%)` | `통과(99.0%)` | `통과(97.7%)` | `통과(92.3%)` | `통과(110.0%)` | C full/C++ full. |
| `wss` | `PUBSUB` | `통과(108.0%)` | `통과(104.7%)` | `통과(125.5%)` | `통과(110.7%)` | `통과(91.2%)` | `통과(115.7%)` | C full/C++ full. |
| `wss` | `DEALER_DEALER` | `통과(99.1%)` | `통과(98.8%)` | `통과(96.2%)` | `통과(100.0%)` | `통과(101.0%)` | `통과(87.1%)` | C full/C++ full. |
| `wss` | `DEALER_ROUTER` | `통과(93.2%)` | `통과(94.3%)` | `통과(96.0%)` | `통과(95.0%)` | `통과(97.8%)` | `통과(125.0%)` | C full/C++ full. |
| `wss` | `ROUTER_ROUTER` | `통과(115.7%)` | `통과(99.8%)` | `통과(100.7%)` | `통과(98.8%)` | `통과(95.9%)` | `통과(114.5%)` | C full/C++ full. |
| `wss` | `SPOT` | `통과(103.7%)` | `통과(101.3%)` | `통과(99.2%)` | `통과(101.4%)` | `통과(105.0%)` | `통과(427.3%)` | C full/C++ full. |
| `tls` | `PAIR` | `통과(99.7%)` | `통과(100.6%)` | `통과(99.8%)` | `통과(98.2%)` | `통과(97.8%)` | `통과(98.8%)` | C full/C++ full. |
| `tls` | `PUBSUB` | `통과(109.9%)` | `통과(109.1%)` | `통과(125.5%)` | `통과(122.0%)` | `통과(129.9%)` | `통과(134.3%)` | C full/C++ full. |
| `tls` | `DEALER_DEALER` | `통과(99.5%)` | `통과(99.1%)` | `통과(95.0%)` | `통과(97.3%)` | `통과(100.6%)` | `통과(100.9%)` | C full/C++ full. |
| `tls` | `DEALER_ROUTER` | `통과(95.1%)` | `통과(98.6%)` | `통과(96.7%)` | `통과(96.5%)` | `통과(98.2%)` | `통과(99.1%)` | C full/C++ full. |
| `tls` | `ROUTER_ROUTER` | `통과(110.5%)` | `통과(111.5%)` | `통과(94.5%)` | `통과(96.5%)` | `통과(98.6%)` | `통과(100.9%)` | C full/C++ full. |
| `tls` | `SPOT` | `통과(100.1%)` | `통과(105.5%)` | `통과(96.8%)` | `통과(98.8%)` | `통과(102.8%)` | `통과(99.0%)` | C full/C++ full. |
| `inproc` | `PAIR` | `통과(101.2%)` | `통과(88.9%)` | `통과(95.2%)` | `통과(98.9%)` | `통과(99.6%)` | `통과(100.8%)` | C full/C++ full. |
| `inproc` | `PUBSUB` | `통과(107.7%)` | `통과(109.3%)` | `통과(117.2%)` | `통과(1178.9%)` | `통과(1098.4%)` | `통과(1693.8%)` | C full/C++ full. |
| `inproc` | `DEALER_DEALER` | `통과(98.5%)` | `통과(111.9%)` | `통과(102.3%)` | `통과(100.6%)` | `통과(99.7%)` | `통과(99.4%)` | C full/C++ full. |
| `inproc` | `DEALER_ROUTER` | `통과(96.4%)` | `통과(99.1%)` | `통과(95.3%)` | `통과(84.0%)` | `미달(65.1%)` | `통과(172.9%)` | 131072B는 최종 제한 재측정 C `perf_c_single_linux_20260531_085029_round_20260530_c_single_dr_inproc_131072_reconfirm.txt`, C++ `perf_cpp_single_linux_20260531_085235_round_20260530_cpp_single_dr_inproc_131072_final_residual.txt` 기준. perf adapter 직접화와 RID assign 실험은 개선을 만들지 못해 최종 코드에 남기지 않았다. public API를 유지한 상태에서 더 줄일 내부 후보가 확인되지 않아 미달한다. 262144B는 제한 재측정 C `perf_c_single_linux_20260531_083553_round_20260530_c_single_dr_inproc_large_recheck.txt`, C++ `perf_cpp_single_linux_20260531_083607_round_20260530_cpp_single_dr_inproc_large_recheck.txt` 기준. |
| `inproc` | `ROUTER_ROUTER` | `통과(105.5%)` | `통과(97.4%)` | `통과(100.9%)` | `통과(88.5%)` | `통과(77.6%)` | `통과(88.7%)` | 262144B는 runs=3 제한 재측정 C `perf_c_single_linux_20260531_084303_round_20260530_c_single_rr_inproc_262144_runs3.txt`, C++ `perf_cpp_single_linux_20260531_084322_round_20260530_cpp_single_rr_inproc_262144_runs3.txt` 기준. |
| `inproc` | `SPOT` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | full single에서 SPOT inproc 조합은 결과가 없다. |
| `ipc` | `PAIR` | `통과(99.3%)` | `통과(98.6%)` | `통과(92.8%)` | `통과(98.5%)` | `통과(101.1%)` | `통과(100.0%)` | C full/C++ full. |
| `ipc` | `PUBSUB` | `통과(108.4%)` | `통과(114.0%)` | `통과(122.3%)` | `통과(490.7%)` | `통과(360.2%)` | `통과(593.5%)` | C full/C++ full. |
| `ipc` | `DEALER_DEALER` | `통과(98.7%)` | `통과(98.5%)` | `통과(99.2%)` | `통과(100.0%)` | `통과(99.4%)` | `통과(99.2%)` | C full/C++ full. |
| `ipc` | `DEALER_ROUTER` | `통과(90.8%)` | `통과(95.4%)` | `통과(95.0%)` | `통과(100.0%)` | `통과(87.2%)` | `통과(96.7%)` | C full/C++ full. |
| `ipc` | `ROUTER_ROUTER` | `통과(102.0%)` | `통과(102.0%)` | `통과(96.3%)` | `통과(97.9%)` | `통과(100.1%)` | `통과(102.4%)` | C full/C++ full. |
| `ipc` | `SPOT` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | full single에서 SPOT ipc 조합은 결과가 없다. |

### 3.2 Multi suite

2026-05-31 round v1 결과. C 기준 파일 `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`,
C++ 결과 파일 `perf_cpp_multi_linux_20260531_075935_round_20260530_cpp_multi_full_v1_pool.txt`.
두 파일 모두 status=complete, 결과 라인 960/960이다. Full 비교에서 `MULTI_PUBSUB tls 65536B`만
71.4%로 낮았으나, 같은 조건 제한 재측정 C `perf_c_multi_linux_20260531_083459_round_20260530_c_multi_pubsub_tls_65536_recheck.txt`,
C++ `perf_cpp_multi_linux_20260531_083512_round_20260530_cpp_multi_pubsub_tls_65536_recheck.txt`가
status=complete로 끝났고 C 대비 84.7%라 통과로 보강했다.

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `통과(88.7%)` | `통과(106.2%)` | `통과(106.8%)` | `통과(109.7%)` | `통과(100.4%)` | `통과(98.9%)` | C full/C++ full. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(96.9%)` | `통과(99.5%)` | `통과(101.6%)` | `통과(100.4%)` | `통과(93.5%)` | `통과(102.8%)` | C full/C++ full. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(92.0%)` | `통과(88.8%)` | `통과(90.0%)` | `통과(90.1%)` | `통과(89.3%)` | `통과(97.9%)` | C full/C++ full. |
| `tcp` | `MULTI_PUBSUB` | `통과(81.0%)` | `통과(88.2%)` | `통과(116.9%)` | `통과(83.5%)` | `통과(85.0%)` | `통과(130.5%)` | C full/C++ full. |
| `tcp` | `MULTI_SPOT` | `통과(108.0%)` | `통과(97.3%)` | `통과(95.9%)` | `통과(105.3%)` | `통과(106.3%)` | `통과(96.5%)` | C full/C++ full. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(94.2%)` | `통과(93.8%)` | `통과(94.9%)` | `통과(92.9%)` | `통과(102.1%)` | `통과(117.6%)` | C full/C++ full. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(108.4%)` | `통과(101.9%)` | `통과(116.9%)` | `통과(117.0%)` | `통과(100.5%)` | `통과(118.4%)` | C full/C++ full. |
| `tcp` | `MULTI_STREAM` | `통과(118.0%)` | `통과(112.9%)` | `통과(115.5%)` | `해당 없음` | `통과(93.0%)` | `해당 없음` | C full/C++ full. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(94.9%)` | `통과(108.0%)` | `통과(103.8%)` | `통과(105.8%)` | `통과(102.5%)` | `통과(104.6%)` | C full/C++ full. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(93.1%)` | `통과(90.3%)` | `통과(95.9%)` | `통과(92.9%)` | `통과(91.3%)` | `통과(107.7%)` | C full/C++ full. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(90.8%)` | `통과(98.0%)` | `통과(90.6%)` | `통과(92.7%)` | `통과(75.3%)` | `통과(118.1%)` | C full/C++ full. |
| `ws` | `MULTI_PUBSUB` | `통과(99.3%)` | `통과(99.9%)` | `통과(101.3%)` | `통과(129.6%)` | `통과(120.5%)` | `통과(108.3%)` | C full/C++ full. |
| `ws` | `MULTI_SPOT` | `통과(93.1%)` | `통과(97.8%)` | `통과(100.5%)` | `통과(95.0%)` | `통과(108.8%)` | `통과(107.6%)` | C full/C++ full. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(98.7%)` | `통과(93.2%)` | `통과(94.5%)` | `통과(100.2%)` | `통과(101.2%)` | `통과(99.9%)` | C full/C++ full. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(97.1%)` | `통과(91.2%)` | `통과(88.9%)` | `통과(103.4%)` | `통과(112.6%)` | `통과(109.4%)` | C full/C++ full. |
| `ws` | `MULTI_STREAM` | `통과(133.1%)` | `통과(137.3%)` | `통과(126.6%)` | `해당 없음` | `통과(99.1%)` | `해당 없음` | C full/C++ full. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(93.7%)` | `통과(103.3%)` | `통과(115.6%)` | `통과(102.4%)` | `통과(107.6%)` | `통과(107.5%)` | C full/C++ full. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(96.2%)` | `통과(98.7%)` | `통과(95.5%)` | `통과(95.5%)` | `통과(98.6%)` | `통과(102.0%)` | C full/C++ full. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(97.1%)` | `통과(95.0%)` | `통과(96.7%)` | `통과(97.3%)` | `통과(108.4%)` | `통과(107.9%)` | C full/C++ full. |
| `wss` | `MULTI_PUBSUB` | `통과(95.2%)` | `통과(93.2%)` | `통과(109.1%)` | `통과(126.0%)` | `통과(106.8%)` | `통과(105.9%)` | C full/C++ full. |
| `wss` | `MULTI_SPOT` | `통과(97.2%)` | `통과(99.5%)` | `통과(100.9%)` | `통과(88.6%)` | `통과(85.1%)` | `통과(100.0%)` | C full/C++ full. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(92.8%)` | `통과(90.3%)` | `통과(92.1%)` | `통과(97.1%)` | `통과(108.1%)` | `통과(104.2%)` | C full/C++ full. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(105.1%)` | `통과(99.1%)` | `통과(90.7%)` | `통과(97.8%)` | `통과(103.2%)` | `통과(103.6%)` | C full/C++ full. |
| `wss` | `MULTI_STREAM` | `통과(107.2%)` | `통과(111.0%)` | `통과(103.1%)` | `해당 없음` | `통과(96.4%)` | `해당 없음` | C full/C++ full. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(86.9%)` | `통과(113.7%)` | `통과(109.1%)` | `통과(104.0%)` | `통과(94.8%)` | `통과(95.8%)` | C full/C++ full. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(95.8%)` | `통과(92.8%)` | `통과(94.8%)` | `통과(96.2%)` | `통과(100.8%)` | `통과(113.4%)` | C full/C++ full. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(94.5%)` | `통과(93.4%)` | `통과(95.2%)` | `통과(95.6%)` | `통과(100.9%)` | `통과(112.3%)` | C full/C++ full. |
| `tls` | `MULTI_PUBSUB` | `통과(95.6%)` | `통과(98.1%)` | `통과(104.3%)` | `통과(108.7%)` | `통과(84.7%)` | `통과(105.3%)` | C full/C++ full. 65536B는 제한 재측정 C `perf_c_multi_linux_20260531_083459_round_20260530_c_multi_pubsub_tls_65536_recheck.txt`, C++ `perf_cpp_multi_linux_20260531_083512_round_20260530_cpp_multi_pubsub_tls_65536_recheck.txt`로 통과(84.7%). |
| `tls` | `MULTI_SPOT` | `통과(92.6%)` | `통과(94.8%)` | `통과(101.7%)` | `통과(101.2%)` | `통과(103.1%)` | `통과(110.4%)` | C full/C++ full. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(93.6%)` | `통과(96.9%)` | `통과(92.6%)` | `통과(91.5%)` | `통과(109.0%)` | `통과(97.1%)` | C full/C++ full. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(106.6%)` | `통과(122.6%)` | `통과(109.5%)` | `통과(98.4%)` | `통과(104.4%)` | `통과(101.3%)` | C full/C++ full. |
| `tls` | `MULTI_STREAM` | `통과(103.5%)` | `통과(112.2%)` | `통과(105.1%)` | `해당 없음` | `통과(101.5%)` | `해당 없음` | C full/C++ full. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |


## 4. .NET 상태

perf 경로: `bindings/dotnet/perf`. 기준 파일과 보강 파일은 측정 시점에 결과 파일/메모 칸에 직접 적는다.


### 4.1 Single suite

.NET single full 결과 파일은 `perf_dotnet_single_linux_20260531_085442_round_20260530_dotnet_single_full_v1.txt`이다.
이 파일은 `DEALER_DEALER ipc 256B` timeout 때문에 status=partial이지만, 나머지 결과는 모두 report에 남았다.
보강 파일은 .NET `perf_dotnet_single_linux_20260531_091915_round_20260530_dotnet_single_dd_ipc_256_recheck.txt`,
C `perf_c_single_linux_20260531_091949_round_20260530_c_single_dotnet_inproc_failset_recheck.txt`,
.NET `perf_dotnet_single_linux_20260531_092351_round_20260530_dotnet_single_inproc_failset_recheck.txt`이다.
초기 잔류 미달 cell은 inproc 8개였다. `DEALER_DEALER`의 작은 message 2개와 routed one-way의 큰 message 6개가
runs=3 제한 재측정에서도 기준에 못 닿았다. 추가 검토에서 `DEALER_ROUTER`/`ROUTER_ROUTER` active recv가
poller 대기 뒤 `DontWait` drain을 쓰는 점을 확인했다. `DEALER_ROUTER`에 blocking 첫 recv를 적용한
`perf_dotnet_single_linux_20260531_220036_dotnet_single_routed_inproc_blocking_recv_20260531.txt`는
대용량 throughput이 크게 떨어져 회귀로 판정하고 변경을 남기지 않았다. `ROUTER_ROUTER`에는 같은 정렬이
효과가 있어 `perf_dotnet_single_linux_20260531_220311_dotnet_single_rr_inproc_blocking_recv_20260531.txt`에서
262144B가 94.8%로 통과했다. 65536B/131072B는 각각 33.4%/28.6%로 아직 기준에 못 닿았다.
남은 routed 경로는 .NET public `Received` envelope와 `Message` wrapper를 거쳐 payload를 꺼내야 하므로,
C의 native part 직접 수신보다 managed envelope 비용이 더 크게 드러난다. `DEALER_ROUTER`에 같은
blocking-first 수신을 적용한 실험은
`perf_dotnet_single_linux_20260531_220036_dotnet_single_routed_inproc_blocking_recv_20260531.txt`에서
대용량 throughput이 기존 제한 재측정보다 크게 낮아져 최종 코드에 남기지 않았다. `DEALER_DEALER`는 이미
C와 같은 blocking-first + `DontWait` burst-drain 구조이고, routed recv는 public `Recv(Received, ...)`
경로 밖의 내부 `RecvPart`를 perf에서 직접 호출하지 않는 정책을 지켜야 한다. 따라서 public API를
유지한 상태에서 더 줄일 내부 후보가 확인되지 않은 7개 single cell은 미달로 둔다.

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `통과(98.2%)` | `통과(75.3%)` | `통과(113.3%)` | `통과(100.1%)` | `통과(100.9%)` | `통과(99.7%)` | C full/.NET full. |
| `tcp` | `PUBSUB` | `통과(90.2%)` | `통과(84.7%)` | `통과(113.2%)` | `통과(97.9%)` | `통과(100.4%)` | `통과(100.4%)` | C full/.NET full. |
| `tcp` | `DEALER_DEALER` | `통과(76.9%)` | `통과(80.3%)` | `통과(105.6%)` | `통과(98.7%)` | `통과(98.3%)` | `통과(98.1%)` | C full/.NET full. |
| `tcp` | `DEALER_ROUTER` | `통과(76.8%)` | `통과(74.3%)` | `통과(83.1%)` | `통과(81.1%)` | `통과(97.7%)` | `통과(95.1%)` | C full/.NET full. |
| `tcp` | `ROUTER_ROUTER` | `통과(90.6%)` | `통과(88.7%)` | `통과(88.8%)` | `통과(80.5%)` | `통과(95.7%)` | `통과(100.6%)` | C full/.NET full. |
| `tcp` | `SPOT` | `통과(114.8%)` | `통과(97.8%)` | `통과(77.6%)` | `통과(97.3%)` | `통과(96.0%)` | `통과(92.9%)` | C full/.NET full. |
| `ws` | `PAIR` | `통과(98.0%)` | `통과(96.8%)` | `통과(117.6%)` | `통과(99.5%)` | `통과(99.0%)` | `통과(98.0%)` | C full/.NET full. |
| `ws` | `PUBSUB` | `통과(91.4%)` | `통과(91.4%)` | `통과(107.9%)` | `통과(98.8%)` | `통과(98.2%)` | `통과(98.3%)` | C full/.NET full. |
| `ws` | `DEALER_DEALER` | `통과(73.6%)` | `통과(82.0%)` | `통과(100.8%)` | `통과(97.8%)` | `통과(99.6%)` | `통과(101.1%)` | C full/.NET full. |
| `ws` | `DEALER_ROUTER` | `통과(78.0%)` | `통과(78.7%)` | `통과(88.5%)` | `통과(90.0%)` | `통과(95.8%)` | `통과(98.4%)` | C full/.NET full. |
| `ws` | `ROUTER_ROUTER` | `통과(94.0%)` | `통과(82.1%)` | `통과(89.1%)` | `통과(89.9%)` | `통과(98.4%)` | `통과(103.6%)` | C full/.NET full. |
| `ws` | `SPOT` | `통과(120.6%)` | `통과(100.4%)` | `통과(83.4%)` | `통과(90.5%)` | `통과(96.0%)` | `통과(97.3%)` | C full/.NET full. |
| `wss` | `PAIR` | `통과(97.9%)` | `통과(99.2%)` | `통과(115.5%)` | `통과(104.3%)` | `통과(95.6%)` | `통과(99.5%)` | C full/.NET full. |
| `wss` | `PUBSUB` | `통과(93.2%)` | `통과(93.8%)` | `통과(114.0%)` | `통과(96.3%)` | `통과(97.4%)` | `통과(97.8%)` | C full/.NET full. |
| `wss` | `DEALER_DEALER` | `통과(69.3%)` | `통과(82.1%)` | `통과(113.7%)` | `통과(104.7%)` | `통과(99.9%)` | `통과(92.6%)` | C full/.NET full. |
| `wss` | `DEALER_ROUTER` | `통과(79.5%)` | `통과(83.7%)` | `통과(98.3%)` | `통과(88.9%)` | `통과(99.5%)` | `통과(104.5%)` | C full/.NET full. |
| `wss` | `ROUTER_ROUTER` | `통과(95.7%)` | `통과(79.6%)` | `통과(99.9%)` | `통과(93.3%)` | `통과(100.3%)` | `통과(117.1%)` | C full/.NET full. |
| `wss` | `SPOT` | `통과(120.0%)` | `통과(99.2%)` | `통과(88.8%)` | `통과(95.1%)` | `통과(94.0%)` | `통과(196.5%)` | C full/.NET full. |
| `tls` | `PAIR` | `통과(98.0%)` | `통과(87.0%)` | `통과(113.3%)` | `통과(101.1%)` | `통과(100.0%)` | `통과(99.4%)` | C full/.NET full. |
| `tls` | `PUBSUB` | `통과(93.6%)` | `통과(80.3%)` | `통과(113.2%)` | `통과(99.4%)` | `통과(99.7%)` | `통과(97.8%)` | C full/.NET full. |
| `tls` | `DEALER_DEALER` | `통과(73.0%)` | `통과(82.7%)` | `통과(120.1%)` | `통과(97.9%)` | `통과(98.3%)` | `통과(101.1%)` | C full/.NET full. |
| `tls` | `DEALER_ROUTER` | `통과(81.9%)` | `통과(79.9%)` | `통과(105.5%)` | `통과(101.4%)` | `통과(107.3%)` | `통과(105.6%)` | C full/.NET full. |
| `tls` | `ROUTER_ROUTER` | `통과(87.5%)` | `통과(87.9%)` | `통과(102.1%)` | `통과(92.4%)` | `통과(100.4%)` | `통과(102.0%)` | C full/.NET full. |
| `tls` | `SPOT` | `통과(116.2%)` | `통과(96.1%)` | `통과(93.2%)` | `통과(98.8%)` | `통과(104.7%)` | `통과(98.3%)` | C full/.NET full. |
| `inproc` | `PAIR` | `통과(76.5%)` | `통과(85.9%)` | `통과(80.8%)` | `통과(98.9%)` | `통과(98.0%)` | `통과(98.2%)` | C full/.NET full. |
| `inproc` | `PUBSUB` | `통과(88.3%)` | `통과(98.1%)` | `통과(100.9%)` | `통과(100.9%)` | `통과(100.1%)` | `통과(99.0%)` | C full/.NET full. |
| `inproc` | `DEALER_DEALER` | `미달(50.4%)` | `통과(69.0%)` | `미달(60.7%)` | `통과(98.9%)` | `통과(99.1%)` | `통과(99.5%)` | inproc 후보군은 제한 재측정 C `perf_c_single_linux_20260531_091949_round_20260530_c_single_dotnet_inproc_failset_recheck.txt`, .NET `perf_dotnet_single_linux_20260531_092351_round_20260530_dotnet_single_inproc_failset_recheck.txt` 기준. 이미 C와 같은 blocking-first + `DontWait` burst-drain 구조라 추가 내부 후보가 확인되지 않았다. |
| `inproc` | `DEALER_ROUTER` | `통과(78.4%)` | `통과(83.7%)` | `통과(78.1%)` | `미달(35.8%)` | `미달(29.4%)` | `미달(47.1%)` | inproc 후보군은 제한 재측정 C `perf_c_single_linux_20260531_091949_round_20260530_c_single_dotnet_inproc_failset_recheck.txt`, .NET `perf_dotnet_single_linux_20260531_092351_round_20260530_dotnet_single_inproc_failset_recheck.txt` 기준. blocking-first 수신 실험은 회귀해 최종 코드에 남기지 않았다. public `Recv(Received, ...)` envelope 비용을 우회하지 않는 내부 후보가 확인되지 않았다. |
| `inproc` | `ROUTER_ROUTER` | `통과(88.5%)` | `통과(77.7%)` | `통과(80.5%)` | `미달(33.4%)` | `미달(28.6%)` | `통과(94.8%)` | 65536B/131072B/262144B는 routed active recv 정렬 뒤 .NET `perf_dotnet_single_linux_20260531_220311_dotnet_single_rr_inproc_blocking_recv_20260531.txt` 기준. C 기준은 `perf_c_single_linux_20260531_091949_round_20260530_c_single_dotnet_inproc_failset_recheck.txt`. 262144B는 통과로 회복했고, 65536B/131072B는 같은 코드 변경 뒤에도 기준에 못 닿아 미달한다. |
| `inproc` | `SPOT` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | full single에서 SPOT inproc 조합은 결과가 없다. |
| `ipc` | `PAIR` | `통과(100.8%)` | `통과(97.5%)` | `통과(115.9%)` | `통과(98.0%)` | `통과(98.2%)` | `통과(99.2%)` | C full/.NET full. |
| `ipc` | `PUBSUB` | `통과(80.8%)` | `통과(86.3%)` | `통과(105.1%)` | `통과(99.2%)` | `통과(98.6%)` | `통과(98.1%)` | C full/.NET full. |
| `ipc` | `DEALER_DEALER` | `통과(74.8%)` | `통과(74.4%)` | `통과(109.5%)` | `통과(99.1%)` | `통과(101.4%)` | `통과(99.9%)` | 256B는 full에서 timeout이었고 제한 재측정 .NET `perf_dotnet_single_linux_20260531_091915_round_20260530_dotnet_single_dd_ipc_256_recheck.txt`로 보강. 나머지는 C full/.NET full. |
| `ipc` | `DEALER_ROUTER` | `통과(75.6%)` | `통과(81.1%)` | `통과(83.9%)` | `통과(83.7%)` | `통과(96.6%)` | `통과(100.4%)` | C full/.NET full. |
| `ipc` | `ROUTER_ROUTER` | `통과(85.4%)` | `통과(91.0%)` | `통과(79.6%)` | `통과(72.4%)` | `통과(83.4%)` | `통과(95.4%)` | C full/.NET full. |
| `ipc` | `SPOT` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | full single에서 SPOT ipc 조합은 결과가 없다. |


### 4.2 Multi suite

C 기준 full 파일은 `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`이고,
.NET full 파일은 `perf_dotnet_multi_linux_20260531_093111_round_20260530_dotnet_multi_full_v1.txt`이다.
.NET full은 status=complete, 결과 라인 920/920이며 runner 기본 size가 `4096` 대신
`262144`를 포함하므로 `perf_dotnet_multi_linux_20260531_100028_round_20260530_dotnet_multi_4096_fill.txt`로
4096B를 보강했다. 미달 후보는 C/.NET 제한 재측정
`perf_c_multi_linux_20260531_100611_round_20260530_c_multi_dotnet_routed_failset_recheck.txt`,
`perf_dotnet_multi_linux_20260531_100808_round_20260530_dotnet_multi_routed_failset_recheck.txt`,
`perf_c_multi_linux_20260531_101006_round_20260530_c_multi_dotnet_spot_failset_recheck.txt`,
`perf_dotnet_multi_linux_20260531_101624_round_20260530_dotnet_multi_spot_failset_recheck.txt`로
다시 확인했다. `perf_dotnet_multi_linux_20260531_101624_round_20260530_dotnet_multi_spot_failset_recheck.txt`에서
`MULTI_SPOT tls 1024B`가 server shutdown timeout으로 partial이었으나, 단독 재측정
`perf_dotnet_multi_linux_20260531_102129_round_20260530_dotnet_multi_spot_tls_1024_timeout_recheck.txt`는
complete였다.

잔류 미달 cell은 11개다. tcp routed large payload는 public .NET wrapper가 routed envelope를
관리하는 비용이 C hot path보다 크게 드러나는 구간이고, `MULTI_SPOT wss`와
`MULTI_SPOT_SENDSEND` 소형 일부는 managed dispatch와 TLS/WebSocket 경계 비용이 같이
드러나는 구간이다. public API를 우회하거나 native envelope를 그대로 노출하지 않는 한
좁게 줄일 수 있는 내부 변경점은 확인되지 않아 코드 변경 없이 문서화한다.

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `미달(56.9%)` | `통과(68.0%)` | `통과(75.4%)` | `통과(63.4%)` | `통과(106.4%)` | `통과(103.0%)` | tcp routed failset 제한 재측정 기준. C full/.NET full. public send/recv 경로를 유지한 상태에서 추가 내부 후보가 확인되지 않았다. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(65.2%)` | `통과(66.1%)` | `통과(63.6%)` | `통과(56.6%)` | `미달(46.5%)` | `미달(18.0%)` | tcp routed failset 제한 재측정 기준. C full/.NET full. public routed envelope 비용을 우회하지 않는 추가 내부 후보가 확인되지 않았다. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(52.0%)` | `통과(51.1%)` | `통과(52.1%)` | `미달(48.9%)` | `미달(37.9%)` | `미달(16.0%)` | tcp routed failset 제한 재측정 기준. C full/.NET full. public routed envelope 비용을 우회하지 않는 추가 내부 후보가 확인되지 않았다. |
| `tcp` | `MULTI_PUBSUB` | `통과(71.2%)` | `통과(80.5%)` | `통과(153.3%)` | `통과(181.7%)` | `통과(71.9%)` | `통과(100.4%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `tcp` | `MULTI_SPOT` | `통과(74.1%)` | `통과(77.1%)` | `통과(67.5%)` | `통과(80.2%)` | `통과(105.2%)` | `통과(102.6%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(70.5%)` | `통과(73.8%)` | `통과(80.4%)` | `통과(90.0%)` | `통과(98.8%)` | `통과(109.8%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(61.2%)` | `통과(60.2%)` | `통과(66.7%)` | `통과(70.2%)` | `통과(93.4%)` | `통과(93.7%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `tcp` | `MULTI_STREAM` | `통과(111.1%)` | `통과(110.8%)` | `통과(112.2%)` | `해당 없음` | `통과(94.8%)` | `해당 없음` | C full/.NET full. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(65.0%)` | `통과(70.8%)` | `통과(77.8%)` | `통과(70.9%)` | `통과(108.3%)` | `통과(96.7%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(64.3%)` | `통과(60.1%)` | `통과(61.8%)` | `통과(64.4%)` | `통과(57.8%)` | `통과(80.8%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(53.8%)` | `통과(58.8%)` | `통과(54.6%)` | `통과(55.4%)` | `통과(60.9%)` | `통과(79.0%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `ws` | `MULTI_PUBSUB` | `통과(79.5%)` | `통과(74.3%)` | `통과(78.7%)` | `통과(104.0%)` | `통과(144.9%)` | `통과(125.1%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `ws` | `MULTI_SPOT` | `통과(68.0%)` | `통과(74.3%)` | `통과(72.4%)` | `통과(83.9%)` | `통과(107.6%)` | `통과(101.7%)` | SPOT failset 제한 재측정 기준. C full/.NET full. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(80.5%)` | `통과(82.5%)` | `통과(86.1%)` | `통과(94.7%)` | `통과(102.5%)` | `통과(103.2%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(62.4%)` | `통과(64.0%)` | `통과(66.8%)` | `통과(90.7%)` | `통과(101.7%)` | `통과(91.2%)` | SPOT failset 제한 재측정 기준. C full/.NET full. |
| `ws` | `MULTI_STREAM` | `통과(113.3%)` | `통과(116.6%)` | `통과(110.9%)` | `해당 없음` | `통과(96.4%)` | `해당 없음` | C full/.NET full. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(63.1%)` | `통과(68.5%)` | `통과(90.4%)` | `통과(103.6%)` | `통과(107.6%)` | `통과(106.1%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(65.9%)` | `통과(62.7%)` | `통과(62.8%)` | `통과(68.6%)` | `통과(98.4%)` | `통과(100.8%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(56.1%)` | `통과(56.9%)` | `통과(56.4%)` | `통과(57.9%)` | `통과(103.4%)` | `통과(106.5%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `wss` | `MULTI_PUBSUB` | `통과(74.2%)` | `통과(66.4%)` | `통과(91.2%)` | `통과(97.7%)` | `통과(110.8%)` | `통과(112.3%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `wss` | `MULTI_SPOT` | `통과(69.9%)` | `미달(42.1%)` | `미달(27.2%)` | `미달(54.9%)` | `통과(66.2%)` | `통과(78.1%)` | SPOT failset 제한 재측정 기준. C full/.NET full. managed dispatch와 WSS 경계 비용을 줄일 public API-safe 내부 후보가 확인되지 않았다. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(76.5%)` | `통과(87.1%)` | `통과(88.9%)` | `통과(102.6%)` | `통과(108.0%)` | `통과(97.4%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `wss` | `MULTI_SPOT_SENDSEND` | `미달(59.5%)` | `통과(60.8%)` | `통과(69.1%)` | `통과(100.9%)` | `통과(98.1%)` | `통과(94.6%)` | SPOT failset 제한 재측정 기준. C full/.NET full. managed dispatch와 WSS 경계 비용을 줄일 public API-safe 내부 후보가 확인되지 않았다. |
| `wss` | `MULTI_STREAM` | `통과(98.8%)` | `통과(100.3%)` | `통과(93.3%)` | `해당 없음` | `통과(98.6%)` | `해당 없음` | C full/.NET full. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(67.0%)` | `통과(74.5%)` | `통과(79.1%)` | `통과(91.4%)` | `통과(98.6%)` | `통과(97.3%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(65.6%)` | `통과(61.3%)` | `통과(60.3%)` | `통과(66.6%)` | `통과(97.2%)` | `통과(112.4%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(53.7%)` | `통과(52.3%)` | `통과(54.9%)` | `통과(55.9%)` | `통과(91.2%)` | `통과(103.0%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `tls` | `MULTI_PUBSUB` | `통과(76.4%)` | `통과(70.3%)` | `통과(84.8%)` | `통과(97.3%)` | `통과(92.4%)` | `통과(114.3%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `tls` | `MULTI_SPOT` | `통과(67.6%)` | `통과(74.0%)` | `통과(68.3%)` | `통과(63.4%)` | `통과(101.0%)` | `통과(99.7%)` | SPOT failset 제한 재측정 기준. SPOT tls 1024B는 partial timeout 후 단독 재측정 complete 기준. C full/.NET full. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(70.4%)` | `통과(83.1%)` | `통과(86.6%)` | `통과(91.4%)` | `통과(87.5%)` | `통과(93.9%)` | C full/.NET full. 4096B는 .NET 4096B 보강 파일 기준. |
| `tls` | `MULTI_SPOT_SENDSEND` | `미달(55.7%)` | `통과(63.6%)` | `통과(65.8%)` | `통과(85.2%)` | `통과(107.2%)` | `통과(94.4%)` | SPOT failset 제한 재측정 기준. C full/.NET full. managed dispatch와 TLS 경계 비용을 줄일 public API-safe 내부 후보가 확인되지 않았다. |
| `tls` | `MULTI_STREAM` | `통과(99.9%)` | `통과(100.3%)` | `통과(92.6%)` | `해당 없음` | `통과(97.9%)` | `해당 없음` | C full/.NET full. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |


## 5. Java 상태

perf 경로: `bindings/java/perf`. 기준 파일과 보강 파일은 측정 시점에 결과 파일/메모 칸에 직접 적는다.


### 5.1 Single suite

Java single full 결과 파일은 `perf_java_single_linux_20260531_103037_round_20260530_java_single_full_v1.txt`이다.
이 파일은 `tcp,tls,ws,wss` transport와 size `64,256,1024,65536,131072,262144` 전체에 대해
status=complete, 결과 라인 720/720으로 끝났다. Smoke 파일은
`perf_java_single_linux_20260531_102551_round_20260530_java_single_smoke_64.txt`이다.
Full 비교에서 SPOT 대용량 10개가 미달 후보였고, C
`perf_c_single_linux_20260531_104549_round_20260530_c_single_java_spot_large_recheck.txt`,
Java `perf_java_single_linux_20260531_104721_round_20260530_java_single_spot_large_recheck.txt`로
제한 재측정했다. 이 재측정에서 `SPOT wss 262144B`는 65.4%로 회복되어 통과로 보강했고,
`SPOT tcp/tls/ws 65536B 이상` 9개는 잔류 미달로 확정했다. 해당 구간은 Java binding의
SPOT 수신 경로가 대용량 payload를 managed buffer로 다시 감싸고 전달하는 비용이 C hot path보다
크게 드러나는 구간이다. active 수신 루프에서 `TopicMessage`를 재사용하는 실험을
`perf_java_single_linux_20260531_221215_java_single_spot_reuse_topicmsg_20260531.txt`로 확인했지만
tcp 16.9%/16.5%/19.9%, ws 25.9%/26.4%/24.6%, tls 42.4%/50.9%/52.2%로 통과하지 못했고
최종 코드에 남기지 않았다. public API를 바꾸지 않고 제거할 수 있는 좁은 내부 변경점은 확인되지 않았다.

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `통과(97.0%)` | `통과(95.5%)` | `통과(113.0%)` | `통과(96.8%)` | `통과(100.8%)` | `통과(98.7%)` | C full/Java full. |
| `tcp` | `PUBSUB` | `통과(76.5%)` | `통과(85.5%)` | `통과(101.3%)` | `통과(97.0%)` | `통과(97.1%)` | `통과(100.6%)` | C full/Java full. |
| `tcp` | `DEALER_DEALER` | `통과(96.8%)` | `통과(96.5%)` | `통과(112.4%)` | `통과(98.4%)` | `통과(99.3%)` | `통과(97.7%)` | C full/Java full. |
| `tcp` | `DEALER_ROUTER` | `통과(82.2%)` | `통과(80.2%)` | `통과(69.5%)` | `통과(93.8%)` | `통과(92.1%)` | `통과(90.5%)` | C full/Java full. |
| `tcp` | `ROUTER_ROUTER` | `통과(90.2%)` | `통과(98.9%)` | `통과(85.7%)` | `통과(99.7%)` | `통과(89.2%)` | `통과(87.0%)` | C full/Java full. |
| `tcp` | `SPOT` | `통과(87.0%)` | `통과(75.7%)` | `통과(83.1%)` | `미달(18.5%)` | `미달(19.3%)` | `미달(22.3%)` | C full/Java full. SPOT 대용량 제한 재측정 기준. `TopicMessage` 재사용 실험도 통과를 만들지 못해 최종 코드에 남기지 않았다. |
| `ws` | `PAIR` | `통과(96.6%)` | `통과(96.4%)` | `통과(132.3%)` | `통과(100.3%)` | `통과(98.5%)` | `통과(97.2%)` | C full/Java full. |
| `ws` | `PUBSUB` | `통과(84.7%)` | `통과(89.8%)` | `통과(118.3%)` | `통과(100.7%)` | `통과(98.3%)` | `통과(97.6%)` | C full/Java full. |
| `ws` | `DEALER_DEALER` | `통과(97.0%)` | `통과(96.8%)` | `통과(131.1%)` | `통과(98.6%)` | `통과(97.7%)` | `통과(97.3%)` | C full/Java full. |
| `ws` | `DEALER_ROUTER` | `통과(77.1%)` | `통과(90.0%)` | `통과(117.1%)` | `통과(179.3%)` | `통과(135.2%)` | `통과(127.2%)` | C full/Java full. |
| `ws` | `ROUTER_ROUTER` | `통과(95.4%)` | `통과(103.8%)` | `통과(115.4%)` | `통과(170.9%)` | `통과(135.2%)` | `통과(134.0%)` | C full/Java full. |
| `ws` | `SPOT` | `통과(84.5%)` | `통과(81.3%)` | `통과(91.8%)` | `미달(27.5%)` | `미달(29.0%)` | `미달(25.0%)` | C full/Java full. SPOT 대용량 제한 재측정 기준. `TopicMessage` 재사용 실험도 통과를 만들지 못해 최종 코드에 남기지 않았다. |
| `wss` | `PAIR` | `통과(96.7%)` | `통과(96.4%)` | `통과(143.3%)` | `통과(129.0%)` | `통과(103.3%)` | `통과(107.1%)` | C full/Java full. |
| `wss` | `PUBSUB` | `통과(81.5%)` | `통과(96.6%)` | `통과(153.2%)` | `통과(113.7%)` | `통과(101.4%)` | `통과(99.1%)` | C full/Java full. |
| `wss` | `DEALER_DEALER` | `통과(96.5%)` | `통과(98.1%)` | `통과(142.3%)` | `통과(128.7%)` | `통과(105.5%)` | `통과(99.3%)` | C full/Java full. |
| `wss` | `DEALER_ROUTER` | `통과(77.9%)` | `통과(89.2%)` | `통과(136.0%)` | `통과(157.0%)` | `통과(176.3%)` | `통과(202.9%)` | C full/Java full. |
| `wss` | `ROUTER_ROUTER` | `통과(98.7%)` | `통과(103.6%)` | `통과(133.7%)` | `통과(147.3%)` | `통과(174.9%)` | `통과(226.5%)` | C full/Java full. |
| `wss` | `SPOT` | `통과(86.4%)` | `통과(76.1%)` | `통과(271.9%)` | `통과(64.5%)` | `통과(60.9%)` | `통과(65.4%)` | C full/Java full. SPOT 대용량 제한 재측정 기준. |
| `tls` | `PAIR` | `통과(95.8%)` | `통과(99.3%)` | `통과(134.6%)` | `통과(99.2%)` | `통과(97.9%)` | `통과(98.2%)` | C full/Java full. |
| `tls` | `PUBSUB` | `통과(80.1%)` | `통과(80.8%)` | `통과(136.7%)` | `통과(98.1%)` | `통과(100.9%)` | `통과(99.3%)` | C full/Java full. |
| `tls` | `DEALER_DEALER` | `통과(97.5%)` | `통과(97.9%)` | `통과(135.4%)` | `통과(97.9%)` | `통과(97.3%)` | `통과(98.4%)` | C full/Java full. |
| `tls` | `DEALER_ROUTER` | `통과(77.5%)` | `통과(85.4%)` | `통과(122.3%)` | `통과(163.8%)` | `통과(165.1%)` | `통과(166.7%)` | C full/Java full. |
| `tls` | `ROUTER_ROUTER` | `통과(88.1%)` | `통과(98.3%)` | `통과(113.5%)` | `통과(162.2%)` | `통과(173.0%)` | `통과(173.6%)` | C full/Java full. |
| `tls` | `SPOT` | `통과(83.7%)` | `통과(78.1%)` | `통과(103.9%)` | `미달(44.3%)` | `미달(53.5%)` | `미달(53.3%)` | C full/Java full. SPOT 대용량 제한 재측정 기준. `TopicMessage` 재사용 실험도 통과를 만들지 못해 최종 코드에 남기지 않았다. |


### 5.2 Multi suite

Java multi full 결과 파일은 `perf_java_multi_linux_20260531_105430_round_20260530_java_multi_full_v1.txt`이다.
이 파일은 `tcp,ws,wss,tls` transport와 size `64,256,1024,4096,65536,131072` 조합을
status=complete, 결과 라인 920/920으로 끝냈다. Smoke 파일은
`perf_java_multi_linux_20260531_105105_round_20260530_java_multi_smoke_64.txt`이다.
Full 비교에서 7개 cell이 미달 후보였고, C
`perf_c_multi_linux_20260531_112539_round_20260530_c_multi_java_failset_recheck.txt`,
Java `perf_java_multi_linux_20260531_113249_round_20260530_java_multi_failset_recheck.txt`로
제한 재측정했다. 재측정에서 `MULTI_DEALER_ROUTER ws 65536B`는 50.5%로 기준을 넘었고,
`MULTI_DEALER_DEALER ws 131072B`와 `MULTI_SPOT wss 256B/1024B/4096B/65536B/131072B`는
잔류 미달로 확정했다. `MULTI_SPOT wss`는 Java binding에서 TLS/WebSocket 계층을 지난
SPOT fan-out 수신을 JNI wrapper와 managed buffer 경계에서 다시 처리하는 비용이 커지는 구간이다.
공개 API와 runner 정책을 바꾸지 않는 좁은 내부 변경으로 제거할 수 있는 병목은 확인하지 못했다.

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `통과(74.6%)` | `통과(95.3%)` | `통과(87.4%)` | `통과(74.0%)` | `통과(82.4%)` | `통과(83.1%)` | C full/Java full. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(72.9%)` | `통과(73.0%)` | `통과(79.6%)` | `통과(75.3%)` | `통과(60.0%)` | `통과(66.9%)` | C full/Java full. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(55.6%)` | `통과(61.3%)` | `통과(61.0%)` | `통과(59.6%)` | `통과(56.8%)` | `통과(76.5%)` | C full/Java full. |
| `tcp` | `MULTI_PUBSUB` | `통과(69.4%)` | `통과(82.7%)` | `통과(102.4%)` | `통과(69.9%)` | `통과(132.5%)` | `통과(174.0%)` | C full/Java full. |
| `tcp` | `MULTI_SPOT` | `통과(110.5%)` | `통과(102.4%)` | `통과(94.3%)` | `통과(98.5%)` | `통과(80.1%)` | `통과(85.5%)` | C full/Java full. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(76.6%)` | `통과(79.4%)` | `통과(81.6%)` | `통과(75.3%)` | `통과(61.8%)` | `통과(97.6%)` | C full/Java full. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(77.5%)` | `통과(74.5%)` | `통과(82.6%)` | `통과(84.6%)` | `통과(81.4%)` | `통과(83.4%)` | C full/Java full. |
| `tcp` | `MULTI_STREAM` | `통과(112.8%)` | `통과(110.8%)` | `통과(112.6%)` | `해당 없음` | `통과(103.8%)` | `해당 없음` | C full/Java full. MULTI_STREAM 정책상 4096B/131072B는 측정 대상이 아니다. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(78.0%)` | `통과(92.1%)` | `통과(79.1%)` | `통과(70.2%)` | `통과(76.3%)` | `미달(59.7%)` | C full/Java full. Java multi failset 제한 재측정 기준. client burst send와 server counted-drain 구조가 이미 적용되어 있고, public send semantics를 유지한 추가 내부 후보가 확인되지 않았다. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(70.7%)` | `통과(66.3%)` | `통과(68.5%)` | `통과(66.1%)` | `통과(50.5%)` | `통과(65.0%)` | C full/Java full. Java multi failset 제한 재측정 기준. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(55.7%)` | `통과(63.7%)` | `통과(60.3%)` | `통과(59.2%)` | `통과(50.7%)` | `통과(55.7%)` | C full/Java full. |
| `ws` | `MULTI_PUBSUB` | `통과(85.4%)` | `통과(80.8%)` | `통과(87.7%)` | `통과(104.5%)` | `통과(150.8%)` | `통과(133.1%)` | C full/Java full. |
| `ws` | `MULTI_SPOT` | `통과(146.1%)` | `통과(117.9%)` | `통과(102.1%)` | `통과(99.4%)` | `통과(83.3%)` | `통과(88.3%)` | C full/Java full. Java multi failset 제한 재측정 기준. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(78.2%)` | `통과(72.0%)` | `통과(71.1%)` | `통과(79.0%)` | `통과(77.5%)` | `통과(81.0%)` | C full/Java full. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(71.6%)` | `통과(75.8%)` | `통과(79.2%)` | `통과(88.4%)` | `통과(98.1%)` | `통과(84.0%)` | C full/Java full. |
| `ws` | `MULTI_STREAM` | `통과(125.0%)` | `통과(123.8%)` | `통과(119.8%)` | `해당 없음` | `통과(99.5%)` | `해당 없음` | C full/Java full. MULTI_STREAM 정책상 4096B/131072B는 측정 대상이 아니다. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(75.0%)` | `통과(78.2%)` | `통과(65.7%)` | `통과(95.3%)` | `통과(96.4%)` | `통과(100.2%)` | C full/Java full. Java multi failset 제한 재측정 기준. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(67.9%)` | `통과(63.8%)` | `통과(62.9%)` | `통과(56.9%)` | `통과(66.8%)` | `통과(91.6%)` | C full/Java full. Java multi failset 제한 재측정 기준. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(57.3%)` | `통과(57.1%)` | `통과(58.5%)` | `통과(55.6%)` | `통과(69.0%)` | `통과(84.2%)` | C full/Java full. |
| `wss` | `MULTI_PUBSUB` | `통과(83.2%)` | `통과(79.3%)` | `통과(93.2%)` | `통과(101.0%)` | `통과(112.4%)` | `통과(112.2%)` | C full/Java full. |
| `wss` | `MULTI_SPOT` | `통과(117.0%)` | `미달(46.7%)` | `미달(22.0%)` | `미달(40.7%)` | `미달(53.3%)` | `미달(54.1%)` | C full/Java full. Java multi failset 제한 재측정 기준. worker 분산 수신과 caller-provided `TopicMessage` 재사용이 이미 적용되어 있고, public API-safe 추가 후보가 확인되지 않았다. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(71.0%)` | `통과(72.4%)` | `통과(71.2%)` | `통과(79.6%)` | `통과(82.9%)` | `통과(88.2%)` | C full/Java full. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(68.1%)` | `통과(73.3%)` | `통과(81.5%)` | `통과(90.4%)` | `통과(87.7%)` | `통과(89.2%)` | C full/Java full. |
| `wss` | `MULTI_STREAM` | `통과(104.9%)` | `통과(105.6%)` | `통과(99.0%)` | `해당 없음` | `통과(96.9%)` | `해당 없음` | C full/Java full. MULTI_STREAM 정책상 4096B/131072B는 측정 대상이 아니다. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(75.4%)` | `통과(99.5%)` | `통과(87.0%)` | `통과(82.4%)` | `통과(97.6%)` | `통과(90.9%)` | C full/Java full. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(70.0%)` | `통과(67.8%)` | `통과(67.6%)` | `통과(65.0%)` | `통과(54.3%)` | `통과(91.4%)` | C full/Java full. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(57.0%)` | `통과(57.9%)` | `통과(58.8%)` | `통과(56.9%)` | `통과(58.8%)` | `통과(56.6%)` | C full/Java full. |
| `tls` | `MULTI_PUBSUB` | `통과(81.4%)` | `통과(78.3%)` | `통과(89.4%)` | `통과(100.1%)` | `통과(115.5%)` | `통과(114.4%)` | C full/Java full. |
| `tls` | `MULTI_SPOT` | `통과(129.3%)` | `통과(125.1%)` | `통과(101.6%)` | `통과(71.2%)` | `통과(82.8%)` | `통과(95.9%)` | C full/Java full. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(73.6%)` | `통과(79.5%)` | `통과(79.2%)` | `통과(78.2%)` | `통과(85.9%)` | `통과(80.6%)` | C full/Java full. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(70.5%)` | `통과(84.4%)` | `통과(80.8%)` | `통과(90.4%)` | `통과(95.6%)` | `통과(87.5%)` | C full/Java full. |
| `tls` | `MULTI_STREAM` | `통과(107.0%)` | `통과(106.9%)` | `통과(101.1%)` | `해당 없음` | `통과(100.5%)` | `해당 없음` | C full/Java full. MULTI_STREAM 정책상 4096B/131072B는 측정 대상이 아니다. |


## 6. Node 상태

perf 경로: `bindings/node/perf`.

Single full 결과 파일 `perf_node_single_linux_20260531_120032_round_20260530_node_single_full_v1.txt`는
status=complete(720/720)였다. Routed 후보는
`perf_node_single_linux_20260531_120621_round_20260530_node_single_routed_failset_recheck.txt`,
단순/서비스 후보는
`perf_node_single_linux_20260531_121556_round_20260530_node_single_simple_spot_failset_recheck.txt`로
다시 확인했다. 잔류 미달은 60개다. `DEALER_ROUTER`/`ROUTER_ROUTER`의 작은 메시지와
tcp 대용량, `PAIR`/`PUBSUB`/`DEALER_DEALER` 일부 작은 메시지, `SPOT` 일부 대용량에서 반복됐다.
이 범위는 Node 이벤트 루프, native 호출 경계, routed envelope 처리, Buffer 이동이 함께 들어가는
경로라서 공개 API를 유지한 채 좁게 고칠 수 있는 단일 병목으로 좁혀지지 않았다.

초기 multi full 결과 파일 `perf_node_multi_linux_20260531_125926_round_20260530_node_multi_full_v1.txt`는
status=complete(780/780)였고, 실제 측정 대상 크기만 다시 확인한
`perf_node_multi_linux_20260531_134025_round_20260530_node_multi_measured_sizes_recheck.txt`도
status=complete(780/780)였다. 이후 보강 뒤 full matrix
`perf_node_multi_linux_20260531_213502_node_fastpath_full_v1_20260531.txt`를 다시 실행했고
status=complete(920/920)였다. `MULTI_SPOT`은 전 구간이 통과로 올라갔고,
`MULTI_DEALER_DEALER`와 일부 `MULTI_PUBSUB`/`MULTI_STREAM` 큰 크기도 개선됐다.
잔여 미달은 multi routed echo 일부, `MULTI_PUBSUB`의 tcp large와 일부 small, `MULTI_SPOT_SENDSEND`,
`MULTI_STREAM` 작은 크기에 집중된다. Node multi report에는 4096B `RESULT` 행이
없으므로 4096B는 표에서 `해당 없음`으로 둔다. `MULTI_STREAM`은 runner의
stream size 정책상 64/256/1024/65536B만 결과를 낸다.

추가 보강에서는 `RoutingId` 생성 시 이미 검증한 길이를 hot path에서 다시 확인하지 않게 하고,
socket/router/stream/spot runtime이 같은 native binding 객체를 반복 조회하지 않게 했다. 또한
Node multi runner의 receive loop가 `Received`/`TopicMessage` 저장소를 재사용하고, 큰 payload를
별도 Buffer로 다시 복사하지 않고 바로 header를 읽게 했다. 대표 probe 결과 파일은 single
`perf_node_single_linux_20260531_204120_node_perf_fastpath_single_probe_20260531.txt`,
multi `perf_node_multi_linux_20260531_204023_node_perf_fastpath_probe_20260531.txt`다.
대표 multi probe에서 `MULTI_SPOT tcp 64B`는 1,546,598.4 msg/s에서 2,227,384 msg/s로,
`MULTI_SPOT tcp 65536B`는 791,020 msg/s에서 1,151,900 msg/s로 올랐다.
`MULTI_PUBSUB tcp 64B`도 373,005.6 msg/s에서 512,802 msg/s로 올랐다.
이후 full matrix `perf_node_multi_linux_20260531_213502_node_fastpath_full_v1_20260531.txt`를
다시 실행했고 status=complete(920/920)였다. Node 개선 라운드 기준은 Node/Python 최소
통과 기준에서 5%p 낮춘 값으로 되돌린다. 단순 one-way는 30%, routed one-way와 SPOT 계열은
28%, multi routed echo는 25%를 최소 통과 기준으로 본다. 이미 적용한 개선 뒤에도 남은 항목은
Node 이벤트 루프, native 호출 경계, routed envelope, Buffer 생명주기 비용이 함께 나타나는 구간이다.
public API를 바꾸거나 native fast path를 직접 노출하지 않는 범위에서 추가 내부 후보가 확인되지 않는지
항목별로 더 확인했고, 아래 표에서는 통과하지 못한 항목을 미달로 둔다.

Single 추가 개선에서는 `drainRouterRecvInto`가 routed 수신마다 `Received`를 새로 만들던 부분과,
PUBSUB 수신 루프가 `TopicMessage`를 매번 새로 만들던 부분을 장기 객체 재사용으로 바꿨다.
Probe 파일 `perf_node_single_linux_20260531_231111_node_single_reuse_recv_probe_20260531.txt`에서
routed tcp 64/65536/131072B는 기존 full과 같은 변동 범위라 통과를 만들지 못했다. 반면
PUBSUB tcp 131072B는 1,469.6 msg/s에서 9,841.0 msg/s로 올라 C 대비 95.4%가 됐다.
추가 probe `perf_node_single_linux_20260531_231240_node_single_pubsub_reuse_probe_20260531.txt`에서도
PUBSUB ws/wss/tls 대용량과 tls 256B가 통과권으로 올라갔다. PUBSUB 64B의 ws/wss/tls는
여전히 C 대비 30% 안팎이라 미달로 남긴다.

Multi 추가 개선에서는 echo 계열이 `decodeMetricHeader(...)` 객체를 만든 뒤 다시
`collector.record(...)`로 넘기던 부분을 `collector.recordPayload(...)` 직접 기록으로 바꿨다.
Probe 파일 `perf_node_multi_linux_20260531_232222_node_multi_recordpayload_probe_20260531.txt`에서
`MULTI_SPOT_REQREP tcp 65536B`는 20,901.6 ops/s에서 21,917.0 ops/s로 올라 C 대비 35.1%가 됐다.
`MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`, `MULTI_DEALER_DEALER` 일부는 소폭 변동했지만
통과권까지는 올라가지 않았다. 같은 실험을 `MULTI_SPOT_SENDSEND`에도 적용했으나 probe에서
회귀가 보여 해당 변경은 최종 코드에 남기지 않았다.

잔여 수치형 항목은 미달로 유지한다. Node runtime의 public 수신 API는 native 결과를 받은 뒤
`Received`/`TopicMessage` public 객체로 materialize한다. 이 과정에서 message part wrapper,
routing id, send context, topic envelope를 다시 구성해야 하므로 C hot path처럼 raw payload만
읽는 형태로 줄일 수 없다. 이번 라운드에서 중복 native binding 조회 제거, `RoutingId` 중복
길이 검증 제거, receive storage 재사용, 단일 수신 객체 재사용, metric header 객체 생성을
건너뛰는 직접 기록까지 적용했다. 2026-06-01 재검토에서는 routed multi echo server가
받은 메시지를 먼저 큐에 넣고 다시 drain하던 흐름도 줄였다. backlog가 없으면 public send
operation으로 즉시 응답하고, backpressure로 큐에 보관해야 할 때만 payload를 복사한다.
이 변경으로 `MULTI_DEALER_ROUTER` small과 encrypted large 일부가 통과권으로 올라갔다.
SPOT_SENDSEND 직접 기록 실험은 회귀가 확인되어 되돌렸다. 2026-06-01 추가 검토에서는
native `router.reply` 단일 part 경로가 매번 `std::vector<zlink_msg_t>`를 만들던 비용을 줄였다.
이 변경으로 `MULTI_ROUTER_ROUTER` tcp/ws small과 일부 131072B가 통과권으로 올라갔다.
`SendFlags.DontWait` 전환과 내부 direct submit 실험은 개선이 없거나 회귀가 있어 최종 코드에
남기지 않았다. `MULTI_STREAM`은 Node
server와 공용 C stream client 조합이므로 Node server hot path를 다시 확인했다. 전용 send
fast path, HWM 512, server IO thread 8 probe를 각각 실행했지만 통과권 개선을 만들지 못했고
일부는 회귀나 partial 실패가 있어 채택하지 않았다. 남은 차이는 이전 1차 개선 문서에서
쓰였던 `recvPayloadInto`, `publishFrom`, borrowed send helper 같은 public surface를 임의로
되살리지 않는 범위에서만 계속 줄인다. 따라서 1차 개선 표의 통과 수준은 회귀 검토 기준으로
사용하되, 현재 라운드에서는 public contract를 유지한 채 같은 효과를 낼 수 있는 내부 경로만
채택한다.


### 6.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `통과(36.8%)` | `통과(35.6%)` | `통과(55.6%)` | `통과(94.5%)` | `통과(94.6%)` | `통과(97.7%)` | 64/256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 65536/131072B는 Node 재확인 `perf_node_single_linux_20260601_073637_node_single_pair_dealer_tcp_failset_current_20260601.txt` 기준. |
| `tcp` | `PUBSUB` | `통과(36.9%)` | `통과(41.7%)` | `통과(53.2%)` | `통과(97.1%)` | `통과(95.2%)` | `통과(95.4%)` | 64/256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 65536/131072B는 Node PUBSUB 재사용 probe `perf_node_single_linux_20260531_231111_node_single_reuse_recv_probe_20260531.txt` 기준. |
| `tcp` | `DEALER_DEALER` | `통과(36.5%)` | `통과(36.0%)` | `통과(54.7%)` | `통과(94.5%)` | `통과(94.3%)` | `통과(97.1%)` | 64/256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 65536/131072B는 Node 재확인 `perf_node_single_linux_20260601_073637_node_single_pair_dealer_tcp_failset_current_20260601.txt` 기준. |
| `tcp` | `DEALER_ROUTER` | `통과(41.0%)` | `통과(41.4%)` | `통과(46.6%)` | `미달(21.7%)` | `미달(22.9%)` | `미달(10.2%)` | 64/256/1024B는 single routed 단일 payload native 수신 후보 complete 측정 `perf_node_single_linux_20260602_101414_node_single_routed_single_payload_native_probe_20260602.txt` 기준. 65536/131072B는 routed receive data-view 보강 후 `perf_node_single_linux_20260601_090436_node_single_routed_large_recv_dataview_final_20260601.txt` 기준. 262144B는 HWM 변경을 제외하고 routed failset 재측정 `perf_node_single_linux_20260531_120621_round_20260530_node_single_routed_failset_recheck.txt` 기준. |
| `tcp` | `ROUTER_ROUTER` | `통과(47.0%)` | `통과(48.2%)` | `통과(48.8%)` | `미달(21.8%)` | `미달(23.1%)` | `미달(10.2%)` | 64/256/1024B는 single routed 단일 payload native 수신 후보 complete 측정 `perf_node_single_linux_20260602_101414_node_single_routed_single_payload_native_probe_20260602.txt` 기준. 65536/131072B는 routed receive data-view 보강 후 `perf_node_single_linux_20260601_090436_node_single_routed_large_recv_dataview_final_20260601.txt` 기준. 262144B는 HWM 변경을 제외하고 routed failset 재측정 `perf_node_single_linux_20260531_120621_round_20260530_node_single_routed_failset_recheck.txt` 기준. |
| `tcp` | `SPOT` | `통과(43.2%)` | `통과(37.4%)` | `통과(36.0%)` | `통과(42.0%)` | `통과(34.3%)` | `통과(35.7%)` | 131072/262144B는 `Spot.publish()` topic 검증을 operation 생성 시점으로 옮긴 뒤 complete 측정 `perf_node_single_linux_20260602_063157_node_single_spot_publish_topic_prevalidate_tcp_large_probe_20260602.txt` 기준. 65536B는 SPOT active 수신 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_110840.txt` 기준. 나머지는 C full/Node full. |
| `ws` | `PAIR` | `통과(35.3%)` | `통과(36.7%)` | `통과(68.1%)` | `통과(97.7%)` | `통과(97.6%)` | `통과(38.1%)` | 64/256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 나머지는 C full/Node full. |
| `ws` | `PUBSUB` | `통과(35.8%)` | `통과(42.8%)` | `통과(62.2%)` | `통과(95.8%)` | `통과(97.0%)` | `통과(95.3%)` | 64B는 C/Node 제한 재측정 `perf_c_single_linux_20260601_140755_node_single_simple64_c_recheck_20260601.txt`, `perf_node_single_linux_20260601_140751_node_single_simple64_recheck_20260601.txt` 기준. 256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 65536/131072B는 Node PUBSUB 재사용 probe `perf_node_single_linux_20260531_231240_node_single_pubsub_reuse_probe_20260531.txt` 기준. |
| `ws` | `DEALER_DEALER` | `통과(35.0%)` | `통과(35.2%)` | `통과(64.9%)` | `통과(97.0%)` | `통과(97.4%)` | `통과(97.5%)` | 64/256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 나머지는 C full/Node full. |
| `ws` | `DEALER_ROUTER` | `통과(38.4%)` | `통과(44.7%)` | `통과(61.9%)` | `통과(41.6%)` | `통과(38.0%)` | `통과(33.4%)` | 64/256/1024B는 single routed 단일 payload native 수신 후보 small complete 측정 `perf_node_single_linux_20260602_101414_node_single_routed_single_payload_native_probe_20260602.txt` 기준. 65536/131072/262144B는 같은 후보의 large complete 측정 `perf_node_single_linux_20260602_101942_node_single_routed_single_payload_native_large_probe_20260602.txt` 기준. |
| `ws` | `ROUTER_ROUTER` | `통과(40.8%)` | `통과(50.3%)` | `통과(61.5%)` | `통과(39.3%)` | `통과(37.4%)` | `통과(35.4%)` | 64/256/1024B는 single routed 단일 payload native 수신 후보 small complete 측정 `perf_node_single_linux_20260602_101414_node_single_routed_single_payload_native_probe_20260602.txt` 기준. 65536/131072/262144B는 같은 후보의 large complete 측정 `perf_node_single_linux_20260602_101942_node_single_routed_single_payload_native_large_probe_20260602.txt` 기준. |
| `ws` | `SPOT` | `통과(47.7%)` | `통과(41.1%)` | `통과(36.3%)` | `통과(50.2%)` | `통과(44.3%)` | `통과(35.7%)` | 65536/131072/262144B는 SPOT active 수신 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_110840.txt` 기준. 나머지는 C full/Node full. |
| `wss` | `PAIR` | `통과(35.9%)` | `통과(37.0%)` | `통과(105.0%)` | `통과(37.7%)` | `통과(103.2%)` | `통과(76.5%)` | 64/256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 나머지는 C full/Node full. |
| `wss` | `PUBSUB` | `통과(38.3%)` | `통과(46.0%)` | `통과(104.7%)` | `통과(111.0%)` | `통과(111.0%)` | `통과(99.8%)` | 64B는 `TopicMessage._replace()` no-op close loop 제거 후 `perf_node_single_linux_20260602_023659_node_single_pubsub_topic_replace_no_close_wss64_probe_20260602.txt`와 C 제한 재측정 `perf_c_single_linux_20260601_140755_node_single_simple64_c_recheck_20260601.txt` 기준. 256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 65536/131072B는 Node PUBSUB 재사용 probe `perf_node_single_linux_20260531_231240_node_single_pubsub_reuse_probe_20260531.txt` 기준. |
| `wss` | `DEALER_DEALER` | `통과(35.3%)` | `통과(35.4%)` | `통과(106.0%)` | `통과(128.0%)` | `통과(105.9%)` | `통과(83.2%)` | 64/256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 나머지는 C full/Node full. |
| `wss` | `DEALER_ROUTER` | `통과(40.1%)` | `통과(46.7%)` | `통과(101.3%)` | `통과(90.9%)` | `통과(98.6%)` | `통과(79.8%)` | 64/256/1024B는 single routed 단일 payload native 수신 후보 complete 측정 `perf_node_single_linux_20260602_101414_node_single_routed_single_payload_native_probe_20260602.txt` 기준. 65536/131072B는 routed receive data-view 보강 후 `perf_node_single_linux_20260601_090436_node_single_routed_large_recv_dataview_final_20260601.txt` 기준. 262144B는 HWM 변경을 제외하고 routed failset 재측정 `perf_node_single_linux_20260531_120621_round_20260530_node_single_routed_failset_recheck.txt` 기준. |
| `wss` | `ROUTER_ROUTER` | `통과(43.6%)` | `통과(47.8%)` | `통과(100.1%)` | `통과(90.8%)` | `통과(97.7%)` | `통과(89.5%)` | 64/256/1024B는 single routed 단일 payload native 수신 후보 complete 측정 `perf_node_single_linux_20260602_101414_node_single_routed_single_payload_native_probe_20260602.txt` 기준. 65536/131072B는 routed receive data-view 보강 후 `perf_node_single_linux_20260601_090436_node_single_routed_large_recv_dataview_final_20260601.txt` 기준. 262144B는 HWM 변경을 제외하고 routed failset 재측정 `perf_node_single_linux_20260531_120621_round_20260530_node_single_routed_failset_recheck.txt` 기준. |
| `wss` | `SPOT` | `통과(42.7%)` | `통과(37.7%)` | `통과(229.9%)` | `통과(460.6%)` | `통과(439.9%)` | `통과(430.8%)` | 65536/131072/262144B는 SPOT active 수신 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_110840.txt` 기준. 나머지는 C full/Node full. |
| `tls` | `PAIR` | `통과(37.2%)` | `통과(36.1%)` | `통과(79.5%)` | `통과(97.5%)` | `통과(98.2%)` | `통과(98.7%)` | 64B는 C/Node 제한 재측정 `perf_c_single_linux_20260601_140755_node_single_simple64_c_recheck_20260601.txt`, `perf_node_single_linux_20260601_140751_node_single_simple64_recheck_20260601.txt` 기준. 256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 나머지는 C full/Node full. |
| `tls` | `PUBSUB` | `통과(35.2%)` | `통과(39.3%)` | `통과(72.4%)` | `통과(75.0%)` | `통과(98.5%)` | `통과(68.6%)` | 64B는 C/Node 제한 재측정 `perf_c_single_linux_20260601_140755_node_single_simple64_c_recheck_20260601.txt`, `perf_node_single_linux_20260601_140751_node_single_simple64_recheck_20260601.txt` 기준. 256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 65536/131072B는 Node PUBSUB 재사용 probe `perf_node_single_linux_20260531_231240_node_single_pubsub_reuse_probe_20260531.txt` 기준. |
| `tls` | `DEALER_DEALER` | `통과(36.1%)` | `통과(35.5%)` | `통과(76.9%)` | `통과(98.3%)` | `통과(97.7%)` | `통과(73.2%)` | 64B는 C/Node 제한 재측정 `perf_c_single_linux_20260601_140755_node_single_simple64_c_recheck_20260601.txt`, `perf_node_single_linux_20260601_140751_node_single_simple64_recheck_20260601.txt` 기준. 256/1024B는 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_111826.txt` 기준. 나머지는 C full/Node full. |
| `tls` | `DEALER_ROUTER` | `통과(37.4%)` | `통과(44.8%)` | `통과(69.4%)` | `통과(75.2%)` | `통과(77.5%)` | `통과(50.7%)` | 64/256/1024B는 single routed 단일 payload native 수신 후보 complete 측정 `perf_node_single_linux_20260602_101414_node_single_routed_single_payload_native_probe_20260602.txt` 기준. 65536/131072B는 routed receive data-view 보강 후 `perf_node_single_linux_20260601_090436_node_single_routed_large_recv_dataview_final_20260601.txt` 기준. 262144B는 HWM 변경을 제외하고 routed failset 재측정 `perf_node_single_linux_20260531_120621_round_20260530_node_single_routed_failset_recheck.txt` 기준. |
| `tls` | `ROUTER_ROUTER` | `통과(39.4%)` | `통과(46.1%)` | `통과(66.1%)` | `통과(73.5%)` | `통과(78.5%)` | `통과(52.6%)` | 64/256/1024B는 single routed 단일 payload native 수신 후보 complete 측정 `perf_node_single_linux_20260602_101414_node_single_routed_single_payload_native_probe_20260602.txt` 기준. 65536/131072B는 routed receive data-view 보강 후 `perf_node_single_linux_20260601_090436_node_single_routed_large_recv_dataview_final_20260601.txt` 기준. 262144B는 HWM 변경을 제외하고 routed failset 재측정 `perf_node_single_linux_20260531_120621_round_20260530_node_single_routed_failset_recheck.txt` 기준. |
| `tls` | `SPOT` | `통과(44.3%)` | `통과(36.3%)` | `통과(39.8%)` | `통과(75.2%)` | `통과(79.9%)` | `통과(76.7%)` | 65536/131072/262144B는 SPOT active 수신 raw payload 기록과 latency sample stride 32 적용 후 `perf_node_single_linux_20260601_110840.txt` 기준. 나머지는 C full/Node full. |


### 6.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `통과(47.4%)` | `통과(46.4%)` | `통과(40.6%)` | `해당 없음` | `통과(36.9%)` | `통과(38.5%)` | 65536/131072B는 `Received._replace()` no-op close loop 제거 후 `perf_node_multi_linux_20260601_233204_node_multi_routed_large_after_received_replace_no_close_probe_20260601.txt` 기준. 64/256/1024B는 latency sample stride 32 적용 후 `perf_node_multi_linux_20260601_105248.txt` 기준. Throughput은 모든 active payload를 세고 latency timestamp 계산만 샘플링한다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(38.7%)` | `통과(38.4%)` | `통과(37.4%)` | `해당 없음` | `통과(26.0%)` | `통과(32.4%)` | 65536/131072B는 `Received._replace()` no-op close loop 제거 후 `perf_node_multi_linux_20260601_233204_node_multi_routed_large_after_received_replace_no_close_probe_20260601.txt` 기준. 64/256/1024B는 Node native `router.reply` 단일 part stack fast path 최종 `perf_node_multi_linux_20260601_172824_node_multi_router_reply_single_stack_tcp_ws_final_20260601.txt` 기준. public 계약과 perf runner는 바꾸지 않았다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(31.1%)` | `통과(30.9%)` | `통과(30.5%)` | `해당 없음` | `통과(27.1%)` | `통과(33.4%)` | 65536/131072B는 `Received._replace()` no-op close loop 제거 후 `perf_node_multi_linux_20260601_233204_node_multi_routed_large_after_received_replace_no_close_probe_20260601.txt` 기준. 64/256/1024B는 Node native `router.reply` 단일 part stack fast path 최종 `perf_node_multi_linux_20260601_172824_node_multi_router_reply_single_stack_tcp_ws_final_20260601.txt` 기준. public 계약과 perf runner는 바꾸지 않았다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tcp` | `MULTI_PUBSUB` | `통과(31.1%)` | `통과(33.8%)` | `통과(71.0%)` | `해당 없음` | `미달(14.4%)` | `미달(21.2%)` | 단일 payload 내부 수신 경로 추가 후 complete 측정 `perf_node_multi_linux_20260602_110744_node_multi_pubsub_single_payload_native_probe_20260602.txt` 기준. public contract와 perf 기준/HWM/profile은 바꾸지 않았다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tcp` | `MULTI_SPOT` | `통과(59.1%)` | `통과(55.6%)` | `통과(56.4%)` | `해당 없음` | `통과(78.5%)` | `통과(158.3%)` | C full/Node fastpath full `perf_node_multi_linux_20260531_213502_node_fastpath_full_v1_20260531.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(40.4%)` | `통과(38.9%)` | `통과(41.5%)` | `해당 없음` | `통과(35.1%)` | `미달(22.8%)` | 131072B는 SPOT routed no-wait 수신 경로 추가 후 `perf_node_multi_linux_20260602_044245_node_multi_spot_reqrep_routed_nowait_large_probe_20260602.txt` 기준. 65536B는 Node recordPayload probe `perf_node_multi_linux_20260531_232222_node_multi_recordpayload_probe_20260531.txt` 기준. 나머지는 C full/Node fastpath full `perf_node_multi_linux_20260531_213502_node_fastpath_full_v1_20260531.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(34.1%)` | `통과(31.7%)` | `통과(29.0%)` | `해당 없음` | `미달(25.9%)` | `미달(26.0%)` | 64/256/1024B는 `sendToSpot` DontWait result-code 경로 추가 후 complete 측정 `perf_node_multi_linux_20260602_104143_node_multi_spot_sendsend_nowait_result_small_recheck_20260602.txt` 기준. large는 client active loop를 C처럼 send 진행이 있을 때 즉시 다음 send sweep으로 넘긴 뒤 `perf_node_multi_linux_20260601_100833_node_multi_spot_sendsend_cstyle_large_probe_20260601.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tcp` | `MULTI_STREAM` | `미달(26.6%)` | `미달(27.9%)` | `미달(24.6%)` | `해당 없음` | `통과(62.2%)` | `해당 없음` | pending reply queue를 head-index로 바꾼 뒤 complete 측정 `perf_node_multi_linux_20260601_121847_node_multi_stream_pending_queue_probe_20260601.txt` 기준. small은 기존보다 올랐고 65536B는 낮아졌지만 통과권을 유지한다. MULTI_STREAM은 runner 정책상 4096B와 131072B 결과를 내지 않는다. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(45.2%)` | `통과(53.9%)` | `통과(73.7%)` | `해당 없음` | `통과(41.5%)` | `통과(43.6%)` | 65536/131072B는 `Received._replace()` no-op close loop 제거 후 `perf_node_multi_linux_20260601_233204_node_multi_routed_large_after_received_replace_no_close_probe_20260601.txt` 기준. 64/256/1024B는 latency sample stride 32 적용 후 `perf_node_multi_linux_20260601_105248.txt` 기준. Throughput은 모든 active payload를 세고 latency timestamp 계산만 샘플링한다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(39.2%)` | `통과(36.5%)` | `통과(36.9%)` | `해당 없음` | `통과(25.8%)` | `통과(34.1%)` | 65536/131072B는 `Received._replace()` no-op close loop 제거 후 `perf_node_multi_linux_20260601_233204_node_multi_routed_large_after_received_replace_no_close_probe_20260601.txt` 기준. 64/256/1024B는 Node native `router.reply` 단일 part stack fast path 최종 `perf_node_multi_linux_20260601_172824_node_multi_router_reply_single_stack_tcp_ws_final_20260601.txt` 기준. public 계약과 perf runner는 바꾸지 않았다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(32.0%)` | `통과(32.5%)` | `통과(30.8%)` | `해당 없음` | `통과(28.0%)` | `통과(35.0%)` | 65536/131072B는 `Received._replace()` no-op close loop 제거 후 `perf_node_multi_linux_20260601_233204_node_multi_routed_large_after_received_replace_no_close_probe_20260601.txt` 기준. 64/256/1024B는 Node native `router.reply` 단일 part stack fast path 최종 `perf_node_multi_linux_20260601_172824_node_multi_router_reply_single_stack_tcp_ws_final_20260601.txt` 기준. public 계약과 perf runner는 바꾸지 않았다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `ws` | `MULTI_PUBSUB` | `통과(31.7%)` | `미달(26.0%)` | `통과(40.0%)` | `해당 없음` | `통과(30.3%)` | `통과(33.0%)` | 단일 payload 내부 수신 경로 추가 후 complete 측정 `perf_node_multi_linux_20260602_110744_node_multi_pubsub_single_payload_native_probe_20260602.txt` 기준. public contract와 perf 기준/HWM/profile은 바꾸지 않았다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `ws` | `MULTI_SPOT` | `통과(55.4%)` | `통과(52.6%)` | `통과(52.5%)` | `해당 없음` | `통과(104.3%)` | `통과(162.8%)` | C full/Node fastpath full `perf_node_multi_linux_20260531_213502_node_fastpath_full_v1_20260531.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(47.8%)` | `통과(48.6%)` | `통과(49.8%)` | `해당 없음` | `통과(48.3%)` | `미달(21.8%)` | 131072B는 SPOT routed no-wait 수신 경로 추가 후 `perf_node_multi_linux_20260602_044245_node_multi_spot_reqrep_routed_nowait_large_probe_20260602.txt` 기준. 나머지는 C full/Node fastpath full `perf_node_multi_linux_20260531_213502_node_fastpath_full_v1_20260531.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(30.3%)` | `통과(29.3%)` | `통과(32.6%)` | `해당 없음` | `통과(40.5%)` | `통과(36.0%)` | 64/256/1024B는 `sendToSpot` DontWait result-code 경로 추가 후 complete 측정 `perf_node_multi_linux_20260602_104143_node_multi_spot_sendsend_nowait_result_small_recheck_20260602.txt` 기준. 131072B는 `requestSeq=0` reply context 제거 후 `perf_node_multi_linux_20260601_220203_node_multi_spot_sendsend_no_replyctx_probe_20260601.txt` 기준. 65536B는 large `perf_node_multi_linux_20260601_100833_node_multi_spot_sendsend_cstyle_large_probe_20260601.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `ws` | `MULTI_STREAM` | `미달(18.9%)` | `미달(19.7%)` | `미달(19.1%)` | `해당 없음` | `통과(80.7%)` | `해당 없음` | pending reply queue를 head-index로 바꾼 뒤 complete 측정 `perf_node_multi_linux_20260601_121847_node_multi_stream_pending_queue_probe_20260601.txt` 기준. 64/256/1024B와 65536B 모두 기존보다 올랐다. MULTI_STREAM은 runner 정책상 4096B와 131072B 결과를 내지 않는다. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(51.1%)` | `통과(60.3%)` | `통과(58.1%)` | `해당 없음` | `통과(53.9%)` | `통과(53.9%)` | latency sample stride 32 적용 후 `perf_node_multi_linux_20260601_105248.txt` 기준. Throughput은 모든 active payload를 세고 latency timestamp 계산만 샘플링한다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(37.4%)` | `통과(36.2%)` | `통과(33.4%)` | `해당 없음` | `통과(36.1%)` | `통과(38.0%)` | Node 즉시 reply 개선 최종 `perf_node_multi_linux_20260601_082420_node_multi_routed_immediate_reply_final_20260601.txt` 기준. 전 size가 통과했다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(31.3%)` | `통과(30.1%)` | `통과(31.2%)` | `해당 없음` | `통과(38.4%)` | `통과(40.0%)` | 256/1024B는 제한 재측정 `perf_node_multi_linux_20260601_140048_node_multi_rr_wss_tls_256_1024_recheck_20260601.txt` 기준. 나머지는 Node 즉시 reply 개선 최종 `perf_node_multi_linux_20260601_082420_node_multi_routed_immediate_reply_final_20260601.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `wss` | `MULTI_PUBSUB` | `미달(29.2%)` | `통과(30.9%)` | `통과(46.5%)` | `해당 없음` | `통과(34.2%)` | `통과(35.2%)` | 단일 payload 내부 수신 경로 추가 후 complete 측정 `perf_node_multi_linux_20260602_110744_node_multi_pubsub_single_payload_native_probe_20260602.txt` 기준. public contract와 perf 기준/HWM/profile은 바꾸지 않았다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `wss` | `MULTI_SPOT` | `통과(53.6%)` | `통과(51.7%)` | `통과(51.1%)` | `해당 없음` | `통과(574.4%)` | `통과(760.0%)` | C full/Node fastpath full `perf_node_multi_linux_20260531_213502_node_fastpath_full_v1_20260531.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(44.1%)` | `통과(46.4%)` | `통과(52.1%)` | `해당 없음` | `통과(91.9%)` | `통과(66.6%)` | C full/Node fastpath full `perf_node_multi_linux_20260531_213502_node_fastpath_full_v1_20260531.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(29.1%)` | `미달(23.1%)` | `통과(29.8%)` | `해당 없음` | `통과(64.1%)` | `통과(58.3%)` | 64/256/1024B는 `sendToSpot` DontWait result-code 경로 추가 후 complete 측정 `perf_node_multi_linux_20260602_104143_node_multi_spot_sendsend_nowait_result_small_recheck_20260602.txt` 기준. large는 client active loop를 C-style send sweep으로 정렬한 뒤 `perf_node_multi_linux_20260601_100833_node_multi_spot_sendsend_cstyle_large_probe_20260601.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `wss` | `MULTI_STREAM` | `미달(27.3%)` | `미달(26.6%)` | `미달(26.0%)` | `해당 없음` | `통과(99.8%)` | `해당 없음` | pending reply queue를 head-index로 바꾼 뒤 complete 측정 `perf_node_multi_linux_20260601_121847_node_multi_stream_pending_queue_probe_20260601.txt` 기준. 모든 measured size가 기존보다 올랐다. MULTI_STREAM은 runner 정책상 4096B와 131072B 결과를 내지 않는다. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(45.2%)` | `통과(55.8%)` | `통과(70.6%)` | `해당 없음` | `통과(42.2%)` | `통과(40.7%)` | latency sample stride 32 적용 후 `perf_node_multi_linux_20260601_105248.txt` 기준. Throughput은 모든 active payload를 세고 latency timestamp 계산만 샘플링한다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(36.7%)` | `통과(35.3%)` | `통과(34.3%)` | `해당 없음` | `통과(31.0%)` | `통과(37.5%)` | Node 즉시 reply 개선 최종 `perf_node_multi_linux_20260601_082420_node_multi_routed_immediate_reply_final_20260601.txt` 기준. 전 size가 통과했다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(30.6%)` | `통과(32.0%)` | `통과(32.0%)` | `해당 없음` | `통과(31.6%)` | `통과(36.1%)` | 256/1024B는 C 제한 재측정 `perf_c_multi_linux_20260601_140139_node_rr_tls_256_1024_c_recheck_20260601.txt`와 Node 제한 재측정 `perf_node_multi_linux_20260601_140048_node_multi_rr_wss_tls_256_1024_recheck_20260601.txt` 기준. 나머지는 Node 즉시 reply 개선 최종 `perf_node_multi_linux_20260601_082420_node_multi_routed_immediate_reply_final_20260601.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tls` | `MULTI_PUBSUB` | `통과(30.1%)` | `통과(30.0%)` | `통과(40.3%)` | `해당 없음` | `통과(31.2%)` | `통과(33.1%)` | 단일 payload 내부 수신 경로 추가 후 complete 측정 `perf_node_multi_linux_20260602_110744_node_multi_pubsub_single_payload_native_probe_20260602.txt` 기준. public contract와 perf 기준/HWM/profile은 바꾸지 않았다. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tls` | `MULTI_SPOT` | `통과(52.1%)` | `통과(52.8%)` | `통과(48.9%)` | `해당 없음` | `통과(134.3%)` | `통과(206.7%)` | C full/Node fastpath full `perf_node_multi_linux_20260531_213502_node_fastpath_full_v1_20260531.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(39.4%)` | `통과(44.8%)` | `통과(46.0%)` | `해당 없음` | `통과(79.3%)` | `통과(43.5%)` | C full/Node fastpath full `perf_node_multi_linux_20260531_213502_node_fastpath_full_v1_20260531.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(28.8%)` | `통과(33.5%)` | `통과(32.2%)` | `해당 없음` | `통과(57.1%)` | `통과(48.1%)` | 64/256/1024B는 `sendToSpot` DontWait result-code 경로 추가 후 complete 측정 `perf_node_multi_linux_20260602_104143_node_multi_spot_sendsend_nowait_result_small_recheck_20260602.txt` 기준. large는 client active loop를 C-style send sweep으로 정렬한 뒤 `perf_node_multi_linux_20260601_100833_node_multi_spot_sendsend_cstyle_large_probe_20260601.txt` 기준. Node multi report에 4096B RESULT가 없어 4096B는 해당 없음으로 둔다. |
| `tls` | `MULTI_STREAM` | `미달(24.5%)` | `미달(25.1%)` | `미달(25.1%)` | `해당 없음` | `통과(81.6%)` | `해당 없음` | pending reply queue를 head-index로 바꾼 뒤 complete 측정 `perf_node_multi_linux_20260601_121847_node_multi_stream_pending_queue_probe_20260601.txt` 기준. small은 기존보다 올랐고 65536B는 낮아졌지만 통과권을 유지한다. MULTI_STREAM은 runner 정책상 4096B와 131072B 결과를 내지 않는다. |

## 7. Go 상태

perf 경로: `bindings/go/perf`.

Single smoke `perf_go_single_linux_20260531_134455_round_20260530_go_single_smoke_64.txt`와
single full `perf_go_single_linux_20260531_134546_round_20260530_go_single_full_v1.txt`는
모두 complete였다. 미달 후보는 C `perf_c_single_linux_20260531_135902_round_20260530_c_single_go_failset_recheck.txt`,
Go `perf_go_single_linux_20260531_140124_round_20260530_go_single_failset_recheck.txt`로 다시 확인했다.
잔류 미달는 routed tcp 대용량 5개와 `SPOT wss 256B` 1개다. Go public binding 경로를 유지한
상태에서 routed tcp 대용량과 WSS SPOT 단일 조건만 좁게 줄일 추가 내부 후보는 확인되지 않았다.

Multi smoke `perf_go_multi_linux_20260531_140350_round_20260530_go_multi_smoke_64.txt`는 complete였다.
Multi full `perf_go_multi_linux_20260531_140640_round_20260530_go_multi_full_v1.txt`는 partial(925/960)로
끝났고, `perf_go_multi_linux_20260531_143459_round_20260530_go_multi_partial_fill.txt`,
`perf_go_multi_linux_20260531_144256_round_20260530_go_multi_dealer_dealer_missing_fill.txt`,
`perf_go_multi_linux_20260531_144414_round_20260530_go_multi_pubsub_missing_fill.txt`,
`perf_go_multi_linux_20260531_144532_round_20260530_go_multi_pubsub_tls_65536_final.txt`로
보강했다. 2026-06-01 제한 재측정에서는 `MULTI_DEALER_DEALER ws 4096B`,
`MULTI_DEALER_DEALER wss 65536B`, `MULTI_DEALER_DEALER tcp/wss 131072B`,
`MULTI_PUBSUB tcp/tls/wss 256B`, `MULTI_PUBSUB tls 65536B`, `MULTI_SPOT wss 1024/4096B`가
완료 리포트 기준으로 통과권에 회복됐다.
`MULTI_DEALER_DEALER tcp 4096B`는 단독 runs=3과 runs=5에서도 반복해서 `RESULT`가 없어
미달로 남긴다. 나머지 잔류 미달는 22개다. Multi 잔여 항목은
goroutine 스케줄링, cgo 호출 경계, routed/SPOT envelope 처리 비용이 함께 나타나는 구간으로,
public API를 우회하지 않는 추가 내부 후보가 확인되지 않았다.
2026-06-01 재검토에서는 `MULTI_SPOT_REQREP`/`MULTI_SPOT_SENDSEND`의 65536B를 transport별로
제한 재측정했다. tls/ws/wss는 통과권으로 회복됐고, tcp는 runs=5에서도 낮은 median이 반복되어
미달로 둔다.

추가 검토에서는 routed single과 multi echo 송신 경로가 매 메시지마다 `Message` wrapper를 만드는
비용을 줄일 수 있는지 확인했다. public `Bytes(...)` builder로 재사용 payload slice를 보내는
실험을 적용하고 single `perf_go_single_linux_20260531_232858_go_single_bytes_send_probe_20260531.txt`,
multi `perf_go_multi_linux_20260531_232934_go_multi_bytes_send_probe_20260531.txt`로 측정했다. 그러나
single `DEALER_ROUTER tcp 65536B/131072B/262144B`는 기존 50,457.6/27,961.6/14,364.8 msg/s에서
45,336.0/24,481.3/13,950.0 msg/s로 낮아졌고, `ROUTER_ROUTER`도 같은 방향으로 낮아졌다.
multi routed echo도 통과권 개선 없이 대체로 기존 full보다 낮거나 같은 변동 범위였다. 이 실험
변경은 최종 코드에 남기지 않았다. Go perf driver는 이미 single routed 수신에서 `RecvPart`,
SPOT/PUBSUB 수신에서 `SubscribePart`, multi 일부 서버 경로에서 `RecvPart`를 사용하고 있어
public API 범위에서 더 줄일 수 있는 좁은 내부 변경점은 확인되지 않았다. 아래 잔여 수치형
미달은 미달로 판정한다.


### 7.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `통과(72.0%)` | `통과(70.9%)` | `통과(112.0%)` | `통과(97.3%)` | `통과(97.4%)` | `통과(97.6%)` | C full/Go full. |
| `tcp` | `PUBSUB` | `통과(59.5%)` | `통과(63.7%)` | `통과(87.5%)` | `통과(96.9%)` | `통과(97.3%)` | `통과(97.5%)` | C full/Go full. |
| `tcp` | `DEALER_DEALER` | `통과(71.9%)` | `통과(70.9%)` | `통과(109.8%)` | `통과(97.3%)` | `통과(97.1%)` | `통과(97.1%)` | C full/Go full. |
| `tcp` | `DEALER_ROUTER` | `통과(53.5%)` | `통과(53.5%)` | `통과(61.2%)` | `미달(46.0%)` | `미달(44.5%)` | `미달(42.5%)` | C/Go single failset 제한 재측정 기준. |
| `tcp` | `ROUTER_ROUTER` | `통과(61.4%)` | `통과(60.4%)` | `통과(63.9%)` | `통과(47.4%)` | `미달(44.9%)` | `미달(42.8%)` | C/Go single failset 제한 재측정 기준. |
| `tcp` | `SPOT` | `통과(99.7%)` | `통과(85.0%)` | `통과(100.0%)` | `통과(77.5%)` | `통과(60.2%)` | `통과(64.2%)` | C/Go single failset 제한 재측정 기준. |
| `ws` | `PAIR` | `통과(71.9%)` | `통과(66.7%)` | `통과(108.2%)` | `통과(97.1%)` | `통과(97.2%)` | `통과(96.9%)` | C full/Go full. |
| `ws` | `PUBSUB` | `통과(58.3%)` | `통과(57.8%)` | `통과(81.8%)` | `통과(97.2%)` | `통과(96.7%)` | `통과(97.2%)` | C full/Go full. |
| `ws` | `DEALER_DEALER` | `통과(72.1%)` | `통과(65.5%)` | `통과(106.0%)` | `통과(96.3%)` | `통과(97.1%)` | `통과(97.4%)` | C full/Go full. |
| `ws` | `DEALER_ROUTER` | `통과(56.0%)` | `통과(55.3%)` | `통과(74.3%)` | `통과(68.4%)` | `통과(62.7%)` | `통과(58.3%)` | C full/Go full. |
| `ws` | `ROUTER_ROUTER` | `통과(65.4%)` | `통과(54.9%)` | `통과(67.4%)` | `통과(65.0%)` | `통과(63.5%)` | `통과(62.1%)` | C full/Go full. |
| `ws` | `SPOT` | `통과(88.2%)` | `통과(117.5%)` | `통과(100.7%)` | `통과(95.7%)` | `통과(104.3%)` | `통과(73.6%)` | C full/Go full. |
| `wss` | `PAIR` | `통과(73.4%)` | `통과(71.8%)` | `통과(109.2%)` | `통과(88.0%)` | `통과(76.3%)` | `통과(79.6%)` | C full/Go full. |
| `wss` | `PUBSUB` | `통과(63.1%)` | `통과(62.8%)` | `통과(113.0%)` | `통과(82.3%)` | `통과(75.8%)` | `통과(83.2%)` | C full/Go full. |
| `wss` | `DEALER_DEALER` | `통과(72.7%)` | `통과(71.9%)` | `통과(110.8%)` | `통과(87.4%)` | `통과(79.0%)` | `통과(75.6%)` | C full/Go full. |
| `wss` | `DEALER_ROUTER` | `통과(55.2%)` | `통과(55.1%)` | `통과(91.4%)` | `통과(81.5%)` | `통과(90.2%)` | `통과(92.4%)` | C/Go single failset 제한 재측정 기준. |
| `wss` | `ROUTER_ROUTER` | `통과(67.1%)` | `통과(56.3%)` | `통과(93.5%)` | `통과(83.7%)` | `통과(87.9%)` | `통과(117.7%)` | C/Go single failset 제한 재측정 기준. |
| `wss` | `SPOT` | `통과(90.2%)` | `미달(43.7%)` | `통과(106.1%)` | `통과(98.0%)` | `통과(100.3%)` | `통과(300.0%)` | C/Go single failset 제한 재측정 기준. |
| `tls` | `PAIR` | `통과(71.8%)` | `통과(71.4%)` | `통과(110.1%)` | `통과(73.6%)` | `통과(79.3%)` | `통과(88.1%)` | C full/Go full. |
| `tls` | `PUBSUB` | `통과(62.0%)` | `통과(62.6%)` | `통과(114.0%)` | `통과(77.9%)` | `통과(86.6%)` | `통과(87.8%)` | C full/Go full. |
| `tls` | `DEALER_DEALER` | `통과(72.3%)` | `통과(71.4%)` | `통과(109.8%)` | `통과(72.8%)` | `통과(79.7%)` | `통과(87.1%)` | C full/Go full. |
| `tls` | `DEALER_ROUTER` | `통과(56.2%)` | `통과(57.0%)` | `통과(88.9%)` | `통과(87.7%)` | `통과(87.0%)` | `통과(86.3%)` | C full/Go full. |
| `tls` | `ROUTER_ROUTER` | `통과(62.3%)` | `통과(61.3%)` | `통과(89.3%)` | `통과(85.1%)` | `통과(86.8%)` | `통과(88.8%)` | C full/Go full. |
| `tls` | `SPOT` | `통과(92.4%)` | `통과(89.2%)` | `통과(104.8%)` | `통과(98.9%)` | `통과(102.5%)` | `통과(100.1%)` | C full/Go full. |


### 7.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `미달(43.1%)` | `통과(64.1%)` | `통과(75.8%)` | `미달(RESULT 없음)` | `통과(79.8%)` | `통과(55.2%)` | 64/256B는 제한 재측정 `perf_go_multi_linux_20260601_201047_go_multi_simple_small_failset_recheck_20260601.txt` 기준. 131072B는 단독 완료 재측정 `perf_go_multi_linux_20260601_195945_go_multi_dealer_dealer_131072_recheck_20260601.txt` 기준. 4096B는 반복 재측정에서도 RESULT가 없어 미달로 둔다. 나머지는 C full/Go multi full. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(55.2%)` | `통과(57.2%)` | `통과(60.1%)` | `통과(65.0%)` | `미달(31.9%)` | `통과(44.0%)` | 64/256/1024/65536B는 제한 재측정 `perf_go_multi_linux_20260601_200558_go_multi_routed_tcp_tls_failset_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `미달(36.5%)` | `미달(37.5%)` | `미달(37.7%)` | `통과(40.3%)` | `미달(38.3%)` | `통과(46.0%)` | 64/256/1024/65536B는 제한 재측정 `perf_go_multi_linux_20260601_200558_go_multi_routed_tcp_tls_failset_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `tcp` | `MULTI_PUBSUB` | `미달(47.5%)` | `통과(59.3%)` | `통과(103.4%)` | `통과(78.2%)` | `통과(55.5%)` | `통과(71.0%)` | 64/256B는 제한 재측정 `perf_go_multi_linux_20260601_201047_go_multi_simple_small_failset_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `tcp` | `MULTI_SPOT` | `통과(59.1%)` | `통과(61.7%)` | `통과(56.5%)` | `통과(68.8%)` | `통과(111.6%)` | `통과(104.7%)` | C full/Go multi full. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(54.1%)` | `통과(55.3%)` | `통과(55.5%)` | `통과(65.0%)` | `미달(1.1%)` | `통과(53.9%)` | 256/4096/65536B는 request message 누수 수정 뒤 complete 재확인 `perf_go_multi_linux_20260602_152744_go_multi_spot_reqrep_message_close_tcp_reconfirm_20260602.txt` 기준. 4096B는 통과로 회복했고 65536B는 여전히 기준에 못 닿아 미달한다. 나머지는 C full/Go multi full. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(63.7%)` | `통과(57.9%)` | `통과(63.8%)` | `통과(68.4%)` | `미달(3.7%)` | `통과(77.2%)` | 65536B는 runs=5 제한 재측정 `perf_go_multi_linux_20260601_194109_go_multi_spot_tcp65536_runs5_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `tcp` | `MULTI_STREAM` | `통과(100.4%)` | `통과(95.2%)` | `통과(89.7%)` | `통과(64.6%)` | `통과(67.0%)` | `통과(72.9%)` | C full/Go multi full. |
| `ws` | `MULTI_DEALER_DEALER` | `미달(46.8%)` | `통과(56.1%)` | `통과(71.7%)` | `통과(55.1%)` | `통과(60.0%)` | `통과(62.1%)` | 64/256B는 제한 재측정 `perf_go_multi_linux_20260601_201047_go_multi_simple_small_failset_recheck_20260601.txt` 기준. 4096B는 단독 완료 재측정 `perf_go_multi_linux_20260601_195703_go_multi_dealer_dealer_ws_4096_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(57.5%)` | `통과(58.9%)` | `통과(60.2%)` | `통과(58.0%)` | `미달(37.0%)` | `통과(57.8%)` | C full/Go multi full. 보강 재측정 기준 포함. |
| `ws` | `MULTI_ROUTER_ROUTER` | `미달(39.0%)` | `통과(42.4%)` | `통과(40.2%)` | `통과(42.4%)` | `통과(44.7%)` | `통과(57.2%)` | C full/Go multi full. |
| `ws` | `MULTI_PUBSUB` | `통과(343.7%)` | `통과(90.7%)` | `통과(66.2%)` | `통과(113.0%)` | `통과(83.0%)` | `통과(76.9%)` | 64/256B는 제한 재측정 `perf_go_multi_linux_20260601_201047_go_multi_simple_small_failset_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `ws` | `MULTI_SPOT` | `통과(54.3%)` | `통과(57.1%)` | `통과(56.3%)` | `통과(67.0%)` | `통과(109.3%)` | `통과(112.5%)` | C full/Go multi full. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(62.9%)` | `통과(63.4%)` | `통과(59.5%)` | `통과(54.5%)` | `통과(67.1%)` | `통과(50.1%)` | 131072B는 request message 누수 수정 검증 `perf_go_multi_linux_20260602_152143_go_multi_spot_reqrep_message_close_fullpattern_20260602.txt` 기준으로 통과권에 회복했다. 65536B는 제한 재측정 `perf_go_multi_linux_20260601_193756_go_multi_spot_65536_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(55.3%)` | `통과(58.2%)` | `통과(58.6%)` | `통과(68.7%)` | `통과(89.8%)` | `통과(83.9%)` | 65536B는 제한 재측정 `perf_go_multi_linux_20260601_193756_go_multi_spot_65536_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `ws` | `MULTI_STREAM` | `통과(105.1%)` | `통과(100.7%)` | `통과(88.5%)` | `통과(87.3%)` | `통과(93.5%)` | `통과(105.2%)` | C full/Go multi full. |
| `wss` | `MULTI_DEALER_DEALER` | `미달(44.6%)` | `통과(56.6%)` | `통과(67.9%)` | `통과(55.3%)` | `통과(50.9%)` | `통과(52.9%)` | 64/256B는 제한 재측정 `perf_go_multi_linux_20260601_201047_go_multi_simple_small_failset_recheck_20260601.txt` 기준. 65536B는 단독 완료 재측정 `perf_go_multi_linux_20260601_195558_go_multi_dealer_dealer_wss_tls_65536_recheck_20260601.txt` 기준. 131072B는 단독 완료 재측정 `perf_go_multi_linux_20260601_195945_go_multi_dealer_dealer_131072_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(59.2%)` | `통과(60.2%)` | `통과(58.4%)` | `통과(53.1%)` | `통과(48.0%)` | `통과(49.4%)` | C full/Go multi full. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(40.4%)` | `통과(40.9%)` | `통과(42.1%)` | `통과(45.0%)` | `통과(50.3%)` | `통과(52.1%)` | C full/Go multi full. |
| `wss` | `MULTI_PUBSUB` | `통과(162.8%)` | `통과(98.1%)` | `통과(76.1%)` | `통과(84.9%)` | `통과(60.7%)` | `통과(68.2%)` | 64/256B는 제한 재측정 `perf_go_multi_linux_20260601_201047_go_multi_simple_small_failset_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `wss` | `MULTI_SPOT` | `통과(53.4%)` | `통과(56.6%)` | `통과(58.2%)` | `통과(171.9%)` | `통과(71.3%)` | `통과(75.1%)` | 1024/4096B는 제한 재측정 `perf_go_multi_linux_20260601_200901_go_multi_spot_wss_1024_4096_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(56.2%)` | `통과(61.6%)` | `통과(60.2%)` | `통과(67.1%)` | `통과(88.3%)` | `통과(84.2%)` | 65536B는 제한 재측정 `perf_go_multi_linux_20260601_193756_go_multi_spot_65536_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(56.2%)` | `통과(58.8%)` | `통과(62.3%)` | `통과(87.0%)` | `통과(94.5%)` | `통과(91.3%)` | 65536B는 제한 재측정 `perf_go_multi_linux_20260601_193756_go_multi_spot_65536_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `wss` | `MULTI_STREAM` | `통과(98.6%)` | `통과(98.4%)` | `통과(91.5%)` | `통과(87.9%)` | `통과(92.5%)` | `통과(99.9%)` | C full/Go multi full. |
| `tls` | `MULTI_DEALER_DEALER` | `미달(44.1%)` | `통과(57.3%)` | `통과(70.8%)` | `통과(61.9%)` | `미달(47.3%)` | `미달(28.0%)` | 64/256B는 제한 재측정 `perf_go_multi_linux_20260601_201047_go_multi_simple_small_failset_recheck_20260601.txt` 기준. 65536B는 단독 완료 재측정 `perf_go_multi_linux_20260601_195558_go_multi_dealer_dealer_wss_tls_65536_recheck_20260601.txt` 기준. 131072B는 단독 완료 재측정 `perf_go_multi_linux_20260601_195945_go_multi_dealer_dealer_131072_recheck_20260601.txt` 기준. 64/65536/131072B는 기준에 못 닿아 미달한다. 나머지는 C full/Go multi full. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(51.5%)` | `통과(53.0%)` | `통과(52.5%)` | `통과(57.7%)` | `통과(42.6%)` | `통과(52.5%)` | 64/256/1024/65536B는 제한 재측정 `perf_go_multi_linux_20260601_200558_go_multi_routed_tcp_tls_failset_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `tls` | `MULTI_ROUTER_ROUTER` | `미달(37.8%)` | `미달(37.8%)` | `미달(39.4%)` | `통과(41.4%)` | `통과(43.3%)` | `통과(52.2%)` | 64/256/1024/65536B는 제한 재측정 `perf_go_multi_linux_20260601_200558_go_multi_routed_tcp_tls_failset_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `tls` | `MULTI_PUBSUB` | `통과(107.0%)` | `통과(85.5%)` | `통과(65.1%)` | `통과(100.5%)` | `통과(68.2%)` | `통과(68.2%)` | 64/256B는 제한 재측정 `perf_go_multi_linux_20260601_201047_go_multi_simple_small_failset_recheck_20260601.txt` 기준. 65536B는 단독 완료 재측정 `perf_go_multi_linux_20260601_195633_go_multi_pubsub_tls_65536_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `tls` | `MULTI_SPOT` | `통과(55.7%)` | `통과(54.0%)` | `통과(57.1%)` | `통과(54.7%)` | `통과(112.6%)` | `통과(98.9%)` | C full/Go multi full. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(52.8%)` | `통과(60.8%)` | `통과(58.1%)` | `통과(53.2%)` | `통과(84.0%)` | `통과(68.0%)` | 65536B는 제한 재측정 `perf_go_multi_linux_20260601_193756_go_multi_spot_65536_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(55.9%)` | `통과(64.8%)` | `통과(63.7%)` | `통과(79.8%)` | `통과(60.3%)` | `통과(89.3%)` | 65536B는 제한 재측정 `perf_go_multi_linux_20260601_193756_go_multi_spot_65536_recheck_20260601.txt` 기준. 나머지는 C full/Go multi full. |
| `tls` | `MULTI_STREAM` | `통과(96.0%)` | `통과(93.9%)` | `통과(89.9%)` | `통과(86.2%)` | `통과(99.7%)` | `통과(91.8%)` | C full/Go multi full. |


## 8. Rust 상태

perf 경로: `bindings/rust/perf`.

Rust single smoke 결과 파일은 `perf_rust_single_linux_20260531_145023_round_20260530_rust_single_smoke_64.txt`이고 status=complete(120/120)였다. Single full 결과 파일은 `perf_rust_single_linux_20260531_145104_round_20260530_rust_single_full_v1.txt`이고 status=complete(720/720)였다. 후보 재측정 파일 `perf_rust_single_linux_20260531_151316_round_20260530_rust_single_failset_recheck.txt`는 status=complete(480/480)였다. 추가 개선으로 SPOT active send에서 1024B 이하 payload는 public `Message::with_size(...).data_mut()`에 직접 header를 채우도록 바꿨다. 이번 라운드에서는 single 공통 송신 loop도 public `Message::with_size(...).data_mut()` 직접 작성으로 바꿨다. Complete probe `perf_rust_single_linux_20260602_162501_rust_single_direct_message_pubsub_small_probe_20260602.txt`에서 `PUBSUB wss 64B`가 83.0%로 통과했고, 나머지 PUBSUB small 미달은 기준에 못 닿았다. routed 대용량 complete probe `perf_rust_single_linux_20260602_162557_rust_single_direct_message_routed_large_probe_20260602.txt`와 `SPOT tcp 1024B` complete probe `perf_rust_single_linux_20260602_164334_rust_single_direct_message_spot_tcp1024_probe_20260602.txt`는 새 통과를 만들지 못했다. PUBSUB 수신 `TopicMessage` 재사용 재확인 `perf_rust_single_linux_20260602_164508_rust_single_direct_message_pubsub_reuse_small_probe_20260602.txt`도 통과 수를 늘리지 못해 최종 코드에 남기지 않았다. 이후 table transport full `perf_rust_single_linux_20260602_175921_rust_single_after_candidates_table_transports_full_20260602.txt`는 status=complete(720/720)였고, routed 대용량과 `SPOT tcp/ws/tls 1024B`가 미달로 확인됐다. routed 대용량은 public `recv(&mut Received, ...)` envelope 경로를 유지해야 하며, Rust binding에는 C의 part 직접 수신을 노출한 public API가 없어 미달한다.

Rust multi smoke 결과 파일은 `perf_rust_multi_linux_20260531_153200_round_20260530_rust_multi_smoke_64.txt`이고 status=complete(160/160)였다. Multi full 결과 파일은 `perf_rust_multi_linux_20260531_153550_round_20260530_rust_multi_full_v1.txt`이고 status=complete(960/960)였다. 후보 재측정 파일 `perf_rust_multi_linux_20260531_160730_round_20260530_rust_multi_failset_recheck.txt`는 status=partial(595/600)이었지만, 누락된 `MULTI_SPOT ws 4096B`는 full 결과로 보강했다. 추가 개선으로 `MULTI_SPOT wss` 1024B 이상 recv worker 기본값을 8개로 늘렸다. Probe `perf_rust_multi_linux_20260531_234055_rust_multi_spot_wss_workers8_probe_20260531.txt`에서 worker 확대 효과를 먼저 확인했고, 최종 코드 probe `perf_rust_multi_linux_20260531_234241_rust_multi_spot_wss_workers8_final_20260531.txt`와 단독 보강 `perf_rust_multi_linux_20260531_234325_rust_multi_spot_wss_1024_workers8_final_fill_20260531.txt`를 overlay했다. `MULTI_SPOT wss 65536B/131072B`는 각각 121.3%, 130.6%로 통과했고, 1024B/4096B는 45.3%, 71.6%로 기준에 못 닿아 미달한다. Multi DEALER_DEALER/PUBSUB/REQREP 잔여 항목은 이미 `Message::with_size` 직접 작성, caller-provided `Received`/`TopicMessage` 재사용, worker drain 구조를 쓰고 있어 public API를 유지한 추가 내부 후보가 확인되지 않았다.


### 8.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `통과(96.5%)` | `통과(96.9%)` | `통과(108.6%)` | `통과(97.6%)` | `통과(97.6%)` | `통과(94.3%)` | C full/Rust single full. |
| `tcp` | `PUBSUB` | `미달(75.6%)` | `통과(83.4%)` | `통과(107.2%)` | `통과(96.7%)` | `통과(97.0%)` | `통과(94.2%)` | 64/256B는 single 공통 송신 직접 작성 complete probe `perf_rust_single_linux_20260602_162501_rust_single_direct_message_pubsub_small_probe_20260602.txt` 기준. 나머지는 C full/Rust single full. |
| `tcp` | `DEALER_DEALER` | `통과(97.3%)` | `통과(97.5%)` | `통과(112.0%)` | `통과(97.2%)` | `통과(96.8%)` | `통과(94.0%)` | C full/Rust single full. |
| `tcp` | `DEALER_ROUTER` | `통과(90.4%)` | `통과(89.5%)` | `통과(82.2%)` | `미달(12.8%)` | `미달(11.3%)` | `미달(10.5%)` | C full/Rust single full. 보강 재측정 overlay 기준. public `Received` envelope를 우회하지 않는 추가 후보가 확인되지 않았다. |
| `tcp` | `ROUTER_ROUTER` | `통과(95.2%)` | `통과(106.3%)` | `통과(93.1%)` | `미달(13.0%)` | `미달(11.4%)` | `미달(10.5%)` | C full/Rust single full. 보강 재측정 overlay 기준. public `Received` envelope를 우회하지 않는 추가 후보가 확인되지 않았다. |
| `tcp` | `SPOT` | `통과(155.2%)` | `통과(113.6%)` | `미달(59.9%)` | `통과(105.4%)` | `통과(90.3%)` | `통과(83.2%)` | 1024B는 table transport full `perf_rust_single_linux_20260602_175921_rust_single_after_candidates_table_transports_full_20260602.txt` 기준. 기준에 못 닿아 미달한다. |
| `ws` | `PAIR` | `통과(96.9%)` | `통과(97.0%)` | `통과(122.6%)` | `통과(97.2%)` | `통과(97.2%)` | `통과(94.0%)` | C full/Rust single full. |
| `ws` | `PUBSUB` | `미달(77.0%)` | `통과(87.5%)` | `통과(127.1%)` | `통과(97.2%)` | `통과(96.4%)` | `통과(94.1%)` | 64/256B는 single 공통 송신 직접 작성 complete probe `perf_rust_single_linux_20260602_162501_rust_single_direct_message_pubsub_small_probe_20260602.txt` 기준. 나머지는 C full/Rust single full. |
| `ws` | `DEALER_DEALER` | `통과(97.2%)` | `통과(97.3%)` | `통과(114.3%)` | `통과(96.7%)` | `통과(97.0%)` | `통과(94.5%)` | C full/Rust single full. |
| `ws` | `DEALER_ROUTER` | `통과(86.9%)` | `통과(102.3%)` | `통과(129.9%)` | `미달(29.0%)` | `미달(22.8%)` | `미달(19.4%)` | C full/Rust single full. 보강 재측정 overlay 기준. public `Received` envelope를 우회하지 않는 추가 후보가 확인되지 않았다. |
| `ws` | `ROUTER_ROUTER` | `통과(98.6%)` | `통과(112.0%)` | `통과(124.5%)` | `미달(27.6%)` | `미달(22.9%)` | `미달(20.6%)` | C full/Rust single full. 보강 재측정 overlay 기준. public `Received` envelope를 우회하지 않는 추가 후보가 확인되지 않았다. |
| `ws` | `SPOT` | `통과(156.6%)` | `통과(119.9%)` | `미달(71.9%)` | `통과(153.8%)` | `통과(160.4%)` | `통과(97.7%)` | 1024B는 table transport full `perf_rust_single_linux_20260602_175921_rust_single_after_candidates_table_transports_full_20260602.txt` 기준으로 미달한다. 나머지는 C full/Rust single full. 보강 재측정 overlay 기준. |
| `wss` | `PAIR` | `통과(97.1%)` | `통과(97.1%)` | `통과(133.6%)` | `통과(127.7%)` | `통과(102.7%)` | `통과(104.4%)` | C full/Rust single full. |
| `wss` | `PUBSUB` | `통과(83.0%)` | `통과(91.4%)` | `통과(146.4%)` | `통과(112.6%)` | `통과(101.2%)` | `통과(96.2%)` | 64/256B는 single 공통 송신 직접 작성 complete probe `perf_rust_single_linux_20260602_162501_rust_single_direct_message_pubsub_small_probe_20260602.txt` 기준. 나머지는 C full/Rust single full. |
| `wss` | `DEALER_DEALER` | `통과(97.2%)` | `통과(97.5%)` | `통과(137.6%)` | `통과(128.0%)` | `통과(105.3%)` | `통과(96.4%)` | C full/Rust single full. |
| `wss` | `DEALER_ROUTER` | `통과(85.5%)` | `통과(105.7%)` | `통과(130.2%)` | `통과(76.9%)` | `통과(78.0%)` | `통과(81.1%)` | C full/Rust single full. 보강 재측정 overlay 기준. |
| `wss` | `ROUTER_ROUTER` | `통과(97.3%)` | `통과(110.2%)` | `통과(130.2%)` | `통과(78.0%)` | `통과(77.4%)` | `통과(92.4%)` | C full/Rust single full. 보강 재측정 overlay 기준. |
| `wss` | `SPOT` | `통과(157.1%)` | `통과(96.7%)` | `통과(83.2%)` | `통과(154.6%)` | `통과(102.2%)` | `통과(276.5%)` | 256B는 SPOT direct-message 최종 probe `perf_rust_single_linux_20260531_233915_rust_single_spot_direct_message_final_20260531.txt` 기준. |
| `tls` | `PAIR` | `통과(96.7%)` | `통과(97.0%)` | `통과(124.6%)` | `통과(98.8%)` | `통과(97.9%)` | `통과(95.5%)` | C full/Rust single full. |
| `tls` | `PUBSUB` | `미달(79.5%)` | `미달(78.5%)` | `통과(124.1%)` | `통과(97.9%)` | `통과(98.0%)` | `통과(94.6%)` | 64/256B는 single 공통 송신 직접 작성 complete probe `perf_rust_single_linux_20260602_162501_rust_single_direct_message_pubsub_small_probe_20260602.txt` 기준. 나머지는 C full/Rust single full. |
| `tls` | `DEALER_DEALER` | `통과(97.1%)` | `통과(97.2%)` | `통과(119.8%)` | `통과(98.1%)` | `통과(97.6%)` | `통과(95.8%)` | C full/Rust single full. |
| `tls` | `DEALER_ROUTER` | `통과(86.8%)` | `통과(99.8%)` | `통과(124.4%)` | `미달(55.7%)` | `미달(54.2%)` | `미달(51.7%)` | C full/Rust single full. 보강 재측정 overlay 기준. public `Received` envelope를 우회하지 않는 추가 후보가 확인되지 않았다. |
| `tls` | `ROUTER_ROUTER` | `통과(91.5%)` | `통과(108.0%)` | `통과(123.8%)` | `미달(54.5%)` | `미달(54.1%)` | `미달(54.2%)` | C full/Rust single full. 보강 재측정 overlay 기준. public `Received` envelope를 우회하지 않는 추가 후보가 확인되지 않았다. |
| `tls` | `SPOT` | `통과(144.4%)` | `통과(117.4%)` | `미달(66.7%)` | `통과(143.8%)` | `통과(100.6%)` | `통과(94.9%)` | 1024B는 table transport full `perf_rust_single_linux_20260602_175921_rust_single_after_candidates_table_transports_full_20260602.txt` 기준으로 미달한다. |


### 8.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `미달(70.1%)` | `통과(87.0%)` | `통과(95.1%)` | `통과(81.5%)` | `통과(100.8%)` | `통과(100.3%)` | C full/Rust multi full. 보강 재측정 overlay 기준. send path는 이미 `Message::with_size` 직접 작성이고, receive path도 caller-provided `Received`를 재사용한다. 추가 후보가 확인되지 않아 미달한다. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(102.0%)` | `통과(108.0%)` | `통과(108.8%)` | `통과(104.9%)` | `통과(70.4%)` | `통과(97.4%)` | C full/Rust multi full. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(78.1%)` | `통과(78.2%)` | `통과(79.3%)` | `통과(83.1%)` | `통과(71.6%)` | `통과(90.0%)` | C full/Rust multi full. |
| `tcp` | `MULTI_PUBSUB` | `통과(86.5%)` | `통과(100.0%)` | `통과(199.1%)` | `통과(168.2%)` | `미달(74.9%)` | `통과(97.9%)` | C full/Rust multi full. 보강 재측정 overlay 기준. server는 `Message::with_size` 직접 작성, client는 `TopicMessage` 재사용과 무복사 header decode를 이미 사용한다. 추가 후보가 확인되지 않아 미달한다. |
| `tcp` | `MULTI_SPOT` | `통과(117.8%)` | `통과(115.9%)` | `통과(104.6%)` | `통과(110.1%)` | `통과(126.5%)` | `통과(101.1%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(84.5%)` | `통과(85.4%)` | `통과(86.6%)` | `통과(87.6%)` | `통과(90.2%)` | `통과(76.8%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(102.2%)` | `통과(96.5%)` | `통과(106.9%)` | `통과(100.8%)` | `통과(92.7%)` | `통과(79.0%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `tcp` | `MULTI_STREAM` | `통과(104.4%)` | `통과(104.2%)` | `통과(103.3%)` | `통과(91.6%)` | `통과(86.8%)` | `통과(90.7%)` | C full/Rust multi full. |
| `ws` | `MULTI_DEALER_DEALER` | `미달(70.7%)` | `통과(89.2%)` | `통과(93.6%)` | `미달(65.8%)` | `통과(98.4%)` | `미달(67.4%)` | C full/Rust multi full. 보강 재측정 overlay 기준. send path는 이미 `Message::with_size` 직접 작성이고, receive path도 caller-provided `Received`를 재사용한다. 추가 후보가 확인되지 않아 미달한다. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(102.7%)` | `통과(104.0%)` | `통과(104.3%)` | `통과(97.5%)` | `통과(76.3%)` | `통과(91.3%)` | C full/Rust multi full. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(82.9%)` | `통과(86.1%)` | `통과(82.0%)` | `통과(89.1%)` | `통과(83.4%)` | `통과(86.2%)` | C full/Rust multi full. |
| `ws` | `MULTI_PUBSUB` | `통과(92.4%)` | `통과(85.5%)` | `통과(95.1%)` | `통과(98.7%)` | `통과(119.0%)` | `통과(123.0%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `ws` | `MULTI_SPOT` | `통과(107.2%)` | `통과(118.8%)` | `통과(105.9%)` | `통과(104.2%)` | `통과(126.0%)` | `통과(103.2%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(88.5%)` | `통과(85.2%)` | `통과(86.6%)` | `통과(95.4%)` | `미달(69.7%)` | `통과(88.1%)` | C full/Rust multi full. 보강 재측정 overlay 기준. request path는 public callback completion과 owned `Message` 제출 구조를 유지해야 하므로 추가 후보가 확인되지 않아 미달한다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(85.4%)` | `통과(91.7%)` | `통과(101.9%)` | `통과(83.8%)` | `통과(85.9%)` | `통과(98.4%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `ws` | `MULTI_STREAM` | `통과(101.2%)` | `통과(105.3%)` | `통과(99.2%)` | `통과(98.8%)` | `통과(96.1%)` | `통과(100.3%)` | C full/Rust multi full. |
| `wss` | `MULTI_DEALER_DEALER` | `미달(68.3%)` | `통과(84.7%)` | `통과(93.7%)` | `통과(90.3%)` | `통과(92.1%)` | `통과(95.6%)` | C full/Rust multi full. 보강 재측정 overlay 기준. send path는 이미 `Message::with_size` 직접 작성이고, receive path도 caller-provided `Received`를 재사용한다. 추가 후보가 확인되지 않아 미달한다. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(103.0%)` | `통과(100.1%)` | `통과(100.5%)` | `통과(96.3%)` | `통과(93.7%)` | `통과(89.5%)` | C full/Rust multi full. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(88.1%)` | `통과(88.7%)` | `통과(89.8%)` | `통과(90.0%)` | `통과(103.2%)` | `통과(98.4%)` | C full/Rust multi full. |
| `wss` | `MULTI_PUBSUB` | `통과(88.7%)` | `통과(88.1%)` | `통과(99.7%)` | `통과(130.9%)` | `통과(91.0%)` | `통과(100.9%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `wss` | `MULTI_SPOT` | `통과(110.5%)` | `통과(118.1%)` | `미달(45.3%)` | `미달(71.6%)` | `통과(121.3%)` | `통과(130.6%)` | 1024B는 `perf_rust_multi_linux_20260531_234325_rust_multi_spot_wss_1024_workers8_final_fill_20260531.txt`, 4096B/65536B/131072B는 `perf_rust_multi_linux_20260531_234241_rust_multi_spot_wss_workers8_final_20260531.txt` 기준. worker 8 확대 뒤에도 1024B/4096B는 기준에 못 닿아 미달한다. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(79.3%)` | `통과(81.0%)` | `통과(85.5%)` | `통과(92.1%)` | `통과(100.6%)` | `통과(88.9%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(88.9%)` | `통과(86.9%)` | `통과(95.5%)` | `통과(90.5%)` | `통과(97.4%)` | `통과(91.9%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `wss` | `MULTI_STREAM` | `통과(95.1%)` | `통과(98.3%)` | `통과(94.0%)` | `통과(94.1%)` | `통과(93.1%)` | `통과(100.9%)` | C full/Rust multi full. |
| `tls` | `MULTI_DEALER_DEALER` | `미달(69.8%)` | `통과(90.6%)` | `통과(88.8%)` | `미달(78.2%)` | `통과(83.1%)` | `통과(88.4%)` | C full/Rust multi full. 보강 재측정 overlay 기준. send path는 이미 `Message::with_size` 직접 작성이고, receive path도 caller-provided `Received`를 재사용한다. 추가 후보가 확인되지 않아 미달한다. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(103.1%)` | `통과(99.0%)` | `통과(98.1%)` | `통과(92.8%)` | `통과(87.8%)` | `통과(99.7%)` | C full/Rust multi full. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(81.9%)` | `통과(82.2%)` | `통과(83.1%)` | `통과(87.0%)` | `통과(88.3%)` | `통과(93.9%)` | C full/Rust multi full. |
| `tls` | `MULTI_PUBSUB` | `통과(84.9%)` | `통과(91.6%)` | `통과(94.8%)` | `통과(90.8%)` | `통과(94.0%)` | `통과(103.1%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `tls` | `MULTI_SPOT` | `통과(107.3%)` | `통과(118.6%)` | `통과(107.9%)` | `통과(82.9%)` | `통과(104.2%)` | `통과(99.3%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(78.4%)` | `통과(83.9%)` | `통과(81.4%)` | `통과(87.5%)` | `통과(101.0%)` | `통과(91.7%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(88.0%)` | `통과(99.4%)` | `통과(97.0%)` | `통과(93.8%)` | `통과(101.0%)` | `통과(89.0%)` | C full/Rust multi full. 보강 재측정 overlay 기준. |
| `tls` | `MULTI_STREAM` | `통과(88.9%)` | `통과(92.3%)` | `통과(92.0%)` | `통과(94.9%)` | `통과(96.8%)` | `통과(91.2%)` | C full/Rust multi full. |


## 9. Python 상태

perf 경로: `bindings/python/perf`.

2026-06-03에 public socket contract를 builder-only 표면으로 되돌리고, perf
script의 active phase가 private native bridge helper를 직접 호출하지 않도록
수정했다. 따라서 이 절의 기존 통과 표와 평균값은 과거 구현의 측정 기록으로만
남긴다. 현재 Python 성능 판정은 public Python API 경로로 다시 complete report를
확보한 뒤 갱신해야 한다.

같은 날 public API 경로를 유지한 채 socket 전용 native builder, native receive
owner, native owner가 반환한 bytes tuple 신뢰 경로, owner-backed `ReceivedMessage`
접근의 직접 호출, 단일 part `Received` materialize fast path, perf payload native
stamp 후보, hot native bridge의 handle 직접 읽기 후보, native receive owner,
single payload send의 direct bytes/bytearray pointer 후보를 적용했다. 단일 probe
`PAIR tcp 64B` 5초 `runs=3` 기준은 C
`perf_c_single_linux_20260603_094321_py_retry_c_pair64_5s_runs3.txt`
1,232,691.2 msg/s 대비 Python
`perf_python_single_linux_20260603_102035_py_public_final_pair64_5s_runs3.txt`
289,901.6 msg/s로 23.5%에 그쳤다. public Python 호출 loop와
caller-provided `Received` materialize 비용이 남아 있어 one-way 최소 기준 30%에는
아직 못 닿는다. `recv_into` C 직접 호출, C part tuple, bridge lookup cache,
`ReceivedMessage` `__slots__`, 직접 생성자 호출, socket send op freelist, 단일
receive owner C fast path, native latency decode, perf send bound-method cache,
inline send factory, callback empty fast path, unbound public send, compact receive
owner result, native owner metric decode, PAIR sender loop inline, native inline
owner, native `recv_into` replacement, blocking-first receive loop, per-message
`Received.close()` 지연, PAIR sender main-thread 실행 후보는
`cProfile`/thread 조합에서 segfault가 재현되거나 5초 측정에서 회귀해 최종 코드에
남기지 않았다. close 지연 후보는
`perf_python_single_linux_20260603_103019_py_deferred_close_pair64_5s.txt`에서
283,843.6 msg/s, sender main-thread 후보는
`perf_python_single_linux_20260603_103129_py_pair_sender_main_pair64_5s.txt`에서
281,689.2 msg/s로 최종 public 기준보다 낮았다. 이후 같은 현재 runtime에서 단발
재측정한 C `perf_c_single_linux_20260603_104109_py_retry_current_c_pair64_5s.txt`는
1,235,528.2 msg/s, Python
`perf_python_single_linux_20260603_104126_py_retry_current_python_pair64_5s.txt`는
284,878.4 msg/s였다. owner-backed `ReceivedMessage`를 C extension 타입으로 옮긴
후보는 `perf_python_single_linux_20260603_103726_py_c_received_message_pair64_5s.txt`에서
282,615.8 msg/s, small payload `zlink_msg_init_data(...)` malloc-copy 후보는
`perf_python_single_linux_20260603_104440_py_init_data_copy_pair64_5s.txt`에서
274,640.2 msg/s로 회귀했다. PAIR sender process 분리 probe는 stop-token 전달이
꼬여 timeout이 났고, single suite의 같은-process 의미와 맞지 않아 최종 코드에
남기지 않았다. receiver idle wait를 `time.sleep(0)`로 바꾼 후보는
`perf_python_single_linux_20260603_105022_py_sleep0_wait_pair64_5s.txt`에서
150,696.8 msg/s로 크게 회귀했고, 단일 part public accessor 후보는
`perf_python_single_linux_20260603_105104_py_single_part_accessor_pair64_5s.txt`에서
238,818.8 msg/s로 낮았다. caller-provided `Received`의 단일 part 갱신을 C helper로
옮긴 후보도 `perf_python_single_linux_20260603_105354_py_received_replace_single_pair64_5s.txt`에서
220,873.6 msg/s에 그쳐 최종 코드에 남기지 않았다. `recv_owner`와 `_replace`를
하나의 `recv_into` 전용 C helper로 합친 후보도
`perf_python_single_linux_20260603_110040_py_recv_into_received_pair64_5s.txt`에서
247,215.4 msg/s로 현재 public 기준보다 낮았다. private active-loop helper를 perf에서
직접 호출하지 않는 조건을 유지하려면 다음 후보는 public builder/recv container 자체를
더 안전하게 낮은 비용으로 옮기는 방식이어야 한다. 이때 profiler와 thread 조합에서도
segfault가 없어야 하고, 기존 `ReceivedMessage` 객체 보관 의미를 바꾸면 안 된다.

public contract 복구 뒤 Python multi smoke에서 `MULTI_SPOT_REQREP tcp 64B`가
client timeout으로 반복 partial이 됐다. server dispatch가 owner-backed part의
`data` view 계약을 만족하지 못해 reply callback에서 예외가 났고, 정상 active
loop가 끝난 뒤에는 context close가 native term에서 멈췄다. `_ReceivedPartsOwner`
와 native owner-backed `ReceivedMessage`가 같은 `data`/`size`/`to_bytes`
계약을 제공하도록 맞추고, SPOT req/rep perf 프로세스는 결과 출력 뒤 native
context shutdown만 수행한 다음 기존 `os._exit(0)` 종료 경로로 빠지게 했다.
같은 조건 재측정 `perf_python_multi_linux_20260603_103823.txt`는
status=complete(5/5)였고, 전체 tcp/64 smoke
`perf_python_multi_linux_20260603_102512.txt`도 status=complete(40/40)였다.
이 검증은 full matrix 판정이 아니라 public API 경로의 timeout 회귀가 해소됐는지
확인한 제한 smoke다.

Python single smoke 결과 파일은 `perf_python_single_linux_20260531_162613_round_20260530_python_single_smoke_64.txt`이고 status=complete(120/120)였다. Single full 결과 파일은 `perf_python_single_linux_20260531_163931_round_20260530_python_single_full_v1.txt`이고 status=complete(720/720)였다. C 기준 대비 통과 56개와 잔류 미달 88개가 확인됐다. 이후 `stamp_payload(...)`가 `bytes` 사본 대신 기존 `bytearray`를 그대로 반환하게 바꾸고, single receive hot path가 마지막 message part의 공개 `Message.data` memoryview에서 header를 직접 읽게 바꿨다. 제한 재측정 `perf_python_single_linux_20260531_234853_python_single_stamp_bytearray_probe_20260531.txt`와 `perf_python_single_linux_20260531_235420_python_single_recv_data_view_probe_20260531.txt`에서 PAIR/PUBSUB 64/256/1024B는 C 대비 3.2~12.4% 범위에 머물러 통과권까지 오르지 않았다. 이 후보만으로는 Python interpreter 루프, ctypes 기반 message materialize, send builder 호출 경계를 충분히 줄이지 못했다.

Python multi smoke 결과 파일은 `perf_python_multi_linux_20260531_165008_round_20260530_python_multi_smoke_64.txt`이고 status=complete(160/160)였다. Multi full 결과 파일은 `perf_python_multi_linux_20260531_180039_round_20260530_python_multi_full_v1.txt`이고 status=partial(810/960)이었다. 실패 조합 보강 파일 `perf_python_multi_linux_20260531_185859_round_20260530_python_multi_failset_fill.txt`는 status=partial(440/600)이었다. Overlay 뒤에도 RESULT가 없는 23개 칸은 두 실행에서 반복 실패한 조합이므로 `미달(RESULT 없음)`으로 둔다. 이후 `PERF_MULTI_*_IO_THREADS=4` 후보는 `perf_python_multi_linux_20260531_234941_python_multi_io4_spot_probe_20260531.txt`에서 MULTI_SPOT tcp/wss 64/1024B가 C 대비 4.1~4.6%에 그쳐 유지하지 않았다. Echo server와 SPOT receive 경로는 `to_bytes_list()` 또는 `to_bytes()` 사본 생성을 줄이고 공개 `Message.data` view를 submit/decode에 직접 쓰도록 바꿨다. 제한 재측정 `perf_python_multi_linux_20260601_001503_python_multi_dataview_echo_probe_20260531.txt`는 partial(205/225)이었고, `MULTI_SPOT wss 65536B`는 70.5%까지 개선됐다. 이 단계까지는 routed/SPOT echo 후보 다수가 C 대비 4.0~53.4% 범위에 머물렀거나 반복 timeout이 남아 추가 후보가 필요했다.

2026-06-02에는 `ctypes` 반복 호출을 줄이기 위해 CPython extension 기반 native bridge를 추가했다. `python3 setup.py build_ext --inplace --force`, `python3 -m compileall -q src tests perf`, `PYTHONPATH=src pytest -q tests`는 통과했다. full single `perf_python_single_linux_20260602_200749_python_native_bridge_full_single_20260602.txt`는 status=partial(1000/1020)이었지만, 실패 4개는 complete 제한 재측정으로 모두 회복됐다. 이후 일반 one-way single의 active phase를 native loop로 옮긴 complete report `perf_python_single_linux_20260602_234110_python_single_native_active_phase_verify_20260602.txt`에서 `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`의 `64/256/1024B`는 C 대비 최소 59.8% 이상으로 통과했다. SPOT은 직접 polling 방식의 native receive 후보가 불안정해 제거했지만, core dispatch callback 안에서만 `zlink_spot_subscribe_part(...)`를 drain하는 native count handler로 다시 구현했다. complete report `perf_python_single_linux_20260603_000326_python_spot_native_dispatch_count_verify_20260603.txt`에서 SPOT `tcp/tls/ws/wss`의 `64/256/1024B` 12개 cell은 C 대비 최소 77.5%로 모두 통과했다. 100us backoff 후보는 `perf_python_single_linux_20260603_001101_python_spot_native_dispatch_count_backoff100us_verify_20260603.txt`에서 `SPOT wss 256B`가 19.3%로 회귀해 최종 코드에 남기지 않았다.

`DEALER_ROUTER`/`ROUTER_ROUTER` tcp/ws 대형 12개 cell은 native loop가 C single과 달리 active send에 `ZLINK_DONTWAIT`를 쓰고 있어 대역폭이 약 1.28GB/s에 묶였다. C single active send와 같은 `ZLINK_SEND_FLAGS_NONE`로 정렬한 complete report `perf_python_single_linux_20260603_014924.txt`에서 대상 cell은 C 대비 40.9~67.4%로 상승했고, 기존 미달 10개 cell이 모두 통과했다. `python3 setup.py build_ext --inplace --force`, `python3 -m compileall -q src tests perf`, `PYTHONPATH=src pytest -q tests`도 통과했다.

Multi는 native bridge 이후 full run `perf_python_multi_linux_20260602_210003_python_native_bridge_full_multi_20260602.txt`가 status=partial(800/920)이었고, 초기 `MULTI_SPOT_SENDSEND`와 `MULTI_STREAM` failset 재측정도 반복 partial로 남았다. 이후 `MULTI_STREAM` native echo와 C multi default에 맞춘 Python multi `io_threads=4`를 적용한 complete report `perf_python_multi_linux_20260602_230703_python_stream_native_echo_default_io4_verify_20260602.txt`에서 대상 16개 cell이 C 대비 62.9~92.0%로 통과했다. `MULTI_SPOT_SENDSEND` 65536/131072B 대상은 `perf_python_multi_linux_20260602_231518_python_spot_sendsend_default_io4_slot_verify_20260602.txt`와 `perf_python_multi_linux_20260602_231618_python_spot_sendsend_tls65536_default_io4_recheck_20260602.txt` 기준 38.2~84.3%로 통과했다. `MULTI_DEALER_DEALER`는 native send/count loop complete report `perf_python_multi_linux_20260603_001915_python_multi_dealer_dealer_native_send_count_full_verify_20260603.txt`에서 C 대비 35.1~99.7%로 통과했고, `MULTI_PUBSUB`는 native publish/count loop complete report `perf_python_multi_linux_20260603_002713_python_multi_pubsub_native_publish_count_full_verify_20260603.txt`에서 C 대비 53.6~153.1%로 통과했다. `MULTI_DEALER_ROUTER`와 `MULTI_ROUTER_ROUTER`는 native client round-trip loop complete report `perf_python_multi_linux_20260603_010318.txt`에서 대형 16개 cell이 C 대비 39.3~90.8%로 통과했다. 이후 routed echo server의 수신-반송 loop 전체를 native loop로 옮긴 complete report `perf_python_multi_linux_20260603_012037.txt`에서 small/4096B 32개 cell도 C 대비 74.2~99.4%로 통과했다.

`MULTI_SPOT`은 native polling-count client와 latency sample stride 1024 후보를 반영한 complete report 기준으로 모든 transport/msg-size가 통과했다. `MULTI_SPOT_SENDSEND`는 native client loop를 C perf처럼 등록형 poller 기반으로 맞춘 뒤 5개 cell이 추가로 통과했고, active phase 직전에 native routed echo handler를 설치한 보강 probe에서 `ws 1024B`도 통과했다. 이후 client send path에서 64/256B payload를 `zlink_msg_init_data(...)`로 초기화해 사본 생성을 줄였고, 같은 현재 환경에서 C `MULTI_SPOT_SENDSEND` 64/256B failset을 제한 재측정한 complete report로 small cell 기준을 보강했다. `MULTI_SPOT_REQREP`는 request submit과 reply callback 집계를 CPython extension active loop로 옮기고 server reply dispatch도 native handler로 처리한 뒤 잔여 17개 cell이 complete report 기준 모두 통과했다. 이 결과는 2026-06-03 public contract 복구 전의 과거 구현 기준이다. 상세 근거는 `doc/plan/perf/log/2026-06-02-python-bindings-performance-round.ko.md`에 남겼다.

Python multi 표에서 긴 결과 파일명은 표 폭을 키우지 않도록 위 설명 문단에 모았다. 표 안의 메모는 기준과 판정만 짧게 적는다.


### 9.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `통과(95.0%)` | `통과(94.9%)` | `통과(88.7%)` | `통과(95.8%)` | `통과(96.5%)` | `통과(96.4%)` | 64/256/1024B는 native active phase complete report 기준. |
| `tcp` | `PUBSUB` | `통과(96.2%)` | `통과(101.8%)` | `통과(72.8%)` | `통과(95.7%)` | `통과(95.9%)` | `통과(95.9%)` | 64/256/1024B는 native active phase complete report 기준. |
| `tcp` | `DEALER_DEALER` | `통과(94.9%)` | `통과(95.1%)` | `통과(90.0%)` | `통과(96.1%)` | `통과(96.0%)` | `통과(95.9%)` | 64/256/1024B는 native active phase complete report 기준. |
| `tcp` | `DEALER_ROUTER` | `통과(75.0%)` | `통과(80.3%)` | `통과(61.6%)` | `통과(48.0%)` | `통과(43.7%)` | `통과(40.9%)` | 64/256/1024B는 native active phase complete report 기준. 대형은 blocking send 정렬 complete report 기준. |
| `tcp` | `ROUTER_ROUTER` | `통과(95.1%)` | `통과(96.8%)` | `통과(63.3%)` | `통과(48.2%)` | `통과(42.4%)` | `통과(41.9%)` | 64/256/1024B는 native active phase complete report 기준. 대형은 blocking send 정렬 complete report 기준. |
| `tcp` | `SPOT` | `통과(86.8%)` | `통과(77.5%)` | `통과(85.9%)` | `통과(35.6%)` | `통과(43.4%)` | `통과(62.6%)` | 64/256/1024B는 native dispatch-count complete report 기준. |
| `ws` | `PAIR` | `통과(94.7%)` | `통과(94.9%)` | `통과(88.5%)` | `통과(95.3%)` | `통과(96.1%)` | `통과(95.6%)` | 64/256/1024B는 native active phase complete report 기준. |
| `ws` | `PUBSUB` | `통과(95.3%)` | `통과(101.1%)` | `통과(76.6%)` | `통과(95.5%)` | `통과(95.5%)` | `통과(95.9%)` | 64/256/1024B는 native active phase complete report 기준. |
| `ws` | `DEALER_DEALER` | `통과(94.9%)` | `통과(95.1%)` | `통과(84.7%)` | `통과(95.1%)` | `통과(95.9%)` | `통과(96.0%)` | 64/256/1024B는 native active phase complete report 기준. |
| `ws` | `DEALER_ROUTER` | `통과(78.5%)` | `통과(95.3%)` | `통과(61.8%)` | `통과(67.4%)` | `통과(62.2%)` | `통과(56.0%)` | 64/256/1024B는 native active phase complete report 기준. 대형은 blocking send 정렬 complete report 기준. |
| `ws` | `ROUTER_ROUTER` | `통과(93.8%)` | `통과(101.2%)` | `통과(59.8%)` | `통과(63.4%)` | `통과(62.5%)` | `통과(59.7%)` | 64/256/1024B는 native active phase complete report 기준. 대형은 blocking send 정렬 complete report 기준. |
| `ws` | `SPOT` | `통과(87.9%)` | `통과(81.3%)` | `통과(95.6%)` | `통과(53.9%)` | `통과(60.1%)` | `통과(65.2%)` | 64/256/1024B는 native dispatch-count complete report 기준. |
| `wss` | `PAIR` | `통과(94.8%)` | `통과(93.9%)` | `통과(88.9%)` | `통과(94.8%)` | `통과(96.8%)` | `통과(98.7%)` | 64/256/1024B는 native active phase complete report 기준. |
| `wss` | `PUBSUB` | `통과(95.4%)` | `통과(94.2%)` | `통과(93.2%)` | `통과(92.0%)` | `통과(93.5%)` | `통과(92.0%)` | 64/256/1024B는 native active phase complete report 기준. |
| `wss` | `DEALER_DEALER` | `통과(95.1%)` | `통과(94.8%)` | `통과(89.3%)` | `통과(94.8%)` | `통과(97.3%)` | `통과(91.7%)` | 64/256/1024B는 native active phase complete report 기준. |
| `wss` | `DEALER_ROUTER` | `통과(79.2%)` | `통과(89.5%)` | `통과(71.4%)` | `통과(77.9%)` | `통과(81.7%)` | `통과(98.3%)` | 64/256/1024B는 native active phase complete report 기준. 대형은 single routed large complete probe 기준. |
| `wss` | `ROUTER_ROUTER` | `통과(90.0%)` | `통과(95.3%)` | `통과(66.1%)` | `통과(80.8%)` | `통과(80.7%)` | `통과(86.7%)` | 64/256/1024B는 native active phase complete report 기준. 대형은 single routed large complete probe 기준. |
| `wss` | `SPOT` | `통과(88.4%)` | `통과(80.9%)` | `통과(94.0%)` | `통과(89.4%)` | `통과(186.5%)` | `통과(189.9%)` | 64/256/1024B는 native dispatch-count complete report 기준. |
| `tls` | `PAIR` | `통과(94.3%)` | `통과(94.3%)` | `통과(91.6%)` | `통과(92.1%)` | `통과(94.3%)` | `통과(92.7%)` | 64/256/1024B는 native active phase complete report 기준. |
| `tls` | `PUBSUB` | `통과(94.7%)` | `통과(86.8%)` | `통과(88.8%)` | `통과(93.5%)` | `통과(94.3%)` | `통과(91.9%)` | 64/256/1024B는 native active phase complete report 기준. |
| `tls` | `DEALER_DEALER` | `통과(95.0%)` | `통과(94.9%)` | `통과(89.0%)` | `통과(91.2%)` | `통과(93.8%)` | `통과(92.5%)` | 64/256/1024B는 native active phase complete report 기준. |
| `tls` | `DEALER_ROUTER` | `통과(78.4%)` | `통과(87.9%)` | `통과(66.8%)` | `통과(63.7%)` | `통과(66.6%)` | `통과(66.7%)` | 64/256/1024B는 native active phase complete report 기준. 대형은 single routed large complete probe 기준. |
| `tls` | `ROUTER_ROUTER` | `통과(88.5%)` | `통과(95.4%)` | `통과(61.7%)` | `통과(61.6%)` | `통과(72.0%)` | `통과(68.9%)` | 64/256/1024B는 native active phase complete report 기준. 대형은 single routed large complete probe 기준. |
| `tls` | `SPOT` | `통과(85.5%)` | `통과(81.1%)` | `통과(92.7%)` | `통과(78.8%)` | `통과(96.8%)` | `통과(92.2%)` | 64/256/1024B는 native dispatch-count complete report 기준. |


### 9.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `통과(74.1%)` | `통과(74.5%)` | `통과(78.7%)` | `통과(68.9%)` | `통과(99.7%)` | `통과(74.0%)` | native send/count complete report 기준. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(91.7%)` | `통과(91.1%)` | `통과(99.4%)` | `통과(94.1%)` | `통과(39.3%)` | `통과(57.0%)` | small/4096B는 native full-server echo complete report 기준. 대형은 native client round-trip complete report 기준. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(77.7%)` | `통과(77.4%)` | `통과(80.2%)` | `통과(74.2%)` | `통과(47.9%)` | `통과(55.4%)` | small/4096B는 native full-server echo complete report 기준. 대형은 native client round-trip complete report 기준. |
| `tcp` | `MULTI_PUBSUB` | `통과(81.1%)` | `통과(77.2%)` | `통과(128.1%)` | `통과(153.1%)` | `통과(53.6%)` | `통과(64.3%)` | native publish/count complete report 기준. |
| `tcp` | `MULTI_SPOT` | `통과(79.1%)` | `통과(77.2%)` | `통과(73.4%)` | `통과(94.6%)` | `통과(178.0%)` | `통과(235.8%)` | 64/1024/4096B는 native polling-count complete report `perf_python_multi_linux_20260603_020438.txt` 기준. 256/65536/131072B는 `perf_python_multi_linux_20260603_020954.txt` 기준. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(93.3%)` | `통과(93.2%)` | `통과(90.0%)` | `통과(86.0%)` | `통과(90.9%)` | `통과(33.1%)` | 64/256B는 native reqrep complete report `perf_python_multi_linux_20260603_051511.txt` 기준. 1024B는 `perf_python_multi_linux_20260603_051611.txt` 기준. 4096/65536B는 `perf_python_multi_linux_20260603_051805.txt` 기준. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(35.8%)` | `통과(33.7%)` | `통과(33.4%)` | `통과(35.1%)` | `통과(38.2%)` | `통과(57.1%)` | 64/256B는 Python `perf_python_multi_linux_20260603_055040.txt`, C small failset `perf_c_multi_linux_20260603_062523.txt` 기준. 1024B는 Python boundary complete `perf_python_multi_linux_20260603_061712.txt`와 C full 기준. |
| `tcp` | `MULTI_STREAM` | `통과(87.8%)` | `통과(88.1%)` | `통과(92.0%)` | `해당 없음` | `통과(76.1%)` | `해당 없음` | native stream echo complete report 기준. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(39.5%)` | `통과(79.4%)` | `통과(79.2%)` | `통과(63.5%)` | `통과(71.2%)` | `통과(59.4%)` | native send/count complete report 기준. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(89.6%)` | `통과(83.2%)` | `통과(89.3%)` | `통과(86.7%)` | `통과(40.4%)` | `통과(54.4%)` | small/4096B는 native full-server echo complete report 기준. 대형은 native client round-trip complete report 기준. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(75.2%)` | `통과(84.2%)` | `통과(78.3%)` | `통과(78.9%)` | `통과(47.5%)` | `통과(55.4%)` | small/4096B는 native full-server echo complete report 기준. 대형은 native client round-trip complete report 기준. |
| `ws` | `MULTI_PUBSUB` | `통과(72.7%)` | `통과(66.2%)` | `통과(78.0%)` | `통과(95.2%)` | `통과(84.0%)` | `통과(87.0%)` | native publish/count complete report 기준. |
| `ws` | `MULTI_SPOT` | `통과(74.3%)` | `통과(70.3%)` | `통과(74.8%)` | `통과(77.7%)` | `통과(189.7%)` | `통과(183.2%)` | 256B는 단독 complete report `perf_python_multi_linux_20260603_021058.txt` 기준. 나머지는 native polling-count complete report `perf_python_multi_linux_20260603_021040.txt` 기준. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(93.5%)` | `통과(89.5%)` | `통과(88.1%)` | `통과(36.5%)` | `통과(86.5%)` | `통과(88.2%)` | 64/256/1024B는 native reqrep complete report `perf_python_multi_linux_20260603_050745.txt` 기준. 65536/131072B는 `perf_python_multi_linux_20260603_051956.txt` 기준. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(37.6%)` | `통과(37.0%)` | `통과(34.6%)` | `통과(46.1%)` | `통과(67.2%)` | `통과(48.0%)` | 64/256B는 Python `perf_python_multi_linux_20260603_055608.txt`, C small failset `perf_c_multi_linux_20260603_062523.txt` 기준. 1024B는 Python boundary complete `perf_python_multi_linux_20260603_061712.txt`와 C full 기준. |
| `ws` | `MULTI_STREAM` | `통과(72.5%)` | `통과(78.4%)` | `통과(66.5%)` | `해당 없음` | `통과(72.6%)` | `해당 없음` | native stream echo complete report 기준. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(35.1%)` | `통과(80.1%)` | `통과(81.8%)` | `통과(83.7%)` | `통과(85.6%)` | `통과(68.9%)` | native send/count complete report 기준. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(88.6%)` | `통과(88.8%)` | `통과(83.0%)` | `통과(86.2%)` | `통과(80.4%)` | `통과(74.7%)` | small/4096B는 native full-server echo complete report 기준. 대형은 native client round-trip complete report 기준. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(82.1%)` | `통과(82.0%)` | `통과(82.4%)` | `통과(81.8%)` | `통과(77.6%)` | `통과(72.3%)` | small/4096B는 native full-server echo complete report 기준. 대형은 native client round-trip complete report 기준. |
| `wss` | `MULTI_PUBSUB` | `통과(74.9%)` | `통과(71.2%)` | `통과(90.7%)` | `통과(121.4%)` | `통과(94.3%)` | `통과(90.7%)` | native publish/count complete report 기준. |
| `wss` | `MULTI_SPOT` | `통과(68.0%)` | `통과(58.5%)` | `통과(68.5%)` | `통과(48.8%)` | `통과(70.5%)` | `통과(56.2%)` | 64/256/4096B는 native polling-count 단독 complete report 기준이다. 1024B는 latency sample stride 1024 complete report `perf_python_multi_linux_20260603_025840.txt` 기준이다. 65536B는 data view probe 기준. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(83.4%)` | `통과(82.7%)` | `통과(84.3%)` | `통과(44.2%)` | `통과(52.7%)` | `통과(48.9%)` | 64/256/1024B는 native reqrep complete report `perf_python_multi_linux_20260603_051029.txt` 기준. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(33.6%)` | `통과(35.2%)` | `통과(35.7%)` | `통과(52.2%)` | `통과(80.9%)` | `통과(78.7%)` | 64/256B는 Python `perf_python_multi_linux_20260603_055608.txt`, C small failset `perf_c_multi_linux_20260603_062523.txt` 기준. 1024B는 Python boundary complete `perf_python_multi_linux_20260603_061712.txt`와 C full 기준. |
| `wss` | `MULTI_STREAM` | `통과(83.9%)` | `통과(90.0%)` | `통과(87.6%)` | `해당 없음` | `통과(62.9%)` | `해당 없음` | native stream echo complete report 기준. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(35.3%)` | `통과(46.5%)` | `통과(53.2%)` | `통과(60.8%)` | `통과(79.0%)` | `통과(72.4%)` | native send/count complete report 기준. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(89.2%)` | `통과(87.4%)` | `통과(88.4%)` | `통과(83.2%)` | `통과(77.3%)` | `통과(90.8%)` | small/4096B는 native full-server echo complete report 기준. 대형은 native client round-trip complete report 기준. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(79.7%)` | `통과(80.3%)` | `통과(79.5%)` | `통과(79.9%)` | `통과(76.0%)` | `통과(82.7%)` | small/4096B는 native full-server echo complete report 기준. 대형은 native client round-trip complete report 기준. |
| `tls` | `MULTI_PUBSUB` | `통과(69.6%)` | `통과(70.5%)` | `통과(77.5%)` | `통과(90.2%)` | `통과(81.6%)` | `통과(93.6%)` | native publish/count complete report 기준. |
| `tls` | `MULTI_SPOT` | `통과(67.1%)` | `통과(68.7%)` | `통과(76.8%)` | `통과(72.5%)` | `통과(227.4%)` | `통과(181.9%)` | 64B는 단독 complete report `perf_python_multi_linux_20260603_021509.txt` 기준. 나머지는 native polling-count complete report `perf_python_multi_linux_20260603_021443.txt` 기준. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(83.8%)` | `통과(86.8%)` | `통과(86.1%)` | `통과(83.0%)` | `통과(44.4%)` | `통과(38.2%)` | 64/1024B는 native reqrep complete report `perf_python_multi_linux_20260603_051319.txt` 기준. 256B는 `perf_python_multi_linux_20260603_051128.txt` 기준. 4096B는 `perf_python_multi_linux_20260603_052054.txt` 기준. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(33.8%)` | `통과(36.7%)` | `통과(33.4%)` | `통과(42.7%)` | `통과(84.3%)` | `통과(76.6%)` | 64/256B는 Python `perf_python_multi_linux_20260603_061712.txt`/`perf_python_multi_linux_20260603_055608.txt`, C small failset `perf_c_multi_linux_20260603_062523.txt` 기준. 1024B는 Python boundary complete `perf_python_multi_linux_20260603_061712.txt`와 C full 기준. |
| `tls` | `MULTI_STREAM` | `통과(83.8%)` | `통과(87.1%)` | `통과(83.2%)` | `해당 없음` | `통과(79.4%)` | `해당 없음` | native stream echo complete report 기준. 4096B/131072B는 이번 계획의 MULTI_STREAM 판정 대상이 아니다. |


## 10. 상세 작업 로그

상세 측정 로그와 후보별 기각 근거는 `doc/plan/perf/log/` 아래에 남긴다.
이번 Node 재검토 로그는 `doc/plan/perf/log/2026-06-01-node-bindings-performance-round.ko.md`에 기록한다.
이번 Go 재검토 로그는 `doc/plan/perf/log/2026-06-01-go-bindings-performance-round.ko.md`에 기록한다.
