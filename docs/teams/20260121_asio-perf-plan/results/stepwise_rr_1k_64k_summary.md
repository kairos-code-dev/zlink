# ASIO 최적화 가설 실험 요약 (ROUTER_ROUTER, 1KB/64KB)

실험 조건
- 패턴: ROUTER_ROUTER
- 전송: tcp, inproc, ipc
- 메시지 크기: 1024B, 65536B
- runs: 3
- 기준선: benchwithzmq/libzmq_cache.json 사용
- 빌드: build/bench, --reuse-build, --skip-libzmq
- 참고: Diff(%)는 libzmq 대비 zlink 변화

가설별 결과
1) Handler allocator 재사용
- 변경: asio_engine async 핸들러에 per-connection allocator 적용
- 결과 파일: docs/teams/20260121_asio-perf-plan/results/exp_step1_handler_alloc_20260121_014706_rr_1k_64k.txt
- tcp tp: +0.64% / +2.79%, tcp lat: +1.45% / -2.73%
- inproc tp: -1.97% / +3.46%, inproc lat: -5.56% / +0.25%
- ipc tp: +1.23% / +6.61%, ipc lat: +8.18% / +18.32%

2) 핸들러 체인 단순화 (즉시 경로 speculative write)
- 변경: handshaking 경로에서 start_async_write 대신 restart_output 사용
- 결과 파일: docs/teams/20260121_asio-perf-plan/results/exp_step2_handler_chain_20260121_015239_rr_1k_64k.txt
- tcp tp: +1.67% / +3.53%, tcp lat: +3.63% / -2.10%
- inproc tp: -0.60% / +6.80%, inproc lat: +0.00% / -1.78%
- ipc tp: +3.80% / +1.43%, ipc lat: +10.29% / +17.01%

3) write coalescing / batching
- 변경: ZMQ_ASIO_MIN_OUT_BATCH=16384 실험
- 결과 파일: docs/teams/20260121_asio-perf-plan/results/exp_step3_write_coalesce_20260121_015358_rr_1k_64k.txt
- tcp/ipc에서 no_data 발생 (측정 실패)
- 조치: 해당 변경은 롤백

4) read/write pump 정리 (write 완료 후 speculative_write)
- 변경: on_write_complete에서 start_async_write 대신 speculative_write 사용
- 결과 파일: docs/teams/20260121_asio-perf-plan/results/exp_step4_write_pump_20260121_015532_rr_1k_64k.txt
- tcp tp: +4.92% / -5.28%, tcp lat: +3.39% / -1.03%
- inproc tp: -1.68% / +6.20%, inproc lat: -5.56% / +0.25%
- ipc tp: +3.99% / +10.59%, ipc lat: +9.50% / +19.34%

5) 버퍼 재사용
- 변경: pending buffer pool 사전 reserve 및 재사용
- 결과 파일: docs/teams/20260121_asio-perf-plan/results/exp_step5_buffer_reuse_20260121_015648_rr_1k_64k.txt
- tcp tp: +1.17% / +0.89%, tcp lat: +0.73% / -1.39%
- inproc tp: -0.50% / +6.23%, inproc lat: +0.00% / +0.25%
- ipc tp: +2.13% / +5.36%, ipc lat: +7.12% / +16.65%

6) timer/wakeup 최소화
- 변경: asio_poller idle backoff (run_for 대기 증가)
- 결과 파일: docs/teams/20260121_asio-perf-plan/results/exp_step6_idle_backoff_20260121_020006_rr_1k_64k.txt
- tcp tp: +3.84% / +3.57%, tcp lat: +4.12% / +0.40%
- inproc tp: -2.86% / +3.94%, inproc lat: -5.56% / -0.25%
- ipc tp: +1.63% / +6.81%, ipc lat: +7.92% / +18.69%

7) io_context 실행 정책
- 변경: ZMQ_ASIO_IOCTX_MODE=run_for 실험
- 결과 파일: docs/teams/20260121_asio-perf-plan/results/exp_step7_ioctx_mode_run_for_20260121_020136_rr_1k_64k.txt
- tcp tp: +3.98% / +1.19%, tcp lat: +4.36% / -3.00%
- inproc tp: +1.72% / +2.97%, inproc lat: +0.00% / +1.78%
- ipc tp: +0.95% / +5.55%, ipc lat: +7.92% / +17.45%

8) 전송 옵션 (gated)
- 변경: ZMQ_ASIO_TCP_NODELAY=1, ZMQ_ASIO_TCP_QUICKACK=1, ZMQ_ASIO_TCP_BUSY_POLL=50
- 결과 파일: docs/teams/20260121_asio-perf-plan/results/exp_step8_transport_opts_20260121_020309_rr_1k_64k.txt
- tcp tp: +0.93% / -5.48%, tcp lat: +4.36% / -2.55%
- inproc tp: +0.63% / -0.09%, inproc lat: +0.00% / -2.29%
- ipc tp: -1.89% / +1.08%, ipc lat: +4.49% / +4.85%

현재 코드 상태
- 유지: handler allocator 적용, handshaking write 경로 개선, write 완료 후 speculative_write,
  pending buffer pool pre-reserve, idle backoff, io_context 모드 토글, TCP 옵션 env 게이트.
- 롤백: write coalescing/min out batch 실험.

요약 판단
- tcp 64KB는 단계별 편차가 있으나, ipc 64KB는 4/5/6/7 단계에서 일관 개선 경향.
- inproc 1KB는 몇 단계에서 소폭 회귀(1.7~2.9%)가 관찰됨.
- TCP 옵션 게이트는 64KB tcp에서 회귀가 발생해 기본 적용은 비권장.
