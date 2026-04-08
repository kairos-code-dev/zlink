# Core PERF STREAM Client

multi `STREAM` 벤치마크 경로에서 공유하는 클라이언트 코드다.

정책 기준:

- [PERF_POLICY.md](../../../../doc/perf/PERF_POLICY.md)
- [PERF_MULTI_TEST_POLICY.md](../../../../doc/perf/PERF_MULTI_TEST_POLICY.md)

핵심 파일:

- `perf_stream_client_options.hpp`: CLI 파싱과 케이스별 결과 보조 함수
- `perf_stream_client_session.hpp`: 비동기 세션, `ready -> active` phase 처리
- `perf_stream_bench_client.hpp`: 오케스트레이터, connect 스케줄링, 결과 출력
- `stream_client.hpp`: stop token 전달에 사용하는 transport client

공유 클라이언트가 내보내는 Tier 1 metric은 다음 다섯 개다.
`throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99`

빌드:

```bash
cmake --build core/build --target perf_stream_client -j$(nproc)
```

예시:

```bash
core/build/bin/perf_stream_client \
  --pattern STREAM \
  --transport tcp \
  --endpoint tcp://127.0.0.1:15557 \
  --sizes 64 \
  --ccu 10000 \
  --duration 5 \
  --io-threads 4 \
  --send-stop-token 1
```

`--transport`는 `tcp`, `tls`, `ws`, `wss`를 지원한다.
