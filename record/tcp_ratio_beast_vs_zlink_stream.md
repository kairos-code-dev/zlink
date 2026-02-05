# TCP 대비 Throughput 비율 (Beast vs zlink)

- Runs: 10
- Beast: `build/bin/bench_beast_stream` (STREAM)
- zlink: STREAM results from `core/bench/benchwithzlink/results/20260123/bench_linux_ALL_20260123_170718.txt`

## Size 64B

- Beast: TLS 19.7% | WS 26.3% | WSS 19.0%
- zlink: TLS 76.1% | WS 73.1% | WSS 69.3%

## Size 256B

- Beast: TLS 19.9% | WS 27.5% | WSS 19.1%
- zlink: TLS 65.6% | WS 83.5% | WSS 66.0%

## Size 1024B

- Beast: TLS 74.3% | WS 94.6% | WSS 72.0%
- zlink: TLS 55.5% | WS 79.0% | WSS 52.0%

## Size 65536B

- Beast: TLS 35.9% | WS 27.4% | WSS 17.1%
- zlink: TLS 33.6% | WS 59.5% | WSS 25.6%

## Size 131072B

- Beast: TLS 33.0% | WS 24.3% | WSS 15.2%
- zlink: TLS 31.6% | WS 58.7% | WSS 24.9%

## Size 262144B

- Beast: TLS 32.1% | WS 23.6% | WSS 14.1%
- zlink: TLS 28.2% | WS 59.6% | WSS 22.7%
