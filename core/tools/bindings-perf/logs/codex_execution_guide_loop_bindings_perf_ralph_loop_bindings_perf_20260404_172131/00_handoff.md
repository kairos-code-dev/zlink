# Handoff

- active_language: cpp
- status: in_progress
- unresolved_work:
  - cpp MULTI_SPOT snapshot/sleep ready gating replacement is done and verified on tcp full-size recv/callback runs; keep that implementation unless a later comparable rerun disproves it.
  - latest baseline-aligned cpp multi recv rerun exists, but it is still non-comparable: `/home/hep7/project/kairos/zlink/bindings/cpp/perf/results/multi/report/perf_linux_recv_20260404_173823_codex_cpp_multi_recv_comparable_20260404.txt` ended `status=partial` with `expected result lines 680`, `actual result lines 620`.
  - fix priority is now policy/full-surface repair, not ratio tuning:
    - repair missing resource metric/result emission for cpp multi echo/raw surfaces (`MULTI_DEALER_DEALER`, `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`) and verify whether `PUBSUB/SPOT/STREAM` should also emit `server_cpu_pct/server_mem_mb/client_cpu_pct/client_mem_mb` like core baseline
    - repair `MULTI_SPOT` secure transports (`tls`, `wss`) in recv mode; current report shows all secure sizes failed with `non_zero_exit_1_size_64` or `non_zero_exit_1_CLIENT_READY,64_size_64`
  - callback comparable rerun has not started yet because recv full-surface normal operation is still broken and remains the higher-priority policy issue.

- previous_session_dir: /home/hep7/project/kairos/zlink/core/tools/bindings-perf/logs/codex_execution_guide_loop_bindings_perf_ralph_loop_bindings_perf_20260404_172004
- instruction:
  - Review the previous session's run state, checklist, notes, prompt, and run log first.
  - If unresolved work remains, carry it forward here before starting new work.
