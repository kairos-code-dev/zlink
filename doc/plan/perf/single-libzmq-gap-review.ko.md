# `single` 상대 성능 격차 재분석 메모

> 이 문서는
> [`single-libzmq-gap-ralph-guide.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-ralph-guide.ko.md)
> 가 참조하는 지속 로그 파일이다.
>
> 랄프루프를 돌리는 동안 이 파일에는 최소한 아래를 계속 갱신한다.
>
> - 최신 가설과 배제된 가설
> - 실제 실행 명령
> - 결과 파일 경로
> - 현재 best / latest throughput 비교
> - 다음 iteration 우선순위
>
> historical regression source:
> [with-zmq-regression-bisect-report.ko.md](/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/perf/with-zmq-regression-bisect-report.ko.md),
> [with-zmq-regression-bisect-log.ko.md](/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/perf/with-zmq-regression-bisect-log.ko.md)
>
> 2026-03-29부터 상세 iteration 실험 기록은
> [logs/iterations/](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/iterations)
> 아래 개별 markdown 파일로 분리한다.
> 이 파일은 current summary와 short review만 유지한다.

> 범위: [`core/bench/with_zmq/single/`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single)
> 기준의 `zlink` 대 `libzmq` 상대 성능 차이
>
> 목적: 현재 남아 있는 격차가 `core` 엔진 자체 회귀인지,
> 아니면 benchmark surface 차이와 public API 비용이 섞인 결과인지
> 현재 코드 기준으로 다시 정리한다.
>
> 이 문서에서 POSD는 설계 해석의 보조 기준일 뿐이다.
> 현재 1차 목표는 historical good 수준까지 throughput을 회복하는 것이며,
> correctness/thread-safe/public contract를 깨지 않는 범위에서는
> 더 빠른 hot-path 구조 복원을 우선한다.

## 0. Current Operating Summary

### 0.1 Current State

- 공통 `oneway` 격차의 주 해석은 `backpressure-only`가 아니라
  `producer-side steady-state send/publication`이다.
- `HWM/sndhwm/rcvhwm = 1000000` probe에서도 `PAIR`, `DEALER_DEALER`,
  `PUBSUB` 64B 격차가 크게 남았으므로, queue가 차지 않아도 드는 공통
  per-message 비용이 본체일 가능성이 높다.
- 별도 bisect worktree 기준으로 historical first direct cause는
  `9b91234c`의 raw `send_exact/zlink_msg_recv` -> public
  `zlink_send/zlink_recv` surface 전환이고,
  first buildable bad는 `77550a0a`다.
- structural design reference oracle은
  `/home/hep7/project/kairos/zlink-perf-regression-bisect`
  의 2026-03-05 good 상태(`7bea9e3f`)다.
  current round는 이 good state를 rollback 대상으로 보지 않고,
  current HEAD가 `send entry / pipe duty / public surface / sender regime`
  중 무엇을 더 떠안았는지 비교하는 기준으로 사용한다.
- POSD는 위 비교를 설명하는 언어이지, candidate를 막는 우선 제약은 아니다.
  current contract와 correctness를 유지하는 범위에서는
  historical good 수준에 더 가까운 구조 회복을 우선한다.
- 필요하면 같은 `with_zmq` single 조건으로
  `/home/hep7/project/kairos/zlink-perf-regression-bisect` worktree를 다시
  측정해 current HEAD와 비교/확인한다.
  다만 그 수치는 current HEAD acceptance가 아니라 reference oracle 확인용
  diagnostic으로만 사용한다.
- 다만 이 historical first collapse는 `9b91234c` 하나로 끝나지 않는다.
  실제 hot-path를 바꾼 concrete 변화는
  `ff0140e5`(`pipe::_out_sync`),
  `a819ea3a`(`socket_base_t::send()` public admission/CAS),
  `98e7d324`(public multipart `zlink_send/zlink_recv`),
  `9b91234c`(bench hot-loop activation + `PERF_SINGLE_MAX_INFLIGHT` 제거)다.
- 즉 historical first collapse는 단순 core 미세 비용보다
  single-frame one-way payload를 public multipart contract로 밀어 넣고,
  sender pacing regime까지 더 aggressive하게 바꾸면서 생긴
  `send-side materialize + public admission/CAS + pipe serialization +
  recv-side export/free`의 조합으로 보는 것이 가장 정확하다.
- 다만 current HEAD residual gap은 이 예전 recv heap-export 문제 하나로는
  설명되지 않고, current raw-msg diagnostic도 일관된 broad win을 못 만들었다.
  따라서 현재 우선순위는 다시 `send-side public admission +
  pipe serialization` 공통축이다.
- 2026-03-28 serial current-tree refresh에서는
  public
  `PAIR tcp/inproc -12.03% / -17.18%`,
  `DEALER_DEALER tcp/inproc -11.12% / -18.60%`,
  raw
  `PAIR tcp/inproc -8.63% / -23.67%`,
  `DEALER_DEALER tcp/inproc -12.06% / -20.63%`였다.
  즉 raw/public wrapper 제거는 `PAIR tcp`만 일부 회복하고
  `PAIR inproc`, `DEALER_DEALER` broad residual은 지우지 못했다.
  또 `PAIR`(no public-api sync)와 `DEALER_DEALER`
  (public-api sync held)의 현재 gap이 여전히 비슷하므로,
  dealer-only `public_api_sync` reuse family도 current primary blocker가
  아니다.
  current residual direct cause는 wrapper나 dealer-only sync reuse가 아니라
  `enter_public_api`가 포함된 common send scope construct floor와
  `_out_sync` write/flush serialization floor의 조합으로 다시 쓴다.
- 다만 같은 current code로 late-session serial refresh +
  rerun
  [`perf_linux_20260328_232530_codex_20260328_post_recursive_sync_refresh_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_232530_codex_20260328_post_recursive_sync_refresh_public.txt),
  [`perf_linux_20260328_232612_codex_20260328_post_recursive_sync_refresh_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_232612_codex_20260328_post_recursive_sync_refresh_raw.txt),
  [`perf_linux_20260328_233054_codex_20260328_post_recursive_sync_refresh_public_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_233054_codex_20260328_post_recursive_sync_refresh_public_rerun.txt),
  [`perf_linux_20260328_233133_codex_20260328_post_recursive_sync_refresh_raw_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_233133_codex_20260328_post_recursive_sync_refresh_raw_rerun.txt)
  을 다시 찍자
  public first/rerun
  `PAIR tcp/inproc -24.67% / -14.98% -> -27.72% / -18.03%`,
  `DEALER_DEALER tcp/inproc -14.63% / -32.73% -> -21.31% / -32.12%`,
  raw first/rerun
  `PAIR tcp/inproc -23.51% / -23.04% -> -23.54% / -25.42%`,
  `DEALER_DEALER tcp/inproc -32.68% / -34.30% -> -23.26% / -25.44%`였다.
  즉 current session baseline 자체가 earlier authority보다 더 낮은 상태로
  반복됐고, common residual 해석은 유지되지만 serial refresh 1회만으로
  acceptance baseline을 다시 덮어쓰면 안 된다.
- 이어서 `pipe.cpp` final-part `write_and_flush()` lock-free snapshot
  candidate도 시도했지만,
  public
  `PAIR tcp/inproc -32.16% / -20.55%`,
  `DEALER_DEALER tcp/inproc -9.76% / -23.34%`로
  `PAIR`가 early authority와 session-local low baseline 둘 다 지키지 못해
  current code에는 남기지 않았다.
- 이어서 same-ordering invariant를 그대로 둔 채
  `pipe::_out_sync` steady-state lock primitive만 plain non-recursive fast
  mutex로 바꾸는 candidate도 시도했지만,
  public `PAIR tcp/inproc -22.34% / -24.82%`,
  `DEALER_DEALER tcp/inproc -9.78% / -32.93%`로
  `PAIR`와 `DEALER_DEALER inproc`이 both guardrail을 못 지켜
  current code에는 남기지 않았다.
- 이어서 `pipe.hpp` / `pipe.cpp` non-conflate `out_pipe`를
  concrete `ypipe_t` fast path로 되돌리는 candidate도 시도했지만,
  public `PAIR tcp/inproc -18.01% / -35.55%`,
  `DEALER_DEALER tcp/inproc -15.12% / -24.02%`,
  raw `PAIR tcp/inproc -8.81% / -28.27%`,
  `DEALER_DEALER tcp/inproc -7.64% / -22.08%`로
  public과 raw `inproc` guardrail을 함께 못 지켜 current code에는
  남기지 않았다.
- 이어서 `socket_base_api.cpp` / `socket_base_msg.cpp` /
  `socket_message_send_api.cpp`에서 same-handle public single-part send를
  API-boundary recursive mutex로 serialize하고 direct send scope를
  우회하는 candidate도 시도했지만,
  public `PAIR tcp/inproc -14.34% / -31.65%`,
  `DEALER_DEALER tcp/inproc -13.44% / -24.93%`,
  `ROUTER_ROUTER tcp/inproc -57.41% / -23.50%`,
  raw `PAIR tcp/inproc -17.38% / -31.55%`,
  `DEALER_DEALER tcp/inproc -11.26% / -19.57%`,
  `ROUTER_ROUTER tcp/inproc -57.61% / -23.28%`로
  public과 raw guardrail을 함께 못 지켜 current code에는 남기지 않았다.
  즉 current `send scope construct` 바닥은 outer same-handle API mutex로만
  대체해도 broad win이 아니었다.
- 이어서 `socket_runtime.hpp` / `socket_runtime.cpp` /
  `socket_base_msg.cpp`에서 direct single-part send 성공 뒤
  same-thread next send만 재사용하는 parked admission handoff를 두는
  structural candidate도 시도했지만,
  public `PAIR tcp/inproc -13.55% / -29.90%`,
  `DEALER_DEALER tcp/inproc -23.40% / -22.04%`,
  raw `PAIR tcp/inproc -24.41% / -23.24%`,
  `DEALER_DEALER tcp/inproc -23.28% / -14.47%`로
  targeted public/raw guardrail을 함께 못 지켜 current code에는 남기지
  않았다.
  즉 `9b91234c` sender-regime 흔적을 same-thread parked send admission
  handoff 하나로만 얇게 해도 current common answer는 아니었다.
- 다만 위 해석을 boundary naming/codegen-only prep으로 곧바로 올리는 것도
  답이 아니었다.
  `socket_runtime.hpp` / `socket_runtime.cpp`
  common data-plane admission boundary helper extraction candidate는
  out-of-line helper, header-inline helper 둘 다
  `PAIR` public/raw와 `DEALER_DEALER tcp raw` guardrail을 함께 지키지
  못해 current code에는 남기지 않았다.
- 이어서 current kept boundary 위에서
  full public lifecycle coordinator 아래로 dedicated public send lease를
  내려 common send admission을 다시 가르는 structural candidate도
  시도했지만,
  authority public rerun
  `PAIR tcp/inproc -15.65% / -25.43%`,
  `DEALER_DEALER tcp/inproc -24.41% / -32.10%`로
  baseline보다 더 악화돼 current code에는 남기지 않았다.
  same-tag public/raw parallel run
  (`215617`, `215700`)은 noisy diagnostic으로만 두고,
  single-process authority rerun
  [`perf_linux_20260328_215743_codex_20260328_send_lease_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_215743_codex_20260328_send_lease_public_authority.txt)
  로 reject를 확정했다.
- 이어서 same shared `public_api_state` 안에서 public/callback inflight와
  direct send inflight를 separate lane으로 가르는 structural candidate도
  시도했지만,
  same-tag public/raw parallel run
  [`perf_linux_20260328_225749_codex_20260328_send_state_lane_split_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225749_codex_20260328_send_state_lane_split_public.txt),
  [`perf_linux_20260328_225749_codex_20260328_send_state_lane_split_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225749_codex_20260328_send_state_lane_split_raw.txt)
  는 noisy diagnostic으로만 두고,
  authority public rerun
  `PAIR tcp/inproc -20.45% / -24.95%`,
  `DEALER_DEALER tcp/inproc -23.79% / -23.41%`,
  authority raw rerun
  `PAIR tcp/inproc -22.49% / -24.59%`,
  `DEALER_DEALER tcp/inproc -10.46% / -19.57%`
  ([`perf_linux_20260328_225833_codex_20260328_send_state_lane_split_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225833_codex_20260328_send_state_lane_split_public_authority.txt),
  [`perf_linux_20260328_225912_codex_20260328_send_state_lane_split_raw_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225912_codex_20260328_send_state_lane_split_raw_authority.txt))
  로 baseline보다 크게 악화돼 current code에는 남기지 않았다.
- 이어서 `public_api_sync` CAS bit를 recursive mutex-backed sync로 바꾸는
  structural candidate도 시도했지만,
  same-tag public/raw parallel run
  ([`perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_public.txt),
  [`perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_raw.txt))
  은 noisy diagnostic으로만 두고,
  authority public rerun
  `PAIR tcp/inproc -16.85% / -21.61%`,
  `DEALER_DEALER tcp/inproc -18.65% / -36.20%`,
  authority raw rerun
  `PAIR tcp/inproc -7.34% / -17.60%`,
  `DEALER_DEALER tcp/inproc -18.83% / -34.78%`
  ([`perf_linux_20260328_231611_codex_20260328_public_sync_recursive_mutex_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231611_codex_20260328_public_sync_recursive_mutex_public_authority.txt),
  [`perf_linux_20260328_231650_codex_20260328_public_sync_recursive_mutex_raw_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231650_codex_20260328_public_sync_recursive_mutex_raw_authority.txt))
  로 public `PAIR/DEALER`와 raw `DEALER`가 baseline보다 크게 악화돼
  current code에는 남기지 않았다.
- 최근 keep된 공통 delta는
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  의 recursive `check_hwm()` elide다.
- 2026-03-28 direct send-side candidate bundle 재검증에서는
  `socket_base_msg.cpp`, `socket_runtime.cpp`, `pipe.cpp`를 직접 수정해
  broad win을 노렸지만, keep할 성능개선 코드는 나오지 않았다.
- 이어서 2026-03-28 direct single-part send scope narrowing probe에서
  `send()` / `send_routed()` public sync를 initial `process_commands()`
  바깥으로 빼고 `xsend()` 주변에서만 다시 잡도록 바꿔 봤지만,
  public `PAIR tcp/inproc -21.29% / -33.25%`,
  `DEALER_DEALER tcp/inproc -23.62% / -25.85%`로 broad win이 아니었다.
- 같은 probe의 raw/public guardrail도
  `PAIR raw tcp/inproc -9.47% / -20.90%`와
  `DEALER_DEALER raw tcp/inproc -28.00% / -26.50%`로 다시 엇갈려
  keep-worthy delta가 아니어서 원복했다.
- 이어서 `lb.cpp` send-state lock을 추가하고 `DEALER` single-part `send()`를
  admission-only로 내리는 probe도 시도했지만,
  public `DEALER_DEALER tcp/inproc -15.36% / -21.40%`까지는 회복한 반면
  raw `-11.45% / -32.76%`로 다시 갈라졌고,
  unchanged `PAIR` public/raw도 `-6.31% / -30.93%`,
  `-9.37% / -35.69%`로 함께 흔들려 stable broad win이 아니어서 원복했다.
- 이어서 `socket_runtime.cpp`에서 `public_api_sync` CAS spin을
  fast-mutex wait로 분리하는 structural candidate도 시도했지만,
  public `PAIR tcp/inproc -32.03% / -29.11%`,
  `DEALER_DEALER tcp/inproc -19.33% / -31.37%`로
  `PAIR`와 `DEALER_DEALER inproc`이 accepted baseline보다 더 악화돼
  keep-worthy broad win이 아니어서 원복했다.
- 이어서 `pipe.cpp` hot send만 non-recursive lock으로 분리하고
  rare lifecycle/teardown은 기존 recursive `_out_sync`에 남기는
  structural candidate도 시도했지만,
  public `PAIR tcp/inproc -17.14% / -34.56%`,
  `DEALER_DEALER tcp/inproc -13.65% / -19.47%`,
  raw `PAIR tcp/inproc -8.61% / -25.46%`,
  `DEALER_DEALER tcp/inproc -20.69% / -21.15%`로
  `PAIR inproc` absolute throughput이 크게 무너져 keep-worthy broad win이
  아니어서 원복했다.
- 이어서 `socket_runtime.hpp` / `socket_runtime.cpp`의
  send-side lifecycle/scope hot-path를 header inline으로 옮겨
  codegen-only steady-state slimming도 시도했지만,
  public `PAIR tcp/inproc -21.70% / -23.83%`,
  `DEALER_DEALER tcp/inproc -10.64% / -17.99%`,
  raw `PAIR tcp/inproc -11.65% / -27.25%`,
  `DEALER_DEALER tcp/inproc -8.99% / -25.87%`로
  `PAIR` public/raw와 `DEALER_DEALER inproc raw`가 함께 무너져
  keep-worthy broad win이 아니어서 원복했다.
- 이어서 `socket_public_send_scope_t` constructor에서
  `enter_public_api()`만 먼저 하고 `xsend()` / `xsend_routed()` 직전에만
  `public_api_sync`를 lazy acquire하는 structural candidate도 시도했지만,
  public `PAIR tcp/inproc -9.29% / -27.64%`,
  `DEALER_DEALER tcp/inproc -26.48% / -25.53%`,
  raw `PAIR tcp/inproc -13.68% / -25.35%`,
  `DEALER_DEALER tcp/inproc -25.66% / -19.94%`로
  `PAIR tcp`만 회복하고 `PAIR inproc`과 `DEALER` broad win을 지키지 못해
  keep-worthy delta가 아니어서 원복했다.
- 이어서 `pipe.cpp` `write_and_flush()/flush()`에서
  `_out_sync` 아래 outbound invariant만 고정하고
  `send_activate_read()` publish를 lock 밖으로 미루는 candidate도
  시도했지만,
  public `PAIR tcp/inproc -16.34% / -25.54%`,
  `DEALER_DEALER tcp/inproc -26.94% / -23.32%`,
  raw `PAIR tcp/inproc -30.05% / -18.28%`,
  `DEALER_DEALER tcp/inproc -7.24% / -17.59%`로
  public baseline을 회복하지 못하고 raw `PAIR tcp`도 크게 무너져
  keep-worthy delta가 아니어서 원복했다.
- 이어서 `socket_base_msg.cpp` / `pair.cpp` / `dealer.cpp` / `lb.cpp` /
  `pipe.cpp`에서 public send scope를 `PAIR`까지 넓히고
  same scope 아래 `pipe::write()/write_and_flush()` lock을 피하는
  `single exclusion boundary merge` candidate도 시도했지만,
  직렬 rerun public `PAIR tcp/inproc -24.15% / -27.75%`,
  `DEALER_DEALER tcp/inproc -20.80% / -19.17%`,
  raw `PAIR tcp/inproc -8.40% / -22.59%`,
  `DEALER_DEALER tcp/inproc -5.92% / -20.81%`로
  `PAIR` public이 accepted baseline보다 더 악화돼 keep-worthy broad win이
  아니어서 원복했다.
- 이어서 `socket_runtime.cpp` `public_api_state` exact-state fast path
  (`0 -> 1`, `1 -> 0`, `1|sync -> 1/0` CAS shortcut) candidate도
  분리해서 시도했지만,
  public `PAIR tcp/inproc -14.49% / -31.80%`,
  `DEALER_DEALER tcp/inproc -12.85% / -21.98%`,
  raw `PAIR tcp/inproc -12.00% / -24.87%`,
  `DEALER_DEALER tcp/inproc -23.71% / -16.20%`로
  `PAIR inproc` public과 `DEALER tcp` raw가 함께 흔들려
  keep-worthy delta가 아니어서 원복했다.
- 즉 최근 직접 코드 라운드의 의미는 "새 개선이 남았다"가 아니라
  "`send()` prep micro-tuning, lifecycle atomic memory-order 완화,
  `_out_sync` hot send non-recursive scope, pipe hot send-only non-recursive
  split, send-scope/lifecycle header-inline codegen-only slimming,
  lazy send-scope sync acquire, flush notify-outside-lock tweak,
  public send scope + pipe exclusion merge, public_api_state exact-state fast
  path, caller-owned `send_serialized` helper는 current residual gap의
  keep-worthy 해법이 아니다"를 확정한 데 있다.
- 또 current `pipe::_out_sync`는 `write()/flush()` hot path만이 아니라
  `process_activate_write()`, `process_activate_read()`, `process_hiccup()`,
  `process_pipe_term*()`와 same-thread direct `activate_write` publish도 함께
  직렬화한다.
- 2026-03-28 invariant round에서는
  [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
  /
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  에
  `write_message_unlocked()/rollback_unlocked()/flush_unlocked()` helper를
  retained change로 남겨 `_out_active`, `_peers_msgs_read`, `_state`,
  `_out_pipe` outbound cluster를 code-level invariant로 고정했다.
- 같은 refactor는 `set_nodelay()`, `terminate()`, `process_delimiter()`,
  `send_disconnect_msg()`, `send_hiccup_msg()`의 recursive
  `rollback()/flush()` 의존을 제거했다.
- same day temporary direct instrumentation
  (`pair_inproc_send_profile_20260328.txt`,
  `dealer_inproc_send_profile_20260328.txt`; patch는 측정 뒤 원복)에서는
  `PAIR` / `DEALER_DEALER` inproc 모두 `process_commands initial ~56 ticks`
  보다 `send scope construct ~1266/1314 ticks`,
  `xsend initial ~731/774 ticks`,
  `pipe_write_and_flush ~576/613 ticks`가 훨씬 컸다.
- 해석: `process_commands`는 current primary cost axis가 아니고,
  `pipe_write_and_flush`가 `xsend()` 내부 대부분을 차지하며,
  `send scope construct`도 same-order steady-state에서 큰 고정비로 남아 있다.
- `PUBSUB`는 공통 send 축만으로 설명하면 안 된다. latest acceptable
  방향은 `XSUB receiver-drain specialization`과 sender-side publication
  differential을 분리해서 보는 것이다.
- current `PUBSUB` retained delta는
  `xsub` empty-subscription accept-all fast path와 requested-only
  `last_recv_source_rid` capture scope 조합으로 유지 중이다.
- guide checklist 기준 `ROUTER` routed path differential 정리 단계는 끝났고,
  current next step은 위 invariant helper 경계를 바탕으로
  common send-side structural candidate를 다시 세우는 것이다.
- 2026-03-28 authority reset은 serial public/raw refresh와
  guide/review/hot-path summary 재정렬로 이미 한 번 소비됐다.
  따라서 다음 iteration은 문서-only reset을 반복하지 않고,
  `core/` structural candidate 1개와 targeted guardrail을 반드시 포함해야
  한다.
- 그리고 그 structural candidate는 반드시 `7bea9e3f` good state 대비
  어떤 의미 단위를 current contract 안에서 더 얇게 복원하는지 설명해야 한다.
  thread-safe 계약 약화나 POSD 위반을 대가로 삼는 후보는 즉시 reject한다.
- 2026-03-28 retained structural prep에서는
  [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  plain/routed direct send retry를 `send_direct_with_retry()` 경계로 합쳤고,
  [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  /
  [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  는 retry 시 sync 유지/해제/재획득 판단을
  `socket_public_send_scope_t` helper로 다시 모았으며,
  [`core/src/core/multipart_send_txn.cpp`](/home/hep7/project/kairos/zlink/core/src/core/multipart_send_txn.cpp)
  는 plain direct send scope 판단을
  `socket_base_t::direct_send_needs_public_api_sync()` 하나로 재사용한다.
- 위 prep의 targeted guardrail은 public
  `PAIR tcp/inproc -14.85% / -21.37%`,
  `DEALER_DEALER tcp/inproc -24.99% / -17.70%`,
  raw `PAIR tcp/inproc -6.56% / -21.49%`,
  `DEALER_DEALER tcp/inproc -20.22% / -20.81%`였다.
  current retained baseline 대비 mixed/noise지만 keep을 막을 만큼의 새
  broad regression은 확인하지 못했다.
- 같은 boundary 위 `socket_runtime.hpp/.cpp`
  `public_api_inflight/public_api_closing/public_api_sync` split candidate도
  시도했지만,
  public `PAIR tcp/inproc -24.72% / -26.53%`,
  `DEALER_DEALER tcp/inproc -19.44% / -22.01%`,
  raw `PAIR tcp/inproc -9.25% / -17.70%`,
  `DEALER_DEALER tcp/inproc -17.73% / -31.95%`로
  `PAIR` public과 `DEALER inproc` raw가 함께 흔들려 원복했다.
- 이어서 `pipe.cpp` / `pair.cpp` / `lb.cpp` final non-routing payload
  flush helper도 시도했지만,
  public `PAIR tcp/inproc -12.51% / -26.39%`,
  `DEALER_DEALER tcp/inproc -9.53% / -19.49%`로 `tcp`만 회복했고,
  raw는 `PAIR inproc -34.78%`, `DEALER_DEALER inproc -21.83%`,
  `DEALER_DEALER tcp timeout`까지 깨져 broad win이 아니어서 원복했다.
- 이어서 `pipe.cpp` `process_activate_write()` already-active
  peer-progress snapshot split candidate도 시도했지만,
  targeted public/raw rerun은
  `PAIR tcp/inproc -13.02% / -17.35%`, `-9.67% / -19.42%`,
  `DEALER_DEALER tcp/inproc -14.66% / -18.53%`, `-8.19% / -19.85%`까지
  회복했어도 broader single
  `DEALER_DEALER inproc -29.32%`, `DEALER_ROUTER inproc -30.93%`,
  partial `ROUTER_ROUTER tcp -52.69%`와
  `comp_zlink_router_router zlink inproc 64` hang을 만들어 원복했다.
- 이어서 같은 family의 `process_activate_write()` atomic peer-progress
  publish candidate도 시도했지만,
  targeted public
  `PAIR tcp/inproc -22.80% / -18.39%`,
  `DEALER_DEALER tcp/inproc -35.30% / -19.86%`,
  raw `PAIR tcp/inproc -23.83% / -31.74%`,
  `DEALER_DEALER tcp/inproc -11.45% / -15.34%`로
  targeted stage부터 broad win이 아니어서 원복했다.
- 이어서 `DEALER` / `ROUTER`처럼 existing public send sync가 이미 잡힌
  caller에서만 `pipe` send lock을 건너뛰는
  `send_serialized` helper candidate도 시도했지만,
  public `PAIR tcp/inproc -13.23% / -17.01%`,
  `DEALER_DEALER tcp/inproc -23.74% / -31.19%`,
  raw `PAIR tcp/inproc -18.50% / -22.98%`,
  `DEALER_DEALER tcp/inproc -20.96% / -23.13%`로
  `PAIR` / `DEALER` public/raw guardrail을 함께 지키지 못해 원복했다.
- 이어서 `DEALER` same-handle send serialization을
  `public_api_sync` 밖 external recursive mutex +
  external `socket_public_send_scope_t` serialized scope로 옮기는
  candidate도 시도했지만,
  public `PAIR tcp/inproc -9.23% / -16.03%`,
  `DEALER_DEALER tcp/inproc -13.09% / -32.97%`,
  raw `PAIR tcp/inproc -13.88% / -26.40%`,
  `DEALER_DEALER tcp/inproc -24.35% / -33.20%`로
  targeted stage부터 broad win이 아니어서 원복했다.
- 이어서 existing public send sync가 이미 잡힌 `DEALER` caller에서
  final `write+flush`만 `_out_sync` 밖 pipe hot-send lease로 보내고
  rare `_out_pipe` mutation이 inflight send를 기다리게 하는
  candidate도 시도했지만,
  targeted public `PAIR tcp/inproc -28.83% / -19.45%`,
  `DEALER_DEALER tcp/inproc -15.40% / -22.79%`로
  public stage부터 broad win이 아니어서 원복했다.
- 이어서
  [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
  native recursive pthread primitive replacement도 시도했지만,
  build 완료 뒤 rerun한 stream/contract gate는 통과했어도
  public `PAIR tcp/inproc -27.78% / -17.52%`,
  `DEALER_DEALER tcp/inproc +3.72% / -21.03%`,
  raw `PAIR tcp/inproc -13.25% / -21.63%`,
  `DEALER_DEALER tcp/inproc -7.74% / -15.86%`로
  unchanged control인 `PAIR public tcp`와 raw `PAIR inproc` guardrail을
  함께 지키지 못해 원복했다.
- latest stdin 기반 `claude -p` consult는 `claude --help`까지는
  통과했지만 latest retry도 `timeout 120s`로 종료돼 usable advisory를
  얻지 못했다.
  이번 structural round의 latest consult는 unavailable로 기록한다.
- `process_activate_write()` snapshot/atomic,
  existing public-send-sync-held `send_serialized`,
  `DEALER` external send-state mutex / external `send_serialized` scope,
  existing public-send-sync-held hot-send lease / outpipe lifetime split,
  `fast_mutex.hpp` native recursive pthread primitive replacement까지
  common send-side structural family 다섯 계열이 연속 reject됐다.
  guide 6.2/6.3 기준으로는 local search drift 구간이므로,
  다음 iteration은 또 다른 hot-path patch가 아니라
  guide/review/hot-path 재정렬부터 시작해야 한다.
- 해석: current residual의 `send admission/scope construct` 비용은
  `public_api_state` packing 하나나 peer-progress publish family 하나,
  existing public send sync 재사용,
  `DEALER` same-handle send serialization 위치 이동,
  existing public-send-sync-held hot-send lease / outpipe lifetime split만으로
  설명되지 않고,
  다음 structural round는 send scope construct와 `_out_sync`
  steady-state duty를 함께 가르는 쪽이 우선이다.
- 2026-03-28 current recheck에서 `ROUTER_ROUTER` single 64B default는
  `tcp/inproc -56.84% / -28.68%`,
  raw-msg probe는 `-58.04% / -23.52%`였다.
- 즉 current raw/public aggregate wrapper 차이는 `inproc`에서만
  `+5.16`%p 정도 움직였고, `tcp`는 오히려 `-1.20`%p 더 나빠졌다.
- 같은 recheck의 zlink absolute throughput은 대체로
  `tcp ~1.21Mmsg/s`, `inproc ~2.42Mmsg/s`였다.
- current `DEALER_ROUTER` single 64B recheck도
  `tcp/inproc -30.83% / -31.56%`라서,
  current `ROUTER` 잔여 gap은 sender-only가 아니라 routed recv/public 쪽에도
  이미 큰 고정비가 남아 있다.
- current blocking `ROUTER` send는
  [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
  에서 이미 routing-id envelope를 `send_routed()` one-part path로 접어 보낸다.
- 같은 envelope fold를 `ZLINK_DONTWAIT` 경로까지 넓히는 candidate도
  bench active phase가 blocking send를 쓴다는 사실만 재확인한 채
  [`perf_linux_20260329_053009_codex_20260329_router_public_nonblocking_envelope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_053009_codex_20260329_router_public_nonblocking_envelope.txt)
  `ROUTER_ROUTER tcp/inproc -58.16% / -31.94%`로 더 나빠져
  keep-worthy delta가 아니었다.
- shared logical multipart entry-state reuse candidate도
  [`perf_linux_20260329_054339_codex_20260329_multipart_sender_regime_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_054339_codex_20260329_multipart_sender_regime_public.txt)
  first `ROUTER_ROUTER tcp/inproc -54.15% / -29.86%`,
  [`perf_linux_20260329_054423_codex_20260329_multipart_sender_regime_public_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_054423_codex_20260329_multipart_sender_regime_public_rerun.txt)
  rerun `-58.08% / -22.16%`로 relative diff는 흔들렸지만,
  zlink absolute throughput은 `tcp 1296.50 -> 1292.20`,
  `inproc 2572.46 -> 2574.88 msg/s`로 거의 안 움직여 keep-worthy delta가
  아니었다.
- 이어서 `socket_base_msg.cpp` / `socket_message_recv_api.cpp`
  routed source-rid zeroing-floor candidate도
  concurrent diagnostic
  [`perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_public.txt),
  [`perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_raw.txt)
  는 public/raw를 동시에 띄워 noisy artifact로만 두고,
  authority public
  [`perf_linux_20260329_055517_codex_20260329_router_recv_rid_zeroing_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055517_codex_20260329_router_recv_rid_zeroing_public_authority.txt)
  `-56.10% / -32.31%`,
  authority raw
  [`perf_linux_20260329_055548_codex_20260329_router_recv_rid_zeroing_raw_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055548_codex_20260329_router_recv_rid_zeroing_raw_authority.txt)
  `-57.70% / -22.00%`로 broad win을 만들지 못했다.
  zlink absolute throughput도 public `1297.34 / 2575.26`,
  raw `1295.90 / 2570.82 Kmsg/s` 수준에 머물러, `recv_routed()` export에서
  `zlink_routing_id_t` full-zero를 `size=0`만으로 줄이는 local tweak는
  keep-worthy delta가 아니었다.
- 같은 `xsend_routed()` final-part hot path에서 ready check와
  `write+flush`를 one-lock helper로 합쳐도 zlink absolute throughput은
  대체로 `tcp ~1.21Mmsg/s`, `inproc ~2.41Mmsg/s`에 머물렀다.
  즉 current `ROUTER_ROUTER` 잔여 gap을 send wrapper나 routed final-part
  micro-fusion, handshake-only nonblocking envelope fast path,
  logical multipart entry-state reuse, local source-rid zeroing-floor 하나로
  설명할 수는 없다.
- 같은 날 same-target routed send cache + final-part one-lock combo도
  relative diff는 `libzmq` baseline 흔들림에 따라 일부 좋아 보였지만,
  direct `comp_zlink_router_router` absolute throughput이
  `tcp 1214300.00`, `inproc 2419754.40 msg/s`로 baseline 수준에 머물러
  keep-worthy delta가 아니었다.
- 이어서 tried한 routed recv current-in/source-rid cache + lazy
  prefetched-id prepare도 default/raw relative diff는 일부 회복했지만,
  direct `comp_zlink_router_router` absolute throughput이
  `tcp 1211724.60`, `inproc 2408252.00 msg/s`로 baseline 수준에 머물러
  keep-worthy delta가 아니었다.
- 이어서 `pipe.hpp` / `pipe.cpp` routing-id export-ready cache +
  `router.cpp` / `socket_base_dispatch.cpp` direct copy candidate도
  public first/rerun
  `ROUTER_ROUTER tcp/inproc -54.57% / -28.37%`,
  `-60.15% / -26.32%`였지만,
  zlink absolute throughput이 `tcp 1301.85 -> 1295.74`,
  `inproc 2572.49 -> 2567.63 Kmsg/s`로 baseline 범위에 머물러
  keep-worthy delta가 아니었다.
- 대신 current code에는
  `test_public_inproc_router_recv_multipart_with_source_rid_blocking()`과
  `test_public_inproc_router_msg_recv_rid_keeps_source_rid_across_reset()`
  회귀만 남겨 routed recv/source-rid contract를 고정했다.

### 0.2 Current Hypothesis

- `libzmq` 대비 공통 차이의 상위 축은
  [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  +
  [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  의 public lifecycle coordinator와
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  +
  [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
  의 send-path serialization이다.
- 다만 방금 reject된 `public_api_inflight/public_api_closing/public_api_sync`
  split candidate까지 합치면, current next step은 coordinator state repack을
  반복하는 것이 아니라 `_out_sync` helper boundary 위 hot send/rare
  teardown duty를 더 직접 가르는 쪽이다.
- 방금 reject된 `send_serialized` helper candidate까지 합치면,
  existing public send sync를 `pipe::_out_sync` 대용 exclusion으로
  재사용하는 발상도 current broad fix family에서는 내린다.
- 다만 above 해석만으로 곧바로 다른 local helper family를 열면 안 된다.
  latest rejected
  public API-boundary same-handle recursive mutex single-part fast path,
  same-thread parked send admission lease,
  `pipe.hpp` / `pipe.cpp` peer-progress refreshed HWM-credit cache,
  `_lwm` boundary `activate_write` progress-command coalesce,
  `msg_t::init_size()/close()` small-lmsg pooled materialize/free,
  send-side layout regroup,
  preflight-before-public-admission candidate까지
  합치면,
  current immediate next step은 coordinator state repack이나 another
  pipe-local shaving, message-local allocator pool,
  layout-only regroup, preflight-before-admission split을 반복하는 것이
  아니라,
  current kept boundary
  (`send_direct_with_retry()` /
  `socket_public_send_scope_t::should_hold_sync_during_retry()` /
  `socket_base_t::direct_send_needs_public_api_sync()` /
  `_out_sync` unlocked helper)
  위에서
  `a819ea3a` admission floor,
  `ff0140e5` pipe serialization floor,
  `98e7d324/9b91234c` public multipart/sender-regime 의미를 함께 다시 읽는
  new broad hypothesis 하나를 먼저 고르는 것이다.
- `pipe.hpp` / `pipe.cpp` `_lwm` 경계 `activate_write` progress-command
  coalesce candidate도
  [`perf_linux_20260329_041240_codex_20260329_activate_write_coalesce_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_041240_codex_20260329_activate_write_coalesce_public.txt)
  에서 public `PAIR tcp/inproc -10.58% / -34.54%`,
  `DEALER_DEALER tcp/inproc -30.23% / -25.32%`로 early authority와
  session-local low baseline을 함께 못 지켜 reject됐다.
  즉 `9b91234c` sender-regime 흔적을 progress-command emission count만
  줄여 얇게 만드는 family도 current broad fix가 아니다.
- latest current-tree direct instrumentation에서
  `pipe_write_and_flush`는
  `PAIR` `866.94 ticks` / `DEALER_DEALER` `862.54 ticks`였고,
  내부 bucket은 `lock 70.95/70.63`, `hwm 25.12/25.33`,
  `write 38.50/38.98`, `flush 259.26/266.00`였다.
  또 `flush_outcome true=8840861/8740895`, `false=539397/506441`로
  false wakeup/notify는 약 5~6%뿐이었다.
  즉 current `pipe serialization floor`의 큰 축은 sleeping-reader wakeup
  자체보다 successful publication/CAS path 쪽이다.
- 하지만 같은 근거로 시도한
  `ypipe_base.hpp` / `ypipe.hpp` / `ypipe_conflate.hpp`
  combined write+publication candidate도
  [`perf_linux_20260329_043927_codex_20260329_ypipe_write_publish_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_043927_codex_20260329_ypipe_write_publish_public.txt)
  에서 public `PAIR tcp/inproc -36.56% / -24.11%`,
  `DEALER_DEALER tcp/inproc -10.62% / -18.47%`로
  early authority와 session-local low baseline을 함께 못 지켜 reject됐다.
  즉 `flush true` dominant라는 사실이 곧바로 another local `ypipe`
  publication helper keep로 이어지지는 않았다.
- 같은 방향으로 재확인한
  `pipe.cpp` `process_activate_read()` steady-state read-activation split
  candidate도
  [`perf_linux_20260329_045615_codex_20260329_recv_activation_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_045615_codex_20260329_recv_activation_public.txt)
  에서 public `PAIR tcp/inproc -27.18% / -17.96%`,
  `DEALER_DEALER tcp/inproc -22.07% / -18.31%`로
  early authority와 session-local low baseline을 함께 못 지켜 reject됐다.
  즉 `ff0140e5` read-side residue를 local helper scope로만 다시 나눠도
  current broad fix는 되지 않았다.
- latest env-gated send-scope split instrumentation
  [`pair_send_scope_profile_20260329.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/pair_send_scope_profile_20260329.txt)
  /
  [`dealer_send_scope_profile_20260329.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/dealer_send_scope_profile_20260329.txt)
  에서는 no-sync `PAIR`
  `enter_public_api/leave_public_api 49.70/50.01 ticks`,
  sync-fast `DEALER_DEALER`
  `enter_public_api_and_lock_sync_fast/unlock_public_api_sync_and_leave`
  `49.66/49.67 ticks`,
  send-scope ctor/dtor total `174.36/175.98`, `174.80/176.78`만 확인됐다.
  profiling overhead 때문에 throughput 자체는 authority가 아니지만,
  earlier `socket_scope_construct ~1266/1314 ticks` bucket이
  lifecycle atomics 단독이 아니라는 점은 분명해졌다.
- 따라서 current first-priority implementation target은
  another admission-floor-only lifecycle split이 아니라,
  current `xsend_initial` / `pipe::_out_sync` send publication floor와
  그 위 `98e7d324/9b91234c` public multipart/sender-regime 흔적을
  함께 읽는 쪽으로 내린다.
- 다음 랄프루프는 이 추상을 다시 처음부터 추정하지 말고,
  bisect가 특정한 historical concrete change 4개를 먼저 입력으로 둔다.
  즉 `ff0140e5 -> a819ea3a -> 98e7d324 -> 9b91234c` 축을 기준으로
  어떤 의미가 현재 HEAD까지 남아 있는지부터 본다.
- `recv parts_out` heap-return/export는 historical first collapse의 직접
  원인이었지만, current residual gap의 1순위는 아니다.
- `echo`가 괜찮고 `oneway`에서만 더 밀리는 건 단순 `send API`가 아니라
  producer가 지속적으로 앞서가는 operating regime에서의 send/publication
  고정비로 해석한다.
- `ROUTER_ROUTER`의 current 잔여 gap은
  [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
  /
  [`core/src/api/socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
  의 aggregate wrapper만이 아니라,
  [`core/src/sockets/router.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.cpp)
  의 routed recv ordering과, 그 아래
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  /
  [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
  공통 serialization floor가 더 큰 축일 가능성이 높다.
- 다만 current recheck의 `DEALER_ROUTER`가 이미 `-30%`대이므로,
  next step은 same-target routed send local cache보다
  `recv_routed()` / routed source-rid export / prefetch ordering differential을
  먼저 분리하는 쪽이 더 우선이다.
- 같은 이유로 routed recv local state/source-rid cache나 lazy
  prefetched-id prepare를 다시 올리기보다, 그 아래 실제 ordering 차이와
  export path work를 더 직접 가르는 쪽이 맞다.
- 같은 이유로 pipe-local routing-id export-ready cache처럼
  blob copy shape만 바꾸는 local export family도
  absolute throughput을 못 움직였으므로,
  next step은 또 다른 cache-only source-rid export tweak가 아니다.

### 0.3 Kept Delta

- `_out_sync` 아래에서 다시 `check_hwm()` recursive lock을 타지 않도록
  `check_hwm_unlocked()`를 사용하는
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  변경은 현재 keep 상태다.
- [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
  /
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  의 `_out_sync` unlocked helper refactor도 current tree에 유지한다.
- [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  의 `send_direct_with_retry()` 경계,
  [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  /
  [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  의 retry sync phase helper,
  [`core/src/core/multipart_send_txn.cpp`](/home/hep7/project/kairos/zlink/core/src/core/multipart_send_txn.cpp)
  의 direct send scope 결정 재사용도 current kept structural prep이다.
- 2026-03-28 direct send-side candidate bundle에서 시도한
  `socket_base_msg.cpp` / `socket_runtime.cpp` / `pipe.cpp` 추가 성능 후보는
  모두 reject되어 current tree에는 남아 있지 않다.
- `PUBSUB` 계열은 latest acceptable path에서 `XSUB` receiver-drain
  specialization을 우선 검토 대상으로 둔다.

### 0.4 Rejected Families

- `backpressure/HWM`만이 본체라는 해석
- `PAIR` 전용 no-sync lifecycle CAS fast path
- `fast_mutex` common-path TID-lazy 후보
- 새 증거 없이 `dist/xpub/pipe` local helper만 반복 추가하는 탐색
- `ROUTER_ROUTER` raw/public aggregate wrapper만이 본체라는 해석
- `ROUTER_ROUTER` `xsend_routed()` final-part one-lock helper
- `ROUTER_ROUTER` same-target routed send cache + final-part one-lock combo
- `ROUTER_ROUTER` routed recv current-in/source-rid cache + lazy prefetched-id
  prepare
- `socket_runtime.hpp/.cpp`
  `public_api_inflight/public_api_closing/public_api_sync` split candidate
- `socket_base_msg.cpp` direct single-part `send_scope` initial unlock +
  relock-around-`xsend()` candidate
- `socket_base_msg.cpp` `DEALER` single-part admission-only +
  `lb.cpp` send-state lock candidate
- `socket_runtime.cpp` `public_api_sync` fast-mutex split candidate
- `pipe.cpp` hot send-only non-recursive lock split candidate
- `socket_runtime.hpp/.cpp` send-scope/lifecycle header-inline codegen-only candidate
- `socket_public_send_scope_t` constructor lazy-sync acquire candidate
- `pipe.cpp` flush notify-outside-`_out_sync` candidate
- `PAIR` public send scope + `pipe` serialized write merge candidate
- `socket_runtime.cpp` `public_api_state` exact-state fast path candidate
- `socket_runtime.hpp` / `socket_runtime.cpp`
  dedicated public send lease split candidate
- `socket_runtime.hpp` / `socket_runtime.cpp`
  shared `public_api_state` public/send inflight lane split candidate
- `socket_runtime.hpp` / `socket_runtime.cpp`
  `public_api_sync` recursive mutex-backed split candidate
- `pipe.cpp` final-part `write_and_flush()` lock-free snapshot candidate
- `pipe::_out_sync` plain non-recursive fast mutex candidate
- `pipe.hpp` / `pipe.cpp` non-conflate out-pipe concrete `ypipe_t` fast path
  candidate
- `pipe.cpp` `process_activate_write()` atomic peer-progress publish candidate
- `dealer.cpp` / `router.cpp` existing public-send-sync-held
  `pipe` `send_serialized` helper candidate
- `socket_runtime.cpp` / `dealer.cpp` `DEALER` external send-state mutex +
  external `send_serialized` scope candidate
- `pipe.cpp` / `lb.cpp` / `dealer.cpp` existing public-send-sync-held pipe
  hot-send lease / outpipe lifetime split candidate
- `fast_mutex.hpp` native recursive pthread primitive replacement candidate
- `socket_runtime.hpp` / `socket_runtime.cpp`
  dedicated public send lease split candidate
- `socket_base_api.cpp` / `socket_base_msg.cpp` /
  `socket_message_send_api.cpp` public API-boundary same-handle recursive
  mutex single-part fast path candidate
- `socket_runtime.hpp` / `socket_runtime.cpp` / `socket_base_msg.cpp`
  same-thread parked send admission lease candidate
- `pipe.hpp` / `pipe.cpp` peer-progress refreshed HWM-credit cache candidate
- `pipe.hpp` / `pipe.cpp` `activate_write` progress-command coalesce
  candidate
- `core/src/core/ypipe_base.hpp` / `core/src/core/ypipe.hpp` /
  `core/src/core/ypipe_conflate.hpp` combined write+publication candidate
- `pipe.cpp` `process_activate_read()` steady-state read-activation split
  candidate
- `pipe.hpp` / `pipe.cpp` routing-id export-ready cache +
  `router.cpp` / `socket_base_dispatch.cpp` direct copy candidate
- `core/src/core/msg.cpp` small-lmsg pooled materialize/free candidate
- `socket_runtime.hpp` / `pipe.hpp` / `pipe.cpp` send-side layout regroup
  candidate
- `socket_base.hpp` / `socket_base_msg.cpp`
  preflight-before-public-admission candidate

### 0.5 Do Not Revisit

- high-HWM probe를 뒤집는 새 증거 없이 `wakeup latency`를 1차 원인으로 다시
  올리지 않는다.
- broad win 없이 특정 pattern 하나만 좋아지는 helper-level 후보를 계속
  누적하지 않는다.
- `process_activate_write()` snapshot / atomic peer-progress publish family,
  existing public-send-sync-held `send_serialized` helper family,
  `DEALER` external send-state mutex / external `send_serialized` scope
  family,
  existing public-send-sync-held pipe hot-send lease / outpipe lifetime split
  family,
  `fast_mutex.hpp` native recursive pthread primitive replacement family,
  dedicated public send lease split family,
  `public_api_sync` recursive mutex-backed split family,
  `pipe.cpp` final-part `write_and_flush()` lock-free snapshot family,
  `pipe::_out_sync` plain non-recursive fast mutex family,
  non-conflate out-pipe concrete `ypipe_t` fast path family,
  public API-boundary same-handle recursive mutex single-part fast path,
  same-thread parked send admission lease,
  peer-progress refreshed HWM-credit cache,
  `activate_write` progress-command coalesce,
  `ypipe` combined write+publication,
  `process_activate_read()` steady-state read-activation split,
  `msg_t::init_size()/close()` small-lmsg pooled materialize/free,
  send-side layout regroup,
  preflight-before-public-admission,
  public `ROUTER` nonblocking envelope -> `send_routed()` same-path fast path,
  shared logical multipart entry-state reuse,
  `recv_routed()` source-rid zeroing-floor,
  pipe-local routing-id export-ready cache
  family는
  새 broad evidence 없이 다시 올리지 않는다.
- latest send-scope split diagnostics가 lifecycle atomics 자체를
  `~50 ticks` 수준으로 낮춰 잡았으므로,
  another admission-floor-only lifecycle fast path family도
  새 broad evidence 없이 다시 올리지 않는다.
- 긴 로그를 처음부터 다시 읽으며 local tweak 후보를 재채집하지 않는다.
  iteration 시작은 항상 이 summary와 최신 pivot부터 본다.

### 0.6 Next Exact Step

- restart 시 먼저 bisect 결론을 기준으로
  `historical first direct cause`와 `current residual direct cause`를 분리해
  읽는다.
- historical axis는 `9b91234c` surface shift만이 아니라,
  `ff0140e5` / `a819ea3a` / `98e7d324` / `9b91234c` 네 변화를
  concrete input으로 유지한다.
- latest send-scope split diagnostics는
  earlier `socket_scope_construct ~1266/1314 ticks` bucket이
  lifecycle admission atomics 단독이 아니라는 점을 보여줬다.
  즉 current clean-tree diagnostic에서는
  no-sync `PAIR` `enter_public_api/leave_public_api ~50/50 ticks`,
  sync-fast `DEALER_DEALER`
  `enter_public_api_and_lock_sync_fast/unlock_public_api_sync_and_leave`
  `~50/50 ticks`, ctor/dtor total `~175/~176 ticks`였다.
  profiling throughput은 authority가 아니지만,
  next candidate를 another admission-floor-only family로 두면 안 된다는
  근거로는 충분하다.
- current HEAD에서는 아래 순서로 본다.
  1. `ff0140e5` 이후 `xsend_initial` 아래 `pipe::_out_sync`가
     steady-state send-path에서 떠안는 publication/serialization 의미
  2. `98e7d324` / `9b91234c`의 public multipart/sender-regime 전환과
     routed/source-rid export가 current raw/public residual에 남긴 흔적
  3. `a819ea3a` admission floor는 historical input으로 유지하되,
     새 broad evidence가 생길 때만 다시 implementation target으로 올린다
- next structural candidate는 local lock primitive 교체가 아니라,
  `pipe::_out_sync` 아래에 함께 묶인
  `outbound publication state`와 `lifecycle / activation state`의
  ownership split 여부를 먼저 설계로 검토하는 family로 고정한다.
  현재 `write()/write_and_flush()` hot path는 `_out_sync` 아래에서
  HWM, `_out_pipe` write/flush, `_out_active`, `_state`, `_peers_msgs_read`,
  activate/term 계열 전이와 같은 coupled 의미를 함께 떠안고 있으므로,
  다음 round는 또 다른 helper-level shave가 아니라
  steady-state send publication cluster를 더 얇게 설명할 수 있는지부터
  판단해야 한다.
- current `ROUTER` 쪽에서는 active single phase가 blocking send를 쓰므로,
  nonblocking envelope local fast path를 다시 여는 대신 blocking default path
  기준의 routed recv ordering / `recv_routed()` export / `_out_sync`
  serialization floor를 우선 본다.
- 같은 이유로 logical multipart scope 아래서 entry `process_commands()`
  reuse만 추가하는 sender-regime family도 absolute throughput을 거의 못
  움직였으므로, next step은 또 다른 local entry-state reuse가 아니다.
- 같은 이유로 `recv_routed()` export에서 source-rid output 전체 zero를
  `size=0` reset으로 줄이는 local zeroing-floor family도
  zlink absolute throughput을 거의 못 움직였으므로,
  next step은 또 다른 local source-rid export memset shaving이 아니다.
- 같은 이유로 pipe-local routing-id export-ready cache family도
  zlink absolute throughput을 거의 못 움직였으므로,
  next step은 또 다른 cache-only source-rid export reshape가 아니다.
- helper-level send micro-tuning(`send()` common prep fast path, lifecycle
  memory-order 완화, `_out_sync` hot send non-recursive scope,
  send-scope/lifecycle header-inline codegen-only slimming)은 최근
  direct candidate bundle에서 이미 reject되었으므로,
  새 structural 근거 없이 다시 올리지 않는다.
- 같은 이유로 `pipe_write_and_flush` successful publication/CAS bucket이
  더 크다고 해서 local `ypipe` combined write+publication helper를
  다시 여는 것도 current broad fix 후보에서 내린다.
- 같은 이유로 `ff0140e5` read-side residue를 local
  `process_activate_read()` helper split으로만 다시 여는 family도
  current broad fix 후보에서 내린다.
- same 이유로 direct single-part `send()` / `send_routed()`에서
  sync를 initial `process_commands()` 바깥으로 빼는 local tweak도
  current broad fix 후보에서 내린다.
- 같은 이유로 `DEALER` one-active `lb_t` mutable send state를
  local lock으로만 감싸고 direct single-part `send()`를 admission-only로
  내리는 후보도 broad fix 후보에서 내린다.
- 같은 이유로 `public_api_sync` wait primitive만 CAS bit에서
  fast-mutex로 바꾸는 structural split도 broad fix 후보에서 내린다.
- 같은 이유로 `pipe` hot send만 non-recursive lock으로 분리하는 family도
  broad fix 후보에서 내린다.
- 같은 이유로 `socket_public_send_scope_t` constructor에서
  `public_api_sync`를 lazy acquire하는 family도 broad fix 후보에서 내린다.
- 같은 이유로 `_out_sync` 안에서만 바뀌는 flush notify placement tweak도
  broad fix 후보에서 내린다.
- 같은 이유로 `PAIR`까지 public send scope를 넓혀
  `pipe` lock을 합치려는 exclusion-boundary merge family도
  broad fix 후보에서 내린다.
- 같은 이유로 `public_api_state` exact-state CAS shortcut family도
  broad fix 후보에서 내린다.
- 같은 이유로 `process_activate_write()` atomic peer-progress publish family도
  broad fix 후보에서 내린다.
- 같은 이유로 `DEALER` / `ROUTER` existing public send sync를
  `pipe::_out_sync` 대용 exclusion으로 재사용하는
  `send_serialized` helper family도 broad fix 후보에서 내린다.
- 같은 이유로 `DEALER` same-handle send serialization을
  `public_api_sync` 밖 external recursive mutex +
  external `socket_public_send_scope_t` serialized scope로 옮기는 family도
  broad fix 후보에서 내린다.
- 같은 이유로 existing public send sync가 이미 잡힌 caller에서
  final `write+flush`만 `_out_sync` 밖 hot-send lease로 보내고
  rare `_out_pipe` mutation이 inflight send를 기다리게 하는 family도
  broad fix 후보에서 내린다.
- 같은 이유로 `fast_mutex.hpp` native recursive pthread primitive
  replacement family도 broad fix 후보에서 내린다.
- 같은 이유로 full public lifecycle coordinator 아래에 dedicated
  public send lease를 두고 send scope를 다시 나누는 family도
  broad fix 후보에서 내린다.
- 같은 이유로 `public_api_sync` CAS bit를 recursive mutex-backed sync로
  바꾸는 family도 broad fix 후보에서 내린다.
- 같은 이유로 `pipe.cpp` final-part `write_and_flush()`를
  lock-free snapshot fast path로 보내는 family도 broad fix 후보에서 내린다.
- 같은 이유로 current ordering/invariant를 유지한
  `pipe::_out_sync` plain non-recursive fast mutex family도
  broad fix 후보에서 내린다.
- 같은 이유로 public API-boundary same-handle recursive mutex single-part
  fast path family도 broad fix 후보에서 내린다.
- 같은 이유로 same-thread parked send admission lease family도
  broad fix 후보에서 내린다.
- 같은 이유로 `_lwm` 경계 `activate_write` progress-command coalesce
  family도 broad fix 후보에서 내린다.
- 같은 이유로 `msg_t::init_size()/close()` small-lmsg pooled materialize/free
  family도 broad fix 후보에서 내린다.
- 같은 이유로 `socket_runtime.hpp` / `pipe.hpp` / `pipe.cpp`
  send-side layout regroup family도 broad fix 후보에서 내린다.
- 같은 이유로 `socket_base.hpp` / `socket_base_msg.cpp`
  preflight-before-public-admission family도 broad fix 후보에서 내린다.
- 따라서 `ROUTER_ROUTER` local sender cache, final-part one-lock helper,
  routed recv local state/source-rid cache 같은 rejected family는
  common send-side pass가 끝나기 전까지 다시 올리지 않는다.
- invariant map과 direct 계측 단계는 끝났다.
- 다음 iteration은 code patch 전에
  `pair_inproc_send_profile_20260328.txt` /
  `dealer_inproc_send_profile_20260328.txt`
  진단 로그와
  [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
  /
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  의 unlocked helper 경계를 먼저 다시 읽는다.
- 실제 코드 적용 우선순위는 아래 순서다.
  1. common send-side structural family가
     `process_activate_write()` snapshot/atomic,
     existing public-send-sync-held `send_serialized`,
     `DEALER` external send-state mutex / external `send_serialized` scope,
     existing public-send-sync-held hot-send lease / outpipe lifetime split,
     `fast_mutex.hpp` native recursive pthread primitive replacement,
     dedicated public send lease split,
     shared `public_api_state` public/send inflight lane split,
     `public_api_sync` recursive-mutex-backed split,
     `pipe.cpp` final-part `write_and_flush()` lock-free snapshot split,
     `pipe::_out_sync` plain non-recursive fast mutex split,
     public API-boundary same-handle recursive mutex single-part fast path,
     `msg_t::init_size()/close()` small-lmsg pooled materialize/free까지
     연속 reject됐다는 현재 summary를 그대로 유지하고 반복 family를
     다시 올리지 않는다.
  2. `pair_inproc_send_profile_20260328.txt` /
     `dealer_inproc_send_profile_20260328.txt`와
     current kept boundary
     (`send_direct_with_retry()` /
     `socket_public_send_scope_t::should_hold_sync_during_retry()` /
     `socket_base_t::direct_send_needs_public_api_sync()` /
     `_out_sync` unlocked helper),
     그리고 historical
     `ff0140e5 -> a819ea3a -> 98e7d324 -> 9b91234c`
     map을 다시 붙여
     current residual direct cause를
     `public admission floor` 대 `pipe serialization floor`로 먼저 쓴다.
  3. late-session serial current-tree `PAIR` / `DEALER_DEALER`
     public/raw refresh + rerun까지 다시 찍은 결과,
     earlier authority보다 더 낮은 session-local baseline이 반복됐다는 점을
     current summary에 유지한다.
  4. 따라서 next candidate는
     21:23/21:24 authority baseline과
     23:25/23:30 session-local low baseline을 둘 다 guardrail로 보고,
     signal이 섞이면 current-tree serial refresh를 먼저 다시 찍은 뒤에만
     keep/reject를 결정한다.
  5. 위 넷이 끝난 현재 next step은
     새 broad hypothesis 하나를 다시 열고,
     그 다음에야 새로운 code family를 선택하는 것이다.
- 새 iteration은 이 summary가 stale하지 않은지 먼저 확인하고, stale하면
  코드 수정 전에 이 블록부터 갱신한다.

## 1. 현재 결론

현재 `with_zmq single` 격차는 "libzmq와 같은 수준의 raw message path"를
비교한 결과가 아니다. 다만 raw/public 분리는 최근 rerun에서도 패턴과
transport에 따라 다시 엇갈렸으므로, 더 이상 "`public wrapper penalty는 이미
항상 작다`"는 식의 고정 전제로 읽으면 안 된다.

현재 HEAD 기준에서는 raw/public을 iteration별 guardrail로 다시 찍으면서,
남은 본체를 send-side core engine differential과 public penalty 변화로 함께
읽는 것이 가장 맞다.

핵심은 다음 네 가지다.

- `3/23 -> 3/27` POSD 직접 회귀는 대부분 회복됐다.
- 현재 `with_zmq single`은 `zlink`와 `libzmq`가 같은 API surface를 타지 않는다.
- 현재 `zlink`의 `pipe_t`는 steady-state `read/write/flush`에서
  `fast_mutex_t`를 잡지만, libzmq `pipe_t`는 같은 잠금이 없다.
- 다만 raw/public 분리 결과는 send-side 변경에 따라 다시 벌어질 수 있다.
  즉 이 값은 "한 번 측정해서 끝나는 결론"이 아니라 매 send-path 변경 뒤
  다시 확인해야 하는 guardrail이다.
- 따라서 지금 보이는 큰 격차는 public wrapper 하나로 설명하면 안 되고,
  현재는 send-side core differential이 더 상위 본체다.
- 다만 2026-03-27에 넣은 `PAIR`/`DEALER` public recv direct fast path로
  `PAIR` 수치가 바로 반응한 것을 보면, recv public surface는 실제 원인 축이
  맞다. 다만 그것만으로 전체 gap이 닫히지는 않는다.
- 최근 `echo`는 거의 동등하고 `oneway`에서 gap이 더 크게 보이는 점은,
  남은 본체가 recv보다 send-side publication/backpressure 쪽이라는 해석과
  맞는다.
- 2026-03-28 `dist_t` one-matching-pipe fast path가
  `PUBSUB tcp 64B`를 `-37.57%` 근처에서 `-24.23%`, rerun `-26.00%`까지
  회복시켰다. 즉 distributor loop/index/deactivate work는 실제 hot-path
  비용 축이었다.
- current HEAD는 그 위에 `xsub` empty-subscription accept-all fast path와
  requested-only `last_recv_source_rid` capture를 결합해
  isolated first/rerun `PUBSUB tcp/inproc 64B`
  `-9.40% / -20.35%`, `-10.43% / -21.59%`,
  broader single `PUBSUB tcp/inproc 64B -11.57% / -20.78%`,
  multi `pubsub tcp 64B +9.25%`, rerun `+8.25%`까지 회복했다.
- 즉 current `PUBSUB` 잔여 gap은 generic single-subscriber dist helper를 더
  얹는 문제가 아니라, retained `xsub` receiver-drain specialization 이후에도
  남아 있는 `inproc` differential과 다른 pattern의 routed path를 분리해 보는
  상태다.
- 다만 2026-03-28 재확인에서
  `comp_zlink_pubsub`는 실제로
  [`core/perf/single/src/perf_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_pubsub.cpp)
  를 빌드하고,
  paired `comp_std_zmq_pubsub`는
  [`core/bench/with_zmq/single/zmq/bench_zmq_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zmq/bench_zmq_pubsub.cpp)
  를 빌드한다.
- 즉 current `PUBSUB` single 비교는
  zlink 쪽 `topic="bench"` + delivery-ready monitor gate +
  `zlink_msg_recv()` 2회 경로와
  libzmq 쪽 payload-only single-part raw path를 직접 비교하고 있다.
- 따라서 guide에 적힌 aligned no-topic interpretation과
  그 위에 쌓인 current `PUBSUB` single semantic map은
  surface realignment 전까지 provisional로 취급해야 한다.
- 이후 same-day realignment에서 `comp_zlink_pubsub`를 실제
  [`core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp)
  로 되돌리고 receiver를 `zlink_msg_recv()` single-part path로 맞췄다.
- surface realignment 직후 historical single `PUBSUB tcp/inproc 64B`는
  `-15.29% / -24.92%`,
  `XPUB_NODROP=0` probe는 `-0.00% / -0.64%`,
  `HWM=16` probe는 `-16.17% / +47.89%`였다.
- historical multi `pubsub tcp 64B`는 default `-29.83%`,
  `BENCH_MULTI_PUBSUB_HWM=16 -22.22%`였다.
- 다만 2026-03-28 current retained-code direct recheck에서는
  single `PUBSUB tcp/inproc 64B`가
  seq1 `-21.73% / -19.43%`,
  rerun `-22.44% / -31.08%`,
  `XPUB_NODROP=0` probe는 `-0.06% / +0.04%`,
  latest multi `pubsub tcp 64B`는 `-22.75%`였다.
- 위 값은 current `xsub` retained delta 직전 baseline으로 유지한다.
- 그 뒤 current HEAD는 `xsub` empty-subscription accept-all fast path와
  requested-only `last_recv_source_rid` capture를 결합해
  isolated first/rerun `PUBSUB tcp/inproc 64B`
  `-9.40% / -20.35%`, `-10.43% / -21.59%`,
  broader single `PUBSUB tcp/inproc 64B -11.57% / -20.78%`,
  multi `pubsub tcp 64B +9.25%`, rerun `+8.25%`까지 회복했다.
- 즉 surface mismatch 자체가 same-day `PUBSUB tcp` gap의 큰 일부였다는
  결론은 유지되고, current source-of-truth는 위 recheck를 baseline으로 삼아
  그 위에 retained `xsub` receiver-drain specialization이 올라간 상태다.
- realigned 기준에서도 `PUBSUB inproc`과 `ROUTER_ROUTER` routed path의
  잔여 gap은 남아 있지만, multi regression은 current HEAD에서 해소됐다.

즉 지금 결과는 더 정확히 말하면:

- `zlink`의 현재 public API path
- 대
- `libzmq`의 더 얇은 raw message path

를 비교한 결과에 가깝다.

## 1.1 `echo` 대비 `oneway` 해석

최근 bench/perf를 함께 읽으면 `echo`에서는 zlink가 거의 동등하거나 차이가
작고, `oneway`에서는 throughput gap이 더 크게 드러난다.

이 패턴은 다음 해석과 맞는다.

- `echo`는 round-trip 특성상 sender가 자연스럽게 pace된다.
- 그래서 queue가 깊게 차거나 HWM/EAGAIN 복귀 경로가 오래 노출되지 않는다.
- 반면 `oneway`는 sender가 계속 밀어 넣기 때문에
  - `socket_base_t::send()` success path
  - HWM 도달 이후 backpressure 복귀
  - `pipe` publication / flush / activation
  - `PUBSUB`이면 distributor fanout
  의 비용이 더 직접적으로 드러난다.

즉 현재 남은 gap은 recv가 완전히 무관하다는 뜻은 아니지만,
우선순위는 recv보다 send-side ordering/publication/backpressure 쪽에 더 가깝다.

같은 맥락에서 `XPUB/XSUB`의 delivery-ready monitor bookkeeping을
steady-state hot path에서 건너뛰도록 줄여봤지만, `PUBSUB 64B` single bench는
유의미하게 움직이지 않았다. 따라서 현재 `PUBSUB` gap의 상위 축은
monitor-ready 유지 비용보다 publication/ordering 쪽으로 본다.

여기에 2026-03-28 `dist_t` one-matching-pipe fast path 결과를 합치면,
현재 `PUBSUB` 해석은 더 좁혀진다.

- monitor-ready bookkeeping은 secondary다.
- 반면 single-subscriber steady-state의 distributor loop/index/deactivate
  work는 실제 비용 축이다.
- 그런데 `inproc`와 multi가 여전히 크게 남으므로, publication/backpressure와
  pattern-specific 잔여 비용이 함께 남아 있다고 본다.

### 1.2 backpressure / wakeup 경로에 대한 현재 판정

`echo`와 `oneway` 차이를 더 좁히기 위해 `HWM 도달 -> activate_write ->
sender 복귀` 경로를 zlink/libzmq 코드 기준으로 다시 대조했다.

현재 확정 가능한 점은 다음과 같다.

- `pipe_t::compute_lwm()`는 zlink와 libzmq가 동일하다.
  - 둘 다 `LWM = (HWM + 1) / 2`
- `pipe_t::read()`에서 `_msgs_read % _lwm == 0`일 때
  `send_activate_write(_peer, _msgs_read)`를 보내는 구조도 동일하다.
- zlink의 [`object_t::send_activate_write()`](/home/hep7/project/kairos/zlink/core/src/core/object.cpp)
  는 같은 thread일 때 `destination_->process_command(cmd)`를 직접 호출한다.
  libzmq는 같은 경우에도 mailbox command를 보낸다.
  즉 "activate_write publication 자체가 zlink에서 더 늦다"는 가설은 현재
  코드만 보면 오히려 약하다.
- `pipe_t::process_activate_write()`도 두 구현 모두
  - `_peers_msgs_read` 갱신
  - `_out_active = true`
  - `write_activated()` 통지
  흐름은 동일하다.

즉 현재 더 유력한 차이는 wakeup publication보다
"sender가 wakeup을 소비하고 다시 send를 시도하는 비용" 쪽이다.

특히 중요한 차이는 여기다.

- libzmq [`socket_base_t::send()`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)는
  함수 시작에서 `scoped_optional_lock_t`를 한 번 잡고,
  blocking retry loop 안에서는 `process_commands(timeout, false)` 뒤
  바로 `xsend()`를 다시 시도한다.
- zlink [`socket_base_t::send()`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)는
  blocking retry loop에서 매번 `public_api_sync`를 다시 잡고 푼다.
  현재 코드는 `send_scope.relock_sync() -> xsend() -> unlock_sync()`
  경로를 반복한다.
- 2026-03-27 현재 HEAD는 send-ready handler가 없는 blocking send에 한해
  retry 동안 `public_api_sync`를 유지하도록 조정됐다.
  이 변경 뒤 `PAIR tcp 64B` public throughput이
  `2.774M -> 3.238M msg/s`로 올라갔다.

따라서 현재 backpressure 해석은 이렇게 고정한다.

- 1차 후보는 "wakeup이 너무 늦게 올라온다"가 아니다.
- 1차 후보는 "wakeup 이후 sender 복귀 경로가 libzmq보다 비싸다"이다.
- 특히 `public_api_sync` 재획득과 send-side lifecycle/admission 구조가
  backpressure 시나리오에서 더 직접적인 cost axis다.

## 2. 데이터 해석

### 2.1 `3/23 pre-POSD` 대비 `3/27 current perf`

`core/perf` 기준으로는 POSD 이후 직접 추가된 병목 대부분이 이미 정리된 쪽이다.

예시:

- `single callback PAIR tcp 64B`
  - `2026-03-23`: `2919.29 Kmsg/s`
  - `2026-03-27`: `3090.08 Kmsg/s`
- `multi recv DEALER_DEALER tcp 64B`
  - `2026-03-23`: `1496.697 Kmsg/s`
  - `2026-03-27`: `1565.549 Kmsg/s`
- `multi recv ROUTER_ROUTER tcp 64B`
  - `2026-03-23`: `897.329 Kops/s`
  - `2026-03-27`: `902.288 Kops/s`

이건 multipart heap materialization 문제와 POSD 직접 병목은 대부분 닫혔다는
해석과 맞는다.

### 2.2 `3/05` baseline은 현재와 surface가 달랐다

오래된 `with_zmq single` baseline:

- [`perf_linux_20260305_204428.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260305_204428.txt)

예시:

- `PAIR tcp 64B`
  - `libzmq 2610.75`
  - `zlink 3632.43`
  - `zlink +39.13%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 2566.54`
  - `zlink 3634.35`
  - `zlink +41.61%`

이 시점의 bench source를 보면 `zlink`와 `libzmq` 모두
`send_exact()` + `zlink_msg_recv()` 중심의 얇은 raw message path를 탔다.

즉 `3/05` baseline은 현재 benchmark와 측정 surface 자체가 다르다.

이 차이는 실제 source diff로도 확인된다.

- `3/05` 근처 [`bench_zlink_pair.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pair.cpp)
  는 `send_exact()` + `zlink_msg_recv()`를 사용했다.
- 현재 같은 파일은 `zlink_send()` + `zlink_recv()`를 사용한다.
- 반면 현재 [`bench_zmq_pair.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zmq/bench_zmq_pair.cpp)
  는 여전히 `send_exact()` + `zlink_msg_recv()`를 사용한다.

즉 `zlink` 쪽만 더 높은 public surface로 옮겨 갔고, `libzmq` 쪽은 raw path를
유지하고 있다.

### 2.3 현재 quick 수치

현재 워크스페이스 기준 quick 결과:

- `PAIR`: `libzmq 4,354,295 msg/s` vs `zlink 2,615,843 msg/s`, `-39.93%`
- `DEALER_DEALER`: `libzmq 3,725,870 msg/s` vs `zlink 2,823,829 msg/s`, `-24.21%`

이 숫자는 gap이 크다는 사실은 보여주지만, 그 차이를 전부 `core` 엔진 회귀로
읽으면 안 된다. 현재 측정 surface가 다르기 때문이다.

### 2.4 current recv fast path 적용 후 quick 수치

2026-03-27에 `PAIR`/`DEALER` public `zlink_recv()` single-part fast path를
조정한 뒤 다시 `1s quick run`으로 찍은 결과:

- `PAIR tcp 64B`
  - `libzmq 3,452.55 Kmsg/s`
  - `zlink 3,012.40 Kmsg/s`
  - `-12.75%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3,537.54 Kmsg/s`
  - `zlink 2,760.03 Kmsg/s`
  - `-21.98%`

해석:

- `PAIR`는 public recv single-part 경량화에 즉시 반응했다.
- `DEALER_DEALER`는 같은 quick run 기준으로 개선이 뚜렷하지 않았다.
- 따라서 현재 남은 격차는 `public recv` 한 축만이 아니라,
  `pipe` steady-state 잠금과 `send` public lifecycle/backpressure 비용이
  함께 남아 있다고 보는 것이 맞다.

### 2.5 current recv-side `pipe` lock 축소 후 3s quick 수치

같은 날 `pipe_t::check_read()` / `read()` 의 steady-state `fast_mutex_t`를
걷고 다시 `3s quick run`으로 찍은 결과:

- `PAIR tcp 64B`
  - `libzmq 3,757.78 Kmsg/s`
  - `zlink 3,025.02 Kmsg/s`
  - `-19.50%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3,751.27 Kmsg/s`
  - `zlink 2,661.38 Kmsg/s`
  - `-29.05%`

해석:

- recv-side public/API 경량화와 recv-side `pipe` lock 축소는 `PAIR`에 직접
  반응한다.
- 하지만 `DEALER_DEALER`는 여전히 큰 gap이 남는다.
- 따라서 현재 남은 상위 원인은
  - send-side `pipe`/lifecycle 고정비
  - public send surface
  - public recv의 남은 routed/aggregate 경로
  로 보는 것이 맞다.

### 2.6 current send-side `write+flush` 병합 후 3s quick 수치

같은 날 send-side final part 경로에서 `pipe::write()` 와 `pipe::flush()`의
별도 lock을 하나의 `write_and_flush()`로 묶은 뒤 다시 `3s quick run`으로
찍은 결과:

- `PAIR tcp 64B`
  - `libzmq 3,646.58 Kmsg/s`
  - `zlink 3,010.85 Kmsg/s`
  - `-17.43%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3,783.27 Kmsg/s`
  - `zlink 2,957.39 Kmsg/s`
  - `-21.83%`

해석:

- `DEALER_DEALER`는 send-side lock 축소에 뚜렷하게 반응했다.
- `PAIR`도 여전히 `3.0M msg/s` 수준을 유지하며, 남은 gap은
  `socket_base_t::send()` lifecycle/public lock과 current benchmark surface
  mismatch 쪽으로 더 좁혀진다.

### 2.7 current TLS-view release 최적화 후 3s quick 수치

같은 날 `zlink_multipart_close()`가 TLS recv view를 바로 release하도록 정리한 뒤
다시 `3s quick run`으로 찍은 결과:

- `PAIR tcp 64B`
  - `libzmq 3,908.78 Kmsg/s`
  - `zlink 3,046.03 Kmsg/s`
  - `-22.07%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3,765.72 Kmsg/s`
  - `zlink 3,180.02 Kmsg/s`
  - `-15.55%`

해석:

- `zlink_recv()` single-part direct path와 TLS-view release 최적화는
  `DEALER_DEALER` 쪽에서도 의미 있는 회복을 만든다.
- `PAIR`는 여전히 `3.0M msg/s` 수준이지만, `libzmq`와의 남은 격차는
  recv 한 축만으로 설명하기 어려운 수준이다.
- 따라서 현재 남은 상위 원인은
  - send-side public lifecycle/backpressure 고정비
  - current benchmark surface mismatch
  - send-side 남은 `pipe` serialization cost
  로 보는 것이 맞다.

### 2.8 current `PAIR` public send sync 축소 후 5s quick 수치

`PAIR`는 `xsend()`가 `pipe::write_and_flush()`만 타고
`lb_t`/`dist_t` 같은 추가 per-socket send 상태를 건드리지 않는다.
그래서 현재는 `PAIR`에 한정해 `socket_base_t::send()`의
public sync를 우회하는 실험을 유지하고 다시 `5s quick run`으로 확인했다.

- `PAIR tcp 64B`
  - `libzmq 3,643.16 Kmsg/s`
  - `zlink 3,058.49 Kmsg/s`
  - `-16.05%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 4,506.82 Kmsg/s`
  - `zlink 3,133.27 Kmsg/s`
  - `-30.48%`

해석:

- `PAIR`에서는 public send sync가 실제 hot-path 비용 축이다.
- 다만 개선 폭은 중간 수준이고, 이 한 축만으로 전체 gap이 설명되진 않는다.
- `DEALER`는 `lb_t`의 mutable send state 때문에 같은 우회를 바로 적용하면
  안 된다.

### 2.9 최근 배제 실험과 그 의미

현재 환경에서는 `perf`가 없어 원인 후보를 직접 A/B로 배제하는 방식이
중요했다. 최근 quick run 기준으로 아래 후보는 모두 배제했다.

- `pipe.cpp` 전체 `_out_sync` no-op
  - `PAIR`: 약 `3.046M -> 2.158M`
  - `DEALER_DEALER`: 약 `3.180M -> 2.468M`
- `pipe::write_and_flush()` / `flush()`의 peer activation을 lock 밖으로 이동
  - `PAIR`: 약 `3.122M -> 2.970M`
- `mailbox.cpp` recv/check_read 측 read lock 제거
  - `PAIR`: 약 `3.058M -> 2.860M`
  - `DEALER_DEALER`: 약 `3.133M -> 2.809M`

이 절은 현재 문서에서 가장 중요한 발견 중 하나다.

의미는 분명하다.

- 현재 남은 gap은 `lock이 있으니 없애면 빨라진다` 식의 단순 문제가 아니다.
- 적어도 지금 구현에서는 `pipe`, `activation`, `mailbox`가 순수 고정비만
  제공하는 구조가 아니며, ordering, activation, progress 보장과 얽혀 있다.
- 따라서 다음 개선 방향은
  - 같은 의미를 더 저렴하게 제공하거나
  - hot path가 그 의미를 덜 자주 필요하게 만들거나
  - 불필요한 work 자체를 줄이는 쪽
  이어야 한다.
- 즉 `lock 제거` 자체는 목표가 아니라, 현재 구조에서 어떤 ordering이 정말
  필요한지 먼저 분리하는 것이 목표다.

### 2.10 특정 시점의 raw/public 분리 결과

`PAIR`/`DEALER_DEALER`에서 `zlink_send/zlink_recv` 기준과
`zlink_msg_send/zlink_msg_recv` 기준을 같은 zlink bench surface에서 다시 찍었다.

직접 측정:

- `PAIR tcp 64B`
  - public: `3,207,629.67 msg/s`
  - raw: `3,104,833.33 msg/s`
  - raw가 약 `-3.20%`
- `PAIR inproc 64B`
  - public: `3,459,143.00 msg/s`
  - raw: `3,511,405.00 msg/s`
  - raw가 약 `+1.51%`
- `DEALER_DEALER tcp 64B`
  - public: `3,208,134.67 msg/s`
  - raw: `3,159,694.67 msg/s`
  - raw가 약 `-1.51%`
- `DEALER_DEALER inproc 64B`
  - public: `3,161,425.00 msg/s`
  - raw: `3,345,499.33 msg/s`
  - raw가 약 `+5.82%`

의미:

- 이 시점의 HEAD 기준으로는 `PAIR`/`DEALER_DEALER`의 zlink 내부 public
  wrapper penalty가 low single-digit 수준이었다.
- 따라서 현재 남은 `libzmq` 대비 큰 상대 gap의 상위 축은
  `zlink_send()/pipe/lifecycle` 쪽 steady-state work로 봐야 한다.
- `public API를 더 얇게 만드는 것`은 여전히 의미가 있지만, 이제는 상위 본체가
  아니라 보조 축이다.

즉 현재 기준으로 gap을 나누면 대략 이렇게 읽는 것이 맞다.

- `core engine gap`
  - `libzmq raw` 대 `zlink raw`
- `surface penalty`
  - `zlink public` 대 `zlink raw`

즉 이 측정은 "public penalty가 항상 작다"는 영구 결론이 아니라,
그 시점의 HEAD에서는 send-side core differential이 더 상위였다는 뜻으로 읽어야
한다.

### 2.10b 1~5 적용 후 public/raw 재측정

2026-03-27에 `1~5` 개선 축을 실제 코드에 반영한 뒤 다시 측정했다.

public 결과:

- [`perf_linux_20260327_210022_items1to5_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_210022_items1to5_public.txt)
- `PAIR tcp 64B`
  - `libzmq 3644.61 Kmsg/s`
  - `zlink 2773.65 Kmsg/s`
  - `-23.90%`
- `PAIR inproc 64B`
  - `libzmq 4205.86 Kmsg/s`
  - `zlink 2825.57 Kmsg/s`
  - `-32.82%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3672.56 Kmsg/s`
  - `zlink 3138.98 Kmsg/s`
  - `-14.53%`
- `DEALER_DEALER inproc 64B`
  - `libzmq 4168.25 Kmsg/s`
  - `zlink 2807.03 Kmsg/s`
  - `-32.66%`
- `DEALER_ROUTER tcp 64B`
  - `libzmq 4551.35 Kmsg/s`
  - `zlink 2782.01 Kmsg/s`
  - `-38.88%`
- `DEALER_ROUTER inproc 64B`
  - `libzmq 4521.56 Kmsg/s`
  - `zlink 2753.86 Kmsg/s`
  - `-39.09%`
- `ROUTER_ROUTER tcp 64B`
  - `libzmq 2986.98 Kmsg/s`
  - `zlink 1241.39 Kmsg/s`
  - `-58.44%`
- `ROUTER_ROUTER inproc 64B`
  - `libzmq 3473.86 Kmsg/s`
  - `zlink 2459.15 Kmsg/s`
  - `-29.21%`
- `PUBSUB tcp 64B`
  - `libzmq 3908.25 Kmsg/s`
  - `zlink 2254.26 Kmsg/s`
  - `-42.32%`
- `PUBSUB inproc 64B`
  - `libzmq 4032.53 Kmsg/s`
  - `zlink 2033.78 Kmsg/s`
  - `-49.57%`

raw 결과:

- [`perf_linux_20260327_210223_items1to5_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_210223_items1to5_raw.txt)
- `PAIR tcp 64B`
  - public `2773.65 Kmsg/s`
  - raw `3354.39 Kmsg/s`
  - raw가 약 `+20.94%`
- `PAIR inproc 64B`
  - public `2825.57 Kmsg/s`
  - raw `3299.18 Kmsg/s`
  - raw가 약 `+16.76%`
- `DEALER_DEALER tcp 64B`
  - public `3138.98 Kmsg/s`
  - raw `2810.42 Kmsg/s`
  - raw가 약 `-10.47%`
- `DEALER_DEALER inproc 64B`
  - public `2807.03 Kmsg/s`
  - raw `3366.84 Kmsg/s`
  - raw가 약 `+19.94%`

의미:

- `1~5` 적용은 `DEALER_DEALER tcp`에는 실제 회복을 만들었다.
  - 직전 public 기준 `2973.05 -> 3138.98 Kmsg/s`
- 반면 `PAIR`, `DEALER_ROUTER`, `PUBSUB`, `ROUTER_ROUTER`는 이번 라운드에서
  뚜렷한 회복을 만들지 못했다.
- 가장 중요한 건 raw/public 분리가 다시 벌어졌다는 점이다.
  - 특히 `PAIR tcp/inproc`는 이번 상태에서 public penalty가 다시 두드러진다.
- 따라서 `2.10`의 low single-digit 결과를 현재 HEAD의 고정 결론으로
  들고 가면 안 된다.
- 현재 해석은 이렇게 고정한다.
  - send-side core differential이 상위 본체인 것은 맞다.
  - 하지만 send-side lifecycle 변경은 public penalty도 다시 키울 수 있으므로
    raw/public 분리를 매 라운드 guardrail로 다시 찍어야 한다.

### 2.11 zlink 절대 throughput 추이

quick run의 libzmq 절대값은 실행 시점마다 흔들리므로,
최근 개선 작업은 zlink 절대 throughput 추이도 같이 봐야 한다.

| 단계 | `PAIR tcp 64B` | `DEALER_DEALER tcp 64B` | 의미 |
|------|----------------|-------------------------|------|
| 2.3 baseline quick | `2.616M` | `2.824M` | 현재 문서 작성 시작점 |
| 2.4 recv public fast path | `3.012M` | `2.760M` | `PAIR` 즉시 회복 |
| 2.5 recv-side `pipe` lock 축소 | `3.025M` | `2.661M` | recv 축 영향 확인, 노이즈 큼 |
| 2.6 send-side `write_and_flush` | `3.011M` | `2.957M` | `DEALER`에 의미 있는 회복 |
| 2.7 TLS-view release | `3.046M` | `3.180M` | recv export steady-state 비용 축소 |
| 2.8 `PAIR` public send sync 축소 | `3.058M` | `3.133M` | `PAIR` send public sync 영향 확인 |
| 2.10b `1~5` 적용 후 public 재측정 | `2.774M` | `3.139M` | `DEALER` 유지, `PAIR`/pattern-specific는 미해결 |
| 2.10c retry-side sync 유지 | `3.238M` | `3.088M` | `PAIR` 회복, send retry 축 재확인 |

이 표가 뜻하는 바는 명확하다.

- `PAIR`가 약 `3.0M` 부근에서 오래 정체한 것은 send-side 병목이 남아 있다는
  강한 신호다.
- 반대로 `DEALER_DEALER`는 recv/TLS와 send `write+flush` 개선에 실제로 더
  많이 반응했다.
- 따라서 이후 우선순위는 `public recv wrapper`를 계속 미세조정하는 것보다
  send lifecycle/backpressure 경로를 더 직접적으로 보는 쪽이 맞다.

### 2.10c retry-side sync 유지 + lifecycle shadow atomic 제거 후 재측정

2026-03-27 현재 HEAD에서 아래 두 변경을 추가했다.

- [`socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  / [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  에서 `public_api_sync` shadow atomic 제거
- [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  에서 send-ready handler가 없는 blocking send는 retry 동안
  `public_api_sync`를 유지

public 순차 재측정:

- [`perf_linux_20260327_212608_retry_hold_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_212608_retry_hold_public_seq.txt)
- `PAIR tcp 64B`
  - `libzmq 3628.97 Kmsg/s`
  - `zlink 3237.93 Kmsg/s`
  - `-10.78%`
- `PAIR inproc 64B`
  - `libzmq 4175.11 Kmsg/s`
  - `zlink 2766.34 Kmsg/s`
  - `-33.74%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3833.30 Kmsg/s`
  - `zlink 3088.44 Kmsg/s`
  - `-19.43%`
- `DEALER_DEALER inproc 64B`
  - `libzmq 4108.89 Kmsg/s`
  - `zlink 3217.18 Kmsg/s`
  - `-21.70%`
- `DEALER_ROUTER tcp 64B`
  - `libzmq 4039.53 Kmsg/s`
  - `zlink 3026.06 Kmsg/s`
  - `-25.09%`
- `DEALER_ROUTER inproc 64B`
  - `libzmq 4506.29 Kmsg/s`
  - `zlink 2956.17 Kmsg/s`
  - `-34.40%`
- `PUBSUB tcp 64B`
  - `libzmq 3795.19 Kmsg/s`
  - `zlink 2369.20 Kmsg/s`
  - `-37.57%`
- `PUBSUB inproc 64B`
  - `libzmq 3751.86 Kmsg/s`
  - `zlink 2200.01 Kmsg/s`
  - `-41.36%`
- `ROUTER_ROUTER tcp 64B`
  - `libzmq 2861.16 Kmsg/s`
  - `zlink 1240.04 Kmsg/s`
  - `-56.66%`
- `ROUTER_ROUTER inproc 64B`
  - `libzmq 3279.51 Kmsg/s`
  - `zlink 2449.13 Kmsg/s`
  - `-25.32%`

raw 재측정:

- [`perf_linux_20260327_213142_retry_hold_raw_seq2.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_213142_retry_hold_raw_seq2.txt)
- `PAIR tcp 64B`
  - `libzmq 3660.66 Kmsg/s`
  - `zlink 3153.50 Kmsg/s`
  - `-13.85%`
- `PAIR inproc 64B`
  - `libzmq 4000.62 Kmsg/s`
  - `zlink 3343.75 Kmsg/s`
  - `-16.42%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 4460.78 Kmsg/s`
  - `zlink 3177.72 Kmsg/s`
  - `-28.76%`
- `DEALER_DEALER inproc 64B`
  - `libzmq 4176.41 Kmsg/s`
  - `zlink 2821.68 Kmsg/s`
  - `-32.44%`

의미:

- `PAIR tcp`는 이번 라운드의 가장 확실한 회복이다.
  public 기준 `2.774M -> 3.238M`로 올랐다.
- `PAIR tcp` raw/public은 현재 `3.154M` vs `3.238M`로 거의 붙는다.
  이 케이스에서는 public wrapper가 본체가 아니라
  send retry/lifecycle 쪽이 본체였다고 보는 게 맞다.
- `DEALER_DEALER`와 `DEALER_ROUTER`는 일부 개선됐지만,
  `PUBSUB`와 `ROUTER_ROUTER`는 여전히 pattern-specific 잔여 비용이 크다.
- 따라서 다음 우선순위는 "wrapper를 더 걷어내자"보다
  `pipe` publication/ordering과 routed/publish path를 더 깊게 보는 쪽이다.

## 3. 현재 `with_zmq single`이 실제로 비교하는 surface

### 3.1 공통 패턴

현재 `PAIR`/`DEALER_DEALER` 기준으로 실제 호출은 다음과 같다.

| Pattern | zlink | libzmq |
|------|------|------|
| `PAIR` | `zlink_send(parts,1)` + `zlink_recv(&parts,&count)` | `send_exact(buffer)` + `zlink_msg_recv(msg)` |
| `DEALER_DEALER` | `zlink_send(parts,1)` + `zlink_recv(&parts,&count)` | `send_exact(buffer)` + `zlink_msg_recv(msg)` |
| `DEALER_ROUTER` | `zlink_send(parts,1)` + `zlink_recv(source_rid,&parts,&count)` | `send_exact(buffer)` + `zlink_msg_recv()` 2회 |
| `PUBSUB` | `zlink_publish(NULL, &part,1)` + `zlink_msg_recv(msg)` | `send_exact(buffer)` + `zlink_msg_recv(msg)` |
| `ROUTER_ROUTER` | `zlink_send(parts,2)` + `zlink_recv(source_rid,&parts,&count)` | `zlink_send(..., "RID", SNDMORE)` + `zlink_send(..., payload)` + `zlink_msg_recv()` 2회 |

의미는 명확하다.

- 현재 `zlink` 측은 public aggregate API를 직접 측정한다.
- 현재 `libzmq` 측은 raw message recv path를 직접 측정한다.
- 이건 `3/05 baseline`과 달리 현재 benchmark 자체가 더 이상 대칭이 아니라는
  뜻이다.
- 2026-03-28 single `PUBSUB` zlink bench를
  `zlink_publish(NULL, &part, 1)` + `zlink_recv()` no-topic payload-only
  경로로 정렬했다.
- aligned first run/rerun은 `tcp -24.51% / -23.17%`,
  `inproc -41.79% / -44.68%`였다.
- 따라서 이전 empty-topic wire-shape mismatch는 제거됐지만,
  남은 `PUBSUB` gap은 여전히 publication/lifecycle/distribution differential을
  강하게 반영한다.
- 다만 `2.10`의 raw/public 분리 결과를 보면, 현재 HEAD에서 이 surface mismatch는
  해석에는 중요하지만 남은 gap의 본체는 아니다.
- 즉 현재 가장 큰 격차는 `public wrapper`보다 send-side core engine differential을
  더 강하게 반영한다.

### 3.2 왜 이게 중요한가

`PAIR`는 원래 경로가 가장 얇다.

그래서 현재처럼:

- zlink는 `zlink_recv()` aggregate export
- libzmq는 `zmq_msg_recv()` raw one-frame recv

를 비교하면, `PAIR`에서 gap이 가장 크게 드러나는 것이 자연스럽다.

즉 지금 `PAIR`의 큰 gap은 오히려
"core transport가 가장 많이 무너졌다"기보다
"public aggregate recv 비용이 가장 잘 드러난다"는 해석이 더 맞다.

## 4. 현재 코드 기준의 실제 hot path 차이

여기서 hot path는 steady-state 동안 메시지 1건마다 반복해서 타는 코드다.

### 4.1 `pipe_t` 잠금은 현재 상위 core-side 후보다

현재 [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp) 의
steady-state 경로는 아래 메서드마다 `scoped_fast_lock_t lock(_out_sync)`를 잡는다.

- `check_read()`
- `read()`
- `check_write()`
- `write()`
- `flush()`

반면 libzmq [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp) 의 같은
경로에는 해당 잠금이 없다.

그리고 이 차이는 단순히 "현재 코드가 libzmq와 다르다" 수준이 아니라,
`3/05` baseline 근처 커밋 `7bea9e3f`의 zlink [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
에도 없던 항목이다. 즉 이 잠금은 남아 있는 상대 gap을 설명하는 후보일 뿐 아니라,
`3/05 -> current` 성능저하를 설명하는 실제 delta 후보이기도 하다.

의미:

- `PAIR` single-part 메시지 1건당 `write + flush + read`로
  최소 3번의 lock/unlock이 steady-state에 들어간다
- 이 비용은 current public API mismatch와 별개로 존재하는
  core-side 후보다
- single-thread benchmark라고 해도 uncontended mutex 비용 자체는 남는다.
  즉 contention이 없다는 사실이 이 후보를 무효화하지는 않는다.

다만 용어는 정확히 써야 한다.

- 현재 `fast_mutex_t`는 순수 CAS spinlock이 아니라
  [`fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
  기준 `pthread_mutex_t` + owner tracking이다
- 즉 "spinlock 3회"라고 단정하는 것보다
  "uncontended fast mutex lock/unlock 3회"라고 보는 편이 맞다

그래도 영향이 작다는 뜻은 아니다. 오히려 현재 기준에선
가장 강한 core-side 단일 후보 중 하나다.

### 4.2 zlink `recv`는 public aggregate export를 탄다

현재 `zlink_recv()`는 [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
에서 두 갈래로 나뉜다.

- `PAIR`/`DEALER` direct single-part fast path
  1. `recv_tls_view::begin_with_first_slot()`
  2. `socket_base_t::recv()`
  3. single-part면 `commit_reserved_single()`
  4. multipart면 follow-up frame 수집 후 `push()/commit()`
- 그 외 generic/public aggregate path
  1. `recv_tls_view::begin()`
  2. `recv_msg_socket()` 또는 `recv_msg_routed_socket()`
  3. single-part면 `recv_tls_view::export_single()`
  4. multipart면 follow-up frame 수집 후 `push()/commit()`

관련 코드:

- [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
- [`recv_tls_view.hpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_tls_view.hpp)

특징:

- direct single-part fast path도 최종 반환은 TLS slot을 통해 이뤄진다.
- generic path의 `begin()`은 이전 결과를 정리하기 위해 `reset()`을 수행한다.
- generic path의 `export_single()`도 최종적으로 TLS slot으로 move한다.
- 현재 HEAD에서는 `zlink_multipart_close()`가 TLS view 결과를 즉시 release하여,
  다음 recv begin이 이미 닫힌 slot을 다시 close/init 하는 낭비를 줄인다.

반면 `libzmq` bench의 `PAIR`/`DEALER_DEALER`는
[`bench_zmq_pair.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zmq/bench_zmq_pair.cpp),
[`bench_zmq_dealer_dealer.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zmq/bench_zmq_dealer_dealer.cpp)
에서 `zlink_msg_recv()`를 바로 사용한다.

즉 현재 격차의 가장 강한 recv-side 원인은:

- `zlink_recv()` aggregate/TLS export
- 대
- `zmq_msg_recv()` raw recv

차이다.

다만 현재 HEAD에는 `PAIR`/`DEALER` public single-part direct recv fast path가
이미 일부 복구돼 있다. 그래서 지금 남아 있는 recv-side 원인은 더 정확히는:

- routed/strip/multipart 경로의 aggregate export
- direct fast path가 닿지 않는 public recv overhead
- `recv_msg_socket()` / mode guard 계층

으로 보는 편이 맞다.

실험 메모:

- `PAIR` send에서 `socket_base_t::send()`의 public sync를 우회하는
  pair-only fast path를 현재 코드에 제한적으로 유지하고 있다.
- same-handle concurrent send regression은 계속 통과한다.
- `5s quick run` 기준 `PAIR tcp 64B`는
  `zlink 3.058M`, `libzmq 3.643M`로 약 `-16.1%` 차이까지 줄었다.
- 개선 폭이 압도적이진 않지만, `PAIR`에서는 public send sync가 실제 비용 축이라는
  실측 근거로는 의미가 있다.
- 다만 `DEALER` 이상으로 일반화할 만큼 단순한 구조는 아니므로,
  이것을 곧바로 전체 해법으로 과대해석하면 안 된다.

### 4.3 zlink `send`도 public path를 탄다

현재 `zlink` bench는 `PAIR`/`DEALER_DEALER`에서도 `zlink_send(parts,1)`를 쓴다.

관련 코드:

- [`bench_zlink_pair.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pair.cpp)
- [`bench_zlink_dealer_dealer.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_dealer_dealer.cpp)

이 경로는 [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
→ [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
로 들어간다.

여기서 중요한 보정이 하나 있다.

- `PAIR`/`DEALER` single-part send는 현재 `zlink_send()`에서
  완전히 일반 multipart helper를 타는 것은 아니다
- [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
  기준 `send_socket_singlepart_fast()`가 실제로 사용된다

즉 send wrapper 전체를 "매우 두꺼운 일반 경로"라고 과장하면 안 된다.
하지만 그 fast path 안쪽에서 결국:

- validation
- public lifecycle coordinator
- process_commands
- backpressure 복귀 loop

를 포함한다.

반면 `libzmq` bench는 `send_exact()`로 raw send를 사용한다.

즉 현재 `single` 상대 비교는 recv뿐 아니라 send 쪽도
`public zlink_send` 대 `raw zmq_send` 차이를 함께 포함하며,
send-side에서는 wrapper depth 자체보다 `socket_base_t::send()` 내부 비용이
더 중요하다.

### 4.4 recv mode guard는 현재도 zlink-only 비용이다

`recv_msg_socket()`은 [`recv_internal.cpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_internal.cpp)
에서 매 recv마다:

- `socket_msg_dispatch_active()`
- `sub_dispatch_active()`
- `xpub_dispatch_active()`
- `stream_dispatch_active()`

를 확인한다.

이건 correctness guard로는 맞지만, libzmq의 raw `msg_recv` 쪽에는 같은 층의
분리가 없다.

즉 이것도 현재 상대 gap에 기여하는 zlink-side 고정비다.

### 4.5 `PAIR/DEALER`의 `last_recv_source_rid`는 현재 P0가 아니다

이건 중요한 수정 사항이다.

`3/05 -> 3/23` 사이 diff에는 `PAIR`/`DEALER`의
`store_last_recv_source_rid()`가 실제로 들어왔었다.
하지만 현재 HEAD의 [`pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp),
[`dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)
기준으로는 그 갱신이 빠져 있다.

즉:

- 과거 시점 분석에는 의미가 있었지만
- 현재 HEAD 원인분석의 상위 후보로 계속 두면 안 된다

현재 문서에서는 이 항목을 historical note로만 취급한다.

### 4.6 callback-dispatch mode awareness는 현재도 남아 있다

`PAIR`/`DEALER`의 attach/read activation 쪽은 여전히 dispatch mode를 의식한다.

관련 코드:

- [`pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp)
- [`dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)

다만 이건 현재 `PAIR`/`DEALER` recv loop의 최상위 직접 원인이라기보다,
mode specialization이 덜 된 구조라는 보조 신호로 보는 것이 맞다.

## 5. 현재 원인 우선순위

현재 코드와 현재 benchmark surface를 함께 보면 우선순위는 이렇게 정리된다.

1. 현재 `with_zmq single` surface mismatch
   - zlink: `zlink_send` + `zlink_recv`
   - libzmq: `send_exact` + `zlink_msg_recv`
2. `zlink_send()`의 public lifecycle/backpressure 고정비
3. send-side `pipe_t`의 남아 있는 steady-state 잠금/serialization 비용
4. `zlink_recv()`의 남아 있는 public aggregate/export 고정비
5. recv-side `pipe_t`의 남아 있는 경로와 `recv_internal.cpp`의 dispatch/mode guard
6. `PUBSUB`/`ROUTER`의 패턴 전용 surface 차이
7. callback-dispatch aware socket 구조

반대로 현재 우선순위가 낮은 것은:

- `fq/lb` 재작성
- `PAIR/DEALER` algorithm 자체 변경
- `pipe` lock 제거 같은 thread-safe 위험 최적화
- 이미 현재 HEAD에서 빠진 `last_recv_source_rid`

여기서 중요한 점은 우선순위 1과 2를 구분해서 읽는 것이다.

- 우선순위 1은 "현재 비교가 완전히 대칭이 아니다"는 해석 문제다.
- 우선순위 2 이하부터가 "그 비대칭을 인정하더라도 zlink public path 안에서
  실제로 줄일 수 있는 steady-state 비용"이다.

즉 실제 개선 작업은 2번부터 시작하지만,
그 결과를 해석할 때는 항상 1번을 같이 붙여서 봐야 한다.

## 6. 해석상 중요한 보정

### 6.1 지금 gap을 전부 `core` 회귀로 읽으면 안 된다

현재 측정은 `core engine vs core engine` 비교가 아니라,
상당 부분:

- `zlink`의 더 높은 public surface 비용
- 대
- `libzmq`의 더 낮은 raw message 비용

을 비교하고 있다.

즉 현재 수치는 "public API까지 포함한 사용자 체감 비용"으로는 의미가 있지만,
"transport core가 libzmq보다 얼마나 느린가"의 순수한 답으로 쓰기엔 과한 면이 있다.

### 6.2 `3/05` baseline과 현재를 직접 1:1로 붙이면 안 된다

`3/05`에는 양쪽이 사실상 `msg_send/msg_recv` 기준으로 붙었다.
현재는 zlink만 `zlink_send/zlink_recv` public path를 쓰고 있다.

따라서 `3/05` 대비 부호가 뒤집힌 사실은 중요하지만,
그 안에는:

- thread-safe/callback 도입 시기의 구조 변화
- 현재 public API surface로 바뀐 benchmark 측정 방식

이 함께 들어 있다.

### 6.3 `perf` 없이 한 직접 A/B 실험의 의미

현재 환경에서는 `perf`를 사용할 수 없어 `perf stat`으로
store-buffer stall이나 cache miss를 직접 찍지 못했다.

대신 `_out_sync`를 `pipe.cpp` 전체에서 no-op으로 만드는 직접 A/B 실험을
짧게 해봤다. 결과는 오히려 악화였다.

- `PAIR tcp 64B`
  - 안정 상태: 약 `3.046M msg/s`
  - `_out_sync` no-op 실험: 약 `2.158M msg/s`
- `DEALER_DEALER tcp 64B`
  - 안정 상태: 약 `3.180M msg/s`
  - `_out_sync` no-op 실험: 약 `2.468M msg/s`

이 관찰이 뜻하는 바는 명확하다.

- `pipe` lock은 여전히 상위 원인 후보로 남지만,
- 현재 구현에서 이 lock은 단순한 순수 고정비가 아니라
  활성화/직렬화 순서와 얽혀 있다.
- 따라서 "lock만 없애면 libzmq와 비슷해진다"는 식의 단순 해석은 틀리다.

즉 `pipe`를 봐야 한다는 결론은 유지하되,
해결 방식은 전체 no-op 제거가 아니라 더 좁은 근거 기반 축소여야 한다.

이 절의 의미를 한 줄로 요약하면:

- `pipe`/`mailbox`의 현재 lock은 원인 후보이지만
- naive 제거 실험이 모두 실패했으므로
- 다음 단계는 `lock 감축`이 아니라 `ordering 보존 + 불필요한 work 제거`다.

### 2.12 send-side differential 재정리

현재 코드 기준으로 libzmq와 직접 다르게 보이는 send-side 항목은 아래 셋이다.

1. [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
   의 lifecycle coordinator
   - zlink는 `send()`마다 `enter_public_api()` / `leave_public_api()`를 탄다.
   - non-`PAIR`는 여기에 `socket_public_api_lock_scope_t`도 추가로 탄다.
   - libzmq [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
     는 non-thread-safe socket에서 이 계층이 없다.
2. [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
   의 `_out_sync`
   - `check_write()` / `write()` / `write_and_flush()` / `flush()`가
     steady-state send마다 lock을 잡는다.
   - libzmq [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)에는
     같은 steady-state lock이 없다.
   - 다만 최근 A/B에서 naive 제거는 모두 악화됐으므로
     "없애자"가 아니라 "같은 ordering을 더 싸게 제공할 수 있나"로 봐야 한다.
3. `oneway`에서 더 자주 노출되는 activation / progress 경로
   - `write_and_flush()` 후 `send_activate_read()`
   - HWM 해제 후 `process_activate_write()`
   - `socket_base_t::send()`의 blocking retry loop

반대로 `process_commands(0, true)` 자체는 현재 zlink와 libzmq가 거의 같은
구조다. 둘 다 `rdtsc` 기반 throttled command poll을 쓰므로, 이 부분은
현재 상대 gap의 1차 differential로 보기 어렵다.

## 7. 다음 작업 우선순위

현재 기준으로는 두 갈래를 분리해서 보는 게 맞다.

### 7.1 원인 분리용

1. `PAIR`/`DEALER_DEALER` raw/public 분리는 완료됐고,
   현재는 send-path 변경 뒤 raw/public 분리를 다시 찍는 guardrail로 유지한다.
2. 이후 비교는 `zlink raw - libzmq raw`보다
   `zlink public/current core path - libzmq raw`를 실제 사용자 체감 gap으로 본다.
3. routed/topic 패턴은 공통 hot path와 pattern-specific 비용을 따로 본다.

### 7.2 실제 개선용

현재 public API 성능을 끌어올리는 쪽은 아래 순서가 맞다.

1. `socket_base_t::send()`의 public lifecycle fast path를 설계한다.
   - 가장 유력한 구조 후보는 `enter_public_api`와 `public_api_sync`를
     steady-state에서 더 적은 atomic으로 합치는 방향이다.
   - 다만 이는 `begin_close_or_fail_busy`, callback handoff, close semantics를
     같이 다시 증명해야 하므로 작은 tweak가 아니라 설계 작업으로 본다.
2. blocking retry 경로에서 반복되는 `public_api_sync` 재획득 비용을 줄이는
   방향을 별도 후보로 본다.
   - 현재 retry loop는 `enter_public_api()`를 다시 하는 구조는 아니다.
   - admission은 `send()` 호출당 한 번만 일어나고,
     반복되는 것은 `socket_public_api_lock_scope_t`다.
   - 따라서 개선 포인트는 "re-admission 제거"가 아니라
     backpressure 복귀 시 `public_api_sync`와 `xsend()` 경로를
     어떻게 더 싸게 재시도하느냐에 가깝다.
   - 현재 HEAD는 send-ready handler가 없는 blocking send에 한해
     retry 동안 sync를 유지하도록 조정했다.
   - `PAIR tcp`가 실제로 크게 올라왔으므로 이 축은 여전히 상위다.
   - 다만 send-ready handler가 걸린 경로까지 같은 방식으로 넓히는 건
     close/callback handoff 검토가 더 필요하다.
3. send-side `pipe_t`는 전체 lock 제거가 아니라
   final-part, activation, wakeup, flush ordering을 유지하면서
   lock 안의 work를 줄이는 방향으로 본다.
   - `write_and_flush()`는 이미 반영됐고 유지한다.
   - `_out_sync` no-op, activation lock 밖 이동, mailbox read lock 제거는
     모두 배제된 실험으로 유지한다.
4. `fast_mutex_t` owner tracking 제거 같은 "pipe 전용 non-reentrant mutex" 제안은
   현재 그대로는 채택하지 않는다.
   - `pipe_t::terminate()`, `set_nodelay()`, `process_delimiter()`,
     `send_disconnect_msg()`는 `_out_sync`를 잡은 상태에서
     `rollback()` / `flush()`를 다시 호출한다.
   - 즉 현재 `pipe_t`는 실제로 reentrant lock 성질을 의존하므로,
     owner/depth 제거는 low-risk가 아니라 correctness risk다.
5. `socket_base_t::send()`의 send-side throttle은 보조 후보로 본다.
   - 현재 `process_commands(0, true)` 구조 자체는 libzmq와 거의 같다.
   - 따라서 이 항목은 "문제의 본체"라기보다, send-side differential을
     더 줄일 수 있는 보조 후보다.
   - `counter-only` 단순 치환은 현재 문서 기준으로 보류다.
6. `zlink_recv()`는 direct single-part fast path가 닿지 않는 경로부터 줄인다.
   - routed
   - strip
   - multipart export
   - 다만 `PAIR`/`DEALER` single-part의 `begin_with_first_slot()` fast path는
     이미 들어가 있으므로, 여기서의 추가 이득은 현재 secondary다.
7. `recv_internal.cpp`의 mode guard는
   dispatch mode가 고정인 steady-state 경로에서 더 얕은 분기로 내릴 수 있는지 본다.
8. `PUBSUB`/`ROUTER`는 공통 개선 후에도 남는 gap을 별도로 본다.
   - topic/routing prefix
   - pattern-specific public surface
9. `perf` 없이도 가능한 A/B 토글과 quick-run 기록을 계속 남겨
   원인 후보를 하나씩 배제한다
10. send-path 변경 후에는 `PAIR`/`DEALER_DEALER` raw/public 분리를 다시 찍어
    공통 core differential과 public penalty를 다시 분리한다

### 7.3 현재 보류하거나 기각한 제안

아래 제안은 현재 문서 기준으로는 바로 진행하지 않는다.

1. `pipe` lock 전체 제거
   - 최근 배제 실험에서 모두 악화됐다.
2. `pipe` activation을 lock 밖으로 이동
   - 최근 배제 실험에서 악화됐다.
3. mailbox read lock 제거
   - 최근 배제 실험에서 악화됐다.
4. send-side throttle의 counter-only 치환
   - recv와 모양은 비슷하지만, 현재 send throttle은 시간 기반 visibility를
     보장하므로 의미가 다르다.
5. pipe 전용 non-reentrant mutex
   - 현재 코드는 reentrant 성질을 실제로 의존한다.
6. same-thread `send_activate_read()` direct delivery
   - [`object.cpp`](/home/hep7/project/kairos/zlink/core/src/core/object.cpp)
     에서 peer `activate_read`를 mailbox 대신 inline `process_command()`로
     보내는 generic 실험은 `PAIR inproc`은 일부 회복했지만
     `DEALER_DEALER tcp 64B`를 `-25.06%`까지 악화시켰다.
   - `PAIR` no-handler 전용 gate로 더 좁혀도
     `PAIR tcp 64B` `-26.32%`, `PAIR inproc 64B` `-32.96%`로 악화됐다.
   - 따라서 현재 `activate_read publication`은
     "mailbox 왕복만 줄이면 바로 이득이 나는 순수 고정비"가 아니다.
     현재 구현에서는 progress ordering / callback / engine wakeup과 얽혀 있어
     direct delivery를 기본 후보로 올리지 않는다.
7. `send_socket_singlepart_fast()`의 중복 `msg->check()` 제거
   - [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
     에서 public single-part fast path의 wrapper-side `msg->check()`를
     걷는 실험은 `PAIR inproc`은 일부 회복했지만
     `DEALER_DEALER inproc 64B`를 `-31.51%`로 악화시켰다.
   - 즉 이 중복 검사는 현재 single bench의 상위 본체가 아니며,
     제거 자체로는 유지 가능한 broad win을 만들지 못했다.

thread-safe 계약을 깨뜨리는 최적화는 이 단계의 후보가 아니다.

## 8. 요약

현재 `single` 격차는 실재하지만, 그 의미를 정확히 읽어야 한다.

- POSD 직접 회귀는 대부분 회복됐다.
- 현재 benchmark surface가 완전히 대칭은 아니지만,
  `2.10` 기준으로 public wrapper penalty는 이미 secondary다.
- 즉 현재 남은 큰 gap의 본체는 send-side core engine differential 쪽이다.
- 현재 HEAD 기준 상위 원인은:
  - `zlink_send()` public lifecycle/backpressure
  - blocking retry 시의 `public_api_sync` 재획득
  - `pipe_t` steady-state 잠금과 activation/progress ordering
  - `zlink_recv()` aggregate/TLS export의 남은 routed/topic 경로
  - recv mode guard
  - pattern-specific public surface 차이
다.

- 추가로 현재는 `PAIR` 한정 public send sync 축소가 실제 개선을 보여서,
  send-side lifecycle/public sync가 여전히 상위 축임을 재확인했다.

따라서 지금 결과는
"public wrapper 때문에 느리다"보다,
"현재 zlink core send-side engine이 libzmq raw path 대비 얼마나 비싼가"
에 더 가깝다.

## 9. 2026-03-27 late-night iteration 로그

- 작업한 가설
  - `DEALER_DEALER` single bench의 steady-state는 active outbound pipe가
    하나뿐이므로, `lb_t::sendpipe()/has_out()`에서 일반 load-balancing loop를
    매 메시지 반복하지 않아도 된다.
- 수정한 파일 경로
  - [`lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)
  - [`test_public_inproc_multipart_send.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_public_inproc_multipart_send.cpp)
- 실행한 명령
  - `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^test_public_inproc_multipart_send$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag dealer_single_pipe_fastpath`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_single_pipe_guardrail`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_single_pipe_guardrail_raw`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag dealer_single_pipe_fastpath_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag dealer_router_single_pipe_fastpath`
  - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py dealer_dealer --build-dir core/build --runs 1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260327_233556_dealer_single_pipe_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_233556_dealer_single_pipe_fastpath.txt)
  - [`perf_linux_20260327_233634_pair_single_pipe_guardrail.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_233634_pair_single_pipe_guardrail.txt)
  - [`perf_linux_20260327_233703_pair_single_pipe_guardrail_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_233703_pair_single_pipe_guardrail_raw.txt)
  - [`perf_linux_20260327_233733_dealer_single_pipe_fastpath_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_233733_dealer_single_pipe_fastpath_raw.txt)
  - [`perf_linux_20260327_233804_dealer_router_single_pipe_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_233804_dealer_router_single_pipe_fastpath.txt)
- 핵심 수치
  - 유지한 `lb` one-pipe fast path
    - `DEALER_DEALER tcp 64B`: `3693.91 Kmsg/s` vs `3265.99 Kmsg/s`, `-11.58%`
    - `DEALER_DEALER inproc 64B`: `4356.51 Kmsg/s` vs `3177.03 Kmsg/s`, `-27.07%`
    - `DEALER_DEALER tcp raw`: `3822.41 Kmsg/s` vs `3352.16 Kmsg/s`, `-12.30%`
    - `DEALER_DEALER inproc raw`: `4164.13 Kmsg/s` vs `2917.13 Kmsg/s`, `-29.95%`
    - `DEALER` public penalty는 이번 상태에서 작다.
      - tcp: `3265.99` vs raw `3352.16`, public이 약 `-2.57%`
      - inproc: `3177.03` vs raw `2917.13`, public이 약 `+8.91%`
    - `PAIR` guardrail
      - public tcp `-10.00%`, inproc `-31.39%`
      - raw tcp `-16.73%`, inproc `-15.13%`
      - 즉 `PAIR inproc` public penalty는 다시 커져 있다.
    - `DEALER_ROUTER` smoke
      - tcp `-33.98%`, inproc `-24.43%`
    - multi `dealer_dealer tcp 64B`
      - `2299.48 Kmsg/s` vs `1619.89 Kmsg/s`, `-29.55%`
- 유지한 변경 / 원복한 변경
  - 유지
    - `lb.cpp` one-active-pipe `DEALER` send fast path
    - `DEALER` public inproc single/multipart/concurrent send regression 추가
  - 원복
    - `fq.cpp` one-active-pipe recv fast path
      - [`perf_linux_20260327_234037_dealer_single_pipe_fastpath_lb_fq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_234037_dealer_single_pipe_fastpath_lb_fq.txt)
      - `DEALER_DEALER inproc 64B`가 `-34.71%`로 악화돼 유지하지 않음
- 해석
  - 이번 라운드의 유지 후보는 "send-side `pipe` publication/order lock 안 work 축소"에
    해당한다.
  - `DEALER_DEALER tcp`는 stop condition 바로 아래까지 회복됐고,
    raw/public 분리상 이번 라운드의 이득은 public wrapper가 아니라
    core send-side work 축 축소로 읽는 편이 맞다.
  - 반대로 `inproc`, `DEALER_ROUTER`, multi guardrail은 아직 미달이다.
  - 따라서 이 변경은 유지할 가치가 있지만, broader acceptance를 통과한
    안정 지점은 아직 아니다.
- 다음 iteration 우선순위
  - `send-side lifecycle/backpressure`에서 더 안전한 공통 atomic 축소 후보가
    실제로 있는지 다시 좁힌다.
  - 동시에 `DEALER` 계열은 이번 one-pipe send fast path를 기준선으로 두고,
    `inproc` core differential과 routed/pattern-specific 잔여 비용을 분리한다.
  - `PUBSUB` / `ROUTER_ROUTER`는 아직 이번 라운드로는 직접 닿지 않았으므로
    우선순위는 그대로 유지한다.

## 10. 2026-03-28 midnight iteration 로그

- 작업한 가설
  - `pipe::write_and_flush()` 뒤 `send_activate_read()` publication을
    same-thread direct delivery로 바꾸면 inproc send publication cost를
    줄일 수 있다.
- 수정한 파일 경로
  - [`object.cpp`](/home/hep7/project/kairos/zlink/core/src/core/object.cpp)
  - [`test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp)
    임시 회귀 시도 후 원복
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R 'test_monitor_perf_contract|test_monitor_socket_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_activate_read_direct`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag dealer_activate_read_direct`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_inline_activate_read_pair_only`
- 생성된 결과 파일 경로
  - [`perf_linux_20260327_235547_pair_activate_read_direct.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_235547_pair_activate_read_direct.txt)
  - [`perf_linux_20260327_235621_dealer_activate_read_direct.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_235621_dealer_activate_read_direct.txt)
  - [`perf_linux_20260328_000053_pair_inline_activate_read_pair_only.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_000053_pair_inline_activate_read_pair_only.txt)
- 핵심 수치
  - generic direct `activate_read`
    - `PAIR tcp 64B`: `3703.59 Kmsg/s` vs `3235.59 Kmsg/s`, `-12.64%`
    - `PAIR inproc 64B`: `4170.15 Kmsg/s` vs `3176.67 Kmsg/s`, `-23.82%`
    - `DEALER_DEALER tcp 64B`: `3737.68 Kmsg/s` vs `2801.02 Kmsg/s`, `-25.06%`
    - `DEALER_DEALER inproc 64B`: `4104.88 Kmsg/s` vs `3294.05 Kmsg/s`, `-19.75%`
  - `PAIR` no-handler 전용 gate
    - `PAIR tcp 64B`: `3722.28 Kmsg/s` vs `2742.57 Kmsg/s`, `-26.32%`
    - `PAIR inproc 64B`: `4165.32 Kmsg/s` vs `2792.45 Kmsg/s`, `-32.96%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음. latest accepted delta는 여전히 `lb.cpp` one-active-pipe `DEALER`
      send fast path다.
  - 원복
    - `object.cpp` generic same-thread `send_activate_read()` direct delivery
    - `PAIR` no-handler 전용 inline `activate_read` gate
    - `test_socket_with_handler.cpp` 임시 recv-handler 회귀 시도
- 해석
  - `activate_read publication`은 현재 코드에서 mailbox 왕복만 줄이면 되는
    순수 오버헤드가 아니다.
  - generic direct path는 `DEALER_DEALER tcp`를 치명적으로 악화시켜
    guardrail을 즉시 벗어났다.
  - `PAIR` 전용으로 좁혀도 `PAIR tcp/inproc` 둘 다 기준선보다 나빠졌다.
  - 따라서 현재 우선순위는 여전히 `send-side lifecycle/backpressure`와
    `pipe` lock 안 work 축소이며, `activate_read` direct publication은
    rejected candidate로 유지한다.
- 다음 iteration 우선순위
  - `socket_base_t::send()` lifecycle/admission 쪽에서 현재 문서에 남은
    공통 atomic 축소 후보를 다시 좁힌다.
  - `DEALER`는 유지 중인 `lb` one-pipe fast path 기준선에서
    `inproc` differential을 계속 분리한다.
  - `PUBSUB` / `ROUTER_ROUTER`는 공통 send-side 후보가 더 이상 유지되지 않으면
    다음 순서로 올린다.

## 11. 2026-03-28 public send fast-path check elision 로그

- 작업한 가설
  - `send_socket_singlepart_fast()`가 `socket->send()` 안에서 다시 수행하는
    `msg->check()`를 wrapper에서 한 번 더 호출하고 있으므로,
    이 중복 검사를 제거하면 `PAIR`/`DEALER` public send penalty를 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R 'test_monitor_perf_contract|test_monitor_socket_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_singlepart_public_check_elision`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag dealer_singlepart_public_check_elision`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_000714_pair_singlepart_public_check_elision.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_000714_pair_singlepart_public_check_elision.txt)
  - [`perf_linux_20260328_000746_dealer_singlepart_public_check_elision.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_000746_dealer_singlepart_public_check_elision.txt)
- 핵심 수치
  - `PAIR tcp 64B`: `3727.31 Kmsg/s` vs `3180.10 Kmsg/s`, `-14.68%`
  - `PAIR inproc 64B`: `4150.14 Kmsg/s` vs `3261.31 Kmsg/s`, `-21.42%`
  - `DEALER_DEALER tcp 64B`: `3593.58 Kmsg/s` vs `3110.56 Kmsg/s`, `-13.44%`
  - `DEALER_DEALER inproc 64B`: `4169.00 Kmsg/s` vs `2855.55 Kmsg/s`, `-31.51%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_message_send_api.cpp` single-part public fast path의
      wrapper-side `msg->check()` 제거
- 해석
  - `PAIR inproc` 하나만 보면 좋아 보일 수 있지만,
    `DEALER_DEALER inproc`이 기준선보다 더 나빠져 broad win이 아니다.
  - 따라서 이 중복 검사는 현재 최상위 병목이 아니며,
    유지 후보가 아니다.
- 다음 iteration 우선순위
  - 여전히 `socket_base_t::send()` lifecycle/backpressure 쪽의
    더 안전한 공통 축소 후보를 먼저 찾는다.
  - 그 다음은 유지 중인 `DEALER` one-pipe send fast path 기준선에서
    `inproc` differential을 더 분리한다.

## 12. 2026-03-28 PUBSUB dist fast path 로그

- 작업한 가설
  - `PUBSUB` steady-state에서 matching pipe가 하나뿐이면
    generic `dist_t` distributor loop와 pipe index lookup을 건너뛰는 것만으로도
    zlink-only send differential을 줄일 수 있다.
- 수정한 파일 경로
  - [`dist.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.hpp)
  - [`dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
  - [`test_multi_socket_contract_regressions.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_multi_socket_contract_regressions.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_multi_socket_contract_regressions)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^test_pubsub_filter_xpub$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_dist_single_pipe_fastpath`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_dist_single_pipe_fastpath_rerun`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_dealer_guardrail_public_after_pubsub_dist_seq`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_dealer_guardrail_raw_after_pubsub_dist_seq`
  - `./core/bench/with_zmq/run_benchmarks_multi.sh --reuse-build --pattern pubsub --msg-sizes 64 --transports tcp --runs 1 --warmup 1 --duration 3 --results-tag pubsub_dist_single_pipe_fastpath_multi`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_002158_pubsub_dist_single_pipe_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_002158_pubsub_dist_single_pipe_fastpath.txt)
  - [`perf_linux_20260328_002301_pubsub_dist_single_pipe_fastpath_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_002301_pubsub_dist_single_pipe_fastpath_rerun.txt)
  - [`perf_linux_20260328_002427_pair_dealer_guardrail_public_after_pubsub_dist_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_002427_pair_dealer_guardrail_public_after_pubsub_dist_seq.txt)
  - [`perf_linux_20260328_002516_pair_dealer_guardrail_raw_after_pubsub_dist_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_002516_pair_dealer_guardrail_raw_after_pubsub_dist_seq.txt)
  - [`perf_linux_20260328_002837_pubsub_dist_single_pipe_fastpath_multi.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/multi/report/perf_linux_20260328_002837_pubsub_dist_single_pipe_fastpath_multi.txt)
- 단계 commit / push
  - commit: `af020ce4`
  - push: `origin/main`
- 핵심 수치
  - `PUBSUB` first run
    - `tcp 64B`: `3180.04 Kmsg/s` vs `2409.49 Kmsg/s`, `-24.23%`
    - `inproc 64B`: `3796.72 Kmsg/s` vs `2050.13 Kmsg/s`, `-46.00%`
  - `PUBSUB` rerun
    - `tcp 64B`: `3309.57 Kmsg/s` vs `2448.99 Kmsg/s`, `-26.00%`
    - `inproc 64B`: `3874.20 Kmsg/s` vs `2227.44 Kmsg/s`, `-42.51%`
  - raw/public guardrail
    - `PAIR` public `tcp/inproc`: `-15.78%` / `-17.62%`
    - `PAIR` raw `tcp/inproc`: `-11.84%` / `-35.47%`
    - `DEALER_DEALER` public `tcp/inproc`: `-13.19%` / `-18.46%`
    - `DEALER_DEALER` raw `tcp/inproc`: `-24.32%` / `-34.91%`
  - multi `pubsub tcp 64B`
    - `6094.02 Kmsg/s` vs `4450.16 Kmsg/s`, `-26.97%`
- 유지한 변경 / 원복한 변경
  - 유지
    - `dist_t` one-matching-pipe `PUBSUB` send fast path
    - index-stable matching-pipe deactivate helper
    - same-handle concurrent `PUB` publish regression
  - 원복
    - 없음
- 해석
  - `PUBSUB tcp`가 즉시 반응했으므로, distributor loop/index/deactivate work는
    실제 hot-path 비용 축이다.
  - raw/public guardrail을 다시 찍어도 이번 라운드가
    `PAIR`/`DEALER` public penalty 재도입으로 설명되진 않는다.
  - 반면 `PUBSUB inproc`와 multi `pubsub`는 여전히 크게 미달이라,
    이 변경은 keep-worthy지만 broader acceptance를 통과한 안정 지점은 아니다.
- 다음 iteration 우선순위
  - 첫 번째는 여전히 `socket_base_t::send()` lifecycle/backpressure 쪽의
    더 안전한 공통 atomic 축소 후보다.
  - 두 번째는 `PUBSUB` single-subscriber win을 `inproc`/multi까지
    확장하는 publication/order 후보 재탐색이다.
  - 세 번째는 `ROUTER_ROUTER` routed path의 pattern-specific differential 정리다.

## 13. 2026-03-28 XPUB single-pipe nodrop fusion 로그

- 작업한 가설
  - `XPUB`에서 matching pipe가 하나뿐인 `nodrop` steady-state는
    `check_hwm()` precheck와 실제 `write`를 같은 pipe lock 안으로 합치면
    `zlink_publish(topic, payload)`의 prefix/payload send differential을
    더 줄일 수 있다.
- 수정한 파일 경로
  - [`dist.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.hpp)
  - [`dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
  - 위 세 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub)$' -j1`
  - `./core/build/bin/test_xpub_nodrop`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_single_pipe_nodrop_write_fusion`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_single_pipe_nodrop_write_fusion_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_004716_pubsub_single_pipe_nodrop_write_fusion.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_004716_pubsub_single_pipe_nodrop_write_fusion.txt)
  - [`perf_linux_20260328_004750_pubsub_single_pipe_nodrop_write_fusion_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_004750_pubsub_single_pipe_nodrop_write_fusion_rerun.txt)
- 핵심 수치
  - first run
    - `PUBSUB tcp 64B`: `3674.19 Kmsg/s` vs `2414.00 Kmsg/s`, `-34.30%`
    - `PUBSUB inproc 64B`: `3806.91 Kmsg/s` vs `2405.61 Kmsg/s`, `-36.81%`
  - rerun
    - `PUBSUB tcp 64B`: `3766.63 Kmsg/s` vs `2405.39 Kmsg/s`, `-36.14%`
    - `PUBSUB inproc 64B`: `3971.62 Kmsg/s` vs `2176.55 Kmsg/s`, `-45.20%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `XPUB` single matching `nodrop` HWM+write fusion helper 전부
- 해석
  - first run에서 `inproc`만 좋아 보였지만 rerun에서 그 이득도 사라졌다.
  - 반대로 `tcp`는 두 번 모두 accepted baseline보다 명확히 나빠졌다.
  - 따라서 이 후보는 keep-worthy broad win이 아니며 rejected candidate로 둔다.
  - 위 해석에서 "failed write가 pipe inactive 전이까지 함께 일으켜
    tcp publication/wakeup 쪽을 더 비싸게 만들었을 가능성"은 현재 코드 기반
    추론이지만, 원인 규명 전까지는 채택하지 않는다.
- 다음 iteration 우선순위
  - 첫 번째는 그대로 `socket_base_t::send()` lifecycle/backpressure 쪽의
    공통 atomic 축소 후보 재검토다.
  - 두 번째는 `PUBSUB`에서 distribution 자체보다 남은 publication/wakeup
    differential을 더 좁게 분리하는 것이다.

## 14. 2026-03-28 `check_hwm_locked()` self-reentry A/B 로그

- 작업한 가설
  - [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp) 의
    `write()` / `write_and_flush()` / `check_write_status()`가 `_out_sync`
    아래에서 `check_hwm()`로 같은 recursive fast mutex를 다시 잡고 있으므로,
    locked helper로 이 self-reentry를 제거하면 send-side `pipe` cost를
    더 줄일 수 있다.
- 수정한 파일 경로
  - [`pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - 위 두 파일은 bench A/B 후 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_socket_with_handler|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pipe_hwm_locked_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pipe_hwm_locked_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pipe_hwm_recursive_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pipe_hwm_recursive_raw`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_010113_codex_pipe_hwm_locked_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_010113_codex_pipe_hwm_locked_public.txt)
  - [`perf_linux_20260328_010154_codex_pipe_hwm_locked_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_010154_codex_pipe_hwm_locked_raw.txt)
  - [`perf_linux_20260328_010424_codex_pipe_hwm_recursive_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_010424_codex_pipe_hwm_recursive_public.txt)
  - [`perf_linux_20260328_010507_codex_pipe_hwm_recursive_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_010507_codex_pipe_hwm_recursive_raw.txt)
- 핵심 수치
  - patched public zlink throughput
    - `PAIR tcp 64B`: `3237.13 Kmsg/s`
    - `PAIR inproc 64B`: `3214.84 Kmsg/s`
    - `DEALER_DEALER tcp 64B`: `3333.61 Kmsg/s`
    - `DEALER_DEALER inproc 64B`: `3191.00 Kmsg/s`
  - reverted public zlink throughput
    - `PAIR tcp 64B`: `3264.20 Kmsg/s`
    - `PAIR inproc 64B`: `2787.27 Kmsg/s`
    - `DEALER_DEALER tcp 64B`: `3062.35 Kmsg/s`
    - `DEALER_DEALER inproc 64B`: `2759.72 Kmsg/s`
  - patched raw zlink throughput
    - `PAIR tcp 64B`: `2958.21 Kmsg/s`
    - `PAIR inproc 64B`: `2807.57 Kmsg/s`
    - `DEALER_DEALER tcp 64B`: `3256.14 Kmsg/s`
    - `DEALER_DEALER inproc 64B`: `3280.86 Kmsg/s`
  - reverted raw zlink throughput
    - `PAIR tcp 64B`: `2867.24 Kmsg/s`
    - `PAIR inproc 64B`: `3193.91 Kmsg/s`
    - `DEALER_DEALER tcp 64B`: `3187.07 Kmsg/s`
    - `DEALER_DEALER inproc 64B`: `3290.80 Kmsg/s`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe.cpp` / `pipe.hpp` locked `check_hwm()` helper
- 해석
  - `DEALER_DEALER` public은 눈에 띄게 좋아졌지만,
    `PAIR`와 raw guardrail은 같은 방향으로 움직이지 않았다.
  - 특히 `PAIR inproc raw`가 `3193.91 -> 2807.57 Kmsg/s`로 내려가
    broad win으로 보긴 어렵다.
  - 따라서 "`_out_sync` self-reentry 제거"는 plausible cost axis인 것은
    맞지만, 현재 구현 형태 그대로는 keep-worthy accepted delta가 아니다.
  - 이 결론은 결과 파일 기반 해석이며, libzmq absolute throughput 자체도 run마다
    흔들려 직접 diff만으로 과대해석하지 않았다.
- 다음 iteration 우선순위
  - 첫 번째는 다시 `socket_base_t::send()` lifecycle/backpressure 쪽의
    공통 atomic/admission 후보를 좁히는 것이다.
  - 두 번째는 send-side `pipe` 경로에서 self-reentry 제거보다 더 직접적인
    ordering/work 축소 후보를 찾는 것이다.
  - 세 번째는 keep-worthy 공통 후보가 더 없으면 `PUBSUB` publication 잔여축과
    `ROUTER_ROUTER` routed path를 순서대로 올린다.

## 15. 2026-03-28 ROUTER single-out-pipe lookup cache 로그

- 작업한 가설
  - `ROUTER`/`STREAM` routed steady-state에서 outbound pipe가 하나뿐이면
    `routing_socket_base_t`의 routed lookup을 cache로 우회하는 것만으로도
    `ROUTER_ROUTER` public send differential을 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
  - [`socket_base_routing.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_routing.cpp)
  - 위 두 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_connect_rid|test_probe_router|test_router_multiple_dealers|test_router_auto_id_format|test_transport_matrix)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag router_single_out_pipe_cache`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag router_single_out_pipe_cache_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_011839_router_single_out_pipe_cache.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_011839_router_single_out_pipe_cache.txt)
  - [`perf_linux_20260328_011918_router_single_out_pipe_cache_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_011918_router_single_out_pipe_cache_rerun.txt)
- 핵심 수치
  - first run
    - `ROUTER_ROUTER tcp 64B`: `2998.27 Kmsg/s` vs `1237.50 Kmsg/s`, `-58.73%`
    - `ROUTER_ROUTER inproc 64B`: `3142.30 Kmsg/s` vs `2453.71 Kmsg/s`, `-21.91%`
  - rerun
    - `ROUTER_ROUTER tcp 64B`: `2844.23 Kmsg/s` vs `1230.30 Kmsg/s`, `-56.74%`
    - `ROUTER_ROUTER inproc 64B`: `3257.12 Kmsg/s` vs `2407.00 Kmsg/s`, `-26.10%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `routing_socket_base` single-out-pipe routed lookup cache 전부
- 해석
  - `inproc` first run만 보면 회복처럼 보였지만 rerun에서 그 이득이 사라졌고,
    `tcp`는 기준선보다 좋아졌다고 보기 어렵다.
  - 따라서 현재 `ROUTER_ROUTER` 잔여 gap을 routed `std::map` lookup 하나로
    설명할 수는 없다.
  - 이 후보는 keep-worthy broad win이 아니며 rejected candidate로 둔다.
- 다음 iteration 우선순위
  - 첫 번째는 여전히 `socket_base_t::send()` lifecycle/backpressure 쪽의
    공통 atomic/admission 후보를 다시 좁히는 것이다.
  - 두 번째는 send-side `pipe` 경로에서 ordering/work 축소 후보다.
  - 세 번째는 keep-worthy 공통 후보가 더 없으면 `PUBSUB` publication 잔여축과
    `ROUTER_ROUTER` public routed send/recv differential을 순서대로 다시 좁힌다.

## 16. 2026-03-28 `public_api_state` full CAS fast path 로그

- 작업한 가설
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    의 `enter_public_api()` / `leave_public_api()` /
    `unlock_public_api_sync_and_leave()` 에 uncontended CAS fast path를 넣으면
    send-side lifecycle atomic cost를 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - [`unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 위 두 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_monitor_perf_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_fast_exit_public_seq`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_fast_exit_raw_seq`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_fast_exit_reverted_public_seq`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_fast_exit_reverted_raw_seq`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_012937_codex_lifecycle_fast_exit_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_012937_codex_lifecycle_fast_exit_public_seq.txt)
  - [`perf_linux_20260328_013023_codex_lifecycle_fast_exit_raw_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013023_codex_lifecycle_fast_exit_raw_seq.txt)
  - [`perf_linux_20260328_013301_codex_lifecycle_fast_exit_reverted_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013301_codex_lifecycle_fast_exit_reverted_public_seq.txt)
  - [`perf_linux_20260328_013344_codex_lifecycle_fast_exit_reverted_raw_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013344_codex_lifecycle_fast_exit_reverted_raw_seq.txt)
- 핵심 수치
  - patched public
    - `PAIR tcp 64B`: `4542.78 Kmsg/s` vs `2800.92 Kmsg/s`, `-38.34%`
    - `PAIR inproc 64B`: `4181.74 Kmsg/s` vs `2795.49 Kmsg/s`, `-33.15%`
    - `DEALER_DEALER tcp 64B`: `3858.44 Kmsg/s` vs `3125.45 Kmsg/s`, `-19.00%`
    - `DEALER_DEALER inproc 64B`: `4080.17 Kmsg/s` vs `3356.60 Kmsg/s`, `-17.73%`
  - patched raw
    - `PAIR tcp 64B`: `4387.06 Kmsg/s` vs `3233.82 Kmsg/s`, `-26.29%`
    - `PAIR inproc 64B`: `4172.40 Kmsg/s` vs `2665.57 Kmsg/s`, `-36.11%`
    - `DEALER_DEALER tcp 64B`: `3714.39 Kmsg/s` vs `3187.49 Kmsg/s`, `-14.19%`
    - `DEALER_DEALER inproc 64B`: `4037.43 Kmsg/s` vs `2833.70 Kmsg/s`, `-29.81%`
  - reverted public
    - `PAIR tcp 64B`: `3625.10 Kmsg/s` vs `3158.47 Kmsg/s`, `-12.87%`
    - `PAIR inproc 64B`: `4188.57 Kmsg/s` vs `3171.66 Kmsg/s`, `-24.28%`
    - `DEALER_DEALER tcp 64B`: `3550.24 Kmsg/s` vs `2751.97 Kmsg/s`, `-22.49%`
    - `DEALER_DEALER inproc 64B`: `4174.18 Kmsg/s` vs `3186.47 Kmsg/s`, `-23.66%`
  - reverted raw
    - `PAIR tcp 64B`: `4567.20 Kmsg/s` vs `3280.92 Kmsg/s`, `-28.16%`
    - `PAIR inproc 64B`: `4192.83 Kmsg/s` vs `3143.78 Kmsg/s`, `-25.02%`
    - `DEALER_DEALER tcp 64B`: `3719.76 Kmsg/s` vs `2797.58 Kmsg/s`, `-24.79%`
    - `DEALER_DEALER inproc 64B`: `4347.30 Kmsg/s` vs `2853.17 Kmsg/s`, `-34.37%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.cpp` full lifecycle CAS fast path
    - `unittest_socket_runtime.cpp` 임시 close-preservation 회귀 추가
- 해석
  - `DEALER` raw/public은 일부 회복했지만 `PAIR` public과 `PAIR inproc raw`가
    clean reverted baseline 대비 너무 크게 흔들렸다.
  - 즉 lifecycle atomic cost axis 자체는 살아 있지만, `enter/leave` 전부를
    CAS fast path로 치환하는 현재 형태는 broad win이 아니다.
  - 따라서 이 후보는 rejected candidate로 두고, 다음 라운드는 `PAIR`를
    건드리지 않는 더 좁은 non-`PAIR` 종료 경로로 다시 본다.
- 다음 iteration 우선순위
  - `unlock_public_api_sync_and_leave()` 단독 fast path로
    `DEALER` 계열 raw 회복이 `PAIR` public 흔들림 없이 유지되는지 확인한다.

## 17. 2026-03-28 `unlock_public_api_sync_and_leave()` CAS fast path 로그

- 작업한 가설
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    의 `unlock_public_api_sync_and_leave()` 하나만 좁게 fast path로 바꾸면
    non-`PAIR` send 종료 비용을 줄이면서도 `PAIR` regression은 피할 수 있다.
- 수정한 파일 경로
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - [`unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 위 두 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_monitor_perf_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_sync_leave_public_seq`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_sync_leave_raw_seq`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_sync_leave_public_rerun_seq`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_013518_codex_lifecycle_sync_leave_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013518_codex_lifecycle_sync_leave_public_seq.txt)
  - [`perf_linux_20260328_013557_codex_lifecycle_sync_leave_raw_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013557_codex_lifecycle_sync_leave_raw_seq.txt)
  - [`perf_linux_20260328_013659_codex_lifecycle_sync_leave_public_rerun_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013659_codex_lifecycle_sync_leave_public_rerun_seq.txt)
- 핵심 수치
  - public first run
    - `PAIR tcp 64B`: `4558.41 Kmsg/s` vs `3090.47 Kmsg/s`, `-32.20%`
    - `PAIR inproc 64B`: `4086.96 Kmsg/s` vs `2810.77 Kmsg/s`, `-31.23%`
    - `DEALER_DEALER tcp 64B`: `4283.26 Kmsg/s` vs `3061.73 Kmsg/s`, `-28.52%`
    - `DEALER_DEALER inproc 64B`: `4250.57 Kmsg/s` vs `3171.47 Kmsg/s`, `-25.39%`
  - raw
    - `PAIR tcp 64B`: `3605.96 Kmsg/s` vs `3319.62 Kmsg/s`, `-7.94%`
    - `PAIR inproc 64B`: `4201.83 Kmsg/s` vs `3348.56 Kmsg/s`, `-20.31%`
    - `DEALER_DEALER tcp 64B`: `3610.30 Kmsg/s` vs `2854.94 Kmsg/s`, `-20.92%`
    - `DEALER_DEALER inproc 64B`: `4096.07 Kmsg/s` vs `3305.53 Kmsg/s`, `-19.30%`
  - public rerun
    - `PAIR tcp 64B`: `3655.07 Kmsg/s` vs `3127.22 Kmsg/s`, `-14.44%`
    - `PAIR inproc 64B`: `4104.06 Kmsg/s` vs `3317.05 Kmsg/s`, `-19.18%`
    - `DEALER_DEALER tcp 64B`: `4262.76 Kmsg/s` vs `3101.84 Kmsg/s`, `-27.23%`
    - `DEALER_DEALER inproc 64B`: `4155.75 Kmsg/s` vs `2873.87 Kmsg/s`, `-30.85%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.cpp` `unlock_public_api_sync_and_leave()` CAS fast path
    - `unittest_socket_runtime.cpp` 임시 close-preservation 회귀 추가
- 해석
  - raw는 `PAIR tcp`, `DEALER_DEALER inproc`까지 꽤 회복해
    lifecycle 종료 비용이 실제 cost axis라는 점은 더 강해졌다.
  - 하지만 public rerun에서는 `DEALER_DEALER tcp/inproc`가
    reverted baseline보다 더 흔들렸고, `PAIR`도 stop condition에는 멀다.
  - 즉 이 후보는 raw/public 분리상 "core differential을 줄이는 방향" 자체는
    맞지만, current public surface에서 keep-worthy broad win으로 보긴 어렵다.
  - 따라서 이 후보도 rejected candidate로 두고 원복한다.
- 다음 iteration 우선순위
  - 첫 번째는 여전히 `socket_base_t::send()` lifecycle/backpressure 쪽의
    다음 공통 후보를 찾는 것이다.
  - 다만 현재 문서 기준으로 단순 uncontended CAS 치환 계열은
    유지 후보가 아니므로 우선순위를 낮춘다.

## 18. 2026-03-28 `PAIR` no-sync send scope enter+leave fast path 로그

- 작업한 가설
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    와
    [`socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    에서 `PAIR` no-sync public send scope의 admission enter/leave 둘 다
    uncontended fast path를 넣으면 `PAIR` public send lifecycle cost를
    더 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - [`socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  - [`unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 위 세 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_monitor_perf_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pair_admission_fast_public_seq`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pair_admission_fast_raw_seq`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_015536_codex_pair_admission_fast_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_015536_codex_pair_admission_fast_public_seq.txt)
  - [`perf_linux_20260328_015622_codex_pair_admission_fast_raw_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_015622_codex_pair_admission_fast_raw_seq.txt)
- 핵심 수치
  - public
    - `PAIR tcp 64B`: `4494.47 Kmsg/s` vs `2788.09 Kmsg/s`, `-37.97%`
    - `PAIR inproc 64B`: `4236.85 Kmsg/s` vs `2850.84 Kmsg/s`, `-32.71%`
    - `DEALER_DEALER tcp 64B`: `3857.80 Kmsg/s` vs `3196.09 Kmsg/s`, `-17.15%`
    - `DEALER_DEALER inproc 64B`: `4205.13 Kmsg/s` vs `2858.53 Kmsg/s`, `-32.02%`
  - raw
    - `PAIR tcp 64B`: `3692.77 Kmsg/s` vs `2954.82 Kmsg/s`, `-19.98%`
    - `PAIR inproc 64B`: `4172.63 Kmsg/s` vs `3347.67 Kmsg/s`, `-19.77%`
    - `DEALER_DEALER tcp 64B`: `3579.95 Kmsg/s` vs `3137.19 Kmsg/s`, `-12.37%`
    - `DEALER_DEALER inproc 64B`: `4119.86 Kmsg/s` vs `3353.52 Kmsg/s`, `-18.60%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.cpp/.hpp` `PAIR` no-sync send scope enter+leave fast path
    - `unittest_socket_runtime.cpp` 임시 fast-path 회귀 추가
- 해석
  - raw만 보면 `PAIR`/`DEALER` 일부 회복이 보이지만,
    public seq에서는 `PAIR tcp/inproc`이 `-37.97% / -32.71%`로 다시 크게
    흔들렸다.
  - 즉 `PAIR` no-sync scope의 enter/leave 둘 다를 CAS fast path로 묶는 현재
    형태는 raw/public guardrail을 깨며 public penalty를 다시 키운다.
  - 이 후보는 lifecycle atomic 축의 방향성만 확인하고 rejected candidate로 둔다.
- 다음 iteration 우선순위
  - `PAIR` no-sync send scope의 enter/leave 둘 다를 한 번에 바꾸는 방향은
    다시 시도하지 않는다.
  - 다음 라운드는 더 좁은 종료 경로만 따로 보거나, keep-worthy 공통 후보가
    없으면 send-side `pipe` ordering/work 축소 쪽으로 옮긴다.

## 19. 2026-03-28 `PAIR` no-sync send scope leave-only fast path 로그

- 작업한 가설
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    의 `PAIR` no-sync public send scope에서 leave 쪽만 좁게 fast path로 바꾸면
    위의 enter+leave 동시 치환보다 public penalty 재도입을 피할 수 있다.
- 수정한 파일 경로
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - [`socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  - [`unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 위 세 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_monitor_perf_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pair_leave_fast_public_seq`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_015918_codex_pair_leave_fast_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_015918_codex_pair_leave_fast_public_seq.txt)
- 핵심 수치
  - public
    - `PAIR tcp 64B`: `3764.37 Kmsg/s` vs `3123.97 Kmsg/s`, `-17.01%`
    - `PAIR inproc 64B`: `4108.01 Kmsg/s` vs `3291.33 Kmsg/s`, `-19.88%`
    - `DEALER_DEALER tcp 64B`: `4462.23 Kmsg/s` vs `2791.81 Kmsg/s`, `-37.43%`
    - `DEALER_DEALER inproc 64B`: `4269.97 Kmsg/s` vs `2809.13 Kmsg/s`, `-34.21%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.cpp/.hpp` `PAIR` no-sync send scope leave-only fast path
    - `unittest_socket_runtime.cpp` 임시 fast-exit 회귀 추가
- 해석
  - `PAIR`만 보면 enter+leave 동시 치환보다 덜 흔들렸지만,
    같은 seq run에서 `DEALER_DEALER tcp/inproc`가 `-37.43% / -34.21%`까지
    내려가 broad win 근거를 만들지 못했다.
  - 즉 leave-only까지 줄여도 지금 형태의 `PAIR` 전용 no-sync lifecycle fast path는
    유지 후보가 아니다.
  - raw/public guardrail 단계까지 갈 가치가 없는 disqualifying public 결과로 보고
    즉시 원복했다.
- 다음 iteration 우선순위
  - `PAIR` no-sync send scope에 대한 단순 uncontended CAS 치환 계열은
    현재 문서 기준으로 보류/기각 영역으로 내린다.
  - 다음 후보는 send-side lifecycle/backpressure의 다른 공통 구조나,
    keep-worthy 공통 후보가 더 없으면 `pipe` ordering/work 축소 쪽을 본다.

## 20. 2026-03-28 idle send-ready handler retry-sync gate 로그

- 작업한 가설
  - [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
    의 blocking retry loop가 "send-ready handler가 설치돼 있다"는 이유만으로
    `public_api_sync`를 매번 풀었다 다시 잡는다.
  - 실제 callback 개입 조건은 "notification이 이미 armed 됐을 때"로 더 좁으므로,
    idle handler만 있는 retry는 sync를 계속 유지하면 backpressure 복귀 비용을
    줄일 수 있다.
- 수정한 파일 경로
  - [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  - [`unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 위 두 파일은 serial bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_monitor_perf_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_idle_send_ready_retry_public_seq_serial`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_idle_send_ready_retry_raw_seq_serial`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_021040_codex_idle_send_ready_retry_public_seq_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_021040_codex_idle_send_ready_retry_public_seq_serial.txt)
  - [`perf_linux_20260328_021124_codex_idle_send_ready_retry_raw_seq_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_021124_codex_idle_send_ready_retry_raw_seq_serial.txt)
  - 아래 두 파일은 public/raw를 동시에 띄운 잘못된 측정이라 해석에서 제외했다.
    - [`perf_linux_20260328_020957_codex_idle_send_ready_retry_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_020957_codex_idle_send_ready_retry_public_seq.txt)
    - [`perf_linux_20260328_020957_codex_idle_send_ready_retry_raw_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_020957_codex_idle_send_ready_retry_raw_seq.txt)
- 핵심 수치
  - public serial
    - `PAIR tcp 64B`: `3806.57 Kmsg/s` vs `3049.30 Kmsg/s`, `-19.89%`
    - `PAIR inproc 64B`: `4138.24 Kmsg/s` vs `3131.20 Kmsg/s`, `-24.33%`
    - `DEALER_DEALER tcp 64B`: `3501.78 Kmsg/s` vs `3158.24 Kmsg/s`, `-9.81%`
    - `DEALER_DEALER inproc 64B`: `4241.06 Kmsg/s` vs `2847.56 Kmsg/s`, `-32.86%`
  - raw serial
    - `PAIR tcp 64B`: `3741.88 Kmsg/s` vs `3244.87 Kmsg/s`, `-13.28%`
    - `PAIR inproc 64B`: `4200.18 Kmsg/s` vs `3251.37 Kmsg/s`, `-22.59%`
    - `DEALER_DEALER tcp 64B`: `3936.01 Kmsg/s` vs `2862.06 Kmsg/s`, `-27.29%`
    - `DEALER_DEALER inproc 64B`: `4357.16 Kmsg/s` vs `3285.83 Kmsg/s`, `-24.59%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_base_msg.cpp` retry loop의 `send_ready_handler_active() -> send_ready_armed` gate
    - `unittest_socket_runtime.cpp` armed-state 회귀 추가
- 해석
  - `DEALER_DEALER tcp` public은 stop condition 안쪽까지 회복됐지만,
    같은 serial run에서 `PAIR inproc` public이 `-24.33%`,
    `DEALER_DEALER inproc` public이 `-32.86%`로 크게 흔들렸다.
  - raw도 `DEALER_DEALER tcp/inproc`가 `-27.29% / -24.59%`여서 broad win이 아니다.
  - 즉 "installed-but-idle send-ready handler 때문에 retry sync를 너무 자주 푼다"
    는 해석은 현재 코드 기준 상위 공통 cost axis로 채택할 수 없다.
  - send-ready callback contract를 깨지 않고 retry gate를 이 조건으로 좁히는
    방향은 rejected candidate로 둔다.
- 다음 iteration 우선순위
  - blocking retry에서 send-ready handler presence를 armed-state로 좁히는
    방향은 다시 시도하지 않는다.
  - send-side lifecycle/backpressure 축은 다른 구조적 후보가 실제로 남는지
    더 좁게 대조한 뒤, keep-worthy 공통 후보가 더 없으면
    `pipe` ordering/work 축소와 `PUBSUB` 잔여 publication 축으로 넘긴다.

## 21. 2026-03-28 single `PUBSUB` no-topic surface alignment 로그

- 작업한 가설
  - current single `PUBSUB` zlink bench가
    `zlink_publish("", &part, 1)` + `zlink_subscribe()`를 써서
    empty-topic frame/topic-aware recv surface를 강제하고 있었다.
  - 저장소 계약/테스트에 이미 있는
    `zlink_publish(NULL, &part, 1)` + `zlink_recv()` no-topic payload-only
    경로로 정렬하면 libzmq single raw send/recv와 shape mismatch를 줄일 수 있다.
- 수정한 파일 경로
  - [`bench_zlink_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_multi_socket_contract_regressions|test_pubsub_filter_xpub)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^test_multi_socket_contract_regressions$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_no_topic_surface_align`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_no_topic_surface_align_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_024920_pubsub_no_topic_surface_align.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_024920_pubsub_no_topic_surface_align.txt)
  - [`perf_linux_20260328_024954_pubsub_no_topic_surface_align_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_024954_pubsub_no_topic_surface_align_rerun.txt)
- 단계 commit / push
  - commit: `d03f56dd`
  - push: `origin/main`
- 핵심 수치
  - first run
    - `PUBSUB tcp 64B`: `3208.01 Kmsg/s` vs `2421.78 Kmsg/s`, `-24.51%`
    - `PUBSUB inproc 64B`: `3818.17 Kmsg/s` vs `2222.58 Kmsg/s`, `-41.79%`
  - rerun
    - `PUBSUB tcp 64B`: `3210.77 Kmsg/s` vs `2466.81 Kmsg/s`, `-23.17%`
    - `PUBSUB inproc 64B`: `3797.16 Kmsg/s` vs `2100.44 Kmsg/s`, `-44.68%`
- 유지한 변경 / 원복한 변경
  - 유지
    - single zlink `PUBSUB` bench의 no-topic payload-only surface alignment
  - 원복
    - 없음
- 해석
  - empty-topic frame/topic-aware recv surface mismatch는 실제로 있었고,
    tcp rerun 기준 diff는 `-26.00% -> -23.17%`로 일부 좁혀졌다.
  - 하지만 `inproc` rerun은 `-44.68%`로 여전히 크며,
    first run도 `-41.79%`여서 이 정렬만으로 잔여 gap이 설명되지는 않는다.
  - 즉 single `PUBSUB` current gap은 더 이상 empty-topic wire-shape 차이보다는
    publication/lifecycle/distribution differential을 본체로 읽는 것이 맞다.
  - 첫 `ctest` 묶음에서는 `test_multi_socket_contract_regressions`가
    `test_pubsub_publish_is_safe_from_multiple_threads`에서 timeout/fail로
    끊겼지만, 같은 바이너리 단독 rerun은 즉시 통과했다.
    이 라운드에서는 bench surface 변경과 직접 연결된 재현으로 보지 않았다.
- 다음 iteration 우선순위
  - `PUBSUB`는 이제 aligned no-topic single surface를 기준선으로 유지한다.
  - 다음 후보는 publication/lifecycle 잔여축과
    single-subscriber win의 `inproc`/multi 확장 여부를 직접 보는 것이다.
  - 공통 send-side 후보가 다시 살아나지 않으면 그 다음은
    `ROUTER_ROUTER` public routed differential 정리다.

## 22. 2026-03-28 no-topic single-part `PUBSUB` public fast path 로그

- 작업한 가설
  - aligned single `PUBSUB` surface의 `zlink_publish(NULL, &part, 1)`는
    여전히 generic multipart publish wrapper를 지난다.
  - no-topic single-part publish만 `socket->send()` direct fast path로 좁히면
    public wrapper penalty를 더 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
  - 위 파일은 isolated bench 확인 후 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^test_multi_socket_contract_regressions$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_no_topic_singlepart_fastpath_isolated`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_030104_pubsub_no_topic_singlepart_fastpath_isolated.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_030104_pubsub_no_topic_singlepart_fastpath_isolated.txt)
- 핵심 수치
  - `PUBSUB tcp 64B`: `3329.43 Kmsg/s` vs `2236.07 Kmsg/s`, `-32.84%`
  - `PUBSUB inproc 64B`: `3817.36 Kmsg/s` vs `2069.11 Kmsg/s`, `-45.80%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_message_send_api.cpp` no-topic single-part `PUBSUB` public fast path
- 해석
  - aligned no-topic single surface에서도 generic multipart wrapper를
    바로 우회하는 현재 형태는 tcp/inproc 둘 다 오히려 더 나빴다.
  - 즉 현재 `PUBSUB` 잔여 gap을 public publish wrapper 한 겹으로
    설명할 수는 없다.

## 23. 2026-03-28 `SUB/XSUB` raw multipart recv fast path 로그

- 작업한 가설
  - aligned single `PUBSUB` zlink bench의 receive side는
    `zlink_recv(..., &parts, &part_count)` payload-only raw multipart surface다.
  - `PAIR/DEALER`처럼 TLS first-slot direct export를 `SUB/XSUB` raw recv에도
    좁게 적용하면 single `PUBSUB` recv-side cost를 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
  - [`test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp)
  - 위 두 파일은 rerun 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^test_socket_with_handler$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_sub_raw_multipart_recv_fastpath`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_sub_raw_multipart_recv_fastpath_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_030652_pubsub_sub_raw_multipart_recv_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_030652_pubsub_sub_raw_multipart_recv_fastpath.txt)
  - [`perf_linux_20260328_030723_pubsub_sub_raw_multipart_recv_fastpath_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_030723_pubsub_sub_raw_multipart_recv_fastpath_rerun.txt)
- 핵심 수치
  - first run
    - `PUBSUB tcp 64B`: `3308.89 Kmsg/s` vs `2294.20 Kmsg/s`, `-30.67%`
    - `PUBSUB inproc 64B`: `3896.50 Kmsg/s` vs `2280.61 Kmsg/s`, `-41.47%`
  - rerun
    - `PUBSUB tcp 64B`: `3318.78 Kmsg/s` vs `2431.17 Kmsg/s`, `-26.74%`
    - `PUBSUB inproc 64B`: `4255.17 Kmsg/s` vs `2098.59 Kmsg/s`, `-50.68%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_message_recv_api.cpp` `SUB/XSUB` raw multipart single-part recv fast path
    - `test_socket_with_handler.cpp` raw multipart recv 회귀 추가
- 해석
  - first run은 `inproc`만 약간 좋아졌지만 tcp는 크게 악화됐고,
    rerun은 tcp가 일부 회복되는 대신 inproc이 `-50.68%`로 더 악화됐다.
  - 즉 raw recv single-part export를 `SUB/XSUB`에 더 직접 적용하는 현재 형태는
    broad win이 아니며, current `PUBSUB` 잔여 gap의 본체를 정확히 짚지 못했다.

## 24. 2026-03-28 `PAIR`/`DEALER_DEALER` serial raw/public 재기준 로그

- 작업한 가설
  - guide/hot-path가 여전히 "`public penalty는 이미 secondary`" 쪽으로
    읽히고 있어, 현재 HEAD의 raw/public guardrail을 직렬 baseline으로 다시
    고정해야 했다.
  - 병렬 perf run은 서로 간섭하므로 폐기하고, `PAIR`/`DEALER_DEALER`의
    public/raw를 모두 직렬로 다시 찍어 현재 해석을 갱신한다.
- 수정한 파일 경로
  - 없음
- 실행한 명령
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_public_serial`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_raw_serial`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_public_serial`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_raw_serial`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_031439_codex_20260328_pair_public_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_031439_codex_20260328_pair_public_serial.txt)
  - [`perf_linux_20260328_031501_codex_20260328_pair_raw_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_031501_codex_20260328_pair_raw_serial.txt)
  - [`perf_linux_20260328_031526_codex_20260328_dealer_public_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_031526_codex_20260328_dealer_public_serial.txt)
  - [`perf_linux_20260328_031551_codex_20260328_dealer_raw_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_031551_codex_20260328_dealer_raw_serial.txt)
- 핵심 수치
  - `PAIR`
    - public `tcp/inproc`: `3200.10 / 2816.95 Kmsg/s`
    - raw `tcp/inproc`: `3367.91 / 3015.08 Kmsg/s`
    - raw-public delta: `+5.24% / +7.03%`
  - `DEALER_DEALER`
    - public `tcp/inproc`: `2830.18 / 3145.71 Kmsg/s`
    - raw `tcp/inproc`: `3263.08 / 2799.88 Kmsg/s`
    - raw-public delta: `+15.30% / -10.99%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - 없음
- 해석
  - raw/public 분리는 현재도 keep해야 할 guardrail이지만, 패턴/transport별로
    방향이 다시 엇갈린다.
  - 따라서 `PAIR`/`DEALER` 공통으로 "`public penalty는 이미 low
    single-digit`"라고 고정하는 해석은 더 이상 안전하지 않다.
  - 다음 code iteration에서는 raw/public을 고정 전제가 아니라
    serial rerun으로 다시 확인해야 하는 동적 guardrail로 취급한다.

## 25. 2026-03-28 logical multipart publish contract fix 로그

- 작업한 가설
  - baseline HEAD는 same-handle concurrent `PUB` publish에서
    topic frame과 payload frame이 interleave되어
    `test_pubsub_publish_is_safe_from_multiple_threads`가
    `part_count Expected 1 Was 2`로 반복 실패했다.
  - `socket_base_t::send()` 단위가 아니라 logical multipart publish/send
    전체를 하나의 public send scope로 묶어야 이 contract를 복구할 수 있다.
- 수정한 파일 경로
  - [`multipart_send_txn.cpp`](/home/hep7/project/kairos/zlink/core/src/core/multipart_send_txn.cpp)
  - [`socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
  - [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_contract_scope_public`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_contract_scope_public_nosinglefast`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_public_contract_scope`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_raw_contract_scope`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_public_contract_scope`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_raw_contract_scope`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_033544_codex_20260328_pubsub_contract_scope_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033544_codex_20260328_pubsub_contract_scope_public.txt)
  - [`perf_linux_20260328_033719_codex_20260328_pubsub_contract_scope_public_nosinglefast.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033719_codex_20260328_pubsub_contract_scope_public_nosinglefast.txt)
  - [`perf_linux_20260328_033847_codex_20260328_pair_public_contract_scope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033847_codex_20260328_pair_public_contract_scope.txt)
  - [`perf_linux_20260328_033914_codex_20260328_pair_raw_contract_scope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033914_codex_20260328_pair_raw_contract_scope.txt)
  - [`perf_linux_20260328_033936_codex_20260328_dealer_public_contract_scope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033936_codex_20260328_dealer_public_contract_scope.txt)
  - [`perf_linux_20260328_033957_codex_20260328_dealer_raw_contract_scope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033957_codex_20260328_dealer_raw_contract_scope.txt)
  - pushed commit: `754ee6080badb92539cd266b29680ea37c1284d9`
- 핵심 수치
  - 회귀 테스트
    - `test_multi_socket_contract_regressions`
    - `test_public_inproc_multipart_send`
    - `unittest_socket_runtime`
    - 모두 재통과
  - `PUBSUB` public
    - contract fix만 유지한 current code:
      `tcp -30.71%`, `inproc -40.37%`
    - no-topic single-part direct-send fallback 잠깐 추가한 rerun:
      `tcp -31.67%`, `inproc -38.76%`
  - `PAIR` public/raw
    - public `tcp/inproc`: `2717.91 / 3326.33 Kmsg/s`
    - raw `tcp/inproc`: `3212.40 / 3136.33 Kmsg/s`
  - `DEALER_DEALER` public/raw
    - public `tcp/inproc`: `3126.42 / 3163.47 Kmsg/s`
    - raw `tcp/inproc`: `3135.91 / 3138.42 Kmsg/s`
- 유지한 변경 / 원복한 변경
  - 유지
    - `multipart_send_txn.cpp` logical multipart publish/send single public
      send scope contract fix
    - `socket_base.hpp` logical multipart wrapper friend 선언과 scoped send helper 연계
    - `socket_base_msg.cpp` scoped rollback assert
  - 원복
    - `multipart_send_txn.cpp` no-topic single-part direct-send fallback
- 해석
  - logical multipart publish/send 전체를 하나의 public send scope로 묶는
    수정 없이는 same-handle concurrent `PUB` publish contract가 깨진다.
    이 수정은 성능 후보가 아니라 correctness fix로 유지해야 한다.
  - 다만 latest `PUBSUB` public rerun은 broad win이 아니었고,
    no-topic single-part direct-send fallback도 추가 회복을 만들지 못했다.
  - post-fix raw/public serial rerun은 `PAIR`/`DEALER_DEALER` 모두 다시 mixed라서
    raw/public 분리는 여전히 동적 guardrail이다.
- 다음 iteration 우선순위
  - contract fix를 유지한 채 send-side lifecycle/backpressure retry cost를
    다시 줄인다.
  - 그 다음은 `PUBSUB` publication path에서 single-subscriber correctness를
    유지하면서 steady-state work를 더 걷는 후보를 본다.

## 26. 2026-03-28 `XPUB` all-attached empty-prefix fast path 로그

- 작업한 가설
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
    에서 모든 attached pipe가 empty-prefix subscription을 가진 steady-state면
    single-part publish마다 `_subscriptions.match()`를 다시 돌지 말고
    `send_to_all()`로 바로 내려보내면
    `PUBSUB`의 no-topic/topic-aware 공통 publication cost를 더 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
  - [`socket_base_monitor.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_monitor.cpp)
  - [`xpub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.hpp)
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
  - [`test_multi_socket_contract_regressions.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_multi_socket_contract_regressions.cpp)
  - 위 다섯 파일은 bench A/B 확인 뒤 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_socket_with_handler|test_pubsub_filter_xpub|test_monitor_perf_contract)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_match_all_public`
  - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_match_all_guardrail_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_match_all_guardrail_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_match_all_broader_single`
  - 원복 후 `cmake --build core/build -j$(nproc) --target test_multi_socket_contract_regressions test_socket_with_handler test_pubsub_filter_xpub test_monitor_perf_contract`
  - 원복 후 `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_socket_with_handler|test_pubsub_filter_xpub|test_monitor_perf_contract)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_040146_codex_20260328_xpub_match_all_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_040146_codex_20260328_xpub_match_all_public.txt)
  - [`perf_linux_20260328_040232_codex_20260328_xpub_match_all_guardrail_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_040232_codex_20260328_xpub_match_all_guardrail_public.txt)
  - [`perf_linux_20260328_040313_codex_20260328_xpub_match_all_guardrail_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_040313_codex_20260328_xpub_match_all_guardrail_raw.txt)
  - [`perf_linux_20260328_040359_codex_20260328_xpub_match_all_broader_single.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_040359_codex_20260328_xpub_match_all_broader_single.txt)
  - multi smoke는 Python runner가 새 report 파일을 남기지 않았고 콘솔 결과만 확인했다.
- 핵심 수치
  - isolated single `PUBSUB` public
    - `tcp 64B`: `-22.28%`
    - `inproc 64B`: `-43.96%`
  - multi smoke `pubsub tcp 64B`
    - `5725.08 Kmsg/s` vs `4488.36 Kmsg/s`, `-21.60%`
  - raw/public guardrail
    - public `PAIR tcp/inproc`: `-18.71%` / `-22.18%`
    - public `DEALER_DEALER tcp/inproc`: `-15.66%` / `-23.31%`
    - raw `PAIR tcp/inproc`: `-35.42%` / `-15.84%`
    - raw `DEALER_DEALER tcp/inproc`: `-12.05%` / `-35.25%`
  - broader single acceptance
    - `PAIR tcp/inproc`: `-16.32%` / `-31.54%`
    - `PUBSUB tcp/inproc`: `-30.53%` / `-42.65%`
    - `DEALER_DEALER tcp/inproc`: `-17.46%` / `-27.16%`
    - `DEALER_ROUTER tcp/inproc`: `-19.95%` / `-36.34%`
    - `ROUTER_ROUTER tcp/inproc`: `-57.55%` / `-25.46%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `XPUB` all-attached empty-prefix `send_to_all()` fast path
    - 해당 회귀 테스트/attached-pipe helper 전부
- 해석
  - empty-prefix trie-match 제거는 isolated `PUBSUB tcp`와 multi `pubsub tcp`
    에서는 반응했지만, `PUBSUB inproc`은 `-43.96%` / broader single
    `-42.65%`로 여전히 큰 gap이 남았다.
  - broader single 기준 `PUBSUB tcp`도 `-30.53%`라서
    current code의 `-30.71%` 대비 keep-worthy broad win이라고 보기 어렵다.
  - 즉 현재 `PUBSUB` 잔여 gap의 본체를 empty-prefix match 비용 하나로
    설명할 수는 없고, publication/lifecycle differential이 더 직접적인 축이다.
- 다음 iteration 우선순위
  - `XPUB` empty-prefix all-attached fast path는 다시 시도하지 않는다.
  - `PUBSUB`은 trie match elimination이 아니라 publication/lifecycle 잔여축을
    더 직접 줄이는 후보를 본다.
  - 공통 축이 더 남지 않으면 guide 순서대로 `pipe` send/publication
    ordering/work 축소와 `ROUTER_ROUTER` 전용 differential 정리로 넘어간다.

## 27. 2026-03-28 `XPUB` single-subscriber ready-count fast path 로그

- 작업한 가설
  - `XPUB`의 single-subscriber wakeup path는 monitor가 없더라도
    delivery-ready count를 다시 계산한다.
  - single attached pipe에서는 이 ready-count bookkeeping을 더 싸게 만들면
    `PUBSUB` publication/wakeup cost를 조금 더 줄일 수 있다.
- 수정한 파일 경로
  - [`dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
  - [`xpub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.hpp)
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
  - 위 세 파일은 bench A/B 확인 뒤 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_monitor_perf_contract|test_monitor_enhanced|test_monitor_socket_contract)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_socket_with_handler|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_single_subscriber_readycount_fastpath`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_single_subscriber_readycount_fastpath_rerun_seq`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_042325_codex_20260328_pubsub_single_subscriber_readycount_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_042325_codex_20260328_pubsub_single_subscriber_readycount_fastpath.txt)
  - [`perf_linux_20260328_042357_codex_20260328_pubsub_single_subscriber_readycount_fastpath_rerun_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_042357_codex_20260328_pubsub_single_subscriber_readycount_fastpath_rerun_seq.txt)
- 핵심 수치
  - first run
    - `PUBSUB tcp 64B`: `3216.78 Kmsg/s` vs `2373.33 Kmsg/s`, `-26.22%`
    - `PUBSUB inproc 64B`: `3572.34 Kmsg/s` vs `2203.87 Kmsg/s`, `-38.31%`
  - rerun
    - `PUBSUB tcp 64B`: `3240.18 Kmsg/s` vs `2303.87 Kmsg/s`, `-28.90%`
    - `PUBSUB inproc 64B`: `3662.76 Kmsg/s` vs `2090.81 Kmsg/s`, `-42.92%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - single-subscriber ready-count bookkeeping fast path 전체
- 해석
  - clean first/rerun 모두 current accepted baseline보다 낫지 않았다.
  - 따라서 `PUBSUB` 잔여 gap의 본체를 delivery-ready ready-count
    bookkeeping으로 읽는 것은 맞지 않다.
  - monitor-ready bookkeeping은 current code 기준으로도 secondary candidate로
    유지한다.

## 28. 2026-03-28 `XPUB` single attached empty-prefix match fast path 로그

- 작업한 가설
  - current no-topic `PUBSUB` single bench는 `zlink_publish(NULL, ...)`라서
    `XPUB`가 payload를 기준으로 `_subscriptions.match()`를 다시 돈다.
  - attached pipe가 정확히 하나이고 그 pipe가 empty-prefix subscription을
    가진 steady-state면, generic trie `match()`를 건너뛰고
    기존 `dist.match()/send_to_matching()` 경로만 쓰면
    `PUBSUB` single-subscriber publication cost를 더 줄일 수 있다.
- 수정한 파일 경로
  - [`xpub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.hpp)
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
  - 위 두 파일은 bench A/B 확인 뒤 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_monitor_perf_contract|test_monitor_enhanced|test_monitor_socket_contract)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_socket_with_handler|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_single_empty_prefix_pipe_fastpath`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_single_empty_prefix_pipe_fastpath_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_042724_codex_20260328_xpub_single_empty_prefix_pipe_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_042724_codex_20260328_xpub_single_empty_prefix_pipe_fastpath.txt)
  - [`perf_linux_20260328_042751_codex_20260328_xpub_single_empty_prefix_pipe_fastpath_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_042751_codex_20260328_xpub_single_empty_prefix_pipe_fastpath_rerun.txt)
- 핵심 수치
  - first run
    - `PUBSUB tcp 64B`: `3202.69 Kmsg/s` vs `2442.46 Kmsg/s`, `-23.74%`
    - `PUBSUB inproc 64B`: `3611.05 Kmsg/s` vs `2286.97 Kmsg/s`, `-36.67%`
  - rerun
    - `PUBSUB tcp 64B`: `3702.91 Kmsg/s` vs `2548.99 Kmsg/s`, `-31.16%`
    - `PUBSUB inproc 64B`: `4094.81 Kmsg/s` vs `2142.69 Kmsg/s`, `-47.67%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - single attached empty-prefix match fast path 전체
- 해석
  - first run만 보면 `tcp`와 `inproc` 둘 다 current code보다 좋아 보였지만,
    clean rerun에서 둘 다 다시 무너졌다.
  - 따라서 current `PUBSUB` 잔여 gap을
    "single attached pipe + empty-prefix trie match" 하나로 설명할 수는 없다.
  - empty-prefix trie match elimination은 all-attached 변형뿐 아니라
    더 좁은 single-attached 변형도 current code 기준으로 keep-worthy broad win이
    아니었다.
- 다음 iteration 우선순위
  - `PUBSUB`은 delivery-ready bookkeeping이나 empty-prefix trie match
    elimination이 아니라 publication/wakeup differential 자체를 더 직접적으로
    줄이는 후보를 본다.
  - keep-worthy 공통 send-side delta가 더 나오지 않으면
    guide 순서대로 `pipe` send/publication ordering/work 축소와
    `ROUTER_ROUTER` 전용 differential 정리로 넘어간다.

## 29. 2026-03-28 `ROUTER` mandatory-HWM 회귀 보강과 rejected candidate 로그

- 작업한 가설
  - `ROUTER` public routed send는 prefix frame 해석 뒤
    `check_write_status()`와 `write()`가 사실상 같은 HWM/ready 확인을
    두 번 밟는다. `check_write_status()`가 성공한 pipe에 한해
    `write_no_hwm_check()+flush()`로 내리면 `ROUTER_ROUTER` public send gap을
    조금 더 줄일 수 있다고 봤다.
  - `ROUTER` public routed recv는 direct fast path에서도
    `zlink_routing_id_t` 전체를 중복 zero-fill한다.
    이 256B clear 하나를 줄이면 routed recv 쪽 고정비를 덜 수 있다고 봤다.
- 수정한 파일 경로
  - 유지
    - [`CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/tests/CMakeLists.txt)
    - [`test_router_mandatory_hwm.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_router_mandatory_hwm.cpp)
  - bench A/B 뒤 원복
    - [`pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`router.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.cpp)
    - [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_router_multiple_dealers|test_stream_send_blocking_wakeup)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^test_router_mandatory_hwm$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_prefix_hwm_skip`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_prefix_hwm_skip_guardrail_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_prefix_hwm_skip_guardrail_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_recv_rid_zero_elision`
  - 원복 후 `ctest --test-dir core/build --output-on-failure -R '^(test_router_mandatory_hwm|test_public_inproc_multipart_send|test_router_multiple_dealers|test_stream_send_blocking_wakeup)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_043954_codex_20260328_router_prefix_hwm_skip.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_043954_codex_20260328_router_prefix_hwm_skip.txt)
  - [`perf_linux_20260328_044023_codex_20260328_router_prefix_hwm_skip_guardrail_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_044023_codex_20260328_router_prefix_hwm_skip_guardrail_public.txt)
  - [`perf_linux_20260328_044103_codex_20260328_router_prefix_hwm_skip_guardrail_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_044103_codex_20260328_router_prefix_hwm_skip_guardrail_raw.txt)
  - [`perf_linux_20260328_044628_codex_20260328_router_recv_rid_zero_elision.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_044628_codex_20260328_router_recv_rid_zero_elision.txt)
- 핵심 수치
  - routed send prefix/HWM second-check elimination
    - `ROUTER_ROUTER tcp 64B`: `2740.96 Kmsg/s` vs `1228.10 Kmsg/s`, `-55.19%`
    - `ROUTER_ROUTER inproc 64B`: `3261.50 Kmsg/s` vs `2444.50 Kmsg/s`, `-25.05%`
  - public/raw guardrail
    - public `PAIR tcp/inproc`: `-27.93%` / `-22.52%`
    - public `DEALER_DEALER tcp/inproc`: `-26.16%` / `-24.15%`
    - raw `PAIR tcp/inproc`: `-16.56%` / `-31.94%`
    - raw `DEALER_DEALER tcp/inproc`: `-15.62%` / `-21.03%`
  - routed recv source-rid zero-elision
    - `ROUTER_ROUTER tcp 64B`: `2947.82 Kmsg/s` vs `1228.20 Kmsg/s`, `-58.34%`
    - `ROUTER_ROUTER inproc 64B`: `3660.93 Kmsg/s` vs `2435.64 Kmsg/s`, `-33.47%`
  - 원복 후 회귀
    - `test_public_inproc_multipart_send`
    - `test_router_multiple_dealers`
    - `test_router_mandatory_hwm`
    - `test_stream_send_blocking_wakeup`
    - 네 테스트 모두 다시 통과
- 유지한 변경 / 원복한 변경
  - 유지
    - `test_router_mandatory_hwm` ctest 등록
    - `test_router_send_rid_mandatory_hwm()` 추가로
      `zlink_send_rid()` mandatory-HWM regression coverage 보강
  - 원복
    - `pipe_t::write_and_flush_no_hwm_check()` helper와 ROUTER send fast path
    - direct routed recv source-rid zero-elision helper와 API-side memset 제거
- 해석
  - routed send의 prefix/HWM second-check elimination은
    `ROUTER_ROUTER tcp`를 약간만 줄였고 `inproc`도 baseline 대비 미세 개선에
    그쳤다. broad single acceptance를 설명할 정도의 축은 아니다.
  - routed recv source-rid zero-fill 제거는 zlink 절대 throughput 기준으로도
    거의 움직이지 않았고 결과는 오히려 더 흔들렸다.
  - 따라서 current `ROUTER` 잔여 gap은 prefix/HWM recheck나
    source-rid zero-fill 같은 micro-elision보다 더 큰 routed/public
    differential에서 찾아야 한다.
- 다음 iteration 우선순위
  - 이번에 유지한 것은 ROUTER mandatory-HWM regression surface 강화뿐이다.
  - guide 순서대로 `pipe`/publication 축을 더 보고,
    그다음 `ROUTER_ROUTER` routed/public differential을 다시 좁힌다.

## 30. 2026-03-28 `xwrite_activated()` delivery-ready refresh 제거 로그

- 작업한 가설
  - `XPUB` / `XSUB`의 delivery-ready count는 attach/subscribe/terminate에서
    실제로 바뀌고, `xwrite_activated()`는 current ready-count 정의를
    직접 바꾸지 않는다.
  - 따라서 `xwrite_activated()`에서 반복하는
    `refresh_delivery_ready_state()`를 빼면
    `PUBSUB` publication/wakeup path의 불필요한 recompute를 조금 더 줄일 수
    있다고 봤다.
- 수정한 파일 경로
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
  - [`xsub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.cpp)
  - 위 두 파일은 bench A/B 확인 뒤 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_public_inproc_multipart_send)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_skip_delivery_ready_refresh_on_write_activated`
  - 원복 후 `cmake --build core/build -j$(nproc)`
  - 원복 후 `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_public_inproc_multipart_send)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_045824_codex_20260328_pubsub_skip_delivery_ready_refresh_on_write_activated.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_045824_codex_20260328_pubsub_skip_delivery_ready_refresh_on_write_activated.txt)
- 핵심 수치
  - `PUBSUB tcp 64B`: `3297.58 Kmsg/s` vs `2397.06 Kmsg/s`, `-27.31%`
  - `PUBSUB inproc 64B`: `3915.21 Kmsg/s` vs `2156.23 Kmsg/s`, `-44.93%`
  - 회귀 테스트
    - `test_monitor_socket_contract`
    - `test_monitor_perf_contract`
    - `test_multi_socket_contract_regressions`
    - `test_public_inproc_multipart_send`
    - `test_pubsub_filter_xpub`
    - 후보 적용 상태와 원복 상태 모두 다시 통과
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `XPUB` / `XSUB` `xwrite_activated()` delivery-ready refresh 제거
- 해석
  - write re-activation에서 ready-count recompute를 빼는 것만으로는
    current `PUBSUB` 잔여 gap을 줄이지 못했고, single acceptance는
    baseline보다 오히려 악화됐다.
  - 즉 current `PUBSUB` 잔여 gap은
    `xwrite_activated()` monitor-ready refresh 하나를 지우는 수준보다
    더 큰 publication/wakeup differential에 있다.
- 다음 iteration 우선순위
  - 이 후보는 다시 시도하지 않는다.
  - guide 순서대로 `pipe`/publication 축의 다른 실제 cost 후보를 다시 본다.

## 31. 2026-03-28 `dist` 전용 non-recursive HWM check 로그

- 작업한 가설
  - generic `check_hwm_locked()` rollout은 `PAIR`/raw guardrail 때문에
    유지하지 못했지만, 실제 publication hot path는 `dist_t::write_at()` 쪽이다.
  - `PUBSUB` path에만 한정해 `pipe`의 recursive HWM self-reentry를 줄이면
    generic rollout의 부작용 없이 publication cost를 더 줄일 수 있다고 봤다.
- 수정한 파일 경로
  - [`pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - [`dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_public_inproc_multipart_send)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_no_recursive_hwm`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_no_recursive_hwm_rerun`
  - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_no_recursive_hwm_broader_single`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_no_recursive_hwm_broader_single_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_050237_codex_20260328_pubsub_dist_no_recursive_hwm.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_050237_codex_20260328_pubsub_dist_no_recursive_hwm.txt)
  - [`perf_linux_20260328_050313_codex_20260328_pubsub_dist_no_recursive_hwm_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_050313_codex_20260328_pubsub_dist_no_recursive_hwm_rerun.txt)
  - [`perf_linux_20260328_050417_codex_20260328_pubsub_dist_no_recursive_hwm_broader_single.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_050417_codex_20260328_pubsub_dist_no_recursive_hwm_broader_single.txt)
  - [`perf_linux_20260328_050636_codex_20260328_pubsub_dist_no_recursive_hwm_broader_single_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_050636_codex_20260328_pubsub_dist_no_recursive_hwm_broader_single_rerun.txt)
  - multi smoke는 Python runner가 새 report 파일을 남기지 않았고 콘솔 결과만 확인했다.
- 핵심 수치
  - isolated first run
    - `PUBSUB tcp 64B`: `3222.16 Kmsg/s` vs `2392.07 Kmsg/s`, `-25.76%`
    - `PUBSUB inproc 64B`: `3862.32 Kmsg/s` vs `2321.95 Kmsg/s`, `-39.88%`
  - isolated rerun
    - `PUBSUB tcp 64B`: `3400.83 Kmsg/s` vs `2738.29 Kmsg/s`, `-19.48%`
    - `PUBSUB inproc 64B`: `3724.72 Kmsg/s` vs `2260.46 Kmsg/s`, `-39.31%`
  - multi smoke `pubsub tcp 64B`
    - `5211.95 Kmsg/s` vs `4344.38 Kmsg/s`, `-16.65%`
  - broader single rerun acceptance
    - `PAIR tcp/inproc`: `-18.89%` / `-17.22%`
    - `PUBSUB tcp/inproc`: `-23.63%` / `-39.84%`
    - `DEALER_DEALER tcp/inproc`: `-24.09%` / `-27.90%`
    - `DEALER_ROUTER tcp/inproc`: `-27.28%` / `-27.07%`
    - `ROUTER_ROUTER tcp/inproc`: `-54.97%` / `-30.77%`
  - 회귀 테스트
    - `test_monitor_socket_contract`
    - `test_monitor_perf_contract`
    - `test_multi_socket_contract_regressions`
    - `test_public_inproc_multipart_send`
    - `test_pubsub_filter_xpub`
    - 모두 통과
- 유지한 변경 / 원복한 변경
  - 유지
    - `pipe`의 non-recursive HWM helper를 `dist` publication path에만 적용
  - 원복
    - 없음
- 해석
  - generic helper rollout은 broad win이 아니었지만,
    `dist` 전용 좁은 적용은 isolated first/rerun, multi `pubsub`,
    broader single rerun까지 current accepted baseline을 넘겼다.
  - 따라서 current accepted 해석은
    "`pipe` self-reentry 제거를 전역으로 밀자"가 아니라
    "`PUBSUB` publication path에서만 실제로 반복되는 lock 안 work를 줄이자"다.
- 다음 iteration 우선순위
  - 이 delta는 유지한다.
  - 다음은 guide 순서대로 남은 send-side lifecycle/backpressure 공통 축을
    다시 보고, 그다음 `PUBSUB`의 publication/distribution 잔여 gap과
    `ROUTER_ROUTER` 전용 differential을 이어서 줄인다.

## 29. 2026-03-28 `XPUB` no-monitor delivery-ready tracking gate 로그

- 가설
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)의
    delivery-ready ready-count recompute는 monitor가 없는 기본 bench에서도
    계속 돌고 있다.
  - monitor가 붙지 않은 steady-state에서는 이 recompute를 건너뛰고,
    monitor open 시 현재 count를 한 번 priming하면
    `PUBSUB` publication/wakeup cost를 더 줄일 수 있다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
    - [`core/src/sockets/socket_base_monitor.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_monitor.cpp)
    - [`core/src/sockets/xpub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.hpp)
    - [`core/src/sockets/xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
    - [`core/tests/integration/monitoring/test_monitor_socket_contract.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_socket_contract.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_monitor_perf_contract|test_monitor_enhanced|test_multi_socket_contract_regressions|test_pubsub_filter_xpub)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_monitor_tracking_gate`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_monitor_tracking_gate_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_052420_codex_20260328_pubsub_monitor_tracking_gate.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_052420_codex_20260328_pubsub_monitor_tracking_gate.txt)
  - [`perf_linux_20260328_052446_codex_20260328_pubsub_monitor_tracking_gate_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_052446_codex_20260328_pubsub_monitor_tracking_gate_rerun.txt)
- 핵심 수치
  - isolated first run
    - `PUBSUB tcp 64B`: `3252.39 Kmsg/s` vs `2383.49 Kmsg/s`, `-26.72%`
    - `PUBSUB inproc 64B`: `3857.46 Kmsg/s` vs `2394.57 Kmsg/s`, `-37.92%`
  - isolated rerun
    - `PUBSUB tcp 64B`: `3191.25 Kmsg/s` vs `2325.76 Kmsg/s`, `-27.12%`
    - `PUBSUB inproc 64B`: `3861.36 Kmsg/s` vs `2170.53 Kmsg/s`, `-43.79%`
  - accepted baseline 대비
    - current accepted broader single `PUBSUB tcp/inproc`: `-23.63% / -39.84%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - no-monitor `XPUB` delivery-ready tracking gate
    - monitor-open ready-count priming hook
    - late-open snapshot regression test도 함께 원복
- 해석
  - monitor가 없는 경로에서 delivery-ready recompute를 줄여도
    isolated first/rerun이 accepted baseline보다 모두 나빠졌다.
  - 즉 current `PUBSUB` 잔여 gap은
    "monitor가 없는데도 ready-count를 계산하는 steady-state bookkeeping" 하나로
    설명되지 않는다.
  - `monitor-ready bookkeeping`은 여전히 secondary고,
    실제 우선순위는 `pipe` publication/order와 wakeup differential 쪽으로 둔다.
- 다음 iteration 우선순위
  - 이 gate는 rejected candidate로 유지한다.
  - guide 순서대로 다른 `pipe`/publication actual cost 후보를 본다.

## 30. 2026-03-28 `lb.cpp` one-active-pipe no-recursive HWM helper 로그

- 가설
  - [`lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)의
    one-active-pipe `DEALER` fast path는 여전히
    `pipe::write()/write_and_flush()` 안에서 recursive `check_hwm()` 재진입을 탄다.
  - generic rollout은 rejected candidate였지만, `DEALER` one-pipe fast path에만
    `write_no_recursive_hwm_check()` /
    `write_and_flush_no_recursive_hwm_check()`를 쓰면
    `DEALER_DEALER` / `DEALER_ROUTER` send cost를 더 줄일 수 있다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/sockets/lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc) --target libzlink comp_zlink_dealer_dealer comp_zlink_dealer_router`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_router_multiple_dealers|test_multi_socket_contract_regressions)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_lb_no_recursive`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_lb_no_recursive_rerun`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_router_lb_no_recursive`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_router_lb_no_recursive_rerun`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_lb_no_recursive_guardrail_public_serial`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_053159_codex_20260328_dealer_lb_no_recursive.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_053159_codex_20260328_dealer_lb_no_recursive.txt)
  - [`perf_linux_20260328_053222_codex_20260328_dealer_lb_no_recursive_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_053222_codex_20260328_dealer_lb_no_recursive_rerun.txt)
  - [`perf_linux_20260328_053249_codex_20260328_dealer_router_lb_no_recursive.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_053249_codex_20260328_dealer_router_lb_no_recursive.txt)
  - [`perf_linux_20260328_053315_codex_20260328_dealer_router_lb_no_recursive_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_053315_codex_20260328_dealer_router_lb_no_recursive_rerun.txt)
  - [`perf_linux_20260328_053439_codex_20260328_lb_no_recursive_guardrail_public_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_053439_codex_20260328_lb_no_recursive_guardrail_public_serial.txt)
- 핵심 수치
  - `DEALER_DEALER` isolated first/rerun
    - `tcp/inproc`: `-14.30% / -33.01%`, `-24.41% / -23.27%`
  - `DEALER_ROUTER` isolated first/rerun
    - `tcp/inproc`: `-25.76% / -32.27%`, `-19.09% / -25.35%`
  - public serial guardrail
    - `PAIR tcp/inproc`: `-23.95% / -31.30%`
    - `DEALER_DEALER tcp/inproc`: `-27.78% / -18.05%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `lb.cpp` one-active-pipe no-recursive HWM helper
- 해석
  - narrow dealer-path helper는 isolated run에서 `DEALER_ROUTER` / `DEALER_DEALER`
    일부 transport를 당겼지만, serial public guardrail에서 `PAIR`가 즉시
    `-23.95% / -31.30%`로 무너졌다.
  - 즉 current accepted `lb.cpp` one-active-pipe fast path 위에
    no-recursive HWM helper를 덧대는 현재 형태는 broad win이 아니다.
  - `pipe` self-reentry cost 축은 여전히 살아 있지만,
    `DEALER` one-pipe send path에만 helper를 꽂는 현재 형태도 rejected candidate로 둔다.
- 다음 iteration 우선순위
  - current accepted `lb.cpp` one-active-pipe fast path만 유지한다.
  - guide 순서대로 다른 `pipe`/publication actual cost 후보를 본다.

## 31. 2026-03-28 `PAIR` final-part no-recursive HWM helper 로그

- 가설
  - generic `check_hwm_locked()` rollout은 raw/public guardrail 때문에
    keep되지 못했지만, [`pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp)
    의 final single-part send는 `pipe::write_and_flush()`만 타므로
    final-part path에만 `write_and_flush_no_recursive_hwm_check()`를 쓰면
    `PAIR` send cost를 더 좁게 줄일 수 있다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/sockets/pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc) --target libzlink comp_zlink_pair`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_public_inproc_multipart_send)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_no_recursive_flush`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_no_recursive_flush_rerun`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_no_recursive_flush_guardrail_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_no_recursive_flush_guardrail_raw`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_054250_codex_20260328_pair_no_recursive_flush.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_054250_codex_20260328_pair_no_recursive_flush.txt)
  - [`perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_rerun.txt)
  - [`perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_guardrail_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_guardrail_public.txt)
  - [`perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_guardrail_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_guardrail_raw.txt)
- 핵심 수치
  - `PAIR` isolated first/rerun
    - `tcp/inproc`: `-8.49% / -18.39%`, `-11.64% / -21.18%`
  - public serial guardrail
    - `PAIR tcp/inproc`: `-16.22% / -13.64%`
    - `DEALER_DEALER tcp/inproc`: `-8.22% / -31.36%`
  - raw serial guardrail
    - `PAIR tcp/inproc`: `-7.44% / -33.95%`
    - `DEALER_DEALER tcp/inproc`: `-19.70% / -30.57%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pair.cpp` final-part `write_and_flush_no_recursive_hwm_check()` 적용
- 해석
  - isolated first run에서는 `PAIR tcp 64B`가 `-8.49%`까지 회복돼
    candidate처럼 보였지만, rerun에서 `PAIR inproc 64B`가 `-21.18%`로
    다시 흔들렸다.
  - 더 중요한 건 serial guardrail에서 `DEALER_DEALER inproc 64B` public이
    `-31.36%`, raw가 `-30.57%`로 크게 무너졌다는 점이다.
  - 즉 final-part helper를 `PAIR`에만 좁게 꽂아도 broad win이 아니라
    `inproc`/guardrail 변동을 키웠다. current code에는 남기지 않는다.
- 다음 iteration 우선순위
  - `PAIR` final-part no-recursive helper도 rejected candidate로 유지한다.
  - guide 순서대로 다른 `pipe`/publication actual cost 후보를 계속 본다.

## 32. 2026-03-28 `XPUB` prechecked no-HWM-recheck 로그

- 가설
  - current accepted `dist` delta 뒤에도 `XPUB nodrop` path는
    `_dist.check_hwm()` precheck를 통과한 뒤 `dist::write_at()`에서
    다시 HWM을 확인한다.
  - normal matching path에서만 prechecked send helper로 second HWM check를
    줄이면 `PUBSUB` publication cost를 더 낮추고, 특히 single/multi `tcp`
    회복을 넓힐 수 있다고 봤다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/src/sockets/dist.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.hpp)
    - [`core/src/sockets/dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
    - [`core/src/sockets/xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc) --target libzlink comp_zlink_pubsub`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_public_inproc_multipart_send)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_prechecked_no_hwm_recheck`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_prechecked_no_hwm_recheck_rerun`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_prechecked_no_hwm_recheck_guardrail_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_prechecked_no_hwm_recheck_guardrail_raw`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_055013_codex_20260328_pubsub_prechecked_no_hwm_recheck.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_055013_codex_20260328_pubsub_prechecked_no_hwm_recheck.txt)
  - [`perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_rerun.txt)
  - [`perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_guardrail_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_guardrail_public.txt)
  - [`perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_guardrail_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_guardrail_raw.txt)
- 핵심 수치
  - `PUBSUB` isolated first/rerun
    - `tcp/inproc`: `-21.70% / -35.47%`, `-19.65% / -41.46%`
  - public serial guardrail
    - `PAIR tcp/inproc`: `-13.42% / -17.59%`
    - `DEALER_DEALER tcp/inproc`: `-18.56% / -19.78%`
  - raw serial guardrail
    - `PAIR tcp/inproc`: `-12.00% / -22.52%`
    - `DEALER_DEALER tcp/inproc`: `-22.70% / -21.25%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `XPUB` prechecked no-HWM-recheck helper 전부
- 해석
  - first run에서는 accepted baseline 대비 `PUBSUB tcp/inproc`가 둘 다 좋아져
    candidate처럼 보였다.
  - 하지만 clean rerun에서 `PUBSUB inproc 64B`가 `-41.46%`로 다시 내려가
    current accepted baseline `-39.84%`보다 나빠졌다.
  - raw/public guardrail 자체는 무너지지 않았지만, single accepted baseline을
    stable하게 넘지 못했으므로 broader single이나 multi smoke로 올릴 이유가
    없었다.
  - 따라서 current `PUBSUB` 잔여 gap을 "`nodrop precheck 뒤 second HWM check`"
    하나로 설명하긴 이르며, current code에는 남기지 않는다.
- 다음 iteration 우선순위
  - `XPUB` prechecked no-HWM-recheck도 rejected candidate로 유지한다.
  - guide 순서대로 다른 `pipe`/publication actual cost 후보를 본다.

## 33. 2026-03-28 `XPUB` all-attached empty-prefix `send_to_all()` v2 로그

- 작업한 가설
  - earlier `XPUB` all-attached empty-prefix fast path는
    current accepted `dist` helper가 들어오기 전 결과였다.
  - current accepted `dist` delta 위에서
    `xpub.cpp` 내부에 empty-prefix subscribed pipe만 좁게 추적해
    all-attached일 때만 `send_to_all()`로 내리면
    `PUBSUB` single/multi `tcp` 회복을 additive하게 넓힐 수 있다고 봤다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/sockets/dist.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.hpp)
    - [`core/src/sockets/xpub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.hpp)
    - [`core/src/sockets/xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_public_inproc_multipart_send)$' -j1`
  - discarded overlap run:
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_empty_prefix_send_all_fastpath_v2`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_rerun`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_clean`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_clean_rerun`
  - keep 판정에 사용한 sequential rerun:
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_seq1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_seq2`
  - 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_public_inproc_multipart_send)$' -j1`
- 생성된 결과 파일 경로
  - discarded overlap run:
    - [`perf_linux_20260328_060145_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_060145_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2.txt)
    - [`perf_linux_20260328_060145_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_060145_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_rerun.txt)
    - [`perf_linux_20260328_060234_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_clean.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_060234_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_clean.txt)
    - [`perf_linux_20260328_060234_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_clean_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_060234_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_clean_rerun.txt)
  - keep 판정에 사용한 sequential rerun:
    - [`perf_linux_20260328_060304_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_seq1.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_060304_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_seq1.txt)
    - [`perf_linux_20260328_060332_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_seq2.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_060332_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_seq2.txt)
- 핵심 수치
  - discarded overlap run은 build/bench 동시 실행과 parallel rerun이 섞여
    판정에 사용하지 않았다.
  - sequential seq1
    - `PUBSUB tcp/inproc 64B`: `-25.77%` / `-40.89%`
  - sequential seq2
    - `PUBSUB tcp/inproc 64B`: `-23.12%` / `-40.39%`
  - current accepted baseline
    - broader single `PUBSUB tcp/inproc 64B`: `-23.63%` / `-39.84%`
  - 회귀 테스트
    - `test_monitor_socket_contract`
    - `test_monitor_perf_contract`
    - `test_multi_socket_contract_regressions`
    - `test_public_inproc_multipart_send`
    - `test_pubsub_filter_xpub`
    - 원복 후 다시 모두 통과
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - current accepted `dist` delta 위
      `XPUB` all-attached empty-prefix `send_to_all()` v2
- 해석
  - current accepted `dist` helper 위에 다시 얹어도
    empty-prefix match elimination은 stable broad win이 아니었다.
  - `tcp`는 seq2에서 accepted baseline을 아주 조금 넘겼지만,
    seq1에서는 baseline보다 나빴고 `inproc`는 seq1/seq2 모두 더 나빴다.
  - 따라서 이 조합도 current code에는 남기지 않는다.
  - same idea를 helper 위치만 바꿔 반복해도 `PUBSUB` 잔여 gap의 본체를
    설명하지 못한다는 근거가 더 강해졌다.
- 다음 iteration 우선순위
  - current accepted `dist` helper는 그대로 유지한다.
  - `XPUB` all-attached empty-prefix `send_to_all()` 계열은
    current accepted delta 위에서도 rejected candidate로 둔다.
  - guide 순서대로 다른 `pipe`/publication actual cost 후보나
    `ROUTER_ROUTER` 전용 differential 정리로 이어간다.

## 34. 2026-03-28 `ROUTER` routed-data view candidate 로그

- 작업한 가설
  - `ROUTER` blocking envelope send와 `zlink_send_rid(..., multipart)`는
    routing-id frame을 `zlink_routing_id_t`로 복사하거나 prefix frame으로
    다시 조립하는 경로가 남아 있다.
  - routed transaction 첫 payload를 raw routing-id view로 직접 내려 보내면
    `ROUTER_ROUTER` public send differential을 조금 더 줄일 수 있다고 봤다.
- 수정한 파일 경로
  - 유지
    - [`core/tests/integration/test_public_inproc_multipart_send.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_public_inproc_multipart_send.cpp)
  - 실험 후 원복
    - [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
    - [`core/src/core/multipart_send_txn.hpp`](/home/hep7/project/kairos/zlink/core/src/core/multipart_send_txn.hpp)
    - [`core/src/core/multipart_send_txn.cpp`](/home/hep7/project/kairos/zlink/core/src/core/multipart_send_txn.cpp)
    - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
    - [`core/src/sockets/socket_base.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
    - [`core/src/sockets/router.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.hpp)
    - [`core/src/sockets/router.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_router_mandatory_hwm|test_router_multiple_dealers|test_multi_socket_contract_regressions|test_stream_send_blocking_wakeup|test_transport_matrix)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_routed_data_view`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_routed_data_view_rerun`
  - 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_router_mandatory_hwm|test_router_multiple_dealers|test_multi_socket_contract_regressions|test_stream_send_blocking_wakeup|test_transport_matrix)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_062033_codex_20260328_router_routed_data_view.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_062033_codex_20260328_router_routed_data_view.txt)
  - [`perf_linux_20260328_062105_codex_20260328_router_routed_data_view_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_062105_codex_20260328_router_routed_data_view_rerun.txt)
  - pushed commit: `efc97d1b0e309869790a19d413c86483a3dc74b6`
- 핵심 수치
  - first run
    - `ROUTER_ROUTER tcp 64B`: `2955.81 Kmsg/s` vs `1222.99 Kmsg/s`, `-58.62%`
    - `ROUTER_ROUTER inproc 64B`: `3472.66 Kmsg/s` vs `2429.63 Kmsg/s`, `-30.04%`
  - rerun
    - `ROUTER_ROUTER tcp 64B`: `2723.92 Kmsg/s` vs `1222.60 Kmsg/s`, `-55.12%`
    - `ROUTER_ROUTER inproc 64B`: `3430.17 Kmsg/s` vs `2433.48 Kmsg/s`, `-29.06%`
  - 원복 후 회귀
    - `test_public_inproc_multipart_send`
    - `test_router_mandatory_hwm`
    - `test_router_multiple_dealers`
    - `test_multi_socket_contract_regressions`
    - `test_stream_send_blocking_wakeup`
    - `test_transport_matrix`
    - 여섯 테스트 모두 다시 통과
- 유지한 변경 / 원복한 변경
  - 유지
    - `test_public_inproc_router_send_rid_multipart_blocking()` 추가로
      `zlink_send_rid()` multipart blocking contract 회귀 보강
  - 원복
    - raw routing-id view direct send helper와 routed multipart direct path 전부
- 해석
  - `inproc`는 약간 회복됐지만 `tcp`는 first run에서 baseline보다 더 나빴고
    rerun도 사실상 baseline 수준에 머물렀다.
  - 따라서 current `ROUTER` 잔여 gap을 routing-id frame copy/elision이나
    multipart first-payload direct routed send 하나로 설명하긴 어렵다.
  - candidate는 keep-worthy stable broad win이 아니므로 current code에는 남기지 않는다.
- 다음 iteration 우선순위
  - retained regression만 가지고 간다.
  - guide 순서대로 다른 `PUBSUB` publication 축이나
    더 큰 `ROUTER_ROUTER` public/aggregate differential을 본다.

## 35. 2026-03-28 `dist.cpp` single-pipe match/activated bookkeeping fast path 로그

- 작업한 가설
  - current accepted `dist` delta는 single-subscriber send/write 쪽은 줄였지만,
    `dist_t::match()`와 `activated()`는 single-pipe steady-state에서도
    여전히 `_pipes.index()`와 swap bookkeeping을 돈다.
  - matching pipe가 하나뿐인 `PUBSUB` steady-state에서
    이 bookkeeping을 좁게 줄이면 publication/wakeup differential을
    추가로 더 줄일 수 있다고 봤다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/sockets/dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_single_pipe_bookkeeping`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_single_pipe_bookkeeping_rerun`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_single_pipe_bookkeeping_seq3`
  - 원복 후
    - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_063622_codex_20260328_pubsub_dist_single_pipe_bookkeeping.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_063622_codex_20260328_pubsub_dist_single_pipe_bookkeeping.txt)
  - [`perf_linux_20260328_063651_codex_20260328_pubsub_dist_single_pipe_bookkeeping_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_063651_codex_20260328_pubsub_dist_single_pipe_bookkeeping_rerun.txt)
  - [`perf_linux_20260328_063722_codex_20260328_pubsub_dist_single_pipe_bookkeeping_seq3.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_063722_codex_20260328_pubsub_dist_single_pipe_bookkeeping_seq3.txt)
- 핵심 수치
  - sequential seq1
    - `PUBSUB tcp 64B`: `3430.55 Kmsg/s` vs `2579.34 Kmsg/s`, `-24.81%`
    - `PUBSUB inproc 64B`: `3756.54 Kmsg/s` vs `2125.68 Kmsg/s`, `-43.41%`
  - sequential seq2
    - `PUBSUB tcp 64B`: `3304.52 Kmsg/s` vs `2554.09 Kmsg/s`, `-22.71%`
    - `PUBSUB inproc 64B`: `3786.45 Kmsg/s` vs `2473.61 Kmsg/s`, `-34.67%`
  - sequential seq3
    - `PUBSUB tcp 64B`: `3270.77 Kmsg/s` vs `2471.98 Kmsg/s`, `-24.42%`
    - `PUBSUB inproc 64B`: `4053.83 Kmsg/s` vs `2364.14 Kmsg/s`, `-41.68%`
  - current accepted baseline
    - broader single `PUBSUB tcp/inproc 64B`: `-23.63% / -39.84%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `dist.cpp` single-pipe `match()/activated()` bookkeeping fast path
- 해석
  - seq2만 보면 `tcp/inproc`가 둘 다 accepted baseline보다 좋아 보여
    candidate처럼 보였지만, seq1과 seq3가 다시 baseline 아래로 내려갔다.
  - 즉 current accepted `dist` helper 위에 single-pipe bookkeeping만 더 줄여도
    stable broad win은 아니었다.
  - 따라서 current `PUBSUB` 잔여 gap을 `match()/activated()` index/swap
    bookkeeping 하나로 설명하진 않는다.
- 다음 iteration 우선순위
  - current accepted `dist` helper만 유지한다.
  - guide 순서대로 다른 `pipe`/publication actual cost 후보나
    더 큰 `ROUTER_ROUTER` public/aggregate differential을 본다.

## 36. 2026-03-28 `dist.cpp` final-part same-thread `send_activate_read()` inline wakeup 로그

- 작업한 가설
  - current accepted `dist` helper는 final-part `write_and_flush`까지는 줄였지만,
    same-thread `PUBSUB inproc`에서는 flush 뒤 `send_activate_read()`가 여전히
    mailbox를 한 번 더 돈다.
  - `dist` final-part 경로에 한해 same-thread `activate_read` wakeup을 inline으로
    전달하면 `PUBSUB` publication/wakeup differential을 더 줄일 수 있다고 봤다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/src/sockets/dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_same_thread_activate_read`
  - 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_064859_codex_20260328_pubsub_dist_same_thread_activate_read.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_064859_codex_20260328_pubsub_dist_same_thread_activate_read.txt)
- 핵심 수치
  - `PUBSUB tcp 64B`: `3253.34 Kmsg/s` vs `2417.16 Kmsg/s`, `-25.70%`
  - `PUBSUB inproc 64B`: `3761.90 Kmsg/s` vs `2163.65 Kmsg/s`, `-42.49%`
  - current accepted baseline:
    `PUBSUB tcp/inproc 64B -23.63% / -39.84%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `dist` final-part same-thread inline `activate_read` wakeup 전부
- 해석
  - same-thread mailbox bounce를 줄여도 `tcp`, `inproc` 모두 accepted baseline보다
    더 나빠졌다.
  - 따라서 current `PUBSUB` 잔여 gap을
    `send_activate_read()` same-thread publication 하나로 설명하진 않는다.
  - 이 candidate는 current accepted `dist` helper 위 keep-worthy broad win이
    아니므로 current code에는 남기지 않는다.
- 다음 iteration 우선순위
  - current accepted `dist` helper만 유지한다.
  - `PUBSUB`은 같은 `activate_read` direct wakeup 계열을 다시 시도하지 않는다.
  - guide 순서대로 다른 `pipe`/publication actual cost 후보나
    `ROUTER_ROUTER` public/aggregate differential을 본다.

## 37. 2026-03-28 랄프루프 pivot 메모

- 상태
  - 2026-03-28 오전 루프는 `PUBSUB` / `ROUTER` pattern-specific 미세 후보를
    여러 개 연속으로 시도했지만 stable broad win을 만들지 못했다.
  - 이 상태는 "원인을 더 좁히지 않은 채 local tweak search에 갇힌 상태"로 본다.
- pivot 근거
  - recent rejected cluster:
    - `XPUB` prechecked no-HWM-recheck
    - `dist.cpp` single-pipe `match()/activated()` bookkeeping fast path
    - `dist.cpp` final-part same-thread `send_activate_read()` inline wakeup
    - `ROUTER` routed-data view candidate
  - 즉 current `PUBSUB` / `ROUTER` 잔여 gap을 local `dist/xpub/pipe/router`
    bookkeeping 하나로 설명하는 방향은 현재까지 broad win을 못 만들었다.
- 추가 probe:
    - [`perf_linux_20260328_070416_codex_20260328_pubsub_nodrop_off_probe.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_070416_codex_20260328_pubsub_nodrop_off_probe.txt)
    - `XPUB_NODROP=0`
    - `PUBSUB tcp 64B`: `491.75 Kmsg/s` vs `1220.95 Kmsg/s`, `+148.29%`
    - `PUBSUB inproc 64B`: `2435.37 Kmsg/s` vs `2150.38 Kmsg/s`, `-11.70%`
  - 이 probe는 현재 `PUBSUB` gap의 큰 축이 local `dist` bookkeeping보다
    `NODROP/HWM/backpressure semantics`일 가능성이 훨씬 크다는 뜻이다.
  - 다만 이 probe는 동일 조건 `libzmq` 비교를 대신하는 acceptance 결과가 아니다.
    즉 `XPUB_NODROP=0/1`은 원인 분리용 진단 probe로만 사용하고,
    최종 성능 판정은 default benchmark 조건으로만 한다.
- 현재 판정
  - `PAIR` / `DEALER` 공통 hot path 개선 방향 자체는 유지한다.
  - 하지만 `PUBSUB`는 더 이상 `dist/xpub/pipe publication` 미세 후보를
    먼저 파면 안 된다.
  - 먼저 semantic/backpressure map을 다시 만들고,
    그 이후에만 code optimization으로 내려가야 한다.
- 다음 iteration 시작 규칙
  - 다음 `PUBSUB` iteration은 아래 셋 중 하나로만 시작한다.
    - `XPUB_NODROP=1/0` 비교
    - HWM 변화 비교
    - single / multi semantic 차이 확인
  - 위 분리 없이 `dist.cpp` / `xpub.cpp` / `pipe publication` 미세 패치를
    다시 시작하지 않는다.

## 38. 2026-03-28 PUBSUB semantic/backpressure map 완료 로그

- 작업한 가설 1개
  - current `PUBSUB` 잔여 gap의 본체는 single-subscriber `dist` helper 추가가
    아니라, default HWM + `XPUB_NODROP=1` publication/backpressure
    differential이다.
- candidate family 1개
  - semantic probe
- high-leverage / semantic probe 근거
  - 가이드 규칙대로 `XPUB_NODROP=0` sign flip 여부와 HWM, single/multi 분리를
    먼저 찍지 않으면 local `dist/xpub/pipe` helper를 다시 파는 drift가 된다.
- 참고한 `libzmq` 대응 파일
  - [`src/xpub.cpp`](/home/hep7/project/kairos/libzmq/src/xpub.cpp)
  - [`src/dist.cpp`](/home/hep7/project/kairos/libzmq/src/dist.cpp)
  - [`src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언
  - consult 수행.
  - drift는 이미 pivot으로 복귀한 상태이고,
    가장 가능성 높은 broad hypothesis는
    default HWM + `XPUB_NODROP=1` backpressure retry/publication differential이라는
    advisory를 받았다.
  - `EAGAIN` 빈도 probe 제안도 받았지만, 이번 iteration은 guide 범위를 우선해
    bench 계측 추가 없이 semantic map 결과 정리까지로 닫았다.
- 수정한 파일 경로
  - 없음. semantic probe iteration으로 유지.
- 실행한 명령
  - `claude --help`
  - `claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake --build core/build -j$(nproc)`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_semantic_default`
  - `PERF_SINGLE_PUBSUB_XPUB_NODROP=0 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_semantic_nodrop0`
  - `PERF_SINGLE_HWM=16 PERF_SINGLE_SNDHWM=16 PERF_SINGLE_RCVHWM=16 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_semantic_hwm16`
  - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1 | tee doc/plan/perf/logs/pubsub_semantic_20260328/multi_pubsub_default.log`
  - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 BENCH_MULTI_HWM=16 BENCH_MULTI_PUBSUB_HWM=16 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1 | tee doc/plan/perf/logs/pubsub_semantic_20260328/multi_pubsub_hwm16.log`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_072836_codex_20260328_pubsub_semantic_default.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_072836_codex_20260328_pubsub_semantic_default.txt)
  - [`perf_linux_20260328_072903_codex_20260328_pubsub_semantic_nodrop0.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_072903_codex_20260328_pubsub_semantic_nodrop0.txt)
  - [`perf_linux_20260328_072938_codex_20260328_pubsub_semantic_hwm16.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_072938_codex_20260328_pubsub_semantic_hwm16.txt)
  - [`multi_pubsub_default.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_semantic_20260328/multi_pubsub_default.log)
  - [`multi_pubsub_hwm16.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_semantic_20260328/multi_pubsub_hwm16.log)
- 핵심 수치
  - default single `PUBSUB tcp/inproc 64B`: `-27.04% / -42.08%`
  - `XPUB_NODROP=0` single `PUBSUB tcp/inproc 64B`: `+0.22% / -23.71%`
  - `HWM=16` single `PUBSUB tcp/inproc 64B`: `+9.40% / +25.10%`
  - latest multi `pubsub tcp 64B`: default `-17.24%`, `HWM=16 -20.30%`
- 유지한 변경 / 원복한 변경
  - 유지
    - current accepted `dist` helper만 유지
  - 원복
    - 없음
- 해석
  - `XPUB_NODROP=0` sign flip과 low-HWM positive single 결과는
    current `PUBSUB` gap의 큰 축이 default benchmark 조건의
    `NODROP/HWM/backpressure semantics`라는 뜻이다.
  - 반면 low-HWM multi는 default보다 더 나빠졌으므로,
    single low-HWM win을 바로 multi/general code candidate로 올리면 안 된다.
  - 따라서 다음 `PUBSUB` code 후보는 default HWM + `XPUB_NODROP=1`
    publication/backpressure differential이어야 하고,
    `dist/xpub/pipe publication` 미세 helper 반복으로 돌아가지 않는다.
- 다음 iteration 우선순위
  - `PUBSUB`는 default HWM + `XPUB_NODROP=1` publication/backpressure
    differential을 pattern-specific code candidate로 좁힌다.
  - `ROUTER_ROUTER`는 그 다음 미완료로 유지한다.

## 39. 2026-03-28 `XPUB` retry matching cache 로그

- 작업한 가설 1개
  - current `PUBSUB` 잔여 gap의 큰 축이 default HWM +
    `XPUB_NODROP=1` retry/publication differential이라면,
    같은 first-part를 `EAGAIN` 뒤 다시 보낼 때 trie rematch를 피하는
    `XPUB` retry cache가 broad win을 만들 수 있다고 봤다.
- candidate family 1개
  - pattern-specific code candidate
- high-leverage / semantic probe 근거
  - semantic map 이후 첫 실제 code candidate로,
    `dist/xpub/pipe` 미시 helper가 아니라 default retry 경로의
    repeated match work를 직접 줄이는 쪽이었다.
- 참고한 `libzmq` 대응 파일
  - [`src/xpub.cpp`](/home/hep7/project/kairos/libzmq/src/xpub.cpp)
  - [`src/dist.cpp`](/home/hep7/project/kairos/libzmq/src/dist.cpp)
  - [`src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언
  - 별도 추가 consult는 하지 않았다.
  - 바로 앞 semantic/backpressure map iteration의 advisory인
    default HWM + `XPUB_NODROP=1` retry/publication differential을
    첫 code 후보로 구현했다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/sockets/xpub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.hpp)
    - [`core/src/sockets/xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `./core/build/bin/test_xpub_nodrop`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_retry_match_cache`
  - 원복 후
    - `cmake --build core/build -j$(nproc)`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_074225_codex_20260328_pubsub_retry_match_cache.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_074225_codex_20260328_pubsub_retry_match_cache.txt)
- 핵심 수치
  - `PUBSUB tcp 64B`: `3264.20 Kmsg/s` vs `2402.47 Kmsg/s`, `-26.40%`
  - `PUBSUB inproc 64B`: `3898.33 Kmsg/s` vs `2170.58 Kmsg/s`, `-44.32%`
  - current default semantic-map baseline:
    `PUBSUB tcp/inproc 64B -27.04% / -42.08%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `XPUB` same first-part retry matching cache 전부
- 해석
  - `tcp`는 noise 범위 수준으로만 움직였고,
    `inproc`은 semantic-map baseline보다 더 악화됐다.
  - 즉 current `PUBSUB` 잔여 gap을
    "`EAGAIN` 뒤 first-part trie rematch가 너무 비싸다" 하나로
    설명하진 않는다.
  - default HWM + `XPUB_NODROP=1` retry/publication differential이라는
    상위 해석은 유지하되, same first-part retry cache는 keep-worthy broad win이
    아니므로 current code에는 남기지 않는다.
- 다음 iteration 우선순위
  - `PUBSUB`는 retry cache 같은 local rematch elision 반복으로
    다시 내려가지 않는다.
  - 같은 미완료 항목 안에서 default HWM + `XPUB_NODROP=1`
    publication/backpressure differential의 다른 code candidate를 찾는다.

## 40. 2026-03-28 `activate_write` mailbox 정렬 로그

- 작업한 가설 1개
  - zlink의 [`object_t::send_activate_write()`](/home/hep7/project/kairos/zlink/core/src/core/object.cpp)
    는 same-thread에서 direct `process_command()`를 쓰고,
    libzmq는 [`object.cpp`](/home/hep7/project/kairos/libzmq/src/object.cpp)
    에서 같은 경우에도 mailbox command를 보낸다.
  - blocking `send()`가 실제로 기다리는 채널은 `process_commands()`의 mailbox이므로,
    `activate_write`를 mailbox 경로로 맞추면 default HWM +
    `XPUB_NODROP=1` wakeup consumption differential을 줄일 수 있다고 봤다.
- candidate family 1개
  - pattern-specific code candidate
- high-leverage / semantic probe 근거
  - tiny helper 추가가 아니라, current `PUBSUB` 잔여 gap과 직접 연결된
    wakeup publication/consumption semantic diff를 맞추는 후보였다.
- 참고한 `libzmq` 대응 파일
  - [`src/object.cpp`](/home/hep7/project/kairos/libzmq/src/object.cpp)
  - [`src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언
  - 별도 consult는 시도하지 않았다.
  - 기존 semantic-map 결론과 code diff 대조를 근거로 바로 후보를 올렸다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/core/object.cpp`](/home/hep7/project/kairos/zlink/core/src/core/object.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_stream_send_blocking_wakeup)$' -j1`
    - `./core/build/bin/test_xpub_nodrop`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_activate_write_mailbox`
  - 원복 후
    - `cmake --build core/build -j$(nproc)`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_075445_codex_20260328_pubsub_activate_write_mailbox.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_075445_codex_20260328_pubsub_activate_write_mailbox.txt)
- 핵심 수치
  - `PUBSUB tcp 64B`: `3265.20 Kmsg/s` vs `2389.72 Kmsg/s`, `-26.81%`
  - `PUBSUB inproc 64B`: `3691.67 Kmsg/s` vs `2086.95 Kmsg/s`, `-43.47%`
  - current default semantic-map baseline:
    `PUBSUB tcp/inproc 64B -27.04% / -42.08%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - same-thread `activate_write` mailbox 정렬 전부
- 해석
  - `tcp`는 사실상 noise 수준이었고,
    `inproc`은 semantic-map baseline보다 더 나빠졌다.
  - 즉 current `PUBSUB` 잔여 gap을
    "`activate_write` same-thread publication channel mismatch" 하나로
    설명하진 않는다.
  - wakeup differential의 상위 해석 자체는 유지하되,
    mailbox 정렬만으로는 keep-worthy broad win이 아니므로 current code에는
    남기지 않는다.
- 다음 iteration 우선순위
  - `PUBSUB`는 wakeup publication 경로 하나만 libzmq처럼 맞추는 식의
    local semantic patch로 다시 내려가지 않는다.
  - 같은 미완료 항목 안에서 blocked sender가 wakeup을 소비하는
    retry/publication cost를 더 직접 건드리는 다음 후보를 찾는다.

## 41. 2026-03-28 `PUBSUB` HWM sweep + `claude` priority rewrite 로그

- 작업한 가설 1개
  - semantic-map 이후 `retry cache`와 `activate_write` 정렬이 연속으로
    rejected 됐으므로, 다음 `PUBSUB` iteration은 code patch가 아니라
    default HWM + `XPUB_NODROP=1` 축이 여전히 맞는지 다시 좁혀야 한다.
- candidate family 1개
  - priority rewrite / semantic probe
- high-leverage / semantic probe 근거
  - guide 6.3 규칙상 같은 family의 local tweak 두 개가 연속 rejected 됐으면
    다음 iteration은 raw/public 재분리, semantic probe, 우선순위 재작성 중
    하나여야 한다.
  - 따라서 다시 `xpub/dist/pipe` helper를 추가하기 전에
    HWM 민감도와 current drift 여부를 먼저 다시 확인했다.
- 참고한 `libzmq` 대응 파일
  - [`src/xpub.cpp`](/home/hep7/project/kairos/libzmq/src/xpub.cpp)
  - [`src/dist.cpp`](/home/hep7/project/kairos/libzmq/src/dist.cpp)
  - [`src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
- `claude` consult 여부와 핵심 조언
  - consult 수행.
  - drift는 semantic-map 이후 `PUBSUB` local helper 탐색 쪽으로 다시
    빨려 들어갈 위험이 크고, current 상위 가설은 여전히
    default HWM + `XPUB_NODROP=1` retry/publication cost라는 advisory를 받았다.
  - 다만 `inproc`가 계속 `tcp`보다 훨씬 큰 gap을 보이므로,
    next step은 또 다른 micro helper보다
    validation surface와 `pipe`/inproc differential을 먼저 고정해야 한다는
    risk도 같이 받았다.
- 수정한 파일 경로
  - 없음. semantic probe / priority rewrite iteration으로 유지.
- 실행한 명령
  - `printf '...' | claude -p --input-format text --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `PERF_SINGLE_HWM=16 PERF_SINGLE_SNDHWM=16 PERF_SINGLE_RCVHWM=16 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_hwm_probe_16 | tee doc/plan/perf/logs/pubsub_hwm_probe_16.log`
  - `PERF_SINGLE_HWM=64 PERF_SINGLE_SNDHWM=64 PERF_SINGLE_RCVHWM=64 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_hwm_probe_64 | tee doc/plan/perf/logs/pubsub_hwm_probe_64.log`
  - `PERF_SINGLE_HWM=256 PERF_SINGLE_SNDHWM=256 PERF_SINGLE_RCVHWM=256 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_hwm_probe_256 | tee doc/plan/perf/logs/pubsub_hwm_probe_256.log`
  - `PERF_SINGLE_HWM=1000 PERF_SINGLE_SNDHWM=1000 PERF_SINGLE_RCVHWM=1000 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_hwm_probe_1000 | tee doc/plan/perf/logs/pubsub_hwm_probe_1000.log`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_080329_codex_20260328_pubsub_hwm_probe_16.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_080329_codex_20260328_pubsub_hwm_probe_16.txt)
  - [`perf_linux_20260328_080353_codex_20260328_pubsub_hwm_probe_64.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_080353_codex_20260328_pubsub_hwm_probe_64.txt)
  - [`perf_linux_20260328_080415_codex_20260328_pubsub_hwm_probe_256.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_080415_codex_20260328_pubsub_hwm_probe_256.txt)
  - [`perf_linux_20260328_080439_codex_20260328_pubsub_hwm_probe_1000.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_080439_codex_20260328_pubsub_hwm_probe_1000.txt)
  - [`pubsub_hwm_probe_16.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_hwm_probe_16.log)
  - [`pubsub_hwm_probe_64.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_hwm_probe_64.log)
  - [`pubsub_hwm_probe_256.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_hwm_probe_256.log)
  - [`pubsub_hwm_probe_1000.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_hwm_probe_1000.log)
- 핵심 수치
  - `HWM=16` single `PUBSUB tcp/inproc 64B`: `+12.34% / +5.19%`
  - `HWM=64` single `PUBSUB tcp/inproc 64B`: `-32.19% / -38.29%`
  - `HWM=256` single `PUBSUB tcp/inproc 64B`: `-22.05% / -39.03%`
  - `HWM=1000` single `PUBSUB tcp/inproc 64B`: `-28.83% / -45.87%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음. current accepted `dist` helper만 유지.
  - 원복
    - 없음
- 해석
  - low-HWM sign flip 자체는 다시 확인됐지만,
    `64 -> 256 -> 1000` sweep이 단순 monotonic backlog-cost 곡선을 만들지는
    않았다.
  - 즉 current `PUBSUB` gap은 default HWM + `XPUB_NODROP=1` 축에 민감하지만,
    그걸 바로 `xpub/dist` micro helper 하나로 환원할 상태는 아니다.
  - current next step은 또 다른 local helper보다
    validation surface와 `inproc` transport-specific differential을 먼저
    고정하는 쪽이다.
- 다음 iteration 우선순위
  - `PUBSUB`는 direct `test_xpub_nodrop` baseline/ctest surface mismatch와
    `inproc` 쪽 `pipe`/publication differential을 먼저 probe한다.
  - 그 다음에만 default HWM + `XPUB_NODROP=1` code candidate를 다시 올린다.

## 42. 2026-03-28 safe single-pipe `nodrop` fusion rejected 로그

- 작업한 가설 1개
  - single matching `PUBSUB` steady-state에서
    `XPUB_NODROP=1` precheck와 실제 write를 같은 pipe lock 아래로 합치되,
    `HWM full`에서는 current active state를 건드리지 않으면
    이전 rejected `nodrop fusion`과 달리 correctness를 유지하면서
    default HWM publication cost를 줄일 수 있다고 봤다.
- candidate family 1개
  - pattern-specific code candidate
- high-leverage / semantic probe 근거
  - semantic probe 이후 첫 code candidate로,
    rejected `retry cache`/`activate_write`와 달리
    default HWM + `XPUB_NODROP=1` cost를 직접 건드리는 후보였다.
- 참고한 `libzmq` 대응 파일
  - [`src/xpub.cpp`](/home/hep7/project/kairos/libzmq/src/xpub.cpp)
  - [`src/dist.cpp`](/home/hep7/project/kairos/libzmq/src/dist.cpp)
  - [`src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언
  - 별도 추가 consult는 하지 않았다.
  - 바로 앞 priority rewrite iteration의 advisory를 코드 후보로 내렸다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/src/sockets/dist.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.hpp)
    - [`core/src/sockets/dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
    - [`core/src/sockets/xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_public_inproc_multipart_send)$' -j1`
    - `./core/build/bin/test_xpub_nodrop`
    - `gdb --batch -ex run -ex bt --args ./core/build/bin/test_xpub_nodrop`
  - candidate를 single-part로 더 좁힌 뒤
    - `cmake --build core/build -j$(nproc)`
    - `./core/build/bin/test_xpub_nodrop`
    - `gdb --batch -ex run -ex bt --args ./core/build/bin/test_xpub_nodrop`
  - 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `./core/build/bin/test_xpub_nodrop`
- 생성된 결과 파일 경로
  - 없음. correctness regression으로 bench 단계 전에 중단했다.
- 핵심 수치
  - targeted ctest surface
    - `test_monitor_socket_contract`
    - `test_monitor_perf_contract`
    - `test_multi_socket_contract_regressions`
    - `test_public_inproc_multipart_send`
    - `test_pubsub_filter_xpub`
    - candidate 적용 상태에서 모두 통과
  - direct binary `test_xpub_nodrop`
    - first candidate: first case PASS 뒤
      `malloc(): unsorted double linked list corrupted`
      / `Bad address (.../xsub.cpp:68)`로 abort
    - single-part-only로 더 좁힌 candidate: first case PASS 뒤
      `Assertion failed: check () (.../msg.cpp:559)`로 abort
    - full revert 후 current baseline direct run도
      second case에서
      `Expected 0 Was 71. subscriber callback observed malformed topic/payload shape`
      로 fail
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - safe single-pipe `nodrop` fusion 전부
- 해석
  - 이 candidate family는 bench를 보기 전에 correctness에서 바로 탈락했다.
  - 특히 full revert 뒤에도 direct `test_xpub_nodrop`가 baseline에서 다시
    fail했으므로, current workspace에서는 이 binary를 sole gate로 계속 쓰기 전에
    baseline/registration mismatch를 먼저 고정해야 한다.
  - 따라서 이 후보는 keep-worthy delta가 아니며, current code에는 남기지 않는다.
- 다음 iteration 우선순위
  - `PUBSUB`는 direct `test_xpub_nodrop` baseline failure와
    ctest unregistered 상태를 먼저 정리한다.
  - 그 다음에만 `inproc` 쪽 `pipe`/publication differential probe나
    새로운 default-HWM code candidate로 넘어간다.

## 43. 2026-03-28 `PUBSUB` queue-probe report + direct `xpub_nodrop` flake 정리 로그

- 작업한 가설 1개
  - direct `test_xpub_nodrop`는 baseline failure 하나로 고정된 게 아니라
    unregistered stale diagnostic일 수 있고, current `PUBSUB inproc` 차등은
    saved report 기준 sender backlog 쪽으로 더 선명하게 잡힐 수 있다고 봤다.
- candidate family 1개
  - validation surface / transport-specific differential probe
- high-leverage / semantic probe 근거
  - guide의 현재 next step이 `test_xpub_nodrop` mismatch 정리와
    `inproc` `pipe` differential probe였고,
    기존 single comparison report는 queue probe 지표를 저장하지 않아
    같은 정보를 매번 raw terminal output으로만 재수집해야 했다.
- 수정한 파일 경로
  - [`core/bench/with_zmq/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/run_comparison.py)
- 실행한 명령
  - `ctest --test-dir core/build --output-on-failure -R '^test_pubsub_filter_xpub$' -j1`
  - `./core/build/bin/test_xpub_nodrop`
  - `LD_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib ./core/build/bin/test_xpub_nodrop`
  - `python3 -m py_compile core/bench/with_zmq/single/run_comparison.py`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_queue_probe_report | tee doc/plan/perf/logs/pubsub_queue_probe_report.log`
  - `PERF_SINGLE_PUBSUB_XPUB_NODROP=0 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_queue_probe_nodrop0 | tee doc/plan/perf/logs/pubsub_queue_probe_nodrop0.log`
  - `PERF_SINGLE_HWM=16 PERF_SINGLE_SNDHWM=16 PERF_SINGLE_RCVHWM=16 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_queue_probe_hwm16 | tee doc/plan/perf/logs/pubsub_queue_probe_hwm16.log`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_082400_codex_20260328_pubsub_queue_probe_report.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_082400_codex_20260328_pubsub_queue_probe_report.txt)
  - [`perf_linux_20260328_082433_codex_20260328_pubsub_queue_probe_nodrop0.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_082433_codex_20260328_pubsub_queue_probe_nodrop0.txt)
  - [`perf_linux_20260328_082433_codex_20260328_pubsub_queue_probe_hwm16.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_082433_codex_20260328_pubsub_queue_probe_hwm16.txt)
  - [`pubsub_queue_probe_report.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_queue_probe_report.log)
  - [`pubsub_queue_probe_nodrop0.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_queue_probe_nodrop0.log)
  - [`pubsub_queue_probe_hwm16.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_queue_probe_hwm16.log)
- 핵심 수치
  - default `PUBSUB tcp/inproc 64B`
    - throughput: `-32.75% / -40.12%`
    - `snd_pending_max`: `654 / 1450`
    - `rcv_pending_max`: `513 / 725`
  - `XPUB_NODROP=0`
    - throughput: `+0.28% / -11.63%`
    - `snd_pending_max`: `508 / 946`
    - `rcv_pending_max`: `622 / 1017`
  - `HWM=16`
    - throughput: `+4.96% / +18.29%`
    - `snd_pending_max`: `9 / 20`
    - `rcv_pending_max`: `15 / 28`
  - direct `test_xpub_nodrop`
    - 당시 ctest surface에는 없었음
    - repeated direct run은 PASS와
      `Assertion failed: check() (.../msg.cpp:559)`,
      `Bad address (.../xsub.cpp:56/68)`,
      `malloc(): corrupted top size`
      abort가 섞여 deterministic baseline이 아니었다.
- 유지한 변경 / 원복한 변경
  - 유지
    - single comparison report의 queue probe 저장
  - 원복
    - 없음
- 해석
  - current `PUBSUB inproc` 차등은 receiver latency보다
    default HWM + `XPUB_NODROP=1`에서 sender backlog가 더 크게 누적되는 쪽과
    더 잘 맞는다.
  - `XPUB_NODROP=0`와 `HWM=16` 모두 `snd_pending_max`를 크게 낮췄고,
    특히 `HWM=16`은 `tcp/inproc` 둘 다 sign flip을 만들었다.
  - direct `test_xpub_nodrop`는 이 시점 기준으로는 ctest gate가 아니라
    stale/unregistered flake diagnostic이었다.
- 다음 iteration 우선순위
  - `PUBSUB`는 sender backlog/publication 누적을 줄이는
    `pipe`/publication 축 code candidate만 다시 올린다.
  - direct `test_xpub_nodrop`는 gate로 승격하지 않고 auxiliary diagnostic으로만
    유지한다.

## 44. 2026-03-28 single-matching `XPUB_NODROP=1` one-lock helper rejected 로그

- 작업한 가설 1개
  - current `PUBSUB` single-subscriber default-HWM `XPUB_NODROP=1` 경로의 큰
    비용 축이 `_dist.check_hwm()`와 실제 `write/flush` 사이의 이중
    `_out_sync` 진입이라면, single-matching steady-state에서 이를 one-lock
    helper로 합치면 sender backlog/publication 누적을 줄일 수 있다고 봤다.
- candidate family 1개
  - pattern-specific code candidate
- high-leverage / semantic probe 근거
  - guide의 첫 미완료 항목이 default HWM + `XPUB_NODROP=1`
    publication/backpressure differential의 actual code candidate였고,
    queue-probe 결과도 sender backlog 누적이 핵심 축임을 가리켰다.
- 참고한 `libzmq` 대응 파일
  - [`src/xpub.cpp`](/home/hep7/project/kairos/libzmq/src/xpub.cpp)
  - [`src/dist.cpp`](/home/hep7/project/kairos/libzmq/src/dist.cpp)
  - [`src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언
  - consult 수행.
  - top hypothesis는 single-subscriber `NODROP` path의 double lock cost라는
    advisory를 받았다.
  - 다만 same-family `nodrop fusion`이 이미 correctness ambiguity를 만든 적이
    있으므로, targeted gate를 고정하지 않은 채 local-search drift로
    되풀이되면 안 된다는 경고도 함께 받았다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/src/sockets/dist.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.hpp)
    - [`core/src/sockets/dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
    - [`core/src/sockets/xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
- 실행한 명령
  - `claude --help`
  - `claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_single_pipe_nodrop_fused`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_single_pipe_nodrop_fused_rerun`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_single_pipe_nodrop_fused_seq3`
    - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_fused_guardrail_public`
    - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_fused_guardrail_raw`
    - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_fused_broader_single`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1 | tee doc/plan/perf/logs/pubsub_fused_20260328/multi_pubsub.log`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1 | tee doc/plan/perf/logs/pubsub_fused_20260328/multi_pubsub_rerun.log`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 3 | tee doc/plan/perf/logs/pubsub_fused_20260328/multi_pubsub_runs3.log`
  - 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_095809_codex_20260328_pubsub_single_pipe_nodrop_fused.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_095809_codex_20260328_pubsub_single_pipe_nodrop_fused.txt)
  - [`perf_linux_20260328_095843_codex_20260328_pubsub_single_pipe_nodrop_fused_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_095843_codex_20260328_pubsub_single_pipe_nodrop_fused_rerun.txt)
  - [`perf_linux_20260328_095916_codex_20260328_pubsub_single_pipe_nodrop_fused_seq3.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_095916_codex_20260328_pubsub_single_pipe_nodrop_fused_seq3.txt)
  - [`perf_linux_20260328_095957_codex_20260328_pubsub_fused_guardrail_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_095957_codex_20260328_pubsub_fused_guardrail_public.txt)
  - [`perf_linux_20260328_100037_codex_20260328_pubsub_fused_guardrail_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_100037_codex_20260328_pubsub_fused_guardrail_raw.txt)
  - [`perf_linux_20260328_100117_codex_20260328_pubsub_fused_broader_single.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_100117_codex_20260328_pubsub_fused_broader_single.txt)
  - [`multi_pubsub.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_fused_20260328/multi_pubsub.log)
  - [`multi_pubsub_rerun.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_fused_20260328/multi_pubsub_rerun.log)
  - [`multi_pubsub_runs3.log`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/pubsub_fused_20260328/multi_pubsub_runs3.log)
- 핵심 수치
  - isolated single `PUBSUB tcp/inproc 64B`
    - seq1: `-27.73% / -38.36%`
    - seq2: `-20.44% / -38.87%`
    - seq3: `-12.75% / -38.71%`
  - broader single
    - `PAIR tcp/inproc 64B`: `-15.37% / -19.27%`
    - `PUBSUB tcp/inproc 64B`: `-21.12% / -26.31%`
    - `DEALER_DEALER tcp/inproc 64B`: `-15.61% / -23.86%`
    - `DEALER_ROUTER tcp/inproc 64B`: `-19.54% / -23.27%`
    - `ROUTER_ROUTER tcp/inproc 64B`: `-58.54% / -25.23%`
  - raw/public guardrail
    - `PAIR` public→raw `tcp/inproc`: `-27.95% -> -34.15%`, `-21.32% -> -24.18%`
    - `DEALER_DEALER` public→raw `tcp/inproc`: `-30.30% -> -22.31%`, `-23.02% -> -31.53%`
  - multi `pubsub tcp 64B`
    - first: `-25.93%`
    - rerun: `-21.63%`
    - `--runs 3`: `-25.68%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - single-matching `XPUB_NODROP=1` one-lock helper 전부
- 해석
  - single과 broader single은 분명히 반응했지만, multi `pubsub tcp`가
    latest baseline `-17.24%`를 안정적으로 지키지 못했다.
  - raw/public guardrail도 `PAIR`/`DEALER_DEALER`에서 mixed여서
    send-path side effect 해석을 다시 넓혔다.
  - 따라서 이 candidate family는 keep-worthy stable broad win이 아니고,
    current code에는 남기지 않는다.
  - 한편 `core/build`를 `-DZLINK_BUILD_TESTS=ON`으로 재configure한 뒤에는
    `test_xpub_nodrop`가 ctest surface에 포함됐고 targeted ctest rerun도 통과했다.
    direct binary flake는 이제 historical auxiliary diagnostic으로만 둔다.
- 다음 iteration 우선순위
  - `PUBSUB`는 same-family one-lock fusion이나 또 다른 local
    `xpub/dist` helper 반복으로 내려가지 않는다.
  - 다음 code candidate는 default HWM multi guardrail을 깨지 않는
    sender backlog/publication differential이어야 한다.
  - targeted gate는 direct binary보다
    `test_multi_socket_contract_regressions`,
    `test_public_inproc_multipart_send`,
    `test_pubsub_filter_xpub`,
    `test_xpub_nodrop` ctest 조합을 우선 쓴다.

## 45. 2026-03-28 `pipe` HWM precheck / sparse queue-probe semantic probe 로그

- 작업한 가설 1개
  - same-family `xpub/dist` helper를 반복하지 않고도
    default-HWM `PUBSUB` backlog 비용을 줄이려면,
    `pipe::check_hwm()`의 `_out_sync` precheck 고정비나
    queue-probe 측정 surface 두께가 더 큰 축일 수 있다고 봤다.
- candidate family 1개
  - broad `pipe` precheck candidate + semantic probe
- high-leverage / semantic probe 근거
  - guide가 same-family one-lock fusion과 local `xpub/dist` helper 반복을
    금지하고 있었고, current next step을
    `inproc` transport-specific differential 또는 measurement-surface 분리로
    좁히도록 요구했다.
- 참고한 `libzmq` 대응 파일
  - [`src/xpub.cpp`](/home/hep7/project/kairos/libzmq/src/xpub.cpp)
  - [`src/dist.cpp`](/home/hep7/project/kairos/libzmq/src/dist.cpp)
  - [`src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`src/ctx.cpp`](/home/hep7/project/kairos/libzmq/src/ctx.cpp)
- `claude` consult 여부와 핵심 조언
  - `claude --help`는 성공했다.
  - 그러나 non-interactive `claude -p` advisory 호출은
    stdin prompt 전달 방식과 `timeout 20s` 재시도 둘 다 응답 없이 종료돼
    이번 iteration에서는 unavailable로 기록한다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
- 실행한 명령
  - `claude --help`
  - `timeout 20s bash -lc 'cat ... | claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink'`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_default_prepatch`
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_default_atomic_hwm`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_default_atomic_hwm_rerun`
  - 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `PERF_SINGLE_QUEUE_SAMPLE_MS=100000 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_queueprobe_sparse`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_101818_codex_20260328_pubsub_default_prepatch.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_101818_codex_20260328_pubsub_default_prepatch.txt)
  - [`perf_linux_20260328_102223_codex_20260328_pubsub_default_atomic_hwm.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_102223_codex_20260328_pubsub_default_atomic_hwm.txt)
  - [`perf_linux_20260328_102254_codex_20260328_pubsub_default_atomic_hwm_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_102254_codex_20260328_pubsub_default_atomic_hwm_rerun.txt)
  - [`perf_linux_20260328_102635_codex_20260328_pubsub_queueprobe_sparse.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_102635_codex_20260328_pubsub_queueprobe_sparse.txt)
- 핵심 수치
  - current baseline single `PUBSUB tcp/inproc 64B`
    - `-25.63% / -37.06%`
  - rejected `pipe` HWM precheck candidate
    - first: `-26.71% / -34.02%`
    - rerun: `-23.86% / -37.64%`
  - sparse queue-probe semantic run
    - `-27.48% / -35.63%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe` HWM precheck atomic candidate 전부
- 해석
  - `pipe::check_hwm()` precheck 고정비를 줄이는 broad candidate는
    `tcp`/`inproc`가 서로 엇갈려 stable broad win을 만들지 못했다.
  - 따라서 이 candidate family는 current code에 남기지 않는다.
  - 반면 sparse queue-probe run도 broad sign flip이나 큰 회복을 만들지 못해,
    current `PUBSUB` 잔여 gap의 본체를 measurement-surface probe overhead로
    설명하진 않는다.
  - 즉 current next step은 queue-sample tweak가 아니라
    default HWM + `XPUB_NODROP=1` 조건의
    `inproc` transport-specific sender backlog/publication differential이다.

## 46. 2026-03-28 `inproc PUBSUB` peer-progress publication cadence / HWM-full peer snapshot refresh 로그

- 작업한 가설 2개
  - 가설 A:
    default-HWM `inproc PUBSUB`에서 sender backlog가 과하게 쌓이는 이유가
    peer read progress publication cadence가 너무 느려서 stale credit가 오래
    유지되기 때문일 수 있다.
  - 가설 B:
    cadence 자체보다 `check_hwm()`이 cached `_peers_msgs_read`만 보고
    full로 들어가는 순간 stale peer progress를 직접 refresh하지 못하는 점이
    더 직접적인 잔여 cost일 수 있다.
- candidate family 2개
  - `inproc PUBSUB` peer-progress notify interval tighten
  - `inproc PUBSUB` HWM-full peer snapshot refresh
- high-leverage / semantic probe 근거
  - same-day semantic map에서 default HWM / `XPUB_NODROP=1` 조건의
    `snd_pending_max`가 `tcp/inproc 924 / 1560` 수준으로 컸고,
    `HWM=16` probe가 single에서 강한 회복을 보였으므로
    current differential을 "default-HWM stale progress / blocked retry 진입"
    쪽에서 먼저 좁히는 것이 타당했다.
- 수정한 파일 경로
  - 두 candidate 모두 실험 후 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/src/sockets/socket_base_endpoint.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_endpoint.cpp)
    - [`core/src/core/ctx_inproc_registry.cpp`](/home/hep7/project/kairos/zlink/core/src/core/ctx_inproc_registry.cpp)
- 실행한 명령
  - candidate A 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_inproc_progress64`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
  - candidate B 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_hwm_refresh`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
  - 두 candidate 원복 후
    - `cmake --build core/build -j$(nproc)`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_103800_codex_20260328_pubsub_inproc_progress64.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_103800_codex_20260328_pubsub_inproc_progress64.txt)
  - [`perf_linux_20260328_104042_codex_20260328_pubsub_hwm_refresh.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_104042_codex_20260328_pubsub_hwm_refresh.txt)
  - multi smoke는 두 번 모두 Python runner가 새 report 파일을 남기지 않았고
    콘솔 결과만 확인했다.
- 핵심 수치
  - candidate A: peer-progress notify interval tighten
    - single `PUBSUB tcp/inproc 64B`: `-22.42% / -37.17%`
    - queue probe `snd_pending_max`: `963 / 178`
    - queue probe `rcv_pending_max`: `189 / 1125`
    - multi `pubsub tcp 64B`: `-26.38%`
  - candidate B: HWM-full peer snapshot refresh
    - single `PUBSUB tcp/inproc 64B`: `-22.84% / -38.24%`
    - queue probe `snd_pending_max`: `918 / 1342`
    - queue probe `rcv_pending_max`: `438 / 695`
    - multi `pubsub tcp 64B`: `-22.78%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `inproc PUBSUB` peer-progress notify interval tighten candidate
    - `inproc PUBSUB` HWM-full peer snapshot refresh candidate
- 해석
  - candidate A는 sender backlog 분포는 크게 바꿨지만
    `inproc` throughput 자체는 same-day baseline `-37.06%`에서
    유의미하게 회복시키지 못했고, 대신 `rcv_pending_max`가 커져
    backlog 위치만 옮긴 형태에 가까웠다.
  - candidate B도 stale peer progress를 full-path에서 직접 refresh했지만
    `inproc`는 `-38.24%`로 더 나빠졌고, default-HWM 잔여 gap을
    "cached peer read snapshot 하나"로 설명하진 못했다.
  - 두 candidate 모두 contract regression은 없었지만
    keep-worthy broad win도 아니어서 current code에는 남기지 않는다.
  - 따라서 current next step은
    sender progress publication cadence나 stale peer snapshot refresh가 아니라,
    default HWM + `XPUB_NODROP=1` 조건에서
    `inproc` receiver drain / blocked-retry differential을
    더 직접 분리하는 쪽이다.
- 다음 iteration 우선순위
  - `PUBSUB`는 queue-probe surface tweak나 `pipe::check_hwm()` broad candidate를
    다시 반복하지 않는다.
  - next code candidate는 `inproc` 쪽 sender backlog/publication differential을
    직접 줄이는 방향으로만 올린다.

## 47. 2026-03-28 `comp_zlink_pubsub` single surface realignment 로그

- 작업한 가설
  - current `PUBSUB` single gap 해석의 선행조건은
    `comp_zlink_pubsub`와 `comp_std_zmq_pubsub`가 같은 비교 surface를
    타는 것이다.
  - 그런데 current build는 zlink 쪽만
    `perf_pubsub.cpp` topic-aware + delivery-ready monitor path를 써서
    same-day `PUBSUB` 해석이 흔들리고 있었다.
- `libzmq` reference / build wiring 확인
  - `compile_commands.json` 확인 결과
    `comp_zlink_pubsub`는
    [`core/perf/single/src/perf_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_pubsub.cpp)
    를 빌드했고,
    paired `comp_std_zmq_pubsub`는
    [`core/bench/with_zmq/single/zmq/bench_zmq_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zmq/bench_zmq_pubsub.cpp)
    를 빌드했다.
  - 즉 zlink 쪽은 `topic="bench"` +
    delivery-ready monitor gate +
    `zlink_msg_recv()` 2회,
    libzmq 쪽은 payload-only `send_exact()` + `zmq_msg_recv()` single-part였다.
- `claude` consult
  - `claude --help`는 통과했다.
  - non-interactive advisory는
    `cat <<'EOF' | claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink ...`
    를 두 번 시도했지만 응답 없이 멈춰 unavailable로 기록한다.
- 수정한 파일 경로
  - [`core/bench/with_zmq/CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/CMakeLists.txt)
  - [`core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp)
- 실행한 명령
  - surface mismatch 확인
    - `python3 - <<'PY' ... compile_commands.json ... PY`
    - `./core/build/bin/comp_zlink_pubsub zlink tcp 64`
    - `./core/build/bin/comp_zlink_pubsub zlink inproc 64`
  - surface realignment 적용 후
    - `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_surface_realign`
    - `PERF_SINGLE_PUBSUB_XPUB_NODROP=0 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_surface_realign_nodrop0`
    - `PERF_SINGLE_HWM=16 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_surface_realign_hwm16`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 BENCH_MULTI_PUBSUB_HWM=16 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_105258_codex_20260328_pubsub_surface_realign.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_105258_codex_20260328_pubsub_surface_realign.txt)
  - [`perf_linux_20260328_105409_codex_20260328_pubsub_surface_realign_nodrop0.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_105409_codex_20260328_pubsub_surface_realign_nodrop0.txt)
  - [`perf_linux_20260328_105409_codex_20260328_pubsub_surface_realign_hwm16.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_105409_codex_20260328_pubsub_surface_realign_hwm16.txt)
  - multi rerun은 Python runner가 새 report 파일을 남기지 않았고
    콘솔 결과만 확인했다.
- 핵심 수치
  - realigned default single `PUBSUB tcp/inproc 64B`
    - `-15.29% / -24.92%`
  - realigned `XPUB_NODROP=0` probe
    - `-0.00% / -0.64%`
  - realigned `HWM=16` probe
    - `-16.17% / +47.89%`
  - multi `pubsub tcp 64B`
    - default: `-29.83%`
    - `BENCH_MULTI_PUBSUB_HWM=16`: `-22.22%`
- 유지한 변경 / 원복한 변경
  - 유지
    - `comp_zlink_pubsub` source retarget
    - `bench_zlink_pubsub.cpp` receiver `zlink_msg_recv()` single-part realignment
  - 원복
    - 없음
- 해석
  - same-day `PUBSUB tcp` gap의 큰 일부는
    zlink 쪽만 더 무거운 topic-aware + monitor-gated surface를 타던
    measurement mismatch였다.
  - realignment 뒤 `PUBSUB tcp 64B`는 `-27%`대에서 `-15.29%`까지 바로 회복됐다.
  - 그러나 `XPUB_NODROP=0`이 여전히 거의 sign flip을 만들고,
    default multi는 `-29.83%`로 크게 남는다.
  - 즉 low-HWM single win이나 surface mismatch correction만으로는
    current `PUBSUB` 잔여 gap을 끝낼 수 없고,
    next step은 realigned surface 기준의
    default HWM + `XPUB_NODROP=1` publication/backpressure differential이다.
  - 또한 current realigned `with_zmq single` surface는
    earlier `perf_pubsub.cpp` queue-probe 지표를 직접 내보내지 않으므로,
    old queue metrics는 auxiliary diagnostic으로만 유지한다.

## 48. 2026-03-28 `PUBSUB` delivery-ready lazy tracking 후보 폐기 로그

- 작업한 가설
  - realigned single `PUBSUB` baseline에서 남은 `tcp` 차이는 이미
    `dist_t` one-matching-pipe helper가 일부 줄였으므로,
    current `default HWM + XPUB_NODROP=1` 잔여 gap 중 일부는
    `XPUB/XSUB` delivery-ready ready-count recompute를
    monitor가 실제로 구독할 때만 수행하도록 좁히면 더 줄어들 수 있다.
  - 동시에 late monitor reopen / snapshot contract를 회귀 테스트로 묶어 두면,
    이후 publication candidate에서도 monitor-ready semantic drift를
    더 빨리 잡을 수 있다.
- `libzmq` reference / guide 확인
  - current guide와 hot-path는 이미
    "delivery-ready bookkeeping은 secondary, publication/ordering이 primary"로
    정렬돼 있었고, 이 후보는 그 가정을 다시 한 번 falsify하는 좁은 검증이다.
  - `/home/hep7/project/kairos/libzmq/src/xpub.cpp`,
    `/home/hep7/project/kairos/libzmq/src/dist.cpp`,
    `/home/hep7/project/kairos/libzmq/src/pipe.cpp`를 다시 읽고
    `PUBSUB` ready-count 유지와 publication work를 비교했다.
- `claude` consult
  - `claude --help`는 통과했다.
  - 앞선 same-family 단계에서 남긴 non-interactive advisory 시도는
    응답 없이 멈춰 unavailable로 기록했고,
    이번 후보 자체는 same-family narrow falsification이라
    그 unavailable 상태를 그대로 이어서 기록한다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
    - [`core/src/sockets/socket_base.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)
    - [`core/src/sockets/socket_base_monitor.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_monitor.cpp)
    - [`core/src/sockets/xpub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.hpp)
    - [`core/src/sockets/xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
    - [`core/src/sockets/xsub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.hpp)
    - [`core/src/sockets/xsub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.cpp)
  - 유지:
    - [`core/tests/integration/monitoring/test_monitor_socket_contract.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_socket_contract.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_delivery_ready_lazy_default`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_delivery_ready_lazy_broader_single`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_delivery_ready_lazy_rerun`
  - candidate 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_111321_codex_20260328_pubsub_delivery_ready_lazy_default.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_111321_codex_20260328_pubsub_delivery_ready_lazy_default.txt)
  - [`perf_linux_20260328_111416_codex_20260328_delivery_ready_lazy_broader_single.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_111416_codex_20260328_delivery_ready_lazy_broader_single.txt)
  - [`perf_linux_20260328_111611_codex_20260328_pubsub_delivery_ready_lazy_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_111611_codex_20260328_pubsub_delivery_ready_lazy_rerun.txt)
  - multi smoke는 Python runner가 새 report 파일을 남기지 않았고
    콘솔 결과만 확인했다.
- 핵심 수치
  - isolated first run `PUBSUB tcp/inproc 64B`
    - `-14.47% / -23.50%`
  - isolated rerun `PUBSUB tcp/inproc 64B`
    - `-14.44% / -29.17%`
  - broader single
    - `PAIR tcp/inproc 64B`: `-14.33% / -23.38%`
    - `PUBSUB tcp/inproc 64B`: `-15.52% / -28.17%`
    - `DEALER_DEALER tcp/inproc 64B`: `-15.15% / -22.46%`
    - `DEALER_ROUTER tcp/inproc 64B`: `-26.49% / -21.13%`
    - `ROUTER_ROUTER tcp/inproc 64B`: `-57.21% / -28.95%`
  - multi `pubsub tcp 64B`
    - `-20.48%`
- 유지한 변경 / 원복한 변경
  - 유지
    - `test_pubsub_delivery_ready_snapshot_and_reopen_after_ready()`
      monitor contract regression
  - 원복
    - `xpub/xsub` delivery-ready lazy tracking candidate
- 해석
  - isolated `tcp` first run은 accepted realigned baseline `-15.29%`보다
    약간 좋아졌지만, same-command rerun에서 `inproc`가 `-29.17%`로 다시
    무너졌고 broader single에서도 `PUBSUB inproc`가 `-28.17%`로 악화됐다.
  - multi `pubsub tcp 64B -20.48%`도 latest accepted multi baseline
    `-17.24%` 또는 realigned default `-29.83%` 해석을 좁히는 stable broad win으로
    보기 어렵다. 즉 현재 후보는 `tcp` isolated improvement 하나만 보였을 뿐,
    general acceptance를 만족하지 못했다.
  - 따라서 current `PUBSUB` 잔여 gap을
    no-monitor delivery-ready bookkeeping 하나로 설명하진 않는다.
    이 계열 후보는 rejected candidate로 남기고 소스 변경은 원복한다.
  - 다만 late monitor reopen / snapshot 계약은
    future `PUBSUB` publication candidate에서도 깨질 수 있으므로,
    새 regression test는 current code에 유지한다.
- 다음 iteration 우선순위
  - `PUBSUB`은 delivery-ready bookkeeping lazy gate 계열을 더 파지 않는다.
  - next code candidate는 여전히 realigned surface 기준
    default HWM multi guardrail을 깨지 않는
    `inproc` receiver drain / blocked-retry differential 쪽이다.

## 49. 2026-03-28 `pipe::process_activate_write()` already-active fast path 폐기 로그

- 작업한 가설
  - current realigned `PUBSUB` 미완료 항목의 핵심은
    default HWM + `XPUB_NODROP=1` 아래 `inproc` sender backlog/publication
    differential이다.
  - libzmq `pipe_t::process_activate_write()`는 잠금 없이
    peer progress와 `_out_active`를 갱신하는 반면,
    zlink는 every `activate_write` command마다 `_out_sync`를 잡는다.
  - `inproc`에서는 receiver drain이 빠르고 `_lwm`마다 activation이 자주 오므로,
    이미 active인 steady state에서만 lock을 건너뛰어도
    `inproc` blocked-retry 진입 전후 비용을 줄일 수 있다고 봤다.
- `libzmq` reference / advisory
  - `/home/hep7/project/kairos/libzmq/src/pipe.cpp`를 다시 읽고
    `process_activate_write()` / `check_hwm()` / `flush()` 차이를 대조했다.
  - 이번 라운드의 `claude -p` non-interactive advisory는
    prompt 전달 오류(`Input must be provided either through stdin or as a prompt argument when using --print`)로
    usable output을 얻지 못해 unavailable로 기록한다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_activate_write_active_fastpath`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_activate_write_active_fastpath_rerun`
  - candidate 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_112849_codex_20260328_pubsub_activate_write_active_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_112849_codex_20260328_pubsub_activate_write_active_fastpath.txt)
  - [`perf_linux_20260328_112920_codex_20260328_pubsub_activate_write_active_fastpath_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_112920_codex_20260328_pubsub_activate_write_active_fastpath_rerun.txt)
- 핵심 수치
  - first run `PUBSUB tcp/inproc 64B`
    - `-9.53% / -28.30%`
  - rerun `PUBSUB tcp/inproc 64B`
    - `-22.77% / -24.83%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe::process_activate_write()` already-active fast path
- 해석
  - first run은 `tcp`가 크게 좋아졌지만 목표였던 `inproc`가
    accepted realigned baseline `-24.92%`보다 오히려 나빠졌다.
  - rerun에서는 반대로 `inproc`가 baseline 근처까지 회복했지만
    `tcp`가 `-22.77%`로 크게 악화됐다.
  - 즉 current candidate는 same command rerun만으로도 방향이 뒤집혀
    stable isolated win조차 만들지 못했다.
  - 따라서 current `PUBSUB` 잔여 gap을
    already-active `activate_write` lock 하나로 설명하진 않는다.
    이 candidate는 rejected로 남기고 원복한다.
- 다음 iteration 우선순위
  - `PUBSUB`은 activation command consumer의 already-active micro helper를
    같은 계열로 계속 파지 않는다.
  - next hypothesis는 여전히
    default HWM + `XPUB_NODROP=1` 아래 `inproc` blocked-retry /
    receiver-drain differential을 더 상위 의미 단위에서 다시 보는 쪽이다.

## 50. 2026-03-28 `pipe::read()` peer read-progress direct publish 폐기 로그

- 작업한 가설
  - current `PUBSUB` 미완료 항목의 남은 차이는 default HWM +
    `XPUB_NODROP=1`에서 `inproc` receiver drain이 `_lwm` 경계마다 만드는
    `activate_write` publication / consume differential일 수 있다.
  - 이미 writer peer가 active 상태라면 `pipe::read()`가 `_msgs_read`를
    peer `_peers_msgs_read`로 직접 publish하고 command queue의
    `activate_write`를 건너뛰어도 steady-state blocked-retry overhead를
    줄일 수 있다고 봤다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `ctest --test-dir core/build --output-on-failure -V -R '^test_xpub_nodrop$' -j1`
  - candidate 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
- 핵심 결과
  - candidate 적용 상태
    - `test_monitor_socket_contract`, `test_multi_socket_contract_regressions`,
      `test_public_inproc_multipart_send`, `test_pubsub_filter_xpub` 통과
    - `test_xpub_nodrop`는
      `/home/hep7/project/kairos/zlink/core/tests/integration/test_xpub_nodrop.cpp:383:test:PASS`
      출력 뒤 `***Timeout 10.01 sec`로 종료 정리 단계에서 hang
  - candidate 원복 상태
    - 동일 ctest 묶음 전부 통과
    - `test_xpub_nodrop` 단독도 `0.75 sec` 내 통과
- 해석
  - `activate_write` command 자체는 peer progress 반영만이 아니라
    object mailbox ordering과 shutdown/teardown invariants에도 걸려 있었다.
  - `pipe::read()`에서 direct publish로 command를 우회하는 현재 형태는
    performance run에 들어가기 전에 integration teardown regression을 만들었다.
  - 따라서 current `PUBSUB` 잔여 gap을
    `_lwm` boundary direct peer-progress publish 하나로 설명하진 않는다.
    이 candidate는 safety regression으로 rejected하고 원복한다.
- 다음 iteration 우선순위
  - `PUBSUB`은 `pipe::read()`에서 command publication을 우회하는
    direct peer-progress shortcut을 계속 파지 않는다.
  - next hypothesis는 여전히
    default HWM multi guardrail을 깨지 않는
    `inproc` receiver-drain / blocked-retry differential의 다른 의미 단위다.

## 51. 2026-03-28 `xsub::xrecv()` last-recv source-rid snapshot 제거 로그

- 작업한 가설
  - current `PUBSUB` 잔여 gap은 sender backlog/publication 쪽으로 보였지만,
    그 역면에는 `inproc` receiver drain 자체가 libzmq보다 무거운 문제도 있다.
  - zlink [`xsub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.cpp)
    는 every matching recv마다 `store_last_recv_source_rid(pipe)`를 호출하지만,
    current single `PUBSUB` surface는 `zlink_msg_recv()`만 써서
    source rid를 전혀 요구하지 않는다.
  - 따라서 `SUB/XSUB` normal recv hot path에서 source-rid snapshot을 걷으면
    receiver drain이 가벼워져 default HWM + `XPUB_NODROP=1`에서
    blocked-retry differential을 줄일 수 있다고 봤다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/sockets/xsub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop|test_socket_with_handler)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_xsub_skip_last_recv_source_rid`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_xsub_skip_last_recv_source_rid_rerun`
  - candidate 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop|test_socket_with_handler)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_114210_codex_20260328_pubsub_xsub_skip_last_recv_source_rid.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_114210_codex_20260328_pubsub_xsub_skip_last_recv_source_rid.txt)
  - [`perf_linux_20260328_114239_codex_20260328_pubsub_xsub_skip_last_recv_source_rid_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_114239_codex_20260328_pubsub_xsub_skip_last_recv_source_rid_rerun.txt)
- 핵심 수치
  - first run `PUBSUB tcp/inproc 64B`
    - `-20.13% / -28.12%`
  - rerun `PUBSUB tcp/inproc 64B`
    - `-19.83% / -23.68%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `xsub::xrecv()` matching recv의 `last_recv_source_rid` snapshot 제거
- 해석
  - receiver drain 쪽 미세 고정비를 줄이는 방향 자체는 맞아서
    rerun `inproc`는 accepted baseline `-24.92%`보다 소폭 회복했다.
  - 하지만 같은 candidate에서 `tcp`가 first/rerun 모두
    `-20%` 안팎으로 baseline `-15.29%`보다 더 나빠졌고,
    first `inproc`도 `-28.12%`로 불안정했다.
  - 즉 current `PUBSUB` 잔여 gap을
    `xsub` normal recv의 source-rid snapshot 하나로 설명하진 않는다.
    이 candidate는 receiver-drain evidence는 줬지만 keep-worthy broad win은
    아니므로 원복한다.
- 다음 iteration 우선순위
  - `PUBSUB`은 `xsub::xrecv()` source-rid snapshot elision 계열을
    그대로 유지 후보로 올리지 않는다.
  - next hypothesis는 여전히
    default HWM multi guardrail을 깨지 않는
    `inproc` receiver-drain / blocked-retry differential의 다른 의미 단위다.

## 52. 2026-03-28 `xsub` empty-subscription accept-all fast path 로그

- 작업한 가설
  - current aligned single `PUBSUB` bench는 `SUB`에 empty subscription `""`를
    걸고 no-topic payload를 받는다.
  - 이 steady-state에서는 `xsub::match()`가 trie를 통해 결국 항상 true를
    돌려주므로, empty subscription이 살아 있는 동안은
    receiver-side `match()` 자체를 건너뛰어도 semantics를 유지할 수 있다.
  - 따라서 `xsub`가 empty subscription presence를 직접 추적해
    accept-all fast path를 타면 `inproc` receiver drain을 줄여
    default HWM + `XPUB_NODROP=1` blocked-retry differential을
    더 직접 좁힐 수 있다고 봤다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/sockets/xsub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.hpp)
    - [`core/src/sockets/xsub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_socket_with_handler|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_xsub_accept_all`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_xsub_accept_all_rerun`
  - candidate 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_socket_with_handler|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_114624_codex_20260328_pubsub_xsub_accept_all.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_114624_codex_20260328_pubsub_xsub_accept_all.txt)
  - [`perf_linux_20260328_114646_codex_20260328_pubsub_xsub_accept_all_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_114646_codex_20260328_pubsub_xsub_accept_all_rerun.txt)
- 핵심 수치
  - first run `PUBSUB tcp/inproc 64B`
    - `-16.33% / -20.00%`
  - rerun `PUBSUB tcp/inproc 64B`
    - `-15.20% / -27.16%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `xsub` empty-subscription accept-all fast path
- 해석
  - first run은 `tcp`가 baseline `-15.29%`에 근접하고 `inproc`가
    `-20.00%`까지 회복해, current receiver-drain 후보 중 가장 그럴듯한
    신호를 줬다.
  - 하지만 same-command rerun에서 `inproc`가 `-27.16%`로 baseline 아래로
    다시 떨어져 stable isolated win을 유지하지 못했다.
  - 즉 empty subscription steady-state의 trie `match()` cost는
    current `PUBSUB` 잔여 gap의 일부 축이긴 하지만, 이것 하나만으로는
    keep-worthy acceptance를 만들지 못했다.
  - 따라서 이 candidate도 원복하고 rejected evidence로만 남긴다.
- 다음 iteration 우선순위
  - `PUBSUB`은 `xsub` empty-subscription accept-all fast path를
    그대로 유지 후보로 올리지 않는다.
  - next hypothesis는 여전히
    default HWM multi guardrail을 깨지 않는
    `inproc` receiver-drain / blocked-retry differential의 다른 의미 단위다.

## 53. 2026-03-28 current retained-code baseline recheck + recv-mode busy-bit guard 로그

- 작업한 가설
  - guide의 현재 `PUBSUB` baseline 숫자가 current retained code와 어긋나면,
    다음 후보 판정 자체가 흔들린다.
  - 먼저 current retained code를 direct recheck하고,
    recv-side mode guard 비용을 cached busy-bit 하나로 줄이면
    `zlink_msg_recv()` / `xrecv` steady-state 비용을 좁힐 수 있다고 봤다.
- `claude` consult
  - `claude --help`는 통과했다.
  - 그러나 이번 단계의 non-interactive `claude -p` 호출은
    `Input must be provided either through stdin or as a prompt argument when using --print`
    오류로 usable advisory를 얻지 못해 unavailable로 기록한다.
- 수정한 파일 경로
  - 유지:
    - [`core/tests/integration/test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp)
  - 실험 후 원복:
    - [`core/src/core/recv_internal.cpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_internal.cpp)
    - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
    - [`core/src/sockets/socket_base_dispatch.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_dispatch.cpp)
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/stream.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/stream.cpp)
    - [`core/src/sockets/xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
    - [`core/src/sockets/xsub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.cpp)
- 실행한 명령
  - current baseline recheck
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_current_baseline`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_current_baseline_rerun`
    - `PERF_SINGLE_PUBSUB_XPUB_NODROP=0 python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_current_nodrop0`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_recv_mode_busy_default`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_recv_mode_busy_rerun`
  - candidate 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_115253_codex_20260328_pubsub_current_baseline.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_115253_codex_20260328_pubsub_current_baseline.txt)
  - [`perf_linux_20260328_115322_codex_20260328_pubsub_current_baseline_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_115322_codex_20260328_pubsub_current_baseline_rerun.txt)
  - [`perf_linux_20260328_115345_codex_20260328_pubsub_current_nodrop0.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_115345_codex_20260328_pubsub_current_nodrop0.txt)
  - [`perf_linux_20260328_120014_codex_20260328_pubsub_recv_mode_busy_default.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_120014_codex_20260328_pubsub_recv_mode_busy_default.txt)
  - [`perf_linux_20260328_120037_codex_20260328_pubsub_recv_mode_busy_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_120037_codex_20260328_pubsub_recv_mode_busy_rerun.txt)
- 핵심 수치
  - current retained-code baseline
    - seq1 `PUBSUB tcp/inproc 64B`: `-21.73% / -19.43%`
    - rerun `PUBSUB tcp/inproc 64B`: `-22.44% / -31.08%`
    - `XPUB_NODROP=0`: `-0.06% / +0.04%`
    - latest multi `pubsub tcp 64B`: `-22.75%`
  - rejected recv-mode busy-bit candidate
    - first `PUBSUB tcp/inproc 64B`: `-16.20% / -27.20%`
    - rerun `PUBSUB tcp/inproc 64B`: `-20.29% / -32.27%`
- 유지한 변경 / 원복한 변경
  - 유지
    - `test_socket_with_handler`의 `zlink_msg_recv(...)=EBUSY` contract regression
  - 원복
    - recv-mode busy-bit cached guard candidate 전부
- 해석
  - guide가 들고 있던 `-15.29% / -24.92%`, multi `-29.83%`는
    surface realignment 당시 historical anchor로는 유효하지만,
    current retained code direct recheck 기준 source-of-truth로는 stale했다.
  - `XPUB_NODROP=0` probe가 current code에서도 거의 sign flip이라,
    현재 `PUBSUB` 잔여 gap의 큰 축이 default HWM + `XPUB_NODROP=1`
    publication/backpressure differential이라는 해석은 유지된다.
  - recv-mode busy-bit candidate는 first run `tcp`만 좋아 보였지만
    rerun `inproc`가 baseline보다 다시 나빠져 stable broad win이 아니었다.
  - 따라서 current next step은 recv mode specialization을 그대로 밀기보다,
    default-HWM multi guardrail을 깨지 않는
    `inproc` receiver-drain / blocked-retry differential의 다른 의미 단위를
    다시 찾는 쪽이다.

## 54. 2026-03-28 no-topic single-part `zlink_publish` fast path rejected 로그

- 작업한 가설
  - current aligned single `PUBSUB` bench가 실제로
    `zlink_publish(NULL, &part, 1)` single-part payload path를 타므로,
    `PUB/XPUB`에서 topic 없는 단일 publish는 logical multipart helper 대신
    direct send로 보내면 current execution baseline을 줄일 수 있다고 봤다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_publish_singlepart_fastpath`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_publish_singlepart_fastpath_rerun`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_121215_codex_20260328_pubsub_publish_singlepart_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_121215_codex_20260328_pubsub_publish_singlepart_fastpath.txt)
  - [`perf_linux_20260328_121215_codex_20260328_pubsub_publish_singlepart_fastpath_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_121215_codex_20260328_pubsub_publish_singlepart_fastpath_rerun.txt)
- 핵심 수치
  - first `PUBSUB tcp/inproc 64B`
    - `-16.16% / -28.59%`
  - rerun `PUBSUB tcp/inproc 64B`
    - `-15.30% / -34.26%`
  - multi `pubsub tcp 64B`
    - first `-31.41%`
    - rerun `-25.50%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - no-topic single-part `zlink_publish` fast path 전부
- 해석
  - single `tcp`는 current baseline보다 좋아 보였지만
    rerun `inproc`가 `-34.26%`로 current baseline `-31.08%`보다 더 나빠졌다.
  - multi `pubsub tcp`도 `-31.41%`, `-25.50%`로 current baseline `-22.75%`를
    안정적으로 지키지 못했다.
  - 따라서 current `PUBSUB` 잔여 gap을 publish API front-end helper 하나로
    설명하진 않는다. 이 candidate는 keep-worthy broad win이 아니어서 원복한다.

## 55. 2026-03-28 `pipe` `activate_read` pending dedupe rejected 로그

- 작업한 가설
  - current `PUBSUB` 잔여 gap의 일부는 default HWM + `XPUB_NODROP=1`
    steady-state에서 같은 pipe pair로 `activate_read`가 중복 publication되는
    비용일 수 있다.
  - mailbox ordering은 그대로 두고 pending command만 dedupe하면
    `inproc` blocked-retry differential을 줄일 수 있다고 봤다.
- 수정한 파일 경로
  - 실험 후 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_activate_read_dedupe`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_activate_read_dedupe_rerun`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
  - candidate 원복 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_121552_codex_20260328_pubsub_activate_read_dedupe.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_121552_codex_20260328_pubsub_activate_read_dedupe.txt)
  - [`perf_linux_20260328_121552_codex_20260328_pubsub_activate_read_dedupe_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_121552_codex_20260328_pubsub_activate_read_dedupe_rerun.txt)
- 핵심 수치
  - first `PUBSUB tcp/inproc 64B`
    - `-27.06% / -29.49%`
  - rerun `PUBSUB tcp/inproc 64B`
    - `-35.43% / -20.57%`
  - multi `pubsub tcp 64B`
    - `-32.15%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `activate_read` pending dedupe candidate 전부
- 해석
  - same-command rerun만으로 `tcp`/`inproc` 방향이 크게 갈렸고,
    multi `pubsub tcp`도 current baseline보다 더 나빠졌다.
  - 즉 current `PUBSUB` 잔여 gap을 `activate_read` duplicate command 하나로
    설명하진 않는다. 이 candidate도 keep-worthy broad win이 아니어서 원복한다.

## 56. 2026-03-28 retained `xsub` receiver-drain specialization 로그

- 작업한 가설
  - section 51, 52에서 각각 본
    `xsub::xrecv()` source-rid snapshot 제거와
    empty-subscription accept-all fast path는
    current recheck baseline `-22.44% / -31.08%` 대비 개선 신호가 있었다.
  - 두 후보를 contract-safe 형태로 다시 묶으면,
    empty-prefix steady-state의 trie `match()`와
    normal recv의 unnecessary source-rid snapshot을 함께 줄이면서도
    `spot_sub_t::recv()`가 실제 source-rid를 요구할 때만 그 비용을 지불하게
    만들 수 있다고 봤다.
- `claude` consult
  - `claude --help`는 통과했다.
  - 이번 단계의 non-interactive `claude -p`는
    `Input must be provided either through stdin or as a prompt argument when using --print`
    오류와 stdin 재시도 무응답으로 usable advisory를 얻지 못해
    unavailable로 기록한다.
- 수정한 파일 경로
  - 유지:
    - [`core/src/sockets/xsub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.hpp)
    - [`core/src/sockets/xsub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.cpp)
    - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
    - [`core/src/sockets/socket_base_dispatch.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_dispatch.cpp)
    - [`core/src/services/spot/spot_sub_recv.cpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_sub_recv.cpp)
  - 기존 유지 regression 재확인:
    - [`core/tests/integration/test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp)
    - [`core/tests/integration/monitoring/test_monitor_socket_contract.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_socket_contract.cpp)
- 실행한 명령
  - candidate 적용 후
    - `cmake --build core/build -j$(nproc)`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop|test_spot_pubsub_scenario)$' -j1`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_xsub_accept_all_rid_store_on_demand`
    - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_xsub_accept_all_rid_store_on_demand_rerun`
    - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xsub_accept_all_rid_store_broader_single`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
    - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_123151_codex_20260328_pubsub_xsub_accept_all_rid_store_on_demand.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_123151_codex_20260328_pubsub_xsub_accept_all_rid_store_on_demand.txt)
  - [`perf_linux_20260328_123215_codex_20260328_pubsub_xsub_accept_all_rid_store_on_demand_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_123215_codex_20260328_pubsub_xsub_accept_all_rid_store_on_demand_rerun.txt)
  - [`perf_linux_20260328_123242_codex_20260328_xsub_accept_all_rid_store_broader_single.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_123242_codex_20260328_xsub_accept_all_rid_store_broader_single.txt)
  - multi `pubsub` smoke는 이번 runner stdout에서만 수치를 확인했고,
    별도 report 파일 경로는 확인하지 못했다.
- 핵심 수치
  - isolated first/rerun `PUBSUB tcp/inproc 64B`
    - `-9.40% / -20.35%`
    - `-10.43% / -21.59%`
  - broader single `PAIR/PUBSUB/DEALER_DEALER/DEALER_ROUTER/ROUTER_ROUTER`
    `tcp/inproc 64B`
    - `-16.64% / -21.71%`
    - `-11.57% / -20.78%`
    - `-26.40% / -21.90%`
    - `-24.17% / -18.30%`
    - `-55.78% / -21.48%`
  - multi `pubsub tcp 64B`
    - first `+9.25%`
    - rerun `+8.25%`
- 유지한 변경 / 원복한 변경
  - 유지
    - `xsub` empty-subscription accept-all fast path
    - requested-only `last_recv_source_rid` capture scope
  - 원복
    - 없음
- 해석
  - section 51, 52의 individual signal을 contract-safe 형태로 결합하자,
    current recheck baseline `-22.44% / -31.08%`, multi `-22.75%` 대비
    `PUBSUB tcp`와 multi `pubsub`가 크게 회복했다.
  - `spot_sub_t::recv()`는 caller가 `source_rid_out`을 넘길 때만
    `last_recv_source_rid`를 저장하므로, normal `XSUB` steady-state에서
    source-rid snapshot 비용을 지우면서 public/spot contract는 유지한다.
  - broader single에서는 `PAIR inproc`만 accepted rerun snapshot 대비
    약 `4.49`%p 나빠졌지만, guide guardrail `5%` 안쪽이다.
    다른 non-target 패턴은 유지 가능 범위거나 개선됐다.
  - 따라서 이 조합은 current retained `PUBSUB` delta로 승격한다.
- 다음 iteration 우선순위
  - `PUBSUB`은 같은 family의 또 다른 local `xpub/dist` micro helper를
    반복하지 않는다.
  - next priority는 guide의 다음 미완료 항목인
    `ROUTER_ROUTER` routed path differential이다.

## 57. 2026-03-28 high-HWM probe 메모

- 작업한 가설
  - current `oneway` gap의 본체가 `HWM` 도달 이후 backpressure 복귀라면,
    `hwm/sndhwm/rcvhwm = 1000000`으로 사실상 backlog ceiling을 제거했을 때
    작은 메시지 throughput gap이 크게 줄어야 한다고 봤다.
- 실행한 명령
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64,256,1024,65536,131072,262144 --transport tcp,ipc,inproc --runs 1 --build-dir core/build --results-tag 20260328_124815`
    with
    `PERF_SINGLE_HWM=1000000 PERF_SINGLE_SNDHWM=1000000 PERF_SINGLE_RCVHWM=1000000`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_124815.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_124815.txt)
- 핵심 수치
  - `PAIR 64B`
    - `tcp -33.07%`
    - `ipc -30.34%`
    - `inproc -30.77%`
  - `PUBSUB 64B`
    - `tcp -38.93%`
    - `ipc -22.86%`
    - `inproc -34.66%`
  - `DEALER_DEALER 64B`
    - `tcp -34.54%`
    - `ipc -27.66%`
    - `inproc -28.64%`
- 해석
  - high-HWM에서도 작은 메시지 gap이 transport 전반에서 그대로 크게 남았다.
  - 따라서 current differential을 `HWM` 도달, `EAGAIN`, `activate_write`
    복귀 지연 같은 backpressure-only 원인으로 설명하는 가설은 크게 약해졌다.
  - 현재 더 유력한 해석은 queue가 차지 않아도 매 메시지마다 드는
    steady-state 공통 고정비가 본체라는 것이다.
    즉 `send/recv hot path`, `pipe` publication/ordering, 패턴별로는
    `XSUB` drain처럼 항상 지불되는 per-message work가 상위 축이다.
  - backpressure는 여전히 secondary amplifier일 수 있지만,
    next root-cause search를 `backpressure only`에 두면 안 된다.
  - 이 probe는 single-run supplementary reference로 보관한다.
    final pivot 판단이 필요하면 같은 조건 rerun 1회 이상으로 재확인한다.

## 58. 2026-03-28 producer-side steady-state differential 재정리

- 작업한 가설
  - section 57의 high-HWM probe까지 합치면 current `oneway` gap의 본체를
    `HWM` 도달과 `activate_write` 복귀 지연만으로 설명하긴 어렵다.
  - `echo ~= 동등, oneway = 격차` 패턴은 여전히 sender-side를 가리키지만,
    지금은 `backpressure-only`보다
    `queue가 차지 않아도 남는 producer-side steady-state send/publication`
    differential이 더 유력하다고 봤다.
- libzmq reference pass
  - zlink
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
  - libzmq
    - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
    - [`libzmq/src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- 핵심 차이
  - zlink public send는 current `PAIR` no-sync 경로에서도
    `socket_public_send_scope_t`를 통해
    `enter_public_api()` / `leave_public_api()` 원자 상태 갱신을 매 메시지마다
    탄다.
  - sync 소켓은 이미 `0 -> (inflight|sync)` 단일 CAS fast path가 있지만,
    no-sync 경로는 여전히 `fetch_add/fetch_sub` 위주다.
  - pipe steady-state send는
    [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    에서 `_out_sync` recursive `fast_mutex_t`를 타고,
    [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
    는 owner tracking + TLS thread-id lookup + pthread mutex를 함께 쓴다.
  - libzmq는 non-thread-safe send에서 optional lock이 사실상 no-op이고,
    pipe steady-state `write()/flush()`가
    [`libzmq/src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
    의 lock-free SPSC queue 위에 있다.
- 해석
  - current common gap은 `wrapper` 하나보다
    `public lifecycle coordinator + pipe send-path serialization`의 합으로 보는
    게 더 정확하다.
  - `PUBSUB`는 위 공통 differential 외에
    retained `XSUB` receiver-drain specialization이 실제로 큰 recovery를
    만들었으므로, send-only 원인으로 단순화하면 안 된다.
  - 하지만 `PAIR/DEALER` 공통축은 여전히 producer-side steady-state send 쪽이
    가장 유력하다.
- 다음 단계
  - no-sync public send/callback 경로에 대해
    uncontended lifecycle fast path를 먼저 넣고
    focused `PAIR/DEALER` bench로 확인한다.
  - sync 소켓의 lifecycle fast path 재설계와
    pipe same-ordering cheaper-serialization은 그 다음 설계 축으로 둔다.

## 59. 2026-03-28 no-sync lifecycle fast path probe

- 작업한 가설
  - section 58에서 정리한 대로, `PAIR` 같은 no-sync public send/callback
    경로는 current code에서 여전히 `enter_public_api()` /
    `leave_public_api()`의 `fetch_add/fetch_sub`를 매번 탄다.
  - uncontended steady-state에서 `0 -> 1`, `1 -> 0`를 CAS/store fast path로
    줄이면 `PAIR` oneway 64B가 broad하게 회복될 수 있다고 봤다.
- 수정한 파일 경로
  - probe 구현 후 원복:
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - 유지:
    - [`core/tests/unittest/unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `./core/build/bin/unittest_socket_runtime`
  - `./core/build/bin/test_public_inproc_multipart_send`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_no_sync_lifecycle_fastpath_pair_only`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_no_sync_lifecycle_fastpath_dealer_only`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_131931_codex_20260328_no_sync_lifecycle_fastpath_pair_only.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_131931_codex_20260328_no_sync_lifecycle_fastpath_pair_only.txt)
  - [`perf_linux_20260328_131857_codex_20260328_no_sync_lifecycle_fastpath_dealer_only.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_131857_codex_20260328_no_sync_lifecycle_fastpath_dealer_only.txt)
- 핵심 수치
  - `PAIR tcp/inproc 64B`
    - `-18.75% / -32.03%`
  - `DEALER_DEALER tcp/inproc 64B`
    - `-11.59% / -21.39%`
- 유지한 변경 / 원복한 변경
  - 유지
    - `no-sync public send scope` contract를 직접 확인하는
      `unittest_socket_runtime` coverage
  - 원복
    - uncontended `enter_public_api()/leave_public_api()` CAS fast path
- 해석
  - candidate는 lifecycle 의미를 깨지는 않았고 unit/integration regression도
    통과했지만, focused bench에서 `PAIR tcp`만 조금 좋아 보이고
    `PAIR inproc`는 다시 크게 밀렸다.
  - `DEALER_DEALER`도 sync 소켓이라 이 candidate의 직접 대상이 아니고,
    나온 수치만으로 common broad win이라고 볼 수 없다.
  - 따라서 current 공통 differential을
    `no-sync admission atomics` 하나로 설명하는 건 무리이고,
    이 probe는 keep-worthy delta가 아니라 rejected candidate로 남긴다.
- 다음 단계
  - current 공통축은 여전히
    `sync 소켓 포함 public lifecycle coordinator`
    와 `pipe send-path serialization`의 same-ordering cost 쪽으로 본다.
  - 다음 코드는 `PAIR` 전용 no-sync fast path보다
    sync 소켓을 포함한 broader send-side 의미 단위에서 찾아야 한다.

## 60. 2026-03-28 pipe recursive `check_hwm()` elide

- 작업한 가설
  - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    의 `check_write_status()`, `write()`, `write_and_flush()`는 이미 `_out_sync`
    를 잡은 상태에서 다시 `check_hwm()`을 호출한다.
  - `check_hwm()`은 내부에서 `scoped_optional_fast_lock_t`로 `_out_sync`를
    다시 잡으므로, current `PAIR/DEALER` hot path는 메시지마다 불필요한
    recursive lock을 한 번 더 탄다.
  - same lock scope 안에서는 `check_hwm_unlocked()`로 바꿔도 의미가 같으니,
    이 recursive lock만 없애면 send steady-state cost가 바로 줄어야 한다고 봤다.
- 수정한 파일 경로
  - 유지:
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `./core/build/bin/test_public_inproc_multipart_send`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pipe_recursive_hwm_elide_pair_only`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pipe_recursive_hwm_elide_dealer_only`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_132414_codex_20260328_pipe_recursive_hwm_elide_pair_only.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_132414_codex_20260328_pipe_recursive_hwm_elide_pair_only.txt)
  - [`perf_linux_20260328_132351_codex_20260328_pipe_recursive_hwm_elide_dealer_only.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_132351_codex_20260328_pipe_recursive_hwm_elide_dealer_only.txt)
- 핵심 수치
  - `PAIR tcp/inproc 64B`
    - `-14.95% / -32.56%`
    - section 59의 probe baseline `-18.75% / -32.03%` 대비
      `tcp`는 약 `+3.80`%p, `inproc`은 약 `-0.53`%p
  - `DEALER_DEALER tcp/inproc 64B`
    - `-9.55% / -20.23%`
    - section 59 baseline `-11.59% / -21.39%` 대비
      `+2.04`%p / `+1.16`%p
- 유지한 변경 / 원복한 변경
  - 유지
    - `_out_sync` lock scope 안의 `check_hwm_unlocked()` 사용
  - 원복
    - 없음
- 해석
  - 이 candidate는 thread-safe contract를 건드리지 않고,
    hot path에서 같은 `_out_sync` recursive lock 한 번을 없애는
    아주 좁은 변화다.
  - `DEALER_DEALER`는 `tcp/inproc` 모두 회복했고,
    `PAIR`도 `tcp`는 눈에 띄게 좋아졌다.
  - `PAIR inproc`는 약간 나빠졌지만 폭이 작아서,
    current stage에선 broad-ish kept delta로 볼 수 있다.
  - 따라서 current common differential에는
    `pipe` send-path recursive bookkeeping이 실제 비용 축이라는 증거가
    하나 더 붙었다.
- 다음 단계
  - current next step은
    `pipe` send-path의 same-lock recursive work를 더 찾는 것과,
    `sync` 소켓 쪽 public lifecycle coordinator 차이를 더 직접 겨냥하는
    broader send-side 설계 차이를 보는 것이다.

## 61. 2026-03-28 `fast_mutex` common-path candidate

- 작업한 가설
  - [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
    의 common unlocked path는 `current_thread_id()`를 먼저 읽고
    owner를 비교한다.
  - recursive 의미는 유지한 채, owner가 `0`일 때는 TID 조회를 늦추면
    `_out_sync` hot path가 조금 더 가벼워질 수 있다고 봤다.
- 수정한 파일 경로
  - probe 구현 후 원복:
    - [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `./core/build/bin/test_public_inproc_multipart_send`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_fast_mutex_common_path_pair_only_clean`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_fast_mutex_common_path_dealer_only_clean`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_132616_codex_20260328_fast_mutex_common_path_pair_only_clean.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_132616_codex_20260328_fast_mutex_common_path_pair_only_clean.txt)
  - [`perf_linux_20260328_132616_codex_20260328_fast_mutex_common_path_dealer_only_clean.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_132616_codex_20260328_fast_mutex_common_path_dealer_only_clean.txt)
- 핵심 수치
  - `PAIR tcp/inproc 64B`
    - `-33.39% / -15.22%`
  - `DEALER_DEALER tcp/inproc 64B`
    - `-15.09% / -24.52%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `fast_mutex` common unlocked path TID-lazy candidate
- 해석
  - 이 candidate는 common unlocked path를 겨냥했지만,
    clean rerun에서는 `PAIR tcp`가 크게 나빠졌고
    broad win으로 읽을 수 있는 패턴이 아니었다.
  - 즉 current `_out_sync` differential을
    `fast_mutex` helper 표면의 TID 조회 순서 하나로 설명하긴 어렵다.
  - 이 후보는 rejected로 남기고, 현재 kept delta는 section 60의
    same-lock recursive `check_hwm()` 제거까지만 유지한다.

## 62. 2026-03-28 `ROUTER_ROUTER` raw-msg probe 로그

- 작업한 가설
  - current `ROUTER_ROUTER` gap의 큰 일부가
    [`core/bench/with_zmq/single/zlink/bench_zlink_router_router.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_router_router.cpp)
    의 aggregate `zlink_send()` / `zlink_recv()` surface라면,
    single-frame routed fast path인 `zlink_msg_send_rid()` /
    `zlink_msg_recv_rid()`로 probe 했을 때 gap이 크게 줄어야 한다고 봤다.
  - 목적은 accepted baseline을 바로 바꾸는 게 아니라,
    current `ROUTER_ROUTER` 미완료 항목의 상위 축이
    aggregate wrapper인지 `router.cpp` core ordering인지 먼저 분리하는 것이었다.
- candidate family
  - semantic probe / validation surface split
- high-leverage 근거
  - 이전 `ROUTER` micro-elision은 모두 noise 수준이었고,
    current review도 residual을 `public/aggregate differential`로 의심하고
    있었으므로, 더 작은 helper 전에 surface-level 분리가 우선이었다.
- 참고한 `libzmq` 대응 파일
  - [`libzmq/src/router.cpp`](/home/hep7/project/kairos/libzmq/src/router.cpp)
  - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
- `claude` consult 여부와 핵심 조언
  - `claude --help`는 통과했다.
  - prompt 인자 방식 `claude -p`는
    `Input must be provided either through stdin or as a prompt argument when using --print`
    오류로 실패했다.
  - stdin 재시도도 30초 이상 usable output 없이 대기만 해
    이번 단계는 advisory unavailable로 기록한다.
- 수정한 파일 경로
  - 유지:
    - [`core/bench/with_zmq/single/zlink/bench_zlink_router_router.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_router_router.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_raw_probe_default_seq`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_raw_probe_raw_seq`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_134724_codex_20260328_router_raw_probe_default_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_134724_codex_20260328_router_raw_probe_default_seq.txt)
  - [`perf_linux_20260328_134753_codex_20260328_router_raw_probe_raw_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_134753_codex_20260328_router_raw_probe_raw_seq.txt)
  - pushed commit: `ec465323748dd6fa16ca6e6b733460775fab8ce8`
- 핵심 수치
  - default `ROUTER_ROUTER tcp/inproc 64B`
    - `-58.12% / -27.77%`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1` probe
    - `-54.07% / -27.78%`
- 유지한 변경 / 원복한 변경
  - 유지
    - `bench_zlink_router_router.cpp` routed raw-msg probe support
  - 원복
    - 없음
- 해석
  - routed raw-msg fast path는 `tcp`를 약 `+4.05`%p 줄였지만
    `inproc`는 사실상 변하지 않았다.
  - 즉 current `ROUTER_ROUTER` gap의 본체를
    aggregate `zlink_send()` / `zlink_recv()` wrapper만으로 설명하긴 어렵다.
  - 다음 단계는 local wrapper elision을 더 파는 것이 아니라,
    `router.cpp`의 `out_pipe` admission/flush와
    prefetch 기반 routed recv ordering 차이를 더 직접 분리하는 쪽이 맞다.
- 다음 iteration 우선순위
  - `ROUTER_ROUTER`는 raw/public aggregate wrapper elision을
    1차 후보로 다시 올리지 않는다.
  - next hypothesis는 `router.cpp` routed send/recv core ordering,
    특히 `out_pipe` admission/flush와 routed recv prefetch differential이다.

## 63. 2026-03-28 `xsend_routed()` final-part one-lock helper 로그

- 작업한 가설
  - current blocking `ROUTER` default send는
    [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
    에서 이미 routing-id envelope를 `send_routed()` one-part path로 접는다.
  - 따라서 current `ROUTER_ROUTER` send hot path에서 남은 작은 차이가
    `router.cpp`의 ready check와
    [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    final `write+flush` 사이의 이중 lock acquisition이라면,
    둘을 one-lock helper로 합쳤을 때 zlink absolute throughput이 바로
    올라야 한다고 봤다.
- candidate family
  - pattern-specific core routed send path
- high-leverage 근거
  - raw-msg probe가 wrapper 본체 가설을 약화시켰고, blocking default send가 이미
    `send_routed()` one-part path를 타는 상태라면 다음 차이는
    `xsend_routed()` + `pipe` final-part hot path 자체여야 했다.
- 참고한 `libzmq` 대응 파일
  - [`libzmq/src/router.cpp`](/home/hep7/project/kairos/libzmq/src/router.cpp)
  - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
- `claude` consult 여부와 핵심 조언
  - `claude --help`는 통과했다.
  - stdin 기반 `claude -p --permission-mode bypassPermissions --add-dir ...`
    호출은 응답을 반환했다.
  - 핵심 조언은 current `ROUTER_ROUTER` residual을 local helper보다
    공통 `_out_sync` per-message lock floor와 routed recv ordering 쪽으로
    먼저 보라는 것이었다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/src/sockets/router.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.cpp)
- 실행한 명령
  - `claude --help | head -n 40`
  - `printf '...' | claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink`
  - `cmake --build core/build -j$(nproc)`
  - `./core/build/bin/test_router_mandatory_hwm`
  - `./core/build/bin/test_public_inproc_multipart_send`
  - `./core/build/bin/test_router_multiple_dealers`
  - `./core/build/bin/test_multi_socket_contract_regressions`
  - `./core/build/bin/test_stream_send_blocking_wakeup`
  - `./core/build/bin/test_transport_matrix`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_one_lock_routed_send`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp --runs 1 --build-dir core/build --results-tag codex_20260328_router_one_lock_routed_send_tcp_only`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_one_lock_routed_send_inproc_only`
  - `timeout 30s ./core/build/bin/comp_zlink_router_router zlink inproc 64`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_140326_codex_20260328_router_one_lock_routed_send.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_140326_codex_20260328_router_one_lock_routed_send.txt)
  - [`perf_linux_20260328_140651_codex_20260328_router_one_lock_routed_send_tcp_only.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_140651_codex_20260328_router_one_lock_routed_send_tcp_only.txt)
  - [`perf_linux_20260328_140708_codex_20260328_router_one_lock_routed_send_inproc_only.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_140708_codex_20260328_router_one_lock_routed_send_inproc_only.txt)
- 핵심 수치
  - first complete run `ROUTER_ROUTER tcp/inproc 64B`
    - `-54.37% / -23.05%`
  - transport-split rerun
    - `tcp -57.14%`
    - `inproc -29.36%`
  - same runs의 zlink absolute throughput은
    - `tcp 1203.40 ~ 1210.70 Kmsg/s`
    - `inproc 2412.64 ~ 2416.74 Kmsg/s`
  - direct binary 확인
    - `comp_zlink_router_router zlink inproc 64`:
      `2415126.40 msg/s`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `xsend_routed()` final-part ready-check + `write+flush` one-lock helper 전부
- 해석
  - diff 값은 libzmq baseline 흔들림에 따라 약간 좋아 보일 때가 있었지만,
    zlink absolute throughput 자체는 current baseline 수준에서 거의
    움직이지 않았다.
  - 즉 current `ROUTER_ROUTER` residual을
    `xsend_routed()` final-part micro-fusion 하나로 설명하긴 어렵다.
  - raw-msg probe와 이 결과를 합치면, current next step은 send wrapper나
    routed final-part helper를 더 파는 것이 아니라
    routed recv prefetch ordering differential과 공통 `_out_sync`
    serialization floor를 분리하는 쪽이다.
- 다음 iteration 우선순위
  - `ROUTER_ROUTER`에서는 aggregate wrapper elision과
    `xsend_routed()` final-part micro-fusion을 다시 올리지 않는다.
  - next hypothesis는 routed recv prefetch ordering differential과
    공통 `_out_sync` floor의 비중을 더 직접 가르는 probe다.

## 64. 2026-03-28 same-target routed send cache + one-lock combo rejected 로그

- 작업한 가설
  - 2026-03-28 current recheck에서 `DEALER_ROUTER`는 이미
    `tcp/inproc -30.83% / -31.56%`였지만,
    `ROUTER_ROUTER`는 `-56.84% / -28.68%`라서
    `tcp`에서는 `ROUTER` sender 전용 비용이 추가로 크게 남아 있다고 봤다.
  - 따라서 `router.cpp` same-target routed send cache와
    `pipe.cpp` final-part one-lock helper를 묶으면
    routed lookup + ready-check + write/flush hot path를 한 번에 줄일 수 있다고
    가정했다.
- candidate family
  - pattern-specific core routed send path
- high-leverage 근거
  - raw-msg probe와 earlier one-lock helper는 각각 단독으로 broad win이
    아니었지만, current recheck는 sender-only differential이 `tcp`에서
    여전히 크다는 새 증거를 줬다.
- 참고한 `libzmq` 대응 파일
  - [`libzmq/src/router.cpp`](/home/hep7/project/kairos/libzmq/src/router.cpp)
  - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
- `claude` consult 여부와 핵심 조언
  - `claude --help`는 통과했다.
  - `claude -p` prompt 인자 호출은
    `Input must be provided either through stdin or as a prompt argument when using --print`
    오류로 실패했다.
  - stdin 기반 재시도는 60초 이상 응답이 없어 advisory를 얻지 못했고,
    이번 iteration에서는 unavailable로 기록한다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/src/sockets/router.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.hpp)
    - [`core/src/sockets/router.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.cpp)
    - [`core/tests/integration/test_router_multiple_dealers.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_router_multiple_dealers.cpp)
- 실행한 명령
  - `claude --help`
  - `claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink "..."`
  - `cat <<'EOF' | claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink ... EOF`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_router_recheck_default`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_router_recheck_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_router_sender_split_recheck`
  - `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_router_multiple_dealers|test_router_mandatory_hwm|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns ROUTER_ROUTER,DEALER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_sender_cache_combo_default`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_sender_cache_combo_raw`
  - `timeout 30s ./core/build/bin/comp_zlink_router_router zlink tcp 64`
  - `timeout 30s ./core/build/bin/comp_zlink_router_router zlink inproc 64`
  - `timeout 30s ./core/build/bin/comp_zlink_dealer_router zlink tcp 64`
  - `timeout 30s ./core/build/bin/comp_zlink_dealer_router zlink inproc 64`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_141636_codex_20260328_router_router_recheck_default.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_141636_codex_20260328_router_router_recheck_default.txt)
  - [`perf_linux_20260328_141636_codex_20260328_router_router_recheck_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_141636_codex_20260328_router_router_recheck_raw.txt)
  - [`perf_linux_20260328_141742_codex_20260328_dealer_router_sender_split_recheck.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_141742_codex_20260328_dealer_router_sender_split_recheck.txt)
  - [`perf_linux_20260328_142545_codex_20260328_router_sender_cache_combo_default.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_142545_codex_20260328_router_sender_cache_combo_default.txt)
  - [`perf_linux_20260328_142545_codex_20260328_router_sender_cache_combo_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_142545_codex_20260328_router_sender_cache_combo_raw.txt)
- 핵심 수치
  - current recheck baseline
    - `ROUTER_ROUTER` default `tcp/inproc -56.84% / -28.68%`
    - `ROUTER_ROUTER` raw `tcp/inproc -58.04% / -23.52%`
    - `DEALER_ROUTER` default `tcp/inproc -30.83% / -31.56%`
  - combo candidate
    - default `ROUTER_ROUTER tcp/inproc -58.18% / -23.12%`
    - raw `ROUTER_ROUTER tcp/inproc -53.09% / -23.25%`
    - default `DEALER_ROUTER tcp/inproc -30.17% / -25.42%`
  - direct zlink absolute throughput
    - `ROUTER_ROUTER tcp`: `1214300.00 msg/s`
    - `ROUTER_ROUTER inproc`: `2419754.40 msg/s`
    - `DEALER_ROUTER tcp`: `2894032.50 msg/s`
    - `DEALER_ROUTER inproc`: `3101483.50 msg/s`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - same-target routed send cache
    - routed final-part one-lock helper combo
    - handover regression wiring attempt
- 해석
  - relative diff만 보면 `inproc`가 좋아진 것처럼 보였지만,
    direct `comp_zlink_*` absolute throughput은 current baseline 수준에서
    사실상 움직이지 않았다.
  - 즉 이번 combo candidate는 `libzmq` baseline 흔들림을 이용한 착시였고,
    current `ROUTER_ROUTER` 잔여 gap을 실제로 줄이지 못했다.
  - same-target routed send local cache와 final-part lock fusion은
    current source-of-truth에서 rejected family로 내려도 된다.
- 다음 iteration 우선순위
  - `ROUTER_ROUTER`에서는 same-target routed send cache,
    aggregate wrapper elision, final-part one-lock helper를 다시 올리지 않는다.
  - next hypothesis는 `recv_routed()` source-rid export와
    routed recv prefetch ordering differential을 먼저 분리하고,
    그 뒤에도 `tcp ~1.21Mmsg/s` cap이 남으면 공통 `_out_sync`
    serialization floor를 다시 직접 재는 것이다.

## 65. 2026-03-28 routed recv state/source-rid cache rejected 로그

- 작업한 가설
  - current `ROUTER_ROUTER` 잔여 gap이 send-side local helper보다
    `recv_routed()` source-rid export와 prefetch ordering 차이에 더 가깝다면,
    `router.cpp`에서 prefetched path와 normal path의 current-in/source-rid
    해석을 한 번으로 모으고 prefetched routing-id 준비를 lazy하게 미루면
    `xrecv()`, `xrecv_routed()`, `xhas_in()` 사이의 중복 work를 줄일 수 있다고
    봤다.
- candidate family
  - pattern-specific routed recv state/export path
- high-leverage 근거
  - raw/public aggregate wrapper, final-part one-lock helper,
    same-target routed send cache가 모두 direct absolute throughput을
    거의 못 움직였기 때문에, next cost center를 routed recv ordering /
    source-rid export 쪽에서 직접 좁혀야 했다.
- 참고한 `libzmq` 대응 파일
  - [`libzmq/src/router.cpp`](/home/hep7/project/kairos/libzmq/src/router.cpp)
  - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
- `claude` consult 여부와 핵심 조언
  - stdin 기반 `claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink`
    consult는 usable advisory를 반환했다.
  - 핵심 조언은 prefetched/non-prefetched source-rid resolution을 한 번으로
    모으는 방향은 시도할 가치가 있지만, keep 여부는 반드시 multipart
    source-rid contract와 direct absolute throughput으로 판정하라는 것이었다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/sockets/router.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.hpp)
    - [`core/src/sockets/router.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.cpp)
  - 유지:
    - [`core/tests/integration/test_public_inproc_multipart_send.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_public_inproc_multipart_send.cpp)
- 실행한 명령
  - `printf '...' | claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_monitor_socket_contract|test_router_multiple_dealers|test_router_mandatory_hwm|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns ROUTER_ROUTER,DEALER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_recv_state_cache_default`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_recv_state_cache_raw`
  - `timeout 30s ./core/build/bin/comp_zlink_router_router zlink tcp 64`
  - `timeout 30s ./core/build/bin/comp_zlink_router_router zlink inproc 64`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_144258_codex_20260328_router_recv_state_cache_default.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_144258_codex_20260328_router_recv_state_cache_default.txt)
  - [`perf_linux_20260328_144258_codex_20260328_router_recv_state_cache_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_144258_codex_20260328_router_recv_state_cache_raw.txt)
- 핵심 수치
  - default candidate
    - `DEALER_ROUTER tcp/inproc -27.89% / -30.42%`
    - `ROUTER_ROUTER tcp/inproc -57.01% / -22.77%`
  - raw candidate
    - `ROUTER_ROUTER tcp/inproc -52.96% / -20.99%`
  - direct zlink absolute throughput
    - `ROUTER_ROUTER tcp`: `1211724.60 msg/s`
    - `ROUTER_ROUTER inproc`: `2408252.00 msg/s`
- 유지한 변경 / 원복한 변경
  - 유지
    - `test_public_inproc_router_recv_multipart_with_source_rid_blocking()`
    - `test_public_inproc_router_msg_recv_rid_keeps_source_rid_across_reset()`
  - 원복
    - routed recv current-in/source-rid cache
    - lazy prefetched-id prepare
- 해석
  - relative diff만 보면 raw `inproc`가 다소 좋아진 것처럼 보였지만,
    direct `comp_zlink_router_router` absolute throughput은 previous baseline
    `tcp ~1.21Mmsg/s`, `inproc ~2.42Mmsg/s` 수준에서 사실상 움직이지 않았다.
  - 즉 이번 candidate도 `libzmq` baseline 흔들림에 따른 noise였고,
    current `ROUTER_ROUTER` 잔여 gap을 실제로 줄이지 못했다.
  - routed recv contract regression 두 개는 useful guardrail이므로 keep한다.
- 다음 iteration 우선순위
  - `ROUTER_ROUTER`에서는 local recv-state/source-rid cache나
    lazy prefetched-id prepare를 다시 올리지 않는다.
  - next hypothesis는 routed recv prefetch ordering differential과
    `recv_routed()` source-rid export path 자체를 더 직접 분리하는 것이다.

## 66. 2026-03-28 bisect 반영 후 실제 코드 적용 후보

- 결론 요약
  - historical first direct cause는 `9b91234c`의
    raw `send_exact/zlink_msg_recv` -> public `zlink_send/zlink_recv`
    surface shift다.
  - current residual direct cause는
    `socket_base_t::send()` public admission/lock과
    `pipe::_out_sync` send-path serialization 조합에 더 가깝다.
  - 따라서 next code candidate는 `ROUTER`/`PUBSUB` local helper보다
    공통 send-side 비용을 먼저 겨냥해야 한다.

- 바로 적용해서 볼 가치가 큰 순서
  1. [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
     의 `socket_base_t::send()` / `send_routed()` steady-state fast path
     - ordinary blocking one-way send에서
       `socket_public_api_scope_t` /
       `socket_public_api_lock_scope_t`
       비용을 더 줄일 수 있는지 본다
     - retry/HWM 경로보다 queue가 안 차도 매 메시지마다 드는 admission/lock
       고정비를 먼저 겨냥한다
  2. [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
     의 public lifecycle coordinator
     - `public_api_state`, `enter_public_api`,
       `begin_close_or_fail_busy`, sync lock state 전이를 더 싼 의미 단위로
       합칠 수 있는지 본다
     - 단, thread-safe contract 약화는 금지
  3. [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
     의 `_out_sync` 아래 same-ordering redundant work 추가 축소
     - current kept delta인 recursive `check_hwm()` elide와 같은 방향으로
       lock 자체를 제거하지 않고 lock 아래 중복 work를 더 줄인다
     - `write`, `write_and_flush`, `flush`, activation 판단/prepare 경로를
       다시 본다
  4. [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
     의 single-part public send fast path 회귀 방지
     - historical first collapse가 public multipart contract 강제에서 시작된 만큼,
       current HEAD에서 one-part send가 다시 clone/materialize 쪽으로
       새지 않도록 guardrail을 둔다
  5. [`core/src/core/multipart_send_txn.cpp`](/home/hep7/project/kairos/zlink/core/src/core/multipart_send_txn.cpp)
     의 single-part fallback residual 확인
     - single-frame one-way가 실제로 multipart txn helper를 다시 타는 residual이
       없는지 재확인한다

- 현재 우선순위를 낮춰야 하는 것
  - `recv parts_out` heap-return/export 추가 미세튜닝
  - `PUBSUB` / `ROUTER` local helper 반복
  - `backpressure/HWM`만을 본체로 보는 재탐색

- 다음 적용 기준
  - `PAIR`, `DEALER_DEALER` `64B tcp/inproc`에서 broad win이 보여야 keep
  - send-path를 건드렸으면 raw/public guardrail을 반드시 다시 찍는다
  - direct absolute throughput이 안 움직이면 relative diff만 좋아 보여도
    rejected candidate로 내린다

## 67. 2026-03-28 direct send-side candidate bundle probe

- 기준
  - manual direct baseline
    - [`perf_linux_20260328_manual_direct_baseline.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_manual_direct_baseline.txt)
    - `PAIR tcp/inproc`: `3214.07 / 3283.74 Kmsg/s`
    - `DEALER_DEALER tcp/inproc`: `3272.14 / 3182.09 Kmsg/s`

- candidate 1
  - 내용
    - `socket_base_msg.cpp`: common `send()` / `send_routed()`에서
      `reset_flags/reset_metadata` fast path 시도
    - `socket_runtime.cpp`: lifecycle atomic memory-order 완화 시도
    - `pair.cpp`, `lb.cpp`: single-part likely branch 시도
  - 결과
    - [`perf_linux_20260328_manual_direct_candidate1.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_manual_direct_candidate1.txt)
    - `PAIR tcp/inproc`: `3147.88 / 3241.36 Kmsg/s`
    - `DEALER_DEALER tcp/inproc`: `3273.01 / 3119.22 Kmsg/s`
  - 판정
    - `DEALER_DEALER tcp` relative diff만 좋아 보였지만 absolute zlink throughput은
      baseline과 사실상 동일했고 `PAIR`, `inproc`은 미세 악화였다.
    - broad win 아님. reject.

- candidate 2
  - 내용
    - candidate 1에서 lifecycle atomic memory-order 완화만 원복
    - `send()` common prep fast path + `PAIR/LB` likely branch만 유지
  - 결과
    - [`perf_linux_20260328_manual_direct_candidate2.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_manual_direct_candidate2.txt)
    - `PAIR tcp/inproc`: `3228.26 / 3231.54 Kmsg/s`
    - `DEALER_DEALER tcp/inproc`: `3207.91 / 3184.23 Kmsg/s`
    - raw/public guardrail:
      [`perf_linux_20260328_manual_direct_candidate2_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_manual_direct_candidate2_raw.txt)
      - `PAIR raw tcp/inproc`: `3267.84 / 2725.91 Kmsg/s`
      - `DEALER_DEALER raw tcp/inproc`: `3238.08 / 3231.58 Kmsg/s`
  - 판정
    - public wrapper penalty를 줄이는 broad win이 아니었다.
    - common `send()` prep micro-tuning은 current residual direct cause로 보기 어렵다.
    - reject.

- candidate 3
  - 내용
    - `fast_mutex.hpp`, `pipe.cpp`
    - `_out_sync`는 유지하되
      `check_write_status()/write()/write_and_flush()` 계열만
      non-recursive lock scope로 내리는 probe
  - 결과
    - [`perf_linux_20260328_manual_direct_candidate3_pipe_norec.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_manual_direct_candidate3_pipe_norec.txt)
    - `PAIR tcp/inproc`: `3091.67 / 3158.45 Kmsg/s`
    - `DEALER_DEALER tcp/inproc`: `3189.38 / 3135.08 Kmsg/s`
  - 판정
    - common one-way absolute throughput이 분명히 내려갔다.
    - `_out_sync` 아래 owner/depth bookkeeping 자체를 이런 식으로 건드리는 건
      current 구조에선 broad win이 아니다.
    - reject.

- keep / revert
  - keep
    - 없음
  - revert
    - `send()` common prep fast path bundle
    - lifecycle atomic memory-order 완화
    - `_out_sync` hot send non-recursive scope
  - current tree 상태
    - 위 세 후보는 모두 원복됐다.
    - 즉 이번 라운드 이후 current tree에 남아 있는 성능개선 코드는 없다.

- 현재 해석
  - `1~3` 후보는 모두 current residual direct cause를 직접 겨냥했지만,
    공통 broad win을 만들지 못했다.
  - 그래서 next step은 `helper-level micro tuning`이 아니라
    `socket_base_t::send()` public admission/lock 의미 단위와
    `pipe::_out_sync` serialization 의미 단위를 더 큰 구조로 다시 보는 것이다.

## 68. 2026-03-28 direct single-part send scope narrowing rejected 로그

- 작업한 가설
  - `a819ea3a` 이후 current `send()` / `send_routed()`는
    `socket_public_send_scope_t`가 initial `process_commands()` 전부터
    public sync를 잡고 들어간다.
  - direct single-part public send에서는 이 widened sync scope가
    공통 잔여 비용일 수 있으므로, admission은 유지하되
    sync를 `xsend()` / `xsend_routed()` 주변에서만 다시 잡으면
    `PAIR` / `DEALER_DEALER` broad win이 나올 수 있다고 봤다.
- candidate family
  - common direct send admission/lock scope
- high-leverage 근거
  - `ff0140e5` / `a819ea3a` / `98e7d324` / `9b91234c` historical map 중
    current tree에 가장 직접적으로 남아 있는 구조 차이는
    public admission/lock widened scope와 `_out_sync` serialization이기 때문이다.
- 참고한 `libzmq` 대응 파일
  - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`libzmq/src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언
  - `claude --help`는 통과했지만 stdin 기반
    `claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink`
    consult는 응답 없이 멈춰 unusable이었다.
  - 따라서 이번 단계의 advisory는 unavailable로 기록하고,
    invariant map + direct instrumentation을 authority 문서와 bench/test로
    직접 채웠다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
- 실행한 명령
  - `claude --help`
  - `printf '...' | claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_socket_with_handler|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_monitor_socket_contract|test_router_mandatory_hwm|test_router_multiple_dealers)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_direct_send_scope_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_direct_send_scope_raw`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_160033_codex_20260328_direct_send_scope_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_160033_codex_20260328_direct_send_scope_public.txt)
  - [`perf_linux_20260328_160113_codex_20260328_direct_send_scope_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_160113_codex_20260328_direct_send_scope_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -21.29% / -33.25%`
    - `DEALER_DEALER tcp/inproc -23.62% / -25.85%`
  - raw
    - `PAIR tcp/inproc -9.47% / -20.90%`
    - `DEALER_DEALER tcp/inproc -28.00% / -26.50%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - direct single-part `send()` / `send_routed()` initial sync unlock
    - relock-around-`xsend()` local tweak
- 해석
  - `PAIR raw tcp`는 좋아졌지만 public `PAIR inproc`와 `DEALER_DEALER`
    public/raw가 함께 무너져 broad win이 아니었다.
  - 즉 current residual gap을 direct single-part send scope narrowing 하나로
    설명할 수는 없고, same layer의 local tweak를 더 반복할 이유도 약하다.
  - next step은 `_out_sync` hot path가 실제로 어떤 cross-command state를
    보호하는지 invariant map을 먼저 적고, 그 뒤 structural 후보를 고르는 것이다.
- 다음 iteration 우선순위
  - `send()` / `send_routed()` direct single-part scope narrowing은
    다시 올리지 않는다.
  - 먼저 `_out_sync`가 보호하는 `_out_active`, `_peers_msgs_read`, `_state`,
    `_out_pipe` invariant를 write/flush/activate/hiccup/term 경로 기준으로
    정리한다.

## 69. 2026-03-28 `_out_sync` invariant map retained + direct instrumentation 로그

- 작업한 가설
  - common send-side structural round로 넘어가기 전에,
    `_out_sync`가 보호하는 outbound state cluster를 code-level invariant로
    먼저 고정해야 다음 후보가 helper-level local tweak로 다시 흩어지지 않는다.
  - 동시에 `PAIR` / `DEALER_DEALER inproc 64B` direct instrumentation 한 번으로
    `send admission/scope construct`와 `pipe serialization` 중 어느 쪽이 더
    두꺼운지 coarse split이 필요했다.
- candidate family
  - common differential structural prep + semantic probe
- high-leverage 근거
  - current residual 본체는 `process_commands` 호출 빈도보다
    public send scope와 `pipe::_out_sync` steady-state duty일 가능성이 높았고,
    다음 structural candidate가 의존할 invariant boundary가 먼저 필요했다.
- 참고한 `libzmq` 대응 파일
  - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`libzmq/src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언
  - `claude --help`는 통과했다.
  - stdin 기반 `claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink`
    consult는 응답 없이 멈춰 unusable이었으므로 unavailable로 기록한다.
- 수정한 파일 경로
  - 유지
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - 계측 뒤 원복
    - [`core/src/utils/clock.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/clock.hpp)
    - [`core/src/utils/clock.cpp`](/home/hep7/project/kairos/zlink/core/src/utils/clock.cpp)
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
- 실행한 명령
  - `claude --help`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_router_mandatory_hwm|test_socket_with_handler)$' -j1`
  - `ZLINK_SEND_PROFILE_PATH=... python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_inproc_profiled`
  - `ZLINK_SEND_PROFILE_PATH=... python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_inproc_profiled`
  - instrumentation patch 원복 뒤
    `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_router_mandatory_hwm|test_socket_with_handler)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pipe_invariant_refactor_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pipe_invariant_refactor_raw`
- 생성된 결과 파일 경로
  - [`pair_inproc_send_profile_20260328.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/pair_inproc_send_profile_20260328.txt)
  - [`dealer_inproc_send_profile_20260328.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/dealer_inproc_send_profile_20260328.txt)
  - [`perf_linux_20260328_162242_codex_20260328_pipe_invariant_refactor_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_162242_codex_20260328_pipe_invariant_refactor_public.txt)
  - [`perf_linux_20260328_162324_codex_20260328_pipe_invariant_refactor_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_162324_codex_20260328_pipe_invariant_refactor_raw.txt)
- pushed commit
  - `ea2527c3` (`refactor: codify pipe out-sync invariants`)
- 핵심 수치
  - `PAIR inproc` profile avg ticks
    - `socket_scope_construct 1265.60`
    - `socket_xsend_initial 731.46`
    - `pipe_write_and_flush 575.93`
    - `socket_process_commands_initial 56.37`
  - `DEALER_DEALER inproc` profile avg ticks
    - `socket_scope_construct 1313.89`
    - `socket_xsend_initial 774.33`
    - `pipe_write_and_flush 613.33`
    - `socket_process_commands_initial 56.28`
  - public guardrail
    - `PAIR tcp/inproc -12.44% / -17.04%`
    - `DEALER_DEALER tcp/inproc -11.19% / -18.11%`
  - raw guardrail
    - `PAIR tcp/inproc -24.04% / -17.10%`
    - `DEALER_DEALER tcp/inproc -32.25% / -23.04%`
- 유지한 변경 / 원복한 변경
  - 유지
    - `pipe.hpp` / `pipe.cpp` unlocked helper refactor
  - 원복
    - temporary direct instrumentation patch
- 해석
  - 계측 patch가 absolute throughput 자체는 크게 흔들었으므로 acceptance에는
    쓰지 않는다. 다만 same-run 내부 비중은 `process_commands`보다
    `send scope construct`와 `pipe_write_and_flush`가 훨씬 두껍다는 점을
    충분히 보여준다.
  - pair와 dealer의 coarse split이 비슷하므로, current 잔여 gap은
    단순 `public_api_sync` 한 비트보다
    public send scope 전체와 `pipe` final-part serialization을 같이 봐야 한다.
  - unlocked helper refactor는 keep-worthy perf delta가 아니라
    다음 structural candidate가 의존할 invariant boundary를 current tree에
    고정한 retained structural prep이다.
- 다음 iteration 우선순위
  - `_out_sync` invariant map 작성 자체는 다시 하지 않는다.
  - `socket_base_t::send()` / `socket_public_send_scope_t`의
    admission/scope construct cost를 줄이는 structural candidate부터 본다.
  - 그 다음 unlocked helper 위에서 hot send와 rare teardown 경계를 더
    분리하는 `_out_sync` structural candidate를 본다.

## 70. 2026-03-28 `DEALER` single-part admission-only + `lb` send-state lock rejected 로그

- 작업한 가설
  - current `PAIR`는 public admission-only fast path를 이미 쓰고 있고,
    `DEALER`만 `socket_public_send_scope_t` sync bit를 계속 잡는다.
  - `DEALER` single-part hot path의 mutable state가 사실상 `lb_t`에
    모여 있으므로, socket-wide public sync 대신 `lb` 내부 send-state lock으로
    직렬화를 내리면 current residual gap을 줄일 수 있다고 봤다.
- candidate family
  - common send admission/lock structural split
- high-leverage 근거
  - guide의 current target과 thread-safety internals 모두
    hot-path admission과 control-path serialization을 분리하는 방향을
    암시하고 있었고, `DEALER_DEALER`는 same-handle concurrent send contract를
    유지하면서도 `PAIR` 다음으로 가장 직접적인 공통 send bench였다.
- 참고한 `libzmq` 대응 파일
  - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`libzmq/src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`libzmq/src/lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)
- `claude` consult 여부와 핵심 조언
  - `claude --help`는 통과했다.
  - 첫 `claude -p ... "<prompt>"` 호출은
    `Input must be provided either through stdin or as a prompt argument when using --print`
    오류로 실패했다.
  - stdin 재시도는 출력 없이 멈춰 프로세스를 종료했고, 이번 단계 advisory는
    unavailable로 기록한다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
    - [`core/src/sockets/lb.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.hpp)
    - [`core/src/sockets/lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)
- 실행한 명령
  - `claude --help`
  - `claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq "<prompt>"`
  - `cat <<'EOF' | claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_multi_socket_contract_regressions|unittest_socket_runtime)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_monitor_socket_contract|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_singlepart_admission_public_pair`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_singlepart_admission_public_dealer`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_singlepart_admission_raw_pair`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_singlepart_admission_raw_dealer`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_163839_codex_20260328_dealer_singlepart_admission_public_pair.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_163839_codex_20260328_dealer_singlepart_admission_public_pair.txt)
  - [`perf_linux_20260328_163839_codex_20260328_dealer_singlepart_admission_public_dealer.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_163839_codex_20260328_dealer_singlepart_admission_public_dealer.txt)
  - [`perf_linux_20260328_163910_codex_20260328_dealer_singlepart_admission_raw_pair.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_163910_codex_20260328_dealer_singlepart_admission_raw_pair.txt)
  - [`perf_linux_20260328_163910_codex_20260328_dealer_singlepart_admission_raw_dealer.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_163910_codex_20260328_dealer_singlepart_admission_raw_dealer.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -6.31% / -30.93%`
    - `DEALER_DEALER tcp/inproc -15.36% / -21.40%`
  - raw
    - `PAIR tcp/inproc -9.37% / -35.69%`
    - `DEALER_DEALER tcp/inproc -11.45% / -32.76%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `DEALER` single-part direct send admission-only
    - `lb_t` send-state internal lock
- 해석
  - `DEALER_DEALER public`은 두 transport 모두 크게 회복했지만,
    raw `inproc`가 다시 크게 벌어져 public penalty 재도입이 아니라
    bench/stability interpretation 자체가 transport별로 엇갈렸다.
  - same run에서 unchanged `PAIR`도 `inproc` public/raw가 함께 크게 흔들려
    keep-worthy broad win으로 승격할 수 없었다.
  - 결론적으로 `DEALER` local lock migration 하나로는
    current common send residual을 설명할 수 없고,
    `lb_t` mutable state를 직접 감싸는 local 구조 변경을 broad hypothesis 없이
    다시 올리면 local search drift가 된다.
- 다음 iteration 우선순위
  - `DEALER` single-part admission-only + `lb_t` local lock family는
    새 structural 근거 없이 다시 올리지 않는다.
  - next step은 여전히 `socket_base_t::send()` admission/scope construct와
    `_out_sync` steady-state duty를 더 큰 구조로 다시 가르는 쪽이다.

## 71. 2026-03-28 `public_api_sync` fast-mutex split rejected 로그

- 작업한 가설
  - `a819ea3a` 이후 current common send residual의 한 축은
    `socket_runtime.cpp` `public_api_sync` CAS spin/wait라고 봤다.
  - inflight/closing bit accounting은 그대로 두고, 실제 sync wait만
    fast-mutex로 분리하면 `send scope construct` 고정비를 줄일 수 있다고
    가정했다.
- candidate family
  - common send admission/lock structural split
- high-leverage 근거
  - same-day direct instrumentation에서
    `PAIR` / `DEALER_DEALER` inproc 모두
    `socket_scope_construct 1265.60 / 1313.89 ticks`가 크게 남았고,
    guide의 current first-priority target도
    `a819ea3a` public admission/CAS 의미 단위를 다시 가르는 쪽이었다.
- 참고한 `libzmq` 대응 파일
  - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`libzmq/src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언
  - `claude --help`는 통과했다.
  - `timeout 40s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink "<prompt>"`는
    `Input must be provided either through stdin or as a prompt argument when using --print`
    오류로 끝나 advisory를 얻지 못했다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
- 실행한 명령
  - `claude --help`
  - `timeout 40s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink "<prompt>"`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_public_sync_mutex_split_pair`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_public_sync_mutex_split_dealer`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤 `ctest --test-dir core/build --output-on-failure -R '^unittest_socket_runtime$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_165110_codex_20260328_public_sync_mutex_split_pair.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_165110_codex_20260328_public_sync_mutex_split_pair.txt)
  - [`perf_linux_20260328_165136_codex_20260328_public_sync_mutex_split_dealer.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_165136_codex_20260328_public_sync_mutex_split_dealer.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -32.03% / -29.11%`
    - `DEALER_DEALER tcp/inproc -19.33% / -31.37%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.cpp` `public_api_sync` fast-mutex split
- 해석
  - `DEALER_DEALER tcp`는 current baseline `-24.09%`보다 회복했지만,
    `PAIR tcp/inproc`와 `DEALER_DEALER inproc`가 함께 더 나빠져
    broad win이 아니었다.
  - 즉 `a819ea3a` 잔여 비용을
    wait primitive 하나의 교체만으로 설명하는 것은 부족했고,
    current residual은 admission/scope 의미 단위와 `_out_sync`
    steady-state duty를 더 직접 가르는 쪽이 맞다.
- 다음 iteration 우선순위
  - `public_api_sync` wait primitive 교체 family는
    새 structural 근거 없이 다시 올리지 않는다.
  - next step은 여전히
    `socket_base_t::send()` admission/scope construct와
    `_out_sync` steady-state duty를 더 직접 분리하는 structural candidate다.

## 72. 2026-03-28 `pipe` hot send-only non-recursive lock split rejected 로그

- 작업한 가설
  - `ff0140e5` 이후 current common residual의 한 축은
    `pipe::_out_sync` recursive owner/depth bookkeeping 자체라고 봤다.
  - `write()/check_write()/flush()` hot path만 별도 non-recursive lock으로
    내리고, rare lifecycle/teardown만 기존 recursive `_out_sync`에 남기면
    same-ordering contract를 유지하면서 steady-state cost를 줄일 수 있다고
    가정했다.
- candidate family
  - common send-side structural split
- high-leverage 근거
  - same-day direct instrumentation에서
    `PAIR` / `DEALER_DEALER` inproc 모두 `pipe_write_and_flush
    575.93 / 613.33 ticks`가 컸고,
    current guide도 unlocked helper 위에서 hot send와 rare teardown 경계를
    더 분리하는 structural candidate를 다음 단계로 두고 있었다.
- 참고한 `libzmq` 대응 파일
  - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`libzmq/src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언
  - `claude --help`는 통과했다.
  - stdin 기반
    `claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
    consult는 60초 이상 출력 없이 멈춰 usable advisory를 얻지 못했고,
    이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
- 실행한 명령
  - `claude --help`
  - `cat <<'EOF' | claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_router_mandatory_hwm|test_socket_with_handler|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pipe_hot_send_split_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pipe_hot_send_split_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_171104_codex_20260328_pipe_hot_send_split_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_171104_codex_20260328_pipe_hot_send_split_public.txt)
  - [`perf_linux_20260328_171104_codex_20260328_pipe_hot_send_split_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_171104_codex_20260328_pipe_hot_send_split_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -17.14% / -34.56%`
    - `DEALER_DEALER tcp/inproc -13.65% / -19.47%`
  - raw
    - `PAIR tcp/inproc -8.61% / -25.46%`
    - `DEALER_DEALER tcp/inproc -20.69% / -21.15%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe` hot send-only non-recursive lock split
- 해석
  - `DEALER_DEALER` public은 두 transport 모두 회복했지만,
    `PAIR inproc` public absolute throughput이
    baseline `3392.77 Kmsg/s` 수준에서 `2610.13 Kmsg/s`로 크게 떨어졌고,
    raw `inproc`도 `3315.27 -> 3028.55 Kmsg/s`로 함께 악화됐다.
  - 즉 current residual을 "`hot send에서 recursive mutex bookkeeping만 떼면
    된다`"로 읽는 건 부족했고,
    pipe-only structural split은 current common broad win이 아니다.
  - 결론적으로 hot send와 rare teardown 경계 분리는 plausible axis이지만,
    pipe lock 계층만 단독으로 바꾸는 방향은 local search drift에 가깝다.
- 다음 iteration 우선순위
  - `pipe` hot send-only non-recursive split family는
    새 structural 근거 없이 다시 올리지 않는다.
  - next step은 여전히 `socket_base_t::send()` /
    `socket_public_send_scope_t` admission/scope construct cost를 먼저 줄이는
    structural candidate다.
  - 그 다음에야 `_out_sync` duty 분리를 다시 보더라도,
    pipe lock 계층 단독이 아니라 send admission/pipe serialization을 함께
    가르는 더 큰 구조여야 한다.

## 73. 2026-03-28 send-scope/lifecycle header-inline codegen-only candidate rejected 로그

- 작업한 가설
  - same-day direct instrumentation에서 `send scope construct`가
    `PAIR/DEALER_DEALER` 모두 가장 두꺼운 고정비로 남았지만,
    앞선 후보들은 주로 atomic 의미나 lock 계층 자체를 바꾸는 쪽이었다.
  - 그래서 semantic은 그대로 두고
    `socket_lifecycle_coordinator_t` /
    `socket_public_send_scope_t` hot-path 메서드를
    `socket_runtime.hpp`로 inline 이동하면,
    `send()` steady-state codegen을 더 얇게 만들어
    common residual을 줄일 수 있다고 봤다.
- candidate family
  - common send admission/scope structural round
- high-leverage 근거
  - current guide의 첫 미완료 항목이 여전히
    `a819ea3a` public admission/CAS 의미 단위와
    `_out_sync` steady-state duty를 더 직접 가르는 structural candidate였고,
    계측상 `send scope construct` 비중이 컸기 때문에
    codegen-only slimming도 같은 family 안에서 빠르게 판정할 가치가 있었다.
- 참고한 `libzmq` 대응 파일
  - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`libzmq/src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언
  - 같은 common send-side structural family continuation이어서
    이번 candidate 직전에 `claude`를 새로 재호출하지 않았다.
  - 가장 최근 same-family consult는 unavailable 상태였고,
    이번 단계는 guide/review/hot-path + actual bench/test를 authority로
    그대로 진행했다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_router_mandatory_hwm|test_socket_with_handler|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_inline_send_scope_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_inline_send_scope_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_172532_codex_20260328_inline_send_scope_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_172532_codex_20260328_inline_send_scope_public.txt)
  - [`perf_linux_20260328_172615_codex_20260328_inline_send_scope_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_172615_codex_20260328_inline_send_scope_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -21.70% / -23.83%`
    - `DEALER_DEALER tcp/inproc -10.64% / -17.99%`
  - raw
    - `PAIR tcp/inproc -11.65% / -27.25%`
    - `DEALER_DEALER tcp/inproc -8.99% / -25.87%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.hpp/.cpp` send-scope/lifecycle header-inline codegen-only candidate
- 해석
  - `DEALER_DEALER tcp`는 일부 회복했지만,
    public `PAIR tcp/inproc`가 current retained baseline보다 더 나빠졌고
    raw `PAIR inproc`, `DEALER_DEALER inproc`도 함께 크게 밀렸다.
  - 즉 current residual을
    "의미는 그대로 두고 codegen만 얇게 만들면 된다"로 읽는 것은 부족했고,
    current common send gap은 여전히 admission/serialization meaning unit
    자체를 더 직접 가르는 structural candidate를 요구한다.
- 다음 iteration 우선순위
  - send-scope/lifecycle header-inline codegen-only family는
    새 structural 근거 없이 다시 올리지 않는다.
  - next step은 여전히
    `socket_base_t::send()` /
    `socket_public_send_scope_t` admission/scope construct 의미 단위와
    `_out_sync` steady-state duty를 함께 다시 가르는 structural candidate다.

## 74. 2026-03-28 lazy send-scope sync acquire candidate rejected 로그

- 작업한 가설 1개
  - `send scope construct` 큰 고정비의 직접 원인 중 하나가
    constructor 시점 `public_api_sync` CAS라고 보고,
    public admission은 유지하되 sync acquire를 `xsend()` 직전으로 미루면
    steady-state send hot path가 회복될 수 있는지 확인했다.
- candidate family 1개
  - `socket_public_send_scope_t` constructor lazy-sync acquire
- high-leverage 또는 semantic probe 근거
  - direct instrumentation에서 `send scope construct ~1266/1314 ticks`가
    `process_commands initial ~56 ticks`보다 훨씬 커서,
    constructor 시점 sync CAS를 직접 줄이는 structural probe였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 성공했다.
  - 다만 repo 문서/파일을 같이 읽히는 `claude -p` consult 두 번은
    대기 시간 안에 유효 응답이 오지 않아 이번 iteration에서는
    unavailable로 기록한다.
- 수정한 파일 경로
  - [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - [`socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  - [`unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_router_mandatory_hwm)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_lazy_send_scope_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_lazy_send_scope_raw`
  - 원복 뒤 source tree 기준 diff가 사라지도록 수동 원복
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_174008_codex_20260328_lazy_send_scope_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_174008_codex_20260328_lazy_send_scope_public.txt)
  - [`perf_linux_20260328_174008_codex_20260328_lazy_send_scope_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_174008_codex_20260328_lazy_send_scope_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -9.29% / -27.64%`
    - `DEALER_DEALER tcp/inproc -26.48% / -25.53%`
  - raw
    - `PAIR tcp/inproc -13.68% / -25.35%`
    - `DEALER_DEALER tcp/inproc -25.66% / -19.94%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_public_send_scope_t` constructor lazy-sync acquire candidate
- 해석
  - `PAIR tcp`만 `-9.29%`까지 회복했지만 `PAIR inproc`가 `-27.64%`로 다시
    크게 벌어졌고 `DEALER_DEALER`도 broad win을 만들지 못했다.
  - raw/public도 한쪽만 좋아지는 sign이 아니어서,
    current residual을 constructor 시점 sync CAS 하나로 설명할 수 없었다.
- 다음 iteration 우선순위
  - lazy send-scope sync acquire family는 broad fix 후보에서 내린다.
  - 다음 step은 더 큰 의미 단위 재정렬 없이
    send-scope constructor tweak를 다시 누적하지 않는다.

## 75. 2026-03-28 flush notify-outside-lock candidate rejected 로그

- 작업한 가설 1개
  - `_out_sync` duty 중 hot send invariants와 peer wakeup publish를 분리하면
    `pipe_write_and_flush` steady-state 비용을 줄일 수 있는지 확인했다.
- candidate family 1개
  - `pipe.cpp` flush notify-outside-`_out_sync`
- high-leverage 또는 semantic probe 근거
  - direct instrumentation에서 `pipe_write_and_flush ~576/613 ticks`가
    `xsend()` 내부 대부분을 차지했으므로,
    lock 아래의 publish duty를 줄이는 structural probe였다.
- 참고한 `libzmq` 대응 파일
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - 이번 iteration의 `claude` consult는 위 74항과 동일하게
    유효 응답을 확보하지 못해 unavailable로 유지한다.
- 수정한 파일 경로
  - [`pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_router_mandatory_hwm)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_flush_notify_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_flush_notify_raw`
  - 원복 뒤 source tree 기준 diff가 사라지도록 수동 원복
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_174346_codex_20260328_flush_notify_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_174346_codex_20260328_flush_notify_public.txt)
  - [`perf_linux_20260328_174346_codex_20260328_flush_notify_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_174346_codex_20260328_flush_notify_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -16.34% / -25.54%`
    - `DEALER_DEALER tcp/inproc -26.94% / -23.32%`
  - raw
    - `PAIR tcp/inproc -30.05% / -18.28%`
    - `DEALER_DEALER tcp/inproc -7.24% / -17.59%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe.cpp` flush notify-outside-`_out_sync` candidate
- 해석
  - public 기준으로는 `PAIR tcp`조차 baseline `-18.89%`에서 noise 수준만
    회복했고, `PAIR inproc` / `DEALER`는 broad win을 만들지 못했다.
  - raw `DEALER tcp/inproc`만 일부 좋아진 것으로는 insufficient하며,
    raw `PAIR tcp -30.05%`가 크게 무너져 keep-worthy candidate가 아니다.
- 다음 iteration 우선순위
  - flush notify placement 같은 helper-level `_out_sync` local tweak는
    새 broad evidence 없이 다시 올리지 않는다.
  - common send-side structural round는 guide 재정렬과 direct profile 재독해를
    먼저 거친 뒤에만 다음 code candidate를 고른다.

## 76. 2026-03-28 public send scope + pipe exclusion merge candidate rejected 로그

- 작업한 가설 1개
  - `public_api_sync`와 `pipe::_out_sync`가 steady-state public send에서
    같은 exclusion을 두 번 표현하고 있다면,
    `PAIR`까지 public send scope를 넓히고 `pipe` hot write lock을 합쳐
    `send scope construct + pipe_write_and_flush`를 함께 줄일 수 있는지
    확인했다.
- candidate family 1개
  - `PAIR` public send scope + `pipe` serialized write merge
- high-leverage 또는 semantic probe 근거
  - `claude` advisory는
    "single exclusion boundary" candidate를 current top structural 후보로
    제시했고, direct profile에서도 `send scope construct`와
    `pipe_write_and_flush`가 둘 다 큰 고정비로 남아 있었다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했고, stdin 경유 `claude -p` consult는 이번에는
    유효 응답을 반환했다.
  - 핵심 조언은
    `public_api_sync`와 `_out_sync`를 steady-state send exclusion 하나로
    합치는 structural candidate를 먼저 검토하라는 것이었다.
- 수정한 파일 경로
  - [`socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
  - [`socket_base.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)
  - [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  - [`multipart_send_txn.cpp`](/home/hep7/project/kairos/zlink/core/src/core/multipart_send_txn.cpp)
  - [`pair.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.hpp)
  - [`pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp)
  - [`dealer.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.hpp)
  - [`dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)
  - [`lb.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.hpp)
  - [`lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)
  - [`pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
- 실행한 명령
  - `claude --help`
  - `printf '<prompt>' | claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_common_send_scope_pipe_merge_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_common_send_scope_pipe_merge_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_common_send_scope_pipe_merge_public_rerun`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_common_send_scope_pipe_merge_raw_rerun`
  - 원복 뒤 source tree 기준 diff가 사라지도록 수동 원복
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_175933_codex_20260328_common_send_scope_pipe_merge_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_175933_codex_20260328_common_send_scope_pipe_merge_public.txt)
  - [`perf_linux_20260328_175933_codex_20260328_common_send_scope_pipe_merge_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_175933_codex_20260328_common_send_scope_pipe_merge_raw.txt)
  - [`perf_linux_20260328_180022_codex_20260328_common_send_scope_pipe_merge_public_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_180022_codex_20260328_common_send_scope_pipe_merge_public_rerun.txt)
  - [`perf_linux_20260328_180103_codex_20260328_common_send_scope_pipe_merge_raw_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_180103_codex_20260328_common_send_scope_pipe_merge_raw_rerun.txt)
- 핵심 수치
  - 직렬 rerun public
    - `PAIR tcp/inproc -24.15% / -27.75%`
    - `DEALER_DEALER tcp/inproc -20.80% / -19.17%`
  - 직렬 rerun raw
    - `PAIR tcp/inproc -8.40% / -22.59%`
    - `DEALER_DEALER tcp/inproc -5.92% / -20.81%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `PAIR` public send scope + `pipe` serialized write merge candidate
- 해석
  - `DEALER` 일부 지표는 좋아졌지만 `PAIR` public이 baseline
    `-18.89% / -17.22%` 대비 명확히 악화돼 broad win이 아니었다.
  - raw/public guardrail도 `PAIR inproc`, `DEALER inproc`에서 여전히 mixed라서
    send exclusion 두 층을 합치는 것만으로 current residual을 설명할 수
    없었다.
- 다음 iteration 우선순위
  - `public_api_sync`와 `_out_sync`를 한 경계로 합치는 family는
    새 evidence 없이 다시 올리지 않는다.
  - 다음 step은 guide/review/hot-path 재정렬 이후 더 직접적인
    의미 단위 분리로 돌아간다.

## 77. 2026-03-28 public_api_state exact-state fast path candidate rejected 로그

- 작업한 가설 1개
  - `a819ea3a` public admission/CAS 의미 단위를 더 직접 줄이기 위해
    `public_api_state`의 exact-state steady-state 전이
    (`0 -> 1`, `1 -> 0`, `1|sync -> 1/0`)만 빠르게 하면
    `send scope construct` 비용이 broad하게 줄 수 있는지 확인했다.
- candidate family 1개
  - `socket_runtime.cpp` `public_api_state` exact-state fast path
- high-leverage 또는 semantic probe 근거
  - 위 76항 merge candidate를 원복한 뒤에도
    `send scope construct`가 direct profile의 가장 큰 cost 축 중 하나였으므로,
    `pipe` 쪽을 건드리지 않고 admission/CAS 의미 단위만 단독으로 분리하는
    structural follow-up probe였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - 이번 candidate는 76항 consult의 후속 분리 probe라
    추가 `claude` 재호출 없이 진행했다.
- 수정한 파일 경로
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_public_api_state_fastpath_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_public_api_state_fastpath_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤 `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_180412_codex_20260328_public_api_state_fastpath_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_180412_codex_20260328_public_api_state_fastpath_public.txt)
  - [`perf_linux_20260328_180452_codex_20260328_public_api_state_fastpath_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_180452_codex_20260328_public_api_state_fastpath_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -14.49% / -31.80%`
    - `DEALER_DEALER tcp/inproc -12.85% / -21.98%`
  - raw
    - `PAIR tcp/inproc -12.00% / -24.87%`
    - `DEALER_DEALER tcp/inproc -23.71% / -16.20%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.cpp` `public_api_state` exact-state fast path candidate
- 해석
  - public `PAIR inproc`가 baseline보다 크게 악화됐고,
    raw는 `DEALER tcp`가 다시 `-23.71%`까지 내려가 guardrail이 mixed였다.
  - 즉 current residual은 exact-state CAS shortcut 하나로는 broad하게
    설명되지 않았고, helper-level lifecycle fast path로도 승격되지 않는다.
- 다음 iteration 우선순위
  - `public_api_state` exact-state fast path family는 broad fix 후보에서 내린다.
  - guide 재작성 트리거를 유지한 채, 다음 round는 code patch 전에
    current summary와 direct profile 해석을 다시 정렬한다.

## 78. 2026-03-28 send admission boundary retained structural prep 로그

- 작업한 가설 1개
  - common send-side actual candidate를 다시 올리기 전에,
    `socket_public_send_scope_t`와 direct send helpers에 엉킨
    public inflight admission, same-handle send serialization,
    retry sync unlock/relock, logical multipart scope reuse를
    code-level 경계로 먼저 분리해 두면
    다음 `a819ea3a` / `_out_sync` structural round를 더 좁게 다룰 수 있다.
- candidate family 1개
  - retained send admission/sync meaning-boundary prep
- high-leverage 또는 semantic probe 근거
  - guide 재작성 뒤 첫 미완료 항목이 local tweak가 아니라
    `send admission` 의미 단위 structural prep을 먼저 남기라는 것이었고,
    current profile도 `send scope construct`와 `pipe_write_and_flush`를
    다음 broad axis로 가리키고 있었다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - 새 family 직전 `timeout 50s claude -p ...` consult는
    유효 응답 없이 종료돼 unavailable로 기록한다.
  - 따라서 이번 단계 authority는 guide/review/hot-path와 실제 build/test/bench다.
- 수정한 파일 경로
  - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
  - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - [`core/src/core/multipart_send_txn.cpp`](/home/hep7/project/kairos/zlink/core/src/core/multipart_send_txn.cpp)
  - docs authority realignment push hash:
    `67b4a4bcc0324dbe3c10bec51e1df9b6c8de1888`
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_scope_boundary_prep_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_scope_boundary_prep_raw`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_182242_codex_20260328_send_scope_boundary_prep_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_182242_codex_20260328_send_scope_boundary_prep_public.txt)
  - [`perf_linux_20260328_182242_codex_20260328_send_scope_boundary_prep_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_182242_codex_20260328_send_scope_boundary_prep_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -14.85% / -21.37%`
    - `DEALER_DEALER tcp/inproc -24.99% / -17.70%`
  - raw
    - `PAIR tcp/inproc -6.56% / -21.49%`
    - `DEALER_DEALER tcp/inproc -20.22% / -20.81%`
- 유지한 변경 / 원복한 변경
  - 유지
    - direct send retry를 `send_direct_with_retry()` 경계로 통합
    - retry sync hold/release/reacquire 판단을
      `socket_public_send_scope_t` helper로 명시
    - plain direct send scope 결정을
      `socket_base_t::direct_send_needs_public_api_sync()`로 재사용
  - 원복
    - 없음
- 해석
  - 이번 단계는 keep-worthy perf delta를 노린 code candidate가 아니라,
    다음 broad candidate를 더 좁게 다루기 위한 retained structural prep이다.
  - public/raw pair+dealer guardrail은 current retained baseline 대비
    mixed/noise였지만, 구조 변경을 버려야 할 만큼의 새 broad regression은
    보이지 않았다.
  - 따라서 current tree에는 이 prep을 남기고,
    다음 단계부터는 이 boundary 위에서 실제 `a819ea3a` / `_out_sync`
    candidate를 다시 고른다.
- 다음 iteration 우선순위
  - `send_direct_with_retry()` boundary 위에서
    `a819ea3a` admission/CAS 의미 단위를 실제로 줄이는 후보를 먼저 고른다.
  - 그 다음 필요하면 `_out_sync` duty candidate를 다시 올리되,
    이미 reject된 helper-level family는 반복하지 않는다.

## 79. 2026-03-28 `public_api_inflight/public_api_closing/public_api_sync` split candidate rejected 로그

- 작업한 가설 1개
  - retained send admission boundary prep 위에서
    `public inflight admission/closing`과 `same-handle send serialization`이
    같은 원자 상태를 두드리는 구조 자체가 `send scope construct` 고정비의
    한 축일 수 있다고 보고,
    `socket_runtime.hpp/.cpp`에서 이를 separate atomic으로 분리하면
    `PAIR` admission-only와 `DEALER` send-sync steady-state를 함께 줄일 수
    있는지 확인했다.
- candidate family 1개
  - common send-side structural split
- high-leverage 또는 semantic probe 근거
  - direct profile에서 `PAIR`조차 sync 없이
    `send scope construct ~1266 ticks`가 컸으므로,
    이번 후보는 `public_api_sync` wait primitive 하나가 아니라
    public lifecycle coordinator의 state packing 자체를 structural하게
    분리하는 probe였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - `timeout 50s claude -p "Review current zlink common send-side perf hypothesis. Focus on socket_base send admission/lock and pipe serialization; suggest one structural candidate only, no code, no files."`
    는 출력 없이 timeout으로 끝나 usable advisory를 얻지 못했다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    - [`core/tests/unittest/unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
- 실행한 명령
  - `claude --help`
  - `timeout 50s claude -p "Review current zlink common send-side perf hypothesis. Focus on socket_base send admission/lock and pipe serialization; suggest one structural candidate only, no code, no files."`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_admission_split_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_admission_split_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤 `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_socket_with_handler)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_183937_codex_20260328_admission_split_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_183937_codex_20260328_admission_split_public.txt)
  - [`perf_linux_20260328_183937_codex_20260328_admission_split_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_183937_codex_20260328_admission_split_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -24.72% / -26.53%`
    - `DEALER_DEALER tcp/inproc -19.44% / -22.01%`
  - raw
    - `PAIR tcp/inproc -9.25% / -17.70%`
    - `DEALER_DEALER tcp/inproc -17.73% / -31.95%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.hpp/.cpp`
      `public_api_inflight/public_api_closing/public_api_sync` split candidate
    - companion `unittest_socket_runtime` adjustment
- 해석
  - `DEALER tcp`와 raw 일부 지표는 noise 수준으로만 움직였지만,
    public `PAIR tcp/inproc`가 retained baseline보다 명확히 악화됐고
    raw `DEALER inproc`도 `-31.95%`까지 내려가 broad win이 아니었다.
  - 즉 current residual의 `send admission/scope construct` 비용은
    public lifecycle coordinator state packing 하나로 설명되지 않는다.
  - 다음 step은 coordinator repack을 반복하지 않고,
    retained send boundary prep + `_out_sync` unlocked helper 위에서
    hot send / rare teardown duty를 직접 가르는 structural candidate다.
- 다음 iteration 우선순위
  - `public_api_inflight/public_api_closing/public_api_sync` split family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 라운드는 `libzmq` pass 뒤
    `_out_pipe` lifetime / `_out_active` / `_state` / peer wakeup publish가
    steady-state `write_and_flush`와 어디서 분리될 수 있는지부터 다시 쓴다.

## 80. 2026-03-28 final non-routing payload flush helper candidate rejected 로그

- 작업한 가설 1개
  - retained send admission boundary prep + `_out_sync` unlocked helper 위에서,
    `PAIR` / `DEALER` final-part steady-state가 여전히
    `write_and_flush()` 안의 `more/routing-id` 분기와 helper hop을
    메시지마다 반복한다고 보고,
    final non-routing payload path 전용 helper를 넣으면 same-ordering을
    유지한 채 `_out_sync` 안 work를 줄일 수 있는지 확인했다.
- candidate family 1개
  - common send-side structural round
- high-leverage 또는 semantic probe 근거
  - same-day profile에서 `pipe_write_and_flush`가 큰 고정비였고,
    이번 후보는 lock 계층을 다시 바꾸지 않고 current helper boundary 위에서
    final payload steady-state work만 직접 줄이는 probe였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `printf '<prompt>' | timeout 50s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
    는 이번에도 timeout으로 끝나 usable advisory를 얻지 못했다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/src/sockets/pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp)
    - [`core/src/sockets/lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)
- 실행한 명령
  - `printf '<prompt>' | timeout 50s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_payload_flush_helper_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_payload_flush_helper_raw`
  - `kill 1179198 1186340`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤 `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_185452_codex_20260328_payload_flush_helper_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_185452_codex_20260328_payload_flush_helper_public.txt)
  - [`perf_linux_20260328_185533_codex_20260328_payload_flush_helper_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_185533_codex_20260328_payload_flush_helper_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -12.51% / -26.39%`
    - `DEALER_DEALER tcp/inproc -9.53% / -19.49%`
  - raw
    - `PAIR inproc -34.78%`
    - `DEALER_DEALER inproc -21.83%`
    - `PAIR tcp no_data`
    - `DEALER_DEALER tcp timeout`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe/pair/lb` final non-routing payload flush helper candidate
- 해석
  - `PAIR tcp`와 `DEALER tcp`는 일부 회복했지만,
    `PAIR inproc` public이 accepted baseline보다 더 악화됐고
    raw `PAIR inproc` guardrail도 `-34.78%`로 크게 깨졌다.
  - raw tcp run 중에는 이전 세션에서 10시간 넘게 남아 있던 stale
    `run_comparison.py` / `comp_zlink_dealer_dealer`를 정리했지만,
    cleanup 뒤에도 `DEALER_DEALER raw tcp timeout`이 남아 keep-worthy broad
    win으로 볼 수 없었다.
  - 결론적으로 payload-only/local helper family는 current residual의 본체가
    아니고, next step은 helper hop 하나를 더 줄이는 것이 아니라
    send admission + pipe serialization 의미 단위를 더 큰 구조로 가르는 쪽이다.
- 다음 iteration 우선순위
  - final non-routing payload flush helper family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 라운드는 여전히 `send_direct_with_retry()` boundary와
    `_out_sync` helper 경계를 함께 써서 hot send / rare teardown duty를
    더 큰 구조로 분리하는 후보를 고른다.

## 81. 2026-03-28 `process_activate_write()` already-active peer-progress snapshot split rejected 로그

- 작업한 가설 1개
  - retained send admission boundary prep + `_out_sync` helper 위에서,
    already-active steady state의 `activate_write` command는
    false->true wakeup 전이를 만들지 않는데도 every command마다 `_out_sync`를
    잡는 점이 common send-side residual의 일부일 수 있다고 봤다.
  - `process_activate_write()`를
    `peer-progress snapshot 갱신`과 `false->true wakeup transition`으로
    분리하면, same-ordering을 유지한 채 hot send와 peer-progress consume
    충돌을 줄일 수 있는지 확인했다.
- candidate family 1개
  - common send-side structural round
- high-leverage 또는 semantic probe 근거
  - guide의 미완료 항목이
    `send admission boundary prep + _out_sync helper` 위의 실제 structural
    round였고,
    this candidate는 payload-only helper가 아니라
    `peer progress / wakeup publish / send serialization` duty split을
    다시 쓰는 쪽이었다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)
  - [`pair.cpp`](/home/hep7/project/kairos/libzmq/src/pair.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반
    `printf '<prompt>' | timeout 50s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
    consult는 이번에도 출력 없이 timeout으로 끝나 usable advisory를 얻지
    못했다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
- 실행한 명령
  - `claude --help`
  - `printf '<prompt>' | timeout 50s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_activate_write_snapshot_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_activate_write_snapshot_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_activate_write_snapshot_public_rerun`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_activate_write_snapshot_raw_rerun`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_activate_write_snapshot_broader_single`
  - `kill 1179198 1186340 2047235 2048830`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤 `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_190959_codex_20260328_activate_write_snapshot_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_190959_codex_20260328_activate_write_snapshot_public.txt)
  - [`perf_linux_20260328_191040_codex_20260328_activate_write_snapshot_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_191040_codex_20260328_activate_write_snapshot_raw.txt)
  - [`perf_linux_20260328_191133_codex_20260328_activate_write_snapshot_public_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_191133_codex_20260328_activate_write_snapshot_public_rerun.txt)
  - [`perf_linux_20260328_191214_codex_20260328_activate_write_snapshot_raw_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_191214_codex_20260328_activate_write_snapshot_raw_rerun.txt)
  - partial broader single artifact:
    [`perf_linux_20260328_191305_codex_20260328_activate_write_snapshot_broader_single.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_191305_codex_20260328_activate_write_snapshot_broader_single.txt)
- 핵심 수치
  - targeted public first/rerun
    - `PAIR tcp/inproc -13.56% / -20.63%`
    - `PAIR tcp/inproc -13.02% / -17.35%`
    - `DEALER_DEALER tcp/inproc -15.10% / -19.03%`
    - `DEALER_DEALER tcp/inproc -14.66% / -18.53%`
  - targeted raw first/rerun
    - `PAIR tcp/inproc -14.64% / -21.01%`
    - `PAIR tcp/inproc -9.67% / -19.42%`
    - `DEALER_DEALER tcp/inproc -6.68% / -26.85%`
    - `DEALER_DEALER tcp/inproc -8.19% / -19.85%`
  - broader single
    - `PAIR tcp/inproc -8.43% / -21.82%`
    - `PUBSUB tcp/inproc -18.27% / -15.75%`
    - `DEALER_DEALER tcp/inproc -11.48% / -29.32%`
    - `DEALER_ROUTER tcp/inproc -19.84% / -30.93%`
    - partial `ROUTER_ROUTER tcp -52.69%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `process_activate_write()` already-active peer-progress snapshot split
- 해석
  - targeted `PAIR` / `DEALER_DEALER` public/raw rerun만 보면 회복 신호가 있었지만,
    broader single에서 `DEALER_DEALER inproc`와 `DEALER_ROUTER inproc`가
    accepted baseline 대비 분명히 더 악화됐다.
  - partial broader single artifact는 `ROUTER_ROUTER tcp -52.69%`까지만 남기고
    `comp_zlink_router_router zlink inproc 64`에서 멈췄다.
    keep-worthy delta는 broader single / smoke 단계에서 hang을 만들면 안 되므로
    이 후보는 reject가 맞다.
  - 결론적으로 `_out_sync` duty를 peer-progress snapshot-only split 하나로
    가르는 family는 current residual의 broad answer가 아니다.
- 다음 iteration 우선순위
  - `process_activate_write()` peer-progress snapshot-only split family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 라운드는 다시
    `send scope construct + pipe serialization` 의미 단위를 함께 줄이는
    structural candidate 하나만 고른다.

## 82. 2026-03-28 `process_activate_write()` atomic peer-progress publish candidate rejected 로그

- 작업한 가설 1개
  - same `process_activate_write()` family라도
    already-active fast path의 peer-progress publish를 atomic snapshot으로
    고정하면,
    previous snapshot split의 broader hang 없이 hot send와 peer-progress
    consume 충돌만 더 안전하게 줄일 수 있을지 확인했다.
- candidate family 1개
  - common send-side structural round
- high-leverage 또는 semantic probe 근거
  - guide의 첫 미완료 항목이 여전히
    `send admission boundary prep + _out_sync helper` 위의 common structural
    round였고,
    this candidate는 payload/local helper가 아니라
    `peer progress publish + send serialization` duty를 다시 가르는 probe였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)
  - [`pair.cpp`](/home/hep7/project/kairos/libzmq/src/pair.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - prompt 인자 형태 `timeout 50s claude -p ... "Review current zlink ..."`
    는 `Input must be provided either through stdin or as a prompt argument`
    오류로 끝났고,
    stdin 기반 재시도는 `timeout 50s`로 종료돼 usable advisory를 얻지 못했다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
- 실행한 명령
  - `claude --help`
  - `timeout 50s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq "Review current zlink common send-side perf hypothesis. Focus on socket_base send admission/lock and pipe serialization; suggest one structural candidate only, no code, no files."`
  - `printf '<prompt>' | timeout 50s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_activate_write_atomic_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_activate_write_atomic_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤 `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_193333_codex_20260328_activate_write_atomic_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_193333_codex_20260328_activate_write_atomic_public.txt)
  - [`perf_linux_20260328_193418_codex_20260328_activate_write_atomic_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_193418_codex_20260328_activate_write_atomic_raw.txt)
- 핵심 수치
  - targeted public
    - `PAIR tcp/inproc -22.80% / -18.39%`
    - `DEALER_DEALER tcp/inproc -35.30% / -19.86%`
  - targeted raw
    - `PAIR tcp/inproc -23.83% / -31.74%`
    - `DEALER_DEALER tcp/inproc -11.45% / -15.34%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `process_activate_write()` atomic peer-progress publish candidate
- 해석
  - same family의 snapshot split first/rerun보다도 이번 atomic publish variant는
    targeted public stage에서 바로 더 나빴다.
    특히 `DEALER_DEALER tcp -35.30%`가 retained baseline과 거리가 멀고,
    raw `PAIR inproc -31.74%`도 guardrail 수준에서 크게 깨졌다.
  - 따라서 이 variant는 broader single로 승격할 가치가 없었고,
    peer-progress publish family 자체를 current residual의 broad answer로
    다시 올리면 안 된다.
- 다음 iteration 우선순위
  - `process_activate_write()` snapshot split / atomic publish family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 라운드는 다시
    `send scope construct + pipe serialization` 의미 단위를 함께 줄이는
    다른 structural candidate 하나만 고른다.

## 83. 2026-03-28 existing public-send-sync-held `send_serialized` pipe helper candidate rejected 로그

- 작업한 가설 1개
  - retained send admission boundary prep + `_out_sync` unlocked helper 위에서,
    existing public send sync가 이미 잡힌 `DEALER` / `ROUTER` caller라면
    `pipe::_out_sync`의 steady-state send lock 일부를
    caller-owned exclusion으로 대체할 수 있다고 봤다.
  - `PAIR`까지 public scope를 넓히지 않고도
    `send scope construct + pipe serialization` 의미 단위를 함께 줄일 수
    있는지 확인했다.
- candidate family 1개
  - common send-side structural round
- high-leverage 또는 semantic probe 근거
  - same-day profile에서 `send scope construct`와 `pipe_write_and_flush`가
    둘 다 큰 고정비였고, 이번 후보는 previous `PAIR` scope widening family를
    반복하지 않으면서 이미-held public send exclusion을
    `pipe` hot send path와 결합해 보는 probe였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)
  - [`pair.cpp`](/home/hep7/project/kairos/libzmq/src/pair.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반 `claude -p` consult는 usable output을 반환했고,
    핵심 조언은 "primitive replacement보다 caller가 이미 쥔 send-side
    exclusion을 활용하는 structural candidate 하나만 보라"는 쪽이었다.
  - 다만 실측상 existing public send sync는 `_out_sync` steady-state duty를
    대체하지 못해 이번 family는 reject로 정리한다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
    - [`core/src/sockets/lb.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.hpp)
    - [`core/src/sockets/lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)
    - [`core/src/sockets/dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)
    - [`core/src/sockets/router.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.cpp)
- 실행한 명령
  - `claude --help`
  - `printf '<prompt>' | claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_router_multiple_dealers|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_serialized_pipe_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_serialized_pipe_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤 `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_router_multiple_dealers|test_stream_send_blocking_wakeup)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_195141_codex_20260328_send_serialized_pipe_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_195141_codex_20260328_send_serialized_pipe_public.txt)
  - [`perf_linux_20260328_195141_codex_20260328_send_serialized_pipe_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_195141_codex_20260328_send_serialized_pipe_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -13.23% / -17.01%`
    - `DEALER_DEALER tcp/inproc -23.74% / -31.19%`
    - `DEALER_ROUTER tcp/inproc -29.36% / -28.30%`
    - `ROUTER_ROUTER tcp/inproc -55.88% / -22.76%`
  - raw
    - `PAIR tcp/inproc -18.50% / -22.98%`
    - `DEALER_DEALER tcp/inproc -20.96% / -23.13%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - existing public-send-sync-held `send_serialized` pipe helper candidate
- 해석
  - `PAIR`는 unchanged control에 가까운 public 결과도 accepted baseline보다
    충분히 좋아지지 않았고, raw guardrail은 오히려 더 나빠졌다.
  - `DEALER_DEALER`는 direct target이었는데도 public `inproc -31.19%`,
    raw `inproc -23.13%`로 악화돼 caller-owned send exclusion이
    `_out_sync` steady-state serialization을 대신하지 못했다.
  - broader public single의 `DEALER_ROUTER` / `ROUTER_ROUTER`도 같이 흔들려,
    이 family는 previous `PAIR` scope widening family와 다른 형태여도
    current broad answer가 아니다.
- 다음 iteration 우선순위
  - existing public-send-sync-held `send_serialized` helper family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 라운드는 다시
    `send scope construct + pipe serialization` 의미 단위를 함께 줄이는
    다른 structural candidate 하나만 고른다.

## 84. 2026-03-28 `DEALER` external send-state mutex + external `send_serialized` scope candidate rejected 로그

- 작업한 가설 1개
  - retained send admission boundary prep + `_out_sync` unlocked helper 위에서,
    `DEALER` same-handle send serialization을 current `public_api_sync`
    밖으로 빼서 external recursive mutex와
    external `socket_public_send_scope_t` serialized scope로 재배치하면,
    caller-visible send scope construct 비용과 `pipe` steady-state
    serialization 비용을 함께 낮출 수 있다고 봤다.
  - previous `send_serialized` helper처럼 existing public send sync를
    재사용하는 대신, `DEALER` send-state 전용 exclusion을 별도 축으로
    세우는 structural probe였다.
- candidate family 1개
  - common send-side structural round
- high-leverage 또는 semantic probe 근거
  - current retained boundary가
    `send_direct_with_retry()` /
    `socket_public_send_scope_t::should_hold_sync_during_retry()` /
    `socket_base_t::direct_send_needs_public_api_sync()` /
    `_out_sync` unlocked helper까지는 이미 준비된 상태였고,
    이번 candidate는 same-handle `DEALER` send serialization 위치만
    바꿔도 common residual이 줄어드는지 보는 high-leverage probe였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)
  - [`pair.cpp`](/home/hep7/project/kairos/libzmq/src/pair.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반 `claude -p` consult는 `timeout 60s`로 종료돼 usable
    advisory를 얻지 못했다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
    - [`core/src/sockets/socket_base_api.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_api.cpp)
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
    - [`core/src/core/multipart_send_txn.cpp`](/home/hep7/project/kairos/zlink/core/src/core/multipart_send_txn.cpp)
    - [`core/src/sockets/dealer.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.hpp)
    - [`core/src/sockets/dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)
    - [`core/tests/unittest/unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
- 실행한 명령
  - `claude --help`
  - `printf '<prompt>' | timeout 60s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_send_mutex_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_send_mutex_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc) --target unittest_socket_runtime test_socket_with_handler test_multi_socket_contract_regressions test_public_inproc_multipart_send test_router_mandatory_hwm`
  - 원복 뒤 `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_201814_codex_20260328_dealer_send_mutex_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_201814_codex_20260328_dealer_send_mutex_public.txt)
  - [`perf_linux_20260328_201905_codex_20260328_dealer_send_mutex_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_201905_codex_20260328_dealer_send_mutex_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -9.23% / -16.03%`
    - `DEALER_DEALER tcp/inproc -13.09% / -32.97%`
  - raw
    - `PAIR tcp/inproc -13.88% / -26.40%`
    - `DEALER_DEALER tcp/inproc -24.35% / -33.20%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `DEALER` external send-state mutex + external `send_serialized` scope
      candidate
- 해석
  - public `PAIR tcp`는 일부 덜 흔들렸지만, direct target인
    `DEALER_DEALER inproc -32.97%`와 raw `DEALER_DEALER tcp/inproc -24.35% /
    -33.20%`가 current accepted baseline과 거리가 멀다.
  - 즉 same-handle `DEALER` send serialization을 current
    `public_api_sync` 밖으로 옮겨도 `_out_sync` steady-state duty와
    common send scope construct residual은 broad하게 줄지 않았다.
  - previous existing-public-sync `send_serialized` helper family와 마찬가지로,
    serialization 위치만 바꾸는 계열은 current broad answer가 아니다.
- 다음 iteration 우선순위
  - `DEALER` external send-state mutex / external `send_serialized` scope
    family는 새 broad evidence 없이 다시 올리지 않는다.
  - 다음 라운드는 다시
    `send scope construct + pipe serialization` 의미 단위를 함께 줄이는
    다른 structural candidate 하나만 고른다.

## 85. 2026-03-28 existing public-send-sync-held pipe hot-send lease / outpipe lifetime split candidate rejected 로그

- 작업한 가설 1개
  - retained send admission boundary prep + `_out_sync` unlocked helper 위에서,
    existing public send sync가 이미 잡힌 `DEALER` caller라면
    final `write+flush` hot path를 `_out_sync` 밖 pipe hot-send lease로
    보내고, rare `_out_pipe` mutation만 inflight send가 끝날 때까지
    기다리게 하면 `_out_sync` steady-state duty를 줄일 수 있다고 봤다.
  - previous `send_serialized` helper처럼 `_out_sync` 자체를 없애는 대신,
    hot send와 rare teardown/outpipe lifetime duty를 다른 방식으로 가르는
    structural probe였다.
- candidate family 1개
  - common send-side structural round
- high-leverage 또는 semantic probe 근거
  - current summary의 actual code priority가
    `_out_sync` no-op이 아니라
    hot send와 rare teardown/outpipe lifetime duty split이었고,
    이번 후보는 existing public send sync 재사용이 가능한 `DEALER`
    caller에서 그 split을 직접 검증하는 첫 구조 실험이었다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)
  - [`pair.cpp`](/home/hep7/project/kairos/libzmq/src/pair.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - 추가 `claude` consult는 실행하지 않았다.
  - current guide priority가
    `pair/dealer` send profile + libzmq/current code reread 뒤
    한 후보만 바로 올리는 단계였으므로, 이번 round는 local code probe로
    바로 진행했다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/src/sockets/lb.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.hpp)
    - [`core/src/sockets/lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)
    - [`core/src/sockets/dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)
    - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_hot_send_lease_public`
  - 원복 뒤 `cmake --build core/build -j$(nproc) --target libzlink unittest_socket_runtime test_socket_with_handler test_multi_socket_contract_regressions test_public_inproc_multipart_send test_router_mandatory_hwm`
  - 원복 뒤 `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_204034_codex_20260328_dealer_hot_send_lease_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_204034_codex_20260328_dealer_hot_send_lease_public.txt)
- 핵심 수치
  - targeted public
    - `PAIR tcp/inproc -28.83% / -19.45%`
    - `DEALER_DEALER tcp/inproc -15.40% / -22.79%`
  - raw
    - public stage에서 이미 reject되어 미실행
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - existing public-send-sync-held pipe hot-send lease / outpipe lifetime
      split candidate
- 해석
  - direct target이던 `DEALER_DEALER`도 `tcp/inproc -15.40% / -22.79%`로
    accepted baseline과 거리가 있었고,
    unchanged control에 가까워야 할 `PAIR` public도
    `tcp/inproc -28.83% / -19.45%`로 크게 흔들렸다.
  - 즉 current code에서 hot send와 rare `_out_pipe` mutation duty를
    caller-owned public send sync 재사용으로 다시 가르는 방식은
    `_out_sync` steady-state cost의 broad answer가 아니었다.
  - previous existing-public-sync `send_serialized` helper family와 마찬가지로,
    caller-owned send serialization reuse 계열은 current broad answer가 아니다.
- 다음 iteration 우선순위
  - existing public-send-sync-held pipe hot-send lease / outpipe lifetime
    split family는 새 broad evidence 없이 다시 올리지 않는다.
  - 다음 라운드는 다시
    `send scope construct + pipe serialization` 의미 단위를 함께 줄이는
    다른 structural candidate 하나만 고른다.

## 86. 2026-03-28 native recursive pthread `fast_mutex_t` primitive replacement candidate rejected 로그

- 작업한 가설 1개
  - current `_out_sync` / stream shard가 공통으로 타는 recursive
    `fast_mutex_t` primitive 자체를 native recursive pthread mutex로 바꾸면,
    caller-owned serialization reuse 없이도 `pipe serialization` floor를
    직접 줄일 수 있다고 봤다.
- candidate family 1개
  - common send-side structural round
- high-leverage 또는 semantic probe 근거
  - current guide의 첫 미완료 항목이 여전히
    `send scope construct + pipe serialization` 의미 단위를 함께 줄이는
    structural candidate였고,
    이번 후보는 rejected helper family를 반복하지 않으면서
    `_out_sync` primitive 자체를 바꿔 same-ordering cost floor를 보는 probe였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반 `claude -p` consult는 응답 없이 끝났고,
    prompt 인자 재시도는
    `Input must be provided either through stdin or as a prompt argument`
    오류로 끝나 usable advisory를 얻지 못했다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - bench A/B 뒤 원복:
    - [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
    - [`core/tests/unittest/unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
- 실행한 명령
  - `claude --help`
  - `printf '<prompt>' | claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink`
  - `timeout 20s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink "<prompt>"`
  - `cmake --build core/build -j$(nproc)`
  - initial parallel ctest
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
    는 build와 겹쳐 `test_stream_send_blocking_wakeup`에서
    `libzlink.so.5 file too short` loader race가 났다.
  - build 완료 뒤 rerun
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_fast_mutex_native_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_fast_mutex_native_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_205816_codex_20260328_fast_mutex_native_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_205816_codex_20260328_fast_mutex_native_public.txt)
  - [`perf_linux_20260328_205901_codex_20260328_fast_mutex_native_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_205901_codex_20260328_fast_mutex_native_raw.txt)
- 핵심 수치
  - targeted public
    - `PAIR tcp/inproc -27.78% / -17.52%`
    - `DEALER_DEALER tcp/inproc +3.72% / -21.03%`
  - targeted raw
    - `PAIR tcp/inproc -13.25% / -21.63%`
    - `DEALER_DEALER tcp/inproc -7.74% / -15.86%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - native recursive pthread `fast_mutex_t` primitive replacement
- 해석
  - `DEALER_DEALER tcp`는 일시적으로 `+3.72%`까지 올라왔지만,
    unchanged control인 `PAIR public tcp -27.78%`가 크게 무너졌고
    raw `PAIR inproc -21.63%`도 guardrail을 벗어났다.
  - stream/contract smoke는 build 완료 뒤 rerun에서 모두 통과했으므로
    correctness regression이 아니라 hot path primitive replacement 자체가
    current broad answer가 아니라고 보는 편이 맞다.
  - 따라서 current residual을 recursive lock primitive 하나로 설명하는
    family는 keep-worthy broad win이 아니며,
    next step은 다시 send scope construct와 `_out_sync` duty를 함께 가르는
    다른 structural candidate다.
- 다음 iteration 우선순위
  - native recursive pthread `fast_mutex_t` primitive replacement family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 라운드는 another structural candidate를 바로 고르는 대신,
    guide 6.2/6.3 trigger에 따라 guide/review/hot-path summary를 먼저 다시
    정렬하고
    historical `a819ea3a` admission floor 대
    `ff0140e5` pipe floor를 current residual direct cause 기준으로 다시 쓴다.

## 87. 2026-03-28 common send-side structural round guide 재정렬 로그

- 작업한 가설 1개
  - common send-side structural round가
    `process_activate_write()` snapshot/atomic,
    existing public-send-sync-held `send_serialized`,
    `DEALER` external send-state mutex / external `send_serialized` scope,
    existing public-send-sync-held hot-send lease / outpipe lifetime split,
    `fast_mutex.hpp` native recursive pthread primitive replacement까지
    다섯 계열 연속 reject된 시점에서,
    current 미완료 항목은 another code patch가 아니라
    guide 6.2/6.3 trigger에 맞는 우선순위 재정렬이라고 봤다.
- candidate family 1개
  - guide/review/hot-path reordering
- high-leverage 또는 semantic probe 근거
  - source-of-truth guide 자체가
    "같은 계열 후보가 2개 이상 연속 rejected 되면 다음 iteration은
    guide/review/hot-path 재정렬부터"라고 고정하고 있었는데,
    current checklist의 마지막 미완료 항목은 여전히 another structural
    candidate를 바로 고르도록 남아 있어 stale했다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반
    `printf '<prompt>' | timeout 90s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
    consult는 `timeout 90s`로 종료돼 usable advisory를 얻지 못했다.
  - latest advisory는 unavailable로 기록한다.
- 수정한 파일 경로
  - 유지:
    - [`doc/plan/perf/single-libzmq-gap-ralph-guide.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-ralph-guide.ko.md)
    - [`doc/plan/perf/single-libzmq-gap-review.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
    - [`doc/internal/hot-path.ko.md`](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
  - 원복:
    - 없음
- 실행한 명령
  - `claude --help`
  - `printf '<prompt>' | timeout 90s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - concurrent diagnostic only:
    `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_current_baseline_public`
  - concurrent diagnostic only:
    `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_current_baseline_raw`
- 생성된 결과 파일 경로
  - concurrent diagnostic only:
    [`perf_linux_20260328_211244_codex_20260328_current_baseline_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_211244_codex_20260328_current_baseline_public.txt)
  - concurrent diagnostic only:
    [`perf_linux_20260328_211244_codex_20260328_current_baseline_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_211244_codex_20260328_current_baseline_raw.txt)
  - pushed commit:
    `d483418592dcab45c9ef5efe92b12d89f21eb37b`
- 핵심 수치
  - authoritative baseline 갱신 없음
  - `cmake --build core/build -j$(nproc)` 성공
  - targeted ctest 7종 통과
- 유지한 변경 / 원복한 변경
  - 유지
    - guide/review/hot-path source-of-truth 재정렬
  - 원복
    - 없음
- 해석
  - current outstanding work는 another structural code patch가 아니라,
    common send-side structural round에서 local search drift가 시작됐다는
    사실을 source-of-truth 문서에 반영하는 것이 맞다.
  - concurrent public/raw refresh는 diagnostic overlap 때문에 authority로
    쓸 수 없으므로, next exact step은 serial current-tree refresh를 다시 찍는
    것이다.
  - 즉 current next step은
    historical `a819ea3a` admission floor 대
    `ff0140e5` pipe floor를 current residual direct cause 기준으로 먼저
    다시 쓰고, 그 다음에야 새 broad hypothesis 하나를 다시 여는 것이다.
- 다음 iteration 우선순위
  - 먼저 serial current-tree `PAIR` / `DEALER_DEALER` public/raw refresh를
    다시 찍어 noise 없는 baseline을 고정한다.
  - 그 다음 guide/review/hot-path summary와 historical map을 붙여
    current residual direct cause를 `admission floor` 대 `pipe floor`로
    다시 서술한다.
  - 위 reset이 끝난 뒤에만 새 broad structural candidate를 다시 고른다.

## 88. 2026-03-28 serial public/raw refresh + admission-vs-pipe 재서술 로그

- 작업한 가설 1개
  - current residual direct cause는 wrapper/raw surface 차이나
    dealer-only `public_api_sync` reuse가 아니라,
    `enter_public_api`가 포함된 common send scope construct floor와
    `_out_sync` write/flush serialization floor의 조합이라고 다시 봤다.
- candidate family 1개
  - serial authority baseline refresh + common data-plane admission
    structural reset
- high-leverage 또는 semantic probe 근거
  - concurrent baseline artifact는 authority로 쓸 수 없었고,
    serial current-tree refresh에서 raw/public wrapper 제거가 broad win을
    만들지 못했다.
  - `PAIR`와 `DEALER_DEALER` current gap이 여전히 비슷해
    dealer-only `public_api_sync` reuse family도 primary blocker가
    아니라는 점이 더 명확해졌다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)
  - [`pair.cpp`](/home/hep7/project/kairos/libzmq/src/pair.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - prompt 인자 기반
    `timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink "<prompt>"`
    consult는 응답 없이 종료돼 usable advisory를 얻지 못했다.
  - latest advisory는 unavailable로 기록한다.
- 수정한 파일 경로
  - 유지:
    - [`doc/plan/perf/single-libzmq-gap-ralph-guide.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-ralph-guide.ko.md)
    - [`doc/plan/perf/single-libzmq-gap-review.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
    - [`doc/internal/hot-path.ko.md`](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
  - 원복:
    - 없음
- 실행한 명령
  - `claude --help`
  - `timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink "<prompt>"`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_serial_refresh_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_serial_refresh_raw`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_212318_codex_20260328_serial_refresh_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_212318_codex_20260328_serial_refresh_public.txt)
  - [`perf_linux_20260328_212402_codex_20260328_serial_refresh_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_212402_codex_20260328_serial_refresh_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -12.03% / -17.18%`
    - `DEALER_DEALER tcp/inproc -11.12% / -18.60%`
  - raw
    - `PAIR tcp/inproc -8.63% / -23.67%`
    - `DEALER_DEALER tcp/inproc -12.06% / -20.63%`
  - `cmake --build core/build -j$(nproc)` 성공
  - targeted ctest 7종 통과
- 유지한 변경 / 원복한 변경
  - 유지
    - serial authority baseline
    - guide/review/hot-path의 admission-vs-pipe 재서술
  - 원복
    - 없음
- 해석
  - raw/public wrapper 제거는 `PAIR tcp` 일부 개선 외에는 broad win을 만들지
    못했고, `PAIR inproc`은 오히려 더 나빠졌다.
  - `PAIR`와 `DEALER_DEALER` 격차가 여전히 비슷하므로,
    dealer-only `public_api_sync` reuse나 wrapper surface만으로
    current common residual을 설명할 수 없다.
  - current next step은 current kept boundary 위에서
    common data-plane admission을 full public lifecycle coordinator 아래의
    더 얇은 steady-state lease로 내리는 structural family를 하나만 고르는
    것이다.
- 다음 iteration 우선순위
  - serial current-tree public/raw baseline을 authority로 유지한다.
  - dealer-only `public_api_sync` reuse family와 wrapper/raw surface family는
    새 broad evidence 없이 다시 올리지 않는다.
  - current kept boundary 위에서 common data-plane admission structural
    candidate 하나만 골라 `core/` patch와 targeted guardrail로 검증한다.

## 89. 2026-03-28 common data-plane admission boundary helper rejected

- 작업한 가설 1개
  - common data-plane admission을 full public lifecycle coordinator 아래의
    더 얇은 steady-state lease로 내리기 전에,
    `socket_public_send_scope_t` constructor/destructor가 쓰는
    send admission boundary를 명시 helper로 먼저 드러내면
    다음 structural round를 더 좁게 실험할 수 있다고 봤다.
- candidate family 1개
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    send admission boundary helper extraction
- high-leverage 또는 semantic probe 근거
  - serial authority baseline 기준으로 wrapper/raw surface와
    dealer-only `public_api_sync` reuse family가 primary blocker가 아니므로,
    다음 라운드는 common admission floor를 더 직접 다루는 쪽이어야 했다.
  - 다만 그 첫 단계가 codegen-only helper extraction이어도 hot path를
    두껍게 만들 수 있는지 먼저 확인할 필요가 있었다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - 이번 라운드에서 새 consult는 추가로 수행하지 않았다.
  - 바로 앞 serial refresh 단계의 latest advisory는 timeout으로
    unavailable 상태를 유지한다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    - [`core/tests/unittest/unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_admission_boundary_prep_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_admission_boundary_prep_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_admission_boundary_prep_public_rerun`
  - helper를 header-inline + original branch shape로 재구성한 뒤
    `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_admission_boundary_prep_inline_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_admission_boundary_prep_inline_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_213423_codex_20260328_send_admission_boundary_prep_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_213423_codex_20260328_send_admission_boundary_prep_public.txt)
  - [`perf_linux_20260328_213505_codex_20260328_send_admission_boundary_prep_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_213505_codex_20260328_send_admission_boundary_prep_raw.txt)
  - [`perf_linux_20260328_213553_codex_20260328_send_admission_boundary_prep_public_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_213553_codex_20260328_send_admission_boundary_prep_public_rerun.txt)
  - [`perf_linux_20260328_213801_codex_20260328_send_admission_boundary_prep_inline_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_213801_codex_20260328_send_admission_boundary_prep_inline_public.txt)
  - [`perf_linux_20260328_214000_codex_20260328_send_admission_boundary_prep_inline_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_214000_codex_20260328_send_admission_boundary_prep_inline_raw.txt)
- 핵심 수치
  - out-of-line helper
    - public `PAIR tcp/inproc -28.25% / -30.71%`
    - public `DEALER_DEALER tcp/inproc -15.00% / -22.13%`
    - raw `PAIR tcp/inproc -25.53% / -22.57%`
    - raw `DEALER_DEALER tcp/inproc -11.38% / -20.77%`
  - public rerun
    - `PAIR tcp/inproc -36.07% / -23.86%`
    - `DEALER_DEALER tcp/inproc -11.07% / -20.97%`
  - header-inline + branch-shape refinement
    - public `PAIR tcp/inproc -21.50% / -14.91%`
    - public `DEALER_DEALER tcp/inproc -10.08% / -19.13%`
    - raw `PAIR tcp/inproc -26.96% / -30.67%`
    - raw `DEALER_DEALER tcp/inproc -22.43% / -21.11%`
  - build / targeted ctest는 각 단계에서 통과했고,
    최종 current code는 원복 뒤에도 `cmake --build core/build -j$(nproc)`와
    targeted ctest 7종이 다시 통과했다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - common data-plane admission boundary helper extraction candidate 전체
- 해석
  - helper naming/boundary extraction만으로도 `PAIR` public/raw와
    `DEALER_DEALER tcp raw` guardrail을 안정적으로 지키지 못했다.
  - 즉 next round는 같은 family를 다시 다듬는 것이 아니라,
    codegen-only boundary helper family 자체를 rejected candidate로 두고
    다른 structural candidate를 골라야 한다.
- 다음 iteration 우선순위
  - `socket_runtime` common data-plane admission boundary helper extraction
    family는 새 broad evidence 없이 다시 올리지 않는다.
  - serial current-tree public/raw baseline을 authority로 유지한 채,
    current kept boundary 위에서 다른 common data-plane admission
    structural candidate를 다시 고른다.

## 90. 2026-03-28 dedicated public send lease split candidate rejected

- 작업한 가설 1개
  - current kept boundary 위에서 `send admission/scope construct` 비용을
    full public lifecycle coordinator와 분리해,
    sync socket direct send가 generic `public_api_sync` bit 대신
    dedicated public send lease를 잡게 하면
    common send-side residual을 줄일 수 있다고 봤다.
- candidate family 1개
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    dedicated public send lease split
- high-leverage 또는 semantic probe 근거
  - helper extraction까지 reject된 뒤에도
    current residual direct cause는 여전히
    `public admission/scope construct + pipe serialization` 조합으로 남아 있었고,
    새 structural family 하나를 실제 코드로 검증해야 했다.
  - 이 candidate는 dealer-only reuse나 pipe caller-owned exclusion reuse가 아니라,
    current lifecycle coordinator 아래에 더 얇은 send lease를 두는
    common admission structural round였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - prompt argument 기반 `claude -p`는
    `Input must be provided either through stdin or as a prompt argument when using --print`
    오류로 unusable이었다.
  - stdin 기반 `timeout 120s claude -p ...` 재시도도 응답 없이 `code 124`
    로 종료돼 latest advisory는 unavailable로 기록한다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    - [`core/tests/unittest/unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_lease_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_lease_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_lease_public_serial`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_lease_raw_serial`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_lease_public_authority`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
- 생성된 결과 파일 경로
  - noisy diagnostic only:
    - [`perf_linux_20260328_215617_codex_20260328_send_lease_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_215617_codex_20260328_send_lease_public.txt)
    - [`perf_linux_20260328_215617_codex_20260328_send_lease_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_215617_codex_20260328_send_lease_raw.txt)
    - [`perf_linux_20260328_215700_codex_20260328_send_lease_public_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_215700_codex_20260328_send_lease_public_serial.txt)
    - [`perf_linux_20260328_215700_codex_20260328_send_lease_raw_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_215700_codex_20260328_send_lease_raw_serial.txt)
  - authority:
    - [`perf_linux_20260328_215743_codex_20260328_send_lease_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_215743_codex_20260328_send_lease_public_authority.txt)
- 핵심 수치
  - authority public rerun
    - `PAIR tcp/inproc -15.65% / -25.43%`
    - `DEALER_DEALER tcp/inproc -24.41% / -32.10%`
  - same-tag raw/public parallel launch과 그 직후 두 번째 parallel launch은
    모두 noisy diagnostic으로만 남기고 acceptance 근거로는 쓰지 않았다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - dedicated public send lease split candidate 전체
- 해석
  - targeted ctest는 통과했지만, authority public rerun이 `PAIR`/`DEALER`
    둘 다 current baseline보다 크게 악화됐다.
  - 즉 common admission을 dedicated send lease로 다시 나누는 현재 형태는
    keep-worthy broad win이 아니고,
    current residual을 `public_api_sync` bit packing 하나로
    풀 수 있다는 가설도 지지하지 못했다.
  - current code는 원복 뒤 rebuild + targeted ctest를 다시 통과했다.
- 다음 iteration 우선순위
  - dedicated public send lease split family는
    새 broad evidence 없이 다시 올리지 않는다.
  - serial current-tree public/raw baseline과 current kept boundary를 유지한 채,
    다른 common send-side structural candidate를 고른다.

## 91. 2026-03-28 public/send inflight lane split candidate rejected

- 작업한 가설 1개
  - current residual의 admission floor가 generic public/callback inflight count와
    direct send inflight count를 같은 `public_api_state` lane에서 다루는 탓일 수
    있으므로, close bit와 shared sync bit는 유지하되 inflight bookkeeping만
    separate lane으로 나누면 steady-state send scope construct가 줄 수 있다고
    봤다.
- candidate family 1개
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    shared `public_api_state` public/send inflight lane split
- high-leverage 또는 semantic probe 근거
  - guide reset 뒤 immediate next step이
    helper-level micro tweak가 아니라 common send-side structural candidate 1개를
    실제 코드로 검증하는 것이었고,
    `PAIR`와 `DEALER_DEALER` gap이 비슷하다는 serial baseline 때문에
    dealer-only sync reuse보다 더 공통인 admission bookkeeping 자체를
    다시 가르는 쪽이 더 직접적인 후보였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반 `timeout 90s claude -p --permission-mode bypassPermissions`
    consult는 다시 `code 124`로 끝나 usable advisory를 얻지 못했다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    - [`core/tests/unittest/unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `claude --help`
  - `timeout 90s claude -p --permission-mode bypassPermissions`
  - `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R 'unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_state_lane_split_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_state_lane_split_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_state_lane_split_public_authority`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_send_state_lane_split_raw_authority`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R 'unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe' -j1`
- 생성된 결과 파일 경로
  - noisy diagnostic only:
    - [`perf_linux_20260328_225749_codex_20260328_send_state_lane_split_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225749_codex_20260328_send_state_lane_split_public.txt)
    - [`perf_linux_20260328_225749_codex_20260328_send_state_lane_split_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225749_codex_20260328_send_state_lane_split_raw.txt)
  - authority:
    - [`perf_linux_20260328_225833_codex_20260328_send_state_lane_split_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225833_codex_20260328_send_state_lane_split_public_authority.txt)
    - [`perf_linux_20260328_225912_codex_20260328_send_state_lane_split_raw_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225912_codex_20260328_send_state_lane_split_raw_authority.txt)
- 핵심 수치
  - authority public rerun
    - `PAIR tcp/inproc -20.45% / -24.95%`
    - `DEALER_DEALER tcp/inproc -23.79% / -23.41%`
  - authority raw rerun
    - `PAIR tcp/inproc -22.49% / -24.59%`
    - `DEALER_DEALER tcp/inproc -10.46% / -19.57%`
  - same-tag public/raw parallel launch은 noisy diagnostic으로만 남기고
    acceptance 근거로는 쓰지 않았다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - shared `public_api_state` public/send inflight lane split candidate 전체
- 해석
  - targeted ctest는 통과했지만 authority public/raw가 모두 baseline보다 크게
    악화돼 keep-worthy broad win이 아니었다.
  - 즉 current residual을 same-state inflight bookkeeping lane split 하나로
    설명할 수 없고, `public_api_state` 안에서 count lane만 다시 배치하는
    family도 현재 broad fix 후보에서 내린다.
  - current code는 원복 뒤 rebuild + targeted ctest를 다시 통과했다.
- 다음 iteration 우선순위
  - shared `public_api_state` public/send inflight lane split family는
    새 broad evidence 없이 다시 올리지 않는다.
  - serial current-tree public/raw baseline과 current kept boundary를 유지한 채,
    다른 common send-side structural candidate를 고른다.

## 92. 2026-03-28 `public_api_sync` recursive mutex-backed split candidate rejected

- 작업한 가설 1개
  - current residual의 common admission floor가
    `public_api_state` 안의 sync bit CAS spin/wait와 inflight accounting이
    함께 엮인 구조일 수 있으므로,
    inflight/closing accounting은 유지한 채 `public_api_sync`만
    recursive mutex-backed sync로 바꾸면
    current kept boundary 위에서 `PAIR`/`DEALER` common send scope cost를
    줄일 수 있다고 봤다.
- candidate family 1개
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    `public_api_sync` recursive mutex-backed split
- high-leverage 또는 semantic probe 근거
  - `PAIR`도 여전히 `enter_public_api` admission floor를 타고,
    non-`PAIR`는 여기에 sync serialization까지 더해지므로,
    libzmq의 recursive `_sync`에 더 가까운 send-side serialization 구조를
    current admission boundary 위에 올려 볼 가치가 있다고 봤다.
  - 이번 후보는 helper-level micro tweak가 아니라,
    current thread-safe/public contract를 유지한 채
    `a819ea3a` send admission/lock 의미 단위를 다른 synchronization shape로
    다시 가르는 structural probe였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - prompt argument 기반 `claude -p`는
    `Input must be provided either through stdin or as a prompt argument when using --print`
    오류로 unusable이었다.
  - stdin 기반 `timeout 120s claude -p ...` 재시도도 `code 124` timeout으로
    끝나 usable advisory를 얻지 못했다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `claude --help`
  - prompt argument 기반 `timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink "<prompt>"`
  - stdin 기반 `cat <<'EOF' | timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink ... EOF`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - noisy diagnostic only:
    - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_public_sync_recursive_mutex_public`
    - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_public_sync_recursive_mutex_raw`
  - authority:
    - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_public_sync_recursive_mutex_public_authority`
    - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_public_sync_recursive_mutex_raw_authority`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤 first ctest는 build와 겹쳐
    `test_stream_send_blocking_wakeup` `libzlink.so.5: file too short`,
    `unittest_socket_runtime` BAD_COMMAND를 냈고,
    build 완료 뒤 같은 gate rerun은 다시 통과했다.
- 생성된 결과 파일 경로
  - noisy diagnostic only:
    - [`perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_public.txt)
    - [`perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_raw.txt)
  - authority:
    - [`perf_linux_20260328_231611_codex_20260328_public_sync_recursive_mutex_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231611_codex_20260328_public_sync_recursive_mutex_public_authority.txt)
    - [`perf_linux_20260328_231650_codex_20260328_public_sync_recursive_mutex_raw_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231650_codex_20260328_public_sync_recursive_mutex_raw_authority.txt)
- 핵심 수치
  - authority public rerun
    - `PAIR tcp/inproc -16.85% / -21.61%`
    - `DEALER_DEALER tcp/inproc -18.65% / -36.20%`
  - authority raw rerun
    - `PAIR tcp/inproc -7.34% / -17.60%`
    - `DEALER_DEALER tcp/inproc -18.83% / -34.78%`
  - same-tag public/raw parallel launch은 noisy diagnostic으로만 남기고
    acceptance 근거로는 쓰지 않았다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `public_api_sync` recursive mutex-backed split candidate 전체
- 해석
  - public authority rerun에서 `PAIR`와 `DEALER`가 baseline보다 모두
    더 악화됐고,
    raw authority rerun도 `DEALER` `tcp/inproc -18.83% / -34.78%`로
    current baseline을 크게 지키지 못했다.
  - 즉 `public_api_sync` wait primitive를 libzmq-style recursive mutex shape로
    바꾸는 것만으로는 current residual의 broad answer가 되지 않았고,
    same-order send admission floor와 `_out_sync` floor를 함께 줄이는
    keep-worthy structural win도 만들지 못했다.
  - current code는 원복 뒤 rebuild + targeted ctest gate를 다시 통과했다.
- 다음 iteration 우선순위
  - `public_api_sync` recursive mutex-backed split family는
    새 broad evidence 없이 다시 올리지 않는다.
  - serial current-tree public/raw baseline과 current kept boundary를 유지한 채,
    다른 common send-side structural candidate를 고른다.

## 93. 2026-03-28 post-recursive serial refresh repeated low baseline 로그

- 작업한 가설 1개
  - `public_api_sync` recursive mutex-backed split candidate를 원복한 current
    code에서 serial public/raw baseline을 다시 찍을 때,
    earlier authority와 같은 수준이 유지되는지 먼저 확인해야 다음
    broad hypothesis를 잘못된 기준선 위에 세우지 않는다고 봤다.
- candidate family 1개
  - post-revert serial current-tree baseline refresh + rerun
- high-leverage 또는 semantic probe 근거
  - guide의 current next step이 새 common structural candidate 선택이므로,
    직전 reject 뒤 current baseline이 어디에 놓였는지 먼저 다시 고정할
    필요가 있었다.
  - same current code에서 lower baseline이 반복되면,
    이후 candidate acceptance도 single refresh 1회 수치만으로는
    결정하면 안 된다.
- 참고한 `libzmq` 대응 파일
  - 없음
- `claude` consult 여부와 핵심 조언 1~3줄
  - 이번 단계는 candidate 검토가 아니라 baseline refresh 반복 확인이어서
    추가 `claude` consult를 실행하지 않았다.
- 수정한 파일 경로
  - 유지:
    - [`doc/plan/perf/single-libzmq-gap-ralph-guide.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-ralph-guide.ko.md)
    - [`doc/plan/perf/single-libzmq-gap-review.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
    - [`doc/internal/hot-path.ko.md`](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
  - 원복:
    - 없음
- 실행한 명령
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_post_recursive_sync_refresh_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_post_recursive_sync_refresh_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_post_recursive_sync_refresh_public_rerun`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_post_recursive_sync_refresh_raw_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_232530_codex_20260328_post_recursive_sync_refresh_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_232530_codex_20260328_post_recursive_sync_refresh_public.txt)
  - [`perf_linux_20260328_232612_codex_20260328_post_recursive_sync_refresh_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_232612_codex_20260328_post_recursive_sync_refresh_raw.txt)
  - [`perf_linux_20260328_233054_codex_20260328_post_recursive_sync_refresh_public_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_233054_codex_20260328_post_recursive_sync_refresh_public_rerun.txt)
  - [`perf_linux_20260328_233133_codex_20260328_post_recursive_sync_refresh_raw_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_233133_codex_20260328_post_recursive_sync_refresh_raw_rerun.txt)
- 핵심 수치
  - public first/rerun
    - `PAIR tcp/inproc -24.67% / -14.98%`
    - `DEALER_DEALER tcp/inproc -14.63% / -32.73%`
    - `PAIR tcp/inproc -27.72% / -18.03%`
    - `DEALER_DEALER tcp/inproc -21.31% / -32.12%`
  - raw first/rerun
    - `PAIR tcp/inproc -23.51% / -23.04%`
    - `DEALER_DEALER tcp/inproc -32.68% / -34.30%`
    - `PAIR tcp/inproc -23.54% / -25.42%`
    - `DEALER_DEALER tcp/inproc -23.26% / -25.44%`
- 유지한 변경 / 원복한 변경
  - 유지
    - current session low-baseline 메모와 next-step guardrail 재정렬
  - 원복
    - 없음
- 해석
  - same current code에서도 late-session serial refresh가 earlier authority
    (`212318` / `212402`)보다 더 낮은 기준선으로 반복됐다.
  - 다만 `PAIR`와 `DEALER_DEALER`가 함께 나빠지는 common residual 해석 자체는
    바뀌지 않았으므로, next hypothesis는 admission floor / pipe floor 축을
    유지하되 acceptance 기준을 더 엄격하게 읽어야 한다.
  - 즉 다음 candidate는 early authority와 current session low baseline을
    둘 다 guardrail로 보고, signal이 섞이면 current-tree refresh를 다시 찍은
    뒤에만 keep/reject를 정한다.
- 다음 iteration 우선순위
  - baseline refresh 반복 자체는 여기서 닫고,
    다음 단계는 새 broad hypothesis 하나를 다시 열어
    new common send-side structural candidate를 고른다.

## 94. 2026-03-28 `pipe.cpp` final-part `write_and_flush()` lock-free snapshot candidate rejected

- 작업한 가설 1개
  - current residual의 `pipe serialization floor`를 더 직접 줄이기 위해,
    final single-part `write_and_flush()`만 steady-state lock-free snapshot
    fast path로 보내고 rare mutation/teardown은 기존 `_out_sync`에 남기면
    common send-side broad win을 만들 수 있다고 봤다.
- candidate family 1개
  - `pipe.cpp` final-part `write_and_flush()` lock-free snapshot split
- high-leverage 또는 semantic probe 근거
  - previous pipe-side 후보들은 lock 계층을 바꾸더라도 hot path에서
    여전히 lock을 잡는 형태였고,
    `7bea9e3f` good state의 `pipe.cpp`는 steady-state write/flush에
    현재 같은 `_out_sync`가 없었다.
  - 따라서 이번 후보는 `_out_sync` no-op이 아니라,
    final-part send 한정으로 현재 `pipe serialization floor`를 더 직접
    줄이는 structural probe였다.
- 참고한 `libzmq` 대응 파일
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - 이번 단계는 late-session baseline refresh 직후 바로 올린
    structural probe라 추가 `claude` consult를 실행하지 않았다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pipe_write_flush_hot_snapshot_public`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_234340_codex_20260328_pipe_write_flush_hot_snapshot_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_234340_codex_20260328_pipe_write_flush_hot_snapshot_public.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -32.16% / -20.55%`
    - `DEALER_DEALER tcp/inproc -9.76% / -23.34%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe.cpp` final-part `write_and_flush()` lock-free snapshot candidate 전체
- 해석
  - `DEALER tcp`는 일부 회복했지만 `PAIR tcp/inproc`가 early authority와
    session-local low baseline 둘 다 못 지켜 public stage에서 이미 broad win이
    아니었다.
  - 즉 final-part `pipe serialization floor`를 lock-free snapshot 하나로만
    더는 family는 current common answer가 아니고, raw 단계까지 확장할 가치도
    없었다.
  - current code는 원복 뒤 rebuild + targeted ctest gate를 다시 통과했다.
- 다음 iteration 우선순위
  - `pipe.cpp` final-part `write_and_flush()` lock-free snapshot family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 단계는 다시
    new broad hypothesis 하나를 열고,
    다른 common send-side structural candidate를 고른다.

## 95. 2026-03-28 `pipe::_out_sync` plain non-recursive fast mutex candidate rejected

- 작업한 가설 1개
  - current residual의 `pipe serialization floor`가 lock 범위를 바꾸는 것보다
    current `fast_mutex_t`의 owner/depth bookkeeping 자체에도 일부 실려
    있을 수 있으므로,
    same-ordering invariant는 그대로 둔 채 `pipe::_out_sync`만
    plain non-recursive fast mutex로 바꾸면
    `PAIR` / `DEALER` common send-side broad win을 만들 수 있다고 봤다.
- candidate family 1개
  - `pipe::_out_sync` plain non-recursive fast mutex split
- high-leverage 또는 semantic probe 근거
  - 이번 후보는 `_out_sync`를 없애거나 hot send와 rare teardown을 분리하는
    family가 아니라,
    current helper/invariant boundary를 유지한 채
    steady-state `lock()/unlock()`에서 owner tracking과 recursive depth
    bookkeeping만 제거하는 structural probe였다.
  - `7bea9e3f` good state의 steady-state `pipe` path가 현재 같은
    recursive-like lock metadata를 메시지마다 건드리지 않았다는 점을
    current contract 안에서 가장 얇게 복원해 보는 후보였다.
- 참고한 `libzmq` 대응 파일
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `timeout 60s claude -p "Review current zlink common send-side perf hypothesis. Focus on whether replacing pipe::_out_sync with a plain non-recursive fast mutex (keeping current ordering/invariants) is a meaningful new structural candidate relative to existing rejected families. Reply in 5 lines max."`
    는 출력 없이 `code 124` timeout으로 끝나 usable advisory를 얻지 못했다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `timeout 60s claude -p "Review current zlink common send-side perf hypothesis. Focus on whether replacing pipe::_out_sync with a plain non-recursive fast mutex (keeping current ordering/invariants) is a meaningful new structural candidate relative to existing rejected families. Reply in 5 lines max."`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_pipe_plain_mutex_public`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_235615_codex_20260329_pipe_plain_mutex_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_235615_codex_20260329_pipe_plain_mutex_public.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -22.34% / -24.82%`
    - `DEALER_DEALER tcp/inproc -9.78% / -32.93%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe::_out_sync` plain non-recursive fast mutex candidate 전체
- 해석
  - `DEALER tcp`만 early authority 기준으로 noise-level 회복을 보였지만,
    `PAIR tcp/inproc`가 both guardrail을 못 지켰고
    `DEALER_DEALER inproc`도 `-32.93%`까지 무너져 broad win이 아니었다.
  - 즉 current residual의 `pipe serialization floor`는
    same-order lock primitive의 owner/depth bookkeeping만 걷는 것으로는
    설명되지 않았고,
    global `fast_mutex.hpp` primitive replacement family와는 별개로
    `pipe::_out_sync` local primitive swap family도 현재 common answer가 아니다.
  - current code는 원복 뒤 rebuild + targeted ctest gate를 다시 통과했다.
- 다음 iteration 우선순위
  - `pipe::_out_sync` plain non-recursive fast mutex family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 단계는 다시
    new broad hypothesis 하나를 열고,
    다른 common send-side structural candidate를 고른다.

## 96. 2026-03-29 `pipe.hpp` / `pipe.cpp` non-conflate out-pipe concrete `ypipe_t` fast path candidate rejected

- 작업한 가설 1개
  - current residual의 `pipe serialization floor` 일부가
    steady-state non-conflate path의 `ypipe_base_t` type-erased
    `write()/flush()` dispatch에 실려 있을 수 있으므로,
    normal `out_pipe`를 concrete `ypipe_t` fast path로 직접 태우면
    `PAIR` / `DEALER` common broad win을 만들 수 있다고 봤다.
- candidate family 1개
  - `pipe.hpp` / `pipe.cpp` non-conflate out-pipe concrete `ypipe_t` fast path
- high-leverage 또는 semantic probe 근거
  - 이번 후보는 `_out_sync` ordering, lifetime, activation 의미를 바꾸지 않고
    non-conflate steady-state `write()/flush()` call shape만 얇게 만드는
    structural probe였다.
  - `7bea9e3f` good state와 libzmq의 `pipe` path가 current tree처럼
    모든 normal out-pipe send를 type-erased base pointer로 다루지 않는다는
    점을 current contract 안에서 가장 얇게 복원해 보는 시도였다.
- 참고한 `libzmq` 대응 파일
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - 이번 candidate 전용 `claude` consult는 실행하지 않았다.
  - 직전 consult는 shared `public_api_state` split family를 내리는 데만
    사용했고, 이번 out-pipe concrete fast path에는 advisory를 따로 붙이지
    못했다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_ypipe|unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_pipe_concrete_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_pipe_concrete_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_ypipe|unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_002224_codex_20260329_pipe_concrete_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_002224_codex_20260329_pipe_concrete_public.txt)
  - [`perf_linux_20260329_002310_codex_20260329_pipe_concrete_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_002310_codex_20260329_pipe_concrete_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -18.01% / -35.55%`
    - `DEALER_DEALER tcp/inproc -15.12% / -24.02%`
  - raw
    - `PAIR tcp/inproc -8.81% / -28.27%`
    - `DEALER_DEALER tcp/inproc -7.64% / -22.08%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe.hpp` / `pipe.cpp` non-conflate out-pipe concrete `ypipe_t`
      fast path candidate 전체
- 해석
  - public `PAIR` / `DEALER`가 both guardrail을 못 지켰고,
    raw에서도 `PAIR inproc`와 `DEALER_DEALER inproc`이 함께 악화됐다.
  - 즉 current `pipe serialization floor`는
    `ypipe_base_t` virtual `write()/flush()` 한 겹만 얇게 걷는 것으로는
    설명되지 않았고, pipe-local out-pipe devirtualization family도
    current common answer가 아니다.
  - current code는 원복 뒤 rebuild + targeted ctest gate를 다시 통과했다.
- 다음 iteration 우선순위
  - `pipe.hpp` / `pipe.cpp` non-conflate out-pipe concrete `ypipe_t`
    fast path family는 새 broad evidence 없이 다시 올리지 않는다.
  - 다음 단계는 또 다른 pipe-local call shaving이 아니라,
    `a819ea3a` send scope construct와
    `98e7d324/9b91234c` public multipart/sender-regime 의미를 함께 다시 보는
    new broad hypothesis 하나를 고른다.

## 97. 2026-03-29 public API-boundary same-handle recursive mutex single-part fast path candidate rejected

- 작업한 가설 1개
  - current residual의 `send scope construct floor` 일부가
    same-handle public serialization과 엮여 있을 수 있으므로,
    public API boundary에서 recursive mutex로 same-handle single-part send를
    serialize하고 direct send scope를 우회하면
    `PAIR` / `DEALER` / routed `ROUTER` common broad win을 만들 수 있다고
    봤다.
- candidate family 1개
  - `socket_base_api.cpp` / `socket_base_msg.cpp` /
    `socket_message_send_api.cpp`
    public API-boundary same-handle recursive mutex single-part fast path
- high-leverage 또는 semantic probe 근거
  - 이번 후보는 current kept boundary
    (`send_direct_with_retry()` /
    `socket_public_send_scope_t::should_hold_sync_during_retry()` /
    `socket_base_t::direct_send_needs_public_api_sync()` /
    `_out_sync` unlocked helper)
    위에서
    `a819ea3a` send admission floor와 `98e7d324/9b91234c`
    public multipart surface를 다시 가르는 structural probe였다.
  - direct single-part public send에서 `socket_public_send_scope_t`를
    외부 API mutex + locked direct send helper로 대체해도
    same-handle close/thread-safe contract를 유지하면서 hot path를
    더 얇게 만들 수 있는지 확인하려는 시도였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - prompt/ststdin 기반 `claude -p` 시도는
    `Input must be provided either through stdin or as a prompt argument`
    오류와 `code 124` timeout으로 끝나 usable advisory를 얻지 못했다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
    - [`core/src/sockets/socket_base_api.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_api.cpp)
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
    - [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_api_locked_send_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_api_locked_send_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_005659_codex_20260329_api_locked_send_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_005659_codex_20260329_api_locked_send_public.txt)
  - [`perf_linux_20260329_005807_codex_20260329_api_locked_send_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_005807_codex_20260329_api_locked_send_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -14.34% / -31.65%`
    - `DEALER_DEALER tcp/inproc -13.44% / -24.93%`
    - `ROUTER_ROUTER tcp/inproc -57.41% / -23.50%`
  - raw
    - `PAIR tcp/inproc -17.38% / -31.55%`
    - `DEALER_DEALER tcp/inproc -11.26% / -19.57%`
    - `ROUTER_ROUTER tcp/inproc -57.61% / -23.28%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_base.hpp` / `socket_base_api.cpp` /
      `socket_base_msg.cpp` / `socket_message_send_api.cpp`
      public API-boundary same-handle recursive mutex single-part fast path
      candidate 전체
- 해석
  - targeted ctest gate는 통과했지만,
    public `PAIR/DEALER` `inproc`와 routed `ROUTER_ROUTER`,
    raw `PAIR/ROUTER`가 함께 흔들려 broad win이 아니었다.
  - 즉 current `send scope construct floor`는
    outer same-handle API mutex로만 바꾼다고 사라지지 않았고,
    same-handle serialization 위치 이동만으로
    `a819ea3a` 계열 residual을 설명할 수는 없었다.
  - current code는 원복 뒤 rebuild + targeted ctest gate를 다시 통과했다.
- 다음 iteration 우선순위
  - public API-boundary same-handle recursive mutex single-part fast path
    family는 새 broad evidence 없이 다시 올리지 않는다.
  - 다음 단계는 또 another API-surface mutex swap이 아니라,
    `a819ea3a` send admission floor,
    `ff0140e5` pipe serialization floor,
    `98e7d324/9b91234c` public multipart/sender-regime 흔적을 함께 읽는
    another common send-side structural candidate를 고른다.

## 98. 2026-03-29 same-thread parked send admission lease candidate rejected

- 작업한 가설 1개
  - current residual의 `a819ea3a` admission floor 일부가
    same-thread hot loop에서 message-to-message 사이에 다시 enter/leave 하는
    public admission churn에 실려 있을 수 있으므로,
    direct single-part send 성공 뒤 다음 same-thread send만 재사용하는
    parked admission handoff를 두면 `PAIR` / `DEALER` common broad win을
    만들 수 있다고 봤다.
- candidate family 1개
  - `socket_runtime.hpp` / `socket_runtime.cpp` /
    `socket_base_msg.cpp` same-thread parked send admission lease
- high-leverage 또는 semantic probe 근거
  - 이번 후보는 반복 금지된 API-boundary mutex swap이나
    coordinator state repack을 다시 여는 대신,
    `9b91234c` hot-loop sender-regime 흔적을 current lifecycle coordinator
    위에서 가장 좁게 분리해 보는 structural probe였다.
  - parked lease는 inflight 상태를 그대로 들고 가지 않고,
    close가 idle-between-send 구간에서 lease를 회수할 수 있게 해서
    thread-safe/close contract를 약화하지 않는 범위에서
    same-thread steady-state admission만 얇게 보려는 시도였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반 `timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink ...` retry는 출력 없이 `code 124` timeout으로 끝나 usable advisory를 얻지 못했다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
    - [`core/tests/unittest/unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `claude --help`
  - `cat <<'EOF' | timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink ... EOF`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_send_parked_lease_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_send_parked_lease_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_012903_codex_20260329_send_parked_lease_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_012903_codex_20260329_send_parked_lease_public.txt)
  - [`perf_linux_20260329_012948_codex_20260329_send_parked_lease_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_012948_codex_20260329_send_parked_lease_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -13.55% / -29.90%`
    - `DEALER_DEALER tcp/inproc -23.40% / -22.04%`
  - raw
    - `PAIR tcp/inproc -24.41% / -23.24%`
    - `DEALER_DEALER tcp/inproc -23.28% / -14.47%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.hpp` / `socket_runtime.cpp` /
      `socket_base_msg.cpp` / `unittest_socket_runtime.cpp`
      parked send admission lease candidate 전체
- 해석
  - candidate 자체의 ctest gate는 통과했지만,
    public `PAIR tcp`만 earlier authority 근처로 부분 회복했을 뿐
    `PAIR inproc`와 `DEALER` public/raw가 함께 guardrail 아래로 내려
    keep-worthy broad win이 아니었다.
  - 즉 `a819ea3a` admission floor와 `9b91234c` sender-regime 흔적은
    same-thread parked handoff 하나로만은 설명되지 않았고,
    current common answer를 다시 smaller same-thread reuse family로 좁히는
    것도 local search drift였다.
  - current code는 원복 뒤 rebuild + targeted ctest gate를 다시 통과했다.
- 다음 iteration 우선순위
  - same-thread parked send admission lease family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 단계는 또 another same-thread reuse handoff가 아니라,
    `a819ea3a` send admission floor,
    `ff0140e5` pipe serialization floor,
    `98e7d324/9b91234c` public multipart/sender-regime 흔적을 함께 읽는
    다른 common send-side structural candidate 하나를 고른다.

## 99. 2026-03-29 `pipe.hpp` / `pipe.cpp` peer-progress refreshed HWM-credit cache candidate rejected

- 작업한 가설 1개
  - current residual의 `pipe serialization floor` 일부가
    steady-state `check_hwm()` arithmetic과 `_peers_msgs_read`
    refresh bookkeeping에 실려 있을 수 있으므로,
    peer-progress publish 시점마다 남은 outbound HWM credit을 cache해 두면
    final send hot path의 common broad win을 만들 수 있다고 봤다.
- candidate family 1개
  - `pipe.hpp` / `pipe.cpp` peer-progress refreshed HWM-credit cache split
- high-leverage 또는 semantic probe 근거
  - 이번 후보는 반복 금지된 plain mutex swap이나
    out-pipe devirtualization family를 다시 여는 대신,
    current `_out_sync` ordering/invariant는 유지한 채
    steady-state `check_hwm()` / `_msgs_written` accounting만 더 얇게 보는
    structural probe였다.
  - `ff0140e5` pipe serialization floor를 local helper call shaving이 아니라
    HWM/peer-progress bookkeeping 의미 단위로 다시 가르는 시도였지만,
    동시에 another pipe-local family로 다시 좁아지는 위험도 큰 후보였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - prompt/ststdin 기반 `timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink ...` consult는 출력 없이 `code 124` timeout으로 끝나 usable advisory를 얻지 못했다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `claude --help`
  - `cat <<'EOF' | timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink ... EOF`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pipe_hwm_credit_public_20260329`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pipe_hwm_credit_raw_20260329`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_014653_pipe_hwm_credit_public_20260329.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_014653_pipe_hwm_credit_public_20260329.txt)
  - [`perf_linux_20260329_014732_pipe_hwm_credit_raw_20260329.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_014732_pipe_hwm_credit_raw_20260329.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -15.04% / -24.43%`
    - `DEALER_DEALER tcp/inproc -8.68% / -32.46%`
  - raw
    - `PAIR tcp/inproc -20.98% / -20.36%`
    - `DEALER_DEALER tcp/inproc -20.37% / -22.58%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe.hpp` / `pipe.cpp` peer-progress refreshed HWM-credit cache
      candidate 전체
- 해석
  - targeted ctest gate는 통과했지만,
    public `PAIR inproc`와 `DEALER_DEALER inproc`,
    raw `PAIR/DEALER` tcp/inproc가 함께 guardrail 아래로 내려
    keep-worthy broad win이 아니었다.
  - 즉 current `pipe serialization floor`는
    steady-state `check_hwm()` arithmetic과 `_peers_msgs_read` refresh를
    cached credit 하나로 다시 쓰는 local pipe family만으로는
    설명되지 않았고, next step을 another pipe-local call shaving으로
    좁히는 것도 local search drift였다.
  - current code는 원복 뒤 rebuild + targeted ctest gate를 다시 통과했다.
- 다음 iteration 우선순위
  - `pipe.hpp` / `pipe.cpp` peer-progress refreshed HWM-credit cache family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 단계는 또 another pipe-local HWM helper가 아니라,
    `a819ea3a` send admission floor,
    `ff0140e5` pipe serialization floor,
    `98e7d324/9b91234c` public multipart/sender-regime 흔적을 함께 읽는
    다른 common send-side structural candidate를 고른다.

## 100. 2026-03-29 `socket_runtime.hpp` / `pipe.hpp` / `pipe.cpp` send-side layout regroup candidate rejected

- 작업한 가설 1개
  - current residual의 일부가
    `socket_lifecycle_coordinator_t`와 `pipe` outbound hot-state cluster의
    cache layout thickening에 실려 있을 수 있으므로,
    hot send-side field cluster를 앞쪽으로 다시 모으면
    `PAIR` / `DEALER` common broad win을 만들 수 있다고 봤다.
- candidate family 1개
  - `socket_runtime.hpp` lifecycle coordinator front-load +
    `pipe.hpp` / `pipe.cpp` send-side layout regroup
- high-leverage 또는 semantic probe 근거
  - 이번 후보는 반복 금지된 state repack/CAS shortcut이나
    pipe-local helper shaving을 다시 여는 대신,
    current kept boundary와 same ordering/invariant를 유지한 채
    send admission floor와 pipe serialization floor의 memory layout만
    얇게 보려는 structural probe였다.
  - 다만 logic 변화 없이 layout만 바꾸는 family라서,
    broad win을 못 만들면 곧바로 배제해야 하는 후보였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반
    `cat <<'EOF' | timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq ... EOF`
    consult는 출력 없이 `code 124` timeout으로 끝나 usable advisory를 얻지 못했다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `claude --help`
  - `cat <<'EOF' | timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq ... EOF`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_send_layout_regroup_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_send_layout_regroup_raw`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_024626_codex_20260329_send_layout_regroup_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_024626_codex_20260329_send_layout_regroup_public.txt)
  - [`perf_linux_20260329_024626_codex_20260329_send_layout_regroup_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_024626_codex_20260329_send_layout_regroup_raw.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -14.64% / -25.65%`
    - `DEALER_DEALER tcp/inproc -12.23% / -38.09%`
  - raw
    - `PAIR tcp/inproc -24.84% / -18.60%`
    - `DEALER_DEALER tcp/inproc -22.59% / -19.83%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.hpp` lifecycle coordinator front-load
    - `pipe.hpp` / `pipe.cpp` send-side layout regroup candidate 전체
- 해석
  - same-tag public/raw run은 parallel diagnostic이라 noisy일 수 있었지만,
    public `PAIR/DEALER` inproc과 raw `PAIR/DEALER` tcp/inproc가 함께 크게
    악화돼 keep-worthy broad win이 아니었다.
  - 즉 current residual은
    lifecycle coordinator / pipe outbound cluster의 memory layout regroup
    하나만으로는 설명되지 않았고,
    current next step을 another layout-only regroup으로 좁히는 것도
    local search drift였다.
  - current code는 원복 뒤 clean rebuild + targeted ctest gate를 다시 통과했다.
- 다음 iteration 우선순위
  - `socket_runtime.hpp` / `pipe.hpp` / `pipe.cpp`
    send-side layout regroup family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 단계는 another layout shuffle이 아니라,
    `a819ea3a` send admission floor,
    `ff0140e5` pipe serialization floor,
    `98e7d324/9b91234c` public multipart/sender-regime 흔적을 함께 읽는
    다른 common send-side structural candidate를 고른다.

## 101. 2026-03-29 `socket_base.hpp` / `socket_base_msg.cpp` preflight-before-public-admission candidate rejected

- 작업한 가설 1개
  - current residual의 일부가
    `a819ea3a` 이후 direct send의 initial preflight
    (`_ctx_terminated` / `msg->check()` / `process_commands(0, true)`)보다
    앞에서 public admission/sync를 잡는 current ordering 자체에 실려 있을 수
    있으므로,
    mailbox lifetime만 잠깐 잡고 preflight를 먼저 끝낸 뒤 실제 `xsend/retry`
    phase에서만 public send scope를 잡으면 `PAIR` / `DEALER` common broad win을
    만들 수 있다고 봤다.
- candidate family 1개
  - `socket_base.hpp` / `socket_base_msg.cpp`
    preflight-before-public-admission split
- high-leverage 또는 semantic probe 근거
  - 이번 후보는 반복 금지된 lazy-sync/state repack family를 다시 여는 대신,
    `a819ea3a` admission floor가 current send preflight보다 앞에서 잡히는
    structural ordering 자체를 줄여 보는 probe였다.
  - 다만 public close/send busy contract를 건드릴 수 있는 family라서,
    contract gate를 먼저 통과하지 못하면 바로 원복해야 하는 후보였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반
    `cat <<'EOF' | timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq ... EOF`
    consult는 끝까지 출력이 없었고 `code 124` timeout으로 종료됐다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `claude --help`
  - `cat <<'EOF' | timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq ... EOF`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_preflight_before_admission_public`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_030207_codex_20260329_preflight_before_admission_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_030207_codex_20260329_preflight_before_admission_public.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -26.28% / -26.58%`
    - `DEALER_DEALER tcp/inproc -24.14% / -35.55%`
  - raw
    - 미실행. public stage에서 guardrail을 크게 깨 바로 reject했다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_base.hpp` / `socket_base_msg.cpp`
      preflight-before-public-admission candidate 전체
- 해석
  - contract gate는 통과했지만 public `PAIR/DEALER` tcp/inproc가
    early authority와 session-local low baseline을 함께 크게 밑돌아
    keep-worthy broad win이 아니었다.
  - 즉 current residual은 direct send initial preflight보다 앞에서 잡히는
    admission ordering 하나만 바꾼다고 해결되지 않았고,
    mailbox-lifetime guard를 곁들여도 broad fix가 되지 않았다.
  - current code는 원복 뒤 rebuild + same contract gate를 다시 통과했다.
- 다음 iteration 우선순위
  - `socket_base.hpp` / `socket_base_msg.cpp`
    preflight-before-public-admission family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 단계는 another admission-order split이 아니라,
    `a819ea3a` send admission floor,
    `ff0140e5` pipe serialization floor,
    `98e7d324/9b91234c` public multipart/sender-regime 흔적을 함께 읽는
    다른 common send-side structural candidate를 고른다.

## 102. 2026-03-29 `public_api_inflight/public_api_closing/public_api_sync` split family stronger-gate recheck rejected

- 작업한 가설 1개
  - 2026-03-28에 이미 reject된
    `public_api_inflight/public_api_closing/public_api_sync` split family를
    stronger contract gate와 latest public authority로 다시 확인하면,
    hot inflight admission과 rare close/public-sync lifecycle bit를
    다시 분리하는 current coordinator state repack이
    `a819ea3a` 이후 common send floor를 broad하게 줄일 여지가 있는지
    재판정할 수 있다고 봤다.
- candidate family 1개
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    `public_api_inflight/public_api_closing/public_api_sync` split family
    stronger-gate recheck
- high-leverage 또는 semantic probe 근거
  - 이번 단계는 새 family 탐색이 아니라,
    이미 reject된 coordinator state split을
    `test_thread_safe_contract_policy` /
    `test_monitor_perf_contract`까지 포함한 stronger contract gate와
    current late-session baseline 기준으로 다시 확인하는 재판정 probe였다.
  - `libzmq`의 [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
    / [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)는
    send entry와 pipe duty가 current zlink보다 덜 얽혀 있으므로,
    이 family가 keep-worthy라면 public `PAIR/DEALER` authority에서도
    broad win이 다시 보여야 했다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반
    `cat <<'EOF' | timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq ... EOF`
    consult는 끝까지 출력이 없었고 `code 124` timeout으로 종료됐다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    - [`core/tests/unittest/unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `claude --help`
  - `cat <<'EOF' | timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq ... EOF`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_close_sync_state_split_public`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_031939_codex_20260329_close_sync_state_split_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_031939_codex_20260329_close_sync_state_split_public.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -17.22% / -24.11%`
    - `DEALER_DEALER tcp/inproc -14.31% / -29.56%`
  - raw
    - 미실행. public stage에서 guardrail을 크게 깨 바로 reject했다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.hpp` / `socket_runtime.cpp`
      `public_api_inflight/public_api_closing/public_api_sync` split family
      stronger-gate recheck 전체
    - companion `unittest_socket_runtime` adjustment
- 해석
  - stronger contract gate는 통과했지만 public `PAIR/DEALER` tcp/inproc가
    early authority와 session-local low baseline을 함께 밑돌아
    keep-worthy broad win이 아니었다.
  - 즉 current residual은 public lifecycle coordinator state packing을
    stronger gate로 다시 확인해도 해결되지 않았고,
    current code는 원복 뒤 rebuild + same contract gate를 다시 통과했다.
- 다음 iteration 우선순위
  - `public_api_inflight/public_api_closing/public_api_sync` split family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 단계는 another state split이 아니라,
    `a819ea3a` send admission floor,
    `ff0140e5` pipe serialization floor,
    `98e7d324/9b91234c` public multipart/sender-regime 흔적을 함께 읽는
    다른 common send-side structural candidate를 고른다.

## 103. 2026-03-29 `socket_base_msg.cpp` / `pair.cpp` / `dealer.cpp` / `lb.cpp` plain final-part sender-regime candidate rejected

- 작업한 가설 1개
  - current residual에는 plain non-routed final-part steady-state send가
    여전히 current send entry와 one-active `pipe::write_and_flush()` 사이의
    sender-regime 차이를 남기고 있을 수 있으므로,
    `PAIR` / `DEALER` final-part plain send를 따로 가르면
    `a819ea3a` admission floor와 `9b91234c` sender-regime 흔적을 함께
    조금이라도 얇게 만들 수 있다고 봤다.
- candidate family 1개
  - `socket_base_msg.cpp` / `pair.cpp` / `dealer.cpp` / `lb.cpp`
    plain non-routed final-part sender-regime split candidate
- high-leverage 또는 semantic probe 근거
  - 이번 단계는 rejected coordinator/pipe-local split family를 다시 여는 대신,
    current retained send boundary 위에서 plain final-part steady-state sender만
    별도 fast path로 보내면 common send-side residual이 줄어드는지 확인하는
    structural candidate였다.
  - `libzmq`의 [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
    / [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)는
    current zlink보다 final-part sender 의미가 덜 분기돼 있으므로,
    keep-worthy라면 public authority `PAIR/DEALER`에서도 broad win이
    보여야 했다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반
    `cat <<'EOF' | timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq ... EOF`
    consult는 끝까지 출력이 없었고 `code 124` timeout으로 종료됐다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
    - [`core/src/sockets/pair.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.hpp)
    - [`core/src/sockets/pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp)
    - [`core/src/sockets/dealer.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.hpp)
    - [`core/src/sockets/dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)
    - [`core/src/sockets/lb.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.hpp)
    - [`core/src/sockets/lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `claude --help`
  - `cat <<'EOF' | timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq ... EOF`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_plain_final_regime_public`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_034150_codex_20260329_plain_final_regime_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_034150_codex_20260329_plain_final_regime_public.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -12.23% / -29.92%`
    - `DEALER_DEALER tcp/inproc -11.44% / -34.04%`
  - raw
    - 미실행. public stage에서 guardrail을 크게 깨 바로 reject했다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_base_msg.cpp` / `pair.cpp` / `dealer.cpp` / `lb.cpp`
      plain non-routed final-part sender-regime split candidate 전체
- 해석
  - stronger contract gate는 통과했지만 public `PAIR/DEALER` 특히 `inproc`가
    early authority와 session-local low baseline을 함께 크게 밑돌아
    keep-worthy broad win이 아니었다.
  - 즉 current residual은 plain final-part sender-regime만 따로 갈라서
    해결되지 않았고, current code는 원복 뒤 rebuild + same contract gate를
    다시 통과했다.
- 다음 iteration 우선순위
  - plain final-part sender-regime split family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 단계는 another final-part direct path split이 아니라,
    `a819ea3a` send admission floor,
    `ff0140e5` pipe serialization floor,
    `98e7d324/9b91234c` public multipart/sender-regime 흔적을 함께 읽는
    다른 common send-side structural candidate를 고른다.

## 104. 2026-03-29 `pipe.hpp` / `pipe.cpp` `activate_write` progress-command coalesce candidate rejected

- 작업한 가설 1개
  - current residual에 아직 `9b91234c` sender-regime 흔적이 남아 있다면,
    `_lwm` boundary에서 반복 발행되는 `activate_write` progress command를
    coalesce하는 것만으로도 plain one-way steady-state send의
    common pipe-side fixed cost를 조금 줄일 수 있다고 봤다.
- candidate family 1개
  - `pipe.hpp` / `pipe.cpp`
    `activate_write` progress-command coalesce candidate
- high-leverage 또는 semantic probe 근거
  - 이번 단계는 fresh public admission split을 다시 열기 전에,
    current retained `_out_sync` boundary 아래에서
    sender-regime/progress-command emission count만 줄였을 때 broad win이
    보이는지 확인하는 semantic probe였다.
  - `libzmq`의 [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)와
    [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)는
    current zlink보다 send progress publication이 덜 두꺼우므로,
    keep-worthy라면 public `PAIR/DEALER` authority에서도 broad win이
    다시 보여야 했다.
- 참고한 `libzmq` 대응 파일
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - 이어진 stdin/prompt 기반 `claude -p --permission-mode
    bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir
    /home/hep7/project/kairos/libzmq ...` consult 시도들은 usable output 없이
    멈췄고, short retry도 `code 124` timeout으로 끝났다.
  - 이번 단계 consult는 unavailable로 기록한다.
- 수정한 파일 경로
  - 원복 전 candidate:
    - [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `claude --help`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_activate_write_coalesce_public`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_041240_codex_20260329_activate_write_coalesce_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_041240_codex_20260329_activate_write_coalesce_public.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -10.58% / -34.54%`
    - `DEALER_DEALER tcp/inproc -30.23% / -25.32%`
  - raw
    - 미실행. public stage에서 early authority와 session-local low baseline을
      함께 못 지켜 바로 reject했다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe.hpp` / `pipe.cpp`
      `activate_write` progress-command coalesce candidate 전체
- 해석
  - targeted gate는 통과했지만 public `PAIR inproc`와
    `DEALER_DEALER tcp/inproc`가 함께 크게 무너져 keep-worthy broad win이
    아니었다.
  - 즉 current residual은 `_lwm` boundary progress-command emission count
    하나만 줄이는 local pipe tweak로는 해결되지 않았고,
    progress-command coalesce family도 current broad fix가 아니다.
  - current code는 원복 뒤 rebuild + same gate를 다시 통과했다.
- 다음 iteration 우선순위
  - `_lwm` boundary `activate_write` progress-command coalesce family는
    새 broad evidence 없이 다시 올리지 않는다.
  - 다음 단계는 another progress-command emission tweak가 아니라,
    `a819ea3a` send admission floor,
    `ff0140e5` pipe serialization floor,
    `98e7d324/9b91234c` public multipart/sender-regime 흔적을 함께 읽는
    다른 common send-side structural candidate를 고른다.

## 105. 2026-03-29 current-tree `pipe_write_and_flush` split instrumentation + `ypipe` combined write/publication candidate rejected

- 작업한 가설 1개
  - current `pipe serialization floor`의 실제 큰 축이
    sleeping-reader wakeup/notify가 아니라 successful publication path라면,
    `ypipe`가 final write와 publication CAS를 한 번에 처리하는 local
    structural helper로 `PAIR` / `DEALER` common win을 만들 수 있다고 봤다.
- candidate family 1개
  - `core/src/core/ypipe_base.hpp` / `core/src/core/ypipe.hpp` /
    `core/src/core/ypipe_conflate.hpp` combined write+publication candidate
- high-leverage 또는 semantic probe 근거
  - 이번 candidate 전 direct instrumentation에서
    `pipe_write_and_flush` 내부 bucket을 split해
    `flush`가 `lock/hwm/write`보다 큰 steady-state cost 축이고,
    `flush false` 비중은 약 5~6%라는 점을 확인했다.
  - 따라서 이번 후보는 이미 reject된 notify placement/progress-command
    tweak를 반복하지 않고, actual successful publication path만 더 직접
    얇게 만드는 probe였다.
- 참고한 `libzmq` 대응 파일
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`ypipe.hpp`](/home/hep7/project/kairos/libzmq/src/ypipe.hpp)
  - [`ypipe_base.hpp`](/home/hep7/project/kairos/libzmq/src/ypipe_base.hpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - 이번 candidate 전용 `claude` consult는 실행하지 않았다.
- 수정한 파일 경로
  - direct instrumentation 후 원복:
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - candidate 적용 후 원복:
    - [`core/src/core/ypipe_base.hpp`](/home/hep7/project/kairos/zlink/core/src/core/ypipe_base.hpp)
    - [`core/src/core/ypipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/ypipe.hpp)
    - [`core/src/core/ypipe_conflate.hpp`](/home/hep7/project/kairos/zlink/core/src/core/ypipe_conflate.hpp)
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`core/tests/unittest/unittest_ypipe.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_ypipe.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `ZLINK_PROFILE_PIPE_WRITE_AND_FLUSH=1 core/build/bin/comp_zlink_pair zlink inproc 64`
  - `ZLINK_PROFILE_PIPE_WRITE_AND_FLUSH=1 core/build/bin/comp_zlink_dealer_dealer zlink inproc 64`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_ypipe|unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_router_multiple_dealers|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract|test_xpub_nodrop)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_ypipe_write_publish_public`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_ypipe|unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_router_multiple_dealers|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract|test_xpub_nodrop)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_043927_codex_20260329_ypipe_write_publish_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_043927_codex_20260329_ypipe_write_publish_public.txt)
  - direct instrumentation은 above two `comp_zlink_*` run stdout/stderr로만 남겼다.
- 핵심 수치
  - direct instrumentation
    - `PAIR inproc 64`: total `866.94`, lock `70.95`, hwm `25.12`,
      write `38.50`, flush `259.26`, flush outcome `true=8840861 false=539397`
    - `DEALER_DEALER inproc 64`: total `862.54`, lock `70.63`,
      hwm `25.33`, write `38.98`, flush `266.00`,
      flush outcome `true=8740895 false=506441`
  - public
    - `PAIR tcp/inproc -36.56% / -24.11%`
    - `DEALER_DEALER tcp/inproc -10.62% / -18.47%`
  - raw
    - 미실행. public `PAIR`가 early authority와 session-local low baseline을
      모두 크게 깨서 바로 reject했다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `ypipe` combined write+publication candidate 전체
    - temporary `pipe_write_and_flush` split instrumentation
- 해석
  - current `pipe_write_and_flush` cost는 local notify/no-op이 아니라
    successful publication/CAS path에 더 많이 실려 있었다.
  - 하지만 같은 ordering을 유지한 local `ypipe` combined publication helper도
    public `PAIR tcp/inproc`를 바로 무너뜨렸다.
  - 즉 current residual은 `flush true` dominant라는 이유만으로 another local
    `ypipe` call-shape shave로 풀리지 않고,
    `a819ea3a` admission floor와 `ff0140e5` pipe floor를 함께 읽는 broader
    structural candidate가 여전히 필요하다.
  - current code는 원복 뒤 rebuild + same gate rerun을 다시 통과했다.
- 다음 iteration 우선순위
  - `ypipe` combined write+publication family는
    새 broad evidence 없이 다시 올리지 않는다.
- next step은 another local publication helper가 아니라,
  `a819ea3a` / `ff0140e5` / `98e7d324` / `9b91234c` 축을 다시 붙여
  새 common send-side structural candidate를 고르는 것이다.

## 107. 2026-03-29 env-gated send-scope split instrumentation shows lifecycle atomics are not the dominant current cost

- 작업한 가설 1개
  - earlier `pair_inproc_send_profile_20260328.txt` /
    `dealer_inproc_send_profile_20260328.txt`의
    `socket_scope_construct ~1266/1314 ticks` bucket이
    정말 `a819ea3a` admission atomics 자체를 가리키는지 먼저 갈라야,
    another admission-floor-only family를 끊고 next broad hypothesis를
    다시 정렬할 수 있다고 봤다.
- candidate family 1개
  - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    env-gated send-scope split instrumentation
- high-leverage 또는 semantic probe 근거
  - 이번 단계는 새 hot-path optimization이 아니라,
    current source-of-truth의 next step을 잘못된 bucket 해석 위에
    계속 두지 않기 위한 diagnostic-only instrumentation이었다.
  - `PAIR` no-sync와 `DEALER_DEALER` sync-fast 양쪽에서
    lifecycle primitive와 scope ctor/dtor를 직접 분리하면,
    current residual direct cause가 admission floor인지
    xsend/pipe floor인지 다시 정렬할 수 있다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - 이번 diagnostic 단계에서는 `claude` consult를 따로 돌리지 않았다.
- 수정한 파일 경로
  - temporary instrumentation 후 원복:
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `ZLINK_SEND_SCOPE_PROFILE_PATH=/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/pair_send_scope_profile_20260329.txt python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR --msg-sizes 64 --transport inproc --runs 1 --build-dir core/build --results-tag codex_20260329_pair_send_scope_profile`
  - `ZLINK_SEND_SCOPE_PROFILE_PATH=/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/dealer_send_scope_profile_20260329.txt python3 core/bench/with_zmq/single/run_comparison.py --patterns DEALER_DEALER --msg-sizes 64 --transport inproc --runs 1 --build-dir core/build --results-tag codex_20260329_dealer_send_scope_profile`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
- 생성된 결과 파일 경로
  - [`pair_send_scope_profile_20260329.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/pair_send_scope_profile_20260329.txt)
  - [`dealer_send_scope_profile_20260329.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/dealer_send_scope_profile_20260329.txt)
  - profiler-on comparison output:
    - [`perf_linux_20260329_051100_codex_20260329_pair_send_scope_profile.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_051100_codex_20260329_pair_send_scope_profile.txt)
    - [`perf_linux_20260329_051100_codex_20260329_dealer_send_scope_profile.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_051100_codex_20260329_dealer_send_scope_profile.txt)
- 핵심 수치
  - `PAIR` no-sync diagnostic
    - `send_scope_ctor_total 174.36`
    - `send_scope_dtor_total 175.98`
    - `lifecycle_enter_public_api 49.70`
    - `lifecycle_leave_public_api 50.01`
  - `DEALER_DEALER` sync-fast diagnostic
    - `send_scope_ctor_total 174.80`
    - `send_scope_dtor_total 176.78`
    - `lifecycle_enter_public_api_and_lock_sync_fast 49.66`
    - `lifecycle_unlock_public_api_sync_and_leave 49.67`
  - profiler-on throughput
    - `PAIR inproc -49.83%`
    - `DEALER_DEALER inproc -53.20%`
    - above throughput는 instrumentation overhead와 overlapped launch 영향 때문에
      authority가 아니라 diagnostic artifact로만 취급한다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - env-gated send-scope split instrumentation 전체
- 해석
  - earlier `socket_scope_construct ~1266/1314 ticks` bucket은
    lifecycle admission atomics 단독으로 설명되지 않았다.
  - current clean-tree 기준 lifecycle primitive 자체는 `~50 ticks`,
    send-scope ctor/dtor total도 `~175/~176 ticks` 수준이라서,
    another admission-floor-only lifecycle family를 next implementation target으로
    두는 건 local search drift다.
  - 따라서 next step은 `a819ea3a` admission floor를 historical input으로만
    유지하고,
    `xsend_initial` / `pipe::_out_sync` publication floor와
    `98e7d324/9b91234c` public multipart/sender-regime,
    routed/source-rid export differential 쪽으로 implementation priority를
    내리는 것이다.
  - current code는 temporary instrumentation 원복 뒤 rebuild + same gate rerun을
    다시 통과했다.
- 다음 iteration 우선순위
  - admission-floor-only lifecycle fast path family는
    새 broad evidence 없이 다시 올리지 않는다.
  - next step은 current kept boundary 위에서
    `xsend_initial` / `pipe serialization` / routed export differential을
    함께 읽는 structural candidate를 고르는 것이다.

## 106. 2026-03-29 `pipe.cpp` `process_activate_read()` steady-state read-activation split candidate rejected

- 작업한 가설 1개
  - `ff0140e5` 이후 current residual에 아직 recv-side activation ordering
    고정비가 남아 있다면, `process_activate_read()`에서만 `_out_sync`
    steady-state work를 더 얇게 나누는 local structural split으로
    `PAIR` / `DEALER` broad win을 만들 수 있다고 봤다.
- candidate family 1개
  - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    `process_activate_read()` steady-state read-activation split candidate
- high-leverage 또는 semantic probe 근거
  - 이번 라운드는 helper-level send micro-tuning을 다시 반복하지 않고,
    bisect historical axis 중 `ff0140e5` read-side residue가 current
    HEAD에도 남아 있는지를 최소 범위로 먼저 확인하는 probe였다.
  - 이미 current tree에서 `check_read()` / `read()`는 lock-free였으므로,
    실제 probe 범위는 `process_activate_read()` one point change였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - stdin 기반
    `timeout 90s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
    consult는 usable output 없이 `code 124` timeout으로 끝나
    이번 candidate에는 advisory를 쓰지 못했다.
- 수정한 파일 경로
  - candidate 적용 후 원복:
    - [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_ypipe|unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_recv_activation_public`
  - 원복 뒤 `cmake --build core/build -j$(nproc)`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_ypipe|unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_045615_codex_20260329_recv_activation_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_045615_codex_20260329_recv_activation_public.txt)
- 핵심 수치
  - public
    - `PAIR tcp/inproc -27.18% / -17.96%`
    - `DEALER_DEALER tcp/inproc -22.07% / -18.31%`
  - raw
    - 미실행. public `PAIR tcp`와 `DEALER_DEALER tcp`가 early authority를
      바로 크게 깨서 raw 없이 reject했다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `process_activate_read()` steady-state read-activation split candidate 전체
- 해석
  - current tree에서 recv-side local activation helper 하나만 더 나눠도
    broad win은 나오지 않았다.
  - 즉 `ff0140e5` read-side residue alone은 current residual direct cause의
    주축이 아니고, `a819ea3a` admission floor +
    `ff0140e5` send-side pipe floor +
    `98e7d324/9b91234c` public multipart/sender-regime 의미를 함께 읽는
    broader structural candidate가 여전히 필요하다.
  - candidate 적용 직후 첫 ctest는 build overlap 때문에
    `core/build/lib/libzlink.so.5: file too short` loader race가 있었지만,
    build 완료 뒤 같은 gate를 다시 실행해 정상 통과를 확인했다.
- 다음 iteration 우선순위
  - `process_activate_read()` steady-state read-activation split family는
    새 broad evidence 없이 다시 올리지 않는다.
  - next step은 another recv-side local helper tweak가 아니라,
    historical axis 4개를 current send/public residual direct cause 관점에서
    다시 묶는 broader structural candidate를 세우는 것이다.

## 107. 2026-03-29 `socket_message_send_api.cpp` public `ROUTER` nonblocking envelope same-path fast path rejected

- 작업한 가설 1개
  - `98e7d324/9b91234c` public multipart/sender-regime 잔여가 current
    `ROUTER` public path에도 남아 있다면, blocking send에서 이미 쓰는
    routing-id envelope `send_routed()` one-part fold를 `ZLINK_DONTWAIT`
    경로까지 넓히는 것만으로도 targeted `ROUTER_ROUTER` 수치가 조금은
    움직여야 한다고 봤다.
- candidate family 1개
  - [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
    public `ROUTER` nonblocking envelope -> `send_routed()` same-path fast path
- high-leverage 또는 semantic probe 근거
  - current blocking default send는 이미 same-path fold를 타므로,
    남은 public routed surface 차이가 nonblocking envelope materialize
    branch에도 있는지 최소 범위로 확인하는 probe였다.
  - 다만 실제 bench hot loop가 이 경로를 쓰지 않으면 reject해야 하므로,
    targeted single로 바로 확인했다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`router.cpp`](/home/hep7/project/kairos/libzmq/src/router.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - 하지만 stdin/prompt 기반 `timeout 120s claude -p --permission-mode bypassPermissions --add-dir ...`
    consult 재시도는 usable output 없이 `code 124` timeout으로 끝나
    이번 candidate에는 advisory를 쓰지 못했다.
- 수정한 파일 경로
  - candidate 적용 후 원복:
    - [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
    - [`core/tests/integration/test_public_inproc_multipart_send.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_public_inproc_multipart_send.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_router_mandatory_hwm|test_multi_socket_contract_regressions|test_socket_with_handler)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_router_public_nonblocking_envelope`
  - 원복 뒤 `cmake --build core/build -j$(nproc) --target test_public_inproc_multipart_send test_router_mandatory_hwm`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_router_mandatory_hwm)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_053009_codex_20260329_router_public_nonblocking_envelope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_053009_codex_20260329_router_public_nonblocking_envelope.txt)
- 핵심 수치
  - public
    - `ROUTER_ROUTER tcp/inproc -58.16% / -31.94%`
  - raw
    - 미실행. targeted public stage에서 accepted baseline보다 더 나빠 바로
      reject했다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - public `ROUTER` nonblocking envelope same-path fast path 전체
- 해석
  - [`bench_zlink_router_router.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_router_router.cpp)
    active phase는 payload send에 `flags=0` blocking path를 쓴다.
  - 즉 이번 candidate는 hot loop가 아니라 handshake/nonblocking probe에만
    걸렸고, targeted single 수치도 더 나빠 keep-worthy delta가 아니었다.
  - current `ROUTER` 잔여 gap은 nonblocking envelope local fast path보다
    blocking default path 기준 routed recv ordering /
    `recv_routed()` source-rid export / 공통 `_out_sync` serialization floor를
    더 직접 분리해야 설명된다.
- 다음 iteration 우선순위
  - public `ROUTER` nonblocking envelope same-path fast path family는
    새 broad evidence 없이 다시 올리지 않는다.
  - next step은 blocking default routed path 기준의
    recv ordering/export differential 또는 공통 send/publication floor다.

## 108. 2026-03-29 shared logical multipart entry-state reuse candidate rejected

- 작업한 가설 1개
  - current residual에 아직 `98e7d324/9b91234c` public multipart/
    sender-regime 흔적이 남아 있다면,
    logical multipart scope 안에서 프레임마다 다시 밟는
    `process_commands(0, true)` entry cost를 한 번으로 줄이는 것만으로도
    `ROUTER` public 2-part hot loop absolute throughput이 움직여야 한다고
    봤다.
- candidate family 1개
  - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  - shared logical multipart entry-state reuse candidate
- high-leverage 또는 semantic probe 근거
  - current `multipart_send_txn.cpp`는 logical multipart send scope를
    공유하지만 실제 send entry는 프레임마다 `send_direct_with_retry()`를 다시
    타므로, active `ROUTER_ROUTER` public default path에서는 같은 메시지마다
    2-part entry cost가 남아 있었다.
  - 이번 후보는 rejected plain final-part sender-regime split과 달리
    non-routed single-part local fast path가 아니라, shared logical multipart
    sender-regime 자체를 current retained boundary 위에서 얇게 만드는 probe였다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
  - [`router.cpp`](/home/hep7/project/kairos/libzmq/src/router.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - `claude --help`는 통과했다.
  - 하지만 stdin 기반
    `printf '...' | timeout 60s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
    consult는 usable output 없이 `code 124` timeout으로 끝나
    이번 candidate에는 advisory를 쓰지 못했다.
- 수정한 파일 경로
  - candidate 적용 후 원복:
    - [`core/src/sockets/socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `printf '...' | timeout 60s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_multipart_sender_regime_public`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_multipart_sender_regime_public_rerun`
  - 원복 뒤
    `cmake --build core/build -j$(nproc) --target unittest_socket_runtime test_public_inproc_multipart_send test_multi_socket_contract_regressions test_socket_with_handler test_router_mandatory_hwm`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260329_054339_codex_20260329_multipart_sender_regime_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_054339_codex_20260329_multipart_sender_regime_public.txt)
  - [`perf_linux_20260329_054423_codex_20260329_multipart_sender_regime_public_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_054423_codex_20260329_multipart_sender_regime_public_rerun.txt)
- 핵심 수치
  - public first
    - `ROUTER_ROUTER tcp/inproc -54.15% / -29.86%`
    - zlink absolute throughput `1296.50 / 2572.46 Kmsg/s`
  - public rerun
    - `ROUTER_ROUTER tcp/inproc -58.08% / -22.16%`
    - zlink absolute throughput `1292.20 / 2574.88 Kmsg/s`
  - raw
    - 미실행. relative diff가 libzmq baseline에 끌려 흔들렸지만 zlink absolute
      throughput이 거의 안 움직여 raw/public guardrail 단계로 올리지 않았다.
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - shared logical multipart entry-state reuse candidate 전체
- 해석
  - relative diff는 `libzmq` baseline 흔들림 때문에 `tcp`/`inproc` 방향이
    엇갈려 보였지만, zlink absolute throughput은 두 run 모두
    `tcp ~1.29Mmsg/s`, `inproc ~2.57Mmsg/s`로 거의 고정됐다.
  - 즉 current residual은 logical multipart scope 아래
    entry `process_commands()` reuse 하나만 추가한다고 줄지 않았고,
    current `ROUTER` 잔여 gap을 local multipart entry-state family 하나로
    설명할 수는 없다.
  - current code는 원복 뒤 targeted rebuild + same gate rerun을 다시 통과했다.
- 다음 iteration 우선순위
  - shared logical multipart entry-state reuse family는
    새 broad evidence 없이 다시 올리지 않는다.
  - next step은 blocking default routed path 기준 recv ordering/export
    differential 또는 공통 `_out_sync` send/publication floor다.

## 109. 2026-03-29 routed source-rid zeroing-floor candidate rejected

- 작업한 가설 1개
  - current `ROUTER_ROUTER` blocking recv residual의 일부가
    `recv_routed()` export 경로에서 `zlink_routing_id_t`(256B)를 반복
    full-zero 하는 고정비라면,
    output reset을 `size=0`만으로 줄여도 absolute throughput이 움직여야
    한다고 봤다.
- candidate family 1개
  - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  - [`core/src/api/socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
  - routed source-rid zeroing-floor candidate
- high-leverage 또는 semantic probe 근거
  - `zlink_routing_id_t`는 `size + data[255]`라서
    `ROUTER` public recv fast path에서는 output reset이 outer public layer와
    `socket_base_t::recv_routed()`에서 반복될 여지가 있었다.
  - active `ROUTER_ROUTER` phase는 blocking default path를 쓰므로,
    local routed export floor를 직접 줄였을 때 absolute throughput이
    안 움직이면 다음 단계는 recv ordering 또는 공통 `_out_sync` 쪽으로
    더 직접 넘어가야 한다.
- 참고한 `libzmq` 대응 파일
  - [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`router.cpp`](/home/hep7/project/kairos/libzmq/src/router.cpp)
- `claude` consult 여부와 핵심 조언 1~3줄
  - stdin 기반
    `printf '...' | timeout 25s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
    consult를 시도했지만 usable output 없이 `code 124` timeout으로 끝나
    advisory를 쓰지 못했다.
- 수정한 파일 경로
  - candidate 적용 후 원복:
    - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
    - [`core/src/api/socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
    - [`core/tests/integration/test_public_inproc_multipart_send.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_public_inproc_multipart_send.cpp)
  - 최종 유지:
    - 없음
- 실행한 명령
  - `printf '...' | timeout 25s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
  - `cmake --build core/build -j$(nproc) --target test_public_inproc_multipart_send test_router_mandatory_hwm unittest_socket_runtime`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_router_mandatory_hwm|test_stream_socket)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_router_recv_rid_zeroing_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_router_recv_rid_zeroing_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_router_recv_rid_zeroing_public_authority`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260329_router_recv_rid_zeroing_raw_authority`
  - 원복 뒤
    `cmake --build core/build -j$(nproc) --target test_public_inproc_multipart_send test_router_mandatory_hwm unittest_socket_runtime`
  - 원복 뒤
    `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_router_mandatory_hwm|test_stream_socket)$' -j1`
- 생성된 결과 파일 경로
  - noisy diagnostic
    - [`perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_public.txt)
    - [`perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_raw.txt)
  - authority
    - [`perf_linux_20260329_055517_codex_20260329_router_recv_rid_zeroing_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055517_codex_20260329_router_recv_rid_zeroing_public_authority.txt)
    - [`perf_linux_20260329_055548_codex_20260329_router_recv_rid_zeroing_raw_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055548_codex_20260329_router_recv_rid_zeroing_raw_authority.txt)
- 핵심 수치
  - noisy concurrent diagnostic
    - public `ROUTER_ROUTER tcp/inproc -56.63% / -29.03%`
    - raw `ROUTER_ROUTER tcp/inproc -57.18% / -27.48%`
  - authority public
    - `ROUTER_ROUTER tcp/inproc -56.10% / -32.31%`
    - zlink absolute throughput `1297.34 / 2575.26 Kmsg/s`
  - authority raw
    - `ROUTER_ROUTER tcp/inproc -57.70% / -22.00%`
    - zlink absolute throughput `1295.90 / 2570.82 Kmsg/s`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - routed source-rid zeroing-floor candidate 전체
- 해석
  - concurrent public/raw run은 noisy diagnostic이라 authority로 쓰지 않았다.
  - authority rerun에서도 public은 `inproc`가 오히려 더 나빠졌고,
    raw는 relative diff가 일부 움직여도 zlink absolute throughput이
    기존 `~1.29M / ~2.57Mmsg/s` 범위에 머물렀다.
  - 즉 current `ROUTER` residual의 본체를
    `recv_routed()` source-rid output full-zero 하나로 설명할 수 없고,
    다음 단계는 local export memset shaving이 아니라
    blocking default routed recv ordering 또는 공통 `_out_sync`
    serialization floor를 더 직접 분리하는 쪽이다.
