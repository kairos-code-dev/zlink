# ASIO 기반 개발 성능 개선 포인트 (상세)

## 1. 목적

이 문서는 `Boost.Asio` 기반 서버/소켓 개발에서 처리량(throughput)과 지연(latency)을 동시에 개선하기 위한 실무 포인트를 정리한다.

대상:
- 고동시성 TCP/STREAM 워크로드
- `length(4) + body` 형태 프레이밍 프로토콜
- `ccu=10,000`급 부하

적용 우선순위:
1. 불필요한 복사 제거
2. 콜백 빈도 감소
3. 락 경합 제거
4. 스레드 토폴로지 단순화
5. 측정 신뢰성 확보


## 2. 성능 최적화의 핵심 원칙

### 2.1 "ASIO를 쓴다"와 "빠르다"는 다르다

성능은 ASIO 사용 여부 자체보다 다음에 좌우된다.
- 어떤 단위로 read/write를 수행하는지
- 메시지 경계 처리에서 copy가 몇 번 일어나는지
- 락/원자 연산이 hot path에 얼마나 있는지
- 이벤트 루프/스레드 구조가 CPU/NUMA에 맞는지

### 2.2 작은 오버헤드의 곱셈 효과

`1KB` 메시지에서 다음 항목은 단독으로는 작아 보이지만 10M+/sec 규모에서 치명적이다.
- 패킷당 malloc/free
- 패킷당 deque push/pop
- 전역 mutex lock/unlock
- 작은 버퍼로 인한 콜백 폭증


## 3. 아키텍처 레벨 개선 포인트

### 3.1 서버/클라이언트 동시 프로세스 실행을 기본으로 두지 말 것

같은 프로세스 안에서 서버와 클라이언트를 함께 돌리면 다음 문제가 생긴다.
- 스케줄러 경쟁 증가
- 캐시/런큐 간섭
- 스레드 수 과증가로 컨텍스트 스위치 증가

권장:
- 성능 측정은 서버/클라이언트를 분리 프로세스로 실행
- 서버는 `accept + session worker` 구조, 클라이언트는 별도 워커 풀

### 3.2 I/O 스레드 모델

권장 순서:
1. 서버 `io_threads` 먼저 튜닝
2. 클라이언트 워커는 서버 대비 축소(예: `1/4`)
3. 둘 다 크게 늘리기 전에 CPU 사용률/스위치 수 확인

주의:
- 단순히 thread 수를 늘리면 성능이 오르지 않는다.
- 과다 thread는 tail latency를 악화시킨다.


## 4. 데이터패스(Hot Path) 최적화

### 4.1 write path: gather write 기본화

문제:
- `header + body`를 하나의 연속 버퍼로 합치면 body copy가 추가된다.

개선:
- `writev`/gather write로 `header`와 `body`를 분리 전송
- large-message 전용이 아니라 STREAM은 small-message에도 기본 적용 고려

효과:
- copy 제거
- write 준비 비용 감소
- 메시지당 CPU 감소

### 4.2 read path: 콜백 빈도 줄이기

문제:
- `4KB`급 read buffer는 `1KB` 메시지에서 콜백이 과도하게 자주 발생

개선:
- read buffer를 `64KB` 이상으로 확대
- packet assembly는 partial buffer 1개로 처리

효과:
- callback/handler 오버헤드 감소
- packet 분해/재조립 비용 감소

### 4.3 send queue 메타데이터 구조 단순화

문제:
- 패킷당 별도 메타데이터 컨테이너(deque 등) 조작은 고비용

개선:
- phase/timestamp 같은 측정용 메타데이터를 payload 헤더에 직접 포함
- recv에서 바로 파싱

효과:
- 동적 컨테이너 조작 감소
- 메모리 locality 개선


## 5. 락/동기화 최적화

### 5.1 전역 latency 벡터 + 전역 mutex 금지

문제:
- 샘플링이 켜진 상태에서 전역 락 1개는 즉시 병목

개선:
- shard-local latency vector + shard-local mutex
- 집계 시점에만 merge

효과:
- 락 경합 급감
- throughput 증가, p99 안정화

### 5.2 backpressure 상태 전이 최소화

권장:
- `input_stopped` 같은 상태 플래그는 전이 횟수를 줄인다.
- buffer overflow 방어는 유지하되 fast path 분기를 단순화한다.


## 6. 프레이밍/프로토콜 처리

### 6.1 parser 비용을 낮추는 규칙

- 고정 길이 헤더는 단일 분기에서 검증
- body 길이 체크는 early-fail
- 데이터/이벤트 메시지 처리 경로 분리
- 라우팅 ID 매핑은 O(1) lookup 유지

### 6.2 프로토콜 호환성과 성능의 균형

권장:
- 인터페이스는 유지하고 내부 프레이밍만 최적화
- wire format 변경 시 fallback 또는 mode 분리 제공


## 7. 소켓/커널 튜닝 포인트

필수 점검:
- `SO_REUSEADDR`, 필요 시 `SO_REUSEPORT`
- `TCP_NODELAY`
- `sndbuf`/`rcvbuf` 일관 설정
- backlog 충분히 크게 설정

실무 함정:
- 서버 listen 포트를 ephemeral range에 두면 클라이언트 ephemeral port와 충돌 가능
- Linux 기본 ephemeral 범위(`32768-60999`) 사용 환경에서는 listen base port를 그 아래로 잡는 것이 안전


## 8. 벤치마크/검증 방법론

### 8.1 고정 파라미터(예시)

- `ccu=10000`, `size=1024`, `inflight=30`
- `warmup=3`, `measure=10`, `drain-timeout=10`
- `connect-concurrency=256`, `backlog=32768`, `hwm=1000000`
- `io_threads=32`, `latency_sample_rate=16`

### 8.2 독립 실행 원칙

- 스택별로 독립 실행
- 같은 호스트, 같은 파라미터, 같은 측정 시간 사용
- 사전 warmup + 사후 drain 확인

### 8.3 결과 해석

필수 지표:
- throughput(msg/s)
- `incomplete_ratio`
- `drain_timeout`
- `gating_violation`
- p50/p95/p99

합격 기준 예:
- `recv > 0`
- `incomplete_ratio <= 0.01`
- `drain_timeout == 0`
- `gating_violation == 0`


## 9. 튜닝 순서(권장 플레이북)

1. 기능/정합 먼저 고정
2. 서버/클라 프로세스 분리
3. read/write buffer 확대
4. gather write 적용
5. 메타데이터 구조 단순화(deque 제거 등)
6. 락 shard-local화
7. thread 수 sweep(8/16/24/32 등)
8. 최종 파라미터 고정 후 회귀 테스트


## 10. 안티패턴

- 성능 문제를 thread 수만 늘려 해결하려는 접근
- benchmark harness 오버헤드를 실제 소켓 성능으로 오해
- global lock 기반 통계 수집을 hot path에 유지
- small message인데 큰 객체/컨테이너를 메시지당 생성
- 같은 프로세스에서 server+client를 동시에 돌리고 결과를 절대 수치로 믿는 것


## 11. 코드 반영 체크리스트

- [x] write path가 `header+body` gather write를 사용한다.
- [x] read buffer가 작은 기본값(4KB 근처)에 고정되어 있지 않다.
- [x] packet assembly가 불필요한 동적 컨테이너 연산 없이 동작한다.
- [x] latency 샘플 저장이 shard-local 구조다.
- [x] benchmark가 server/client 분리 실행을 지원한다.
- [x] 포트 설정이 ephemeral 충돌을 피한다.
- [ ] `ctest` 전체 회귀를 통과한다.

상태 기준일: `2026-02-15`

비고:
- 체크 항목 1~6은 코드/시나리오 기준 반영 확인 완료.
- `ctest` 전체 회귀는 이번 성능 튜닝 턴에서 미실행(별도 턴 필요).


## 12. 실무 결론

ASIO 성능 개선의 본질은 "핫패스에서 메시지당 일을 줄이는 것"이다.

가장 효과가 큰 조합:
- 프로세스 분리 실행
- 큰 read/write 버퍼
- gather write 기본 적용
- per-message 메타데이터 자료구조 제거
- shard-local 통계 수집

이 5가지를 먼저 적용하면, 이후 옵션 튜닝(threads, buffers, sample rate)은 미세조정 단계로 내려간다.


## 13. 2026-02-15 실측 반영 내역

### 13.1 목표와 기준선

- 목표: `zlink stream`이 `cppserver` 대비 각 메시지 크기에서 `±5%` 이내(또는 우수) 성능을 달성.
- 64KB 기준선 선검증:
  - `cppserver s2` (`ccu=10000`, `inflight=30`, `size=65536`) 단독 실행 결과
  - throughput: `69,498.10 msg/s`, `incomplete_ratio=0`, `drain_timeout=0`, `PASS`
  - 결과 파일: `core/tests/scenario/stream/result/cppserver_64k_verify_20260215_005919/metrics.csv`

### 13.2 효과가 컸던 개선 항목

1. STREAM gather threshold 상향 (`2048 -> 8192`)
- 변경: `core/src/engine/asio/asio_engine.cpp`
- 의도: `4KB` 구간에서 gather 경로 대신 encoder batch 경로를 우선 사용.
- 효과:
  - 변경 전(자동 튜닝만 적용): `4KB zlink=1,409,028.00`, `cpp=1,926,706.30`, 비율 `73.13%` (`FAIL`)
  - 변경 후: `4KB zlink=1,810,347.00`, `cpp=1,829,124.20`, 비율 `98.97%` (`PASS`)
  - 기준 파일:
    - `core/tests/scenario/stream/result/size_compare_after_auto_tune_20260215_015700/summary.csv`
    - `core/tests/scenario/stream/result/size_compare_after_gather8192_20260215_020245/summary.csv`

2. 64KB send-path 정체(stall) 완화
- 변경: `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp`
- 핵심:
  - `raw_send_all()`의 `EAGAIN` 처리에서 busy-yield 대신 `poll(POLLOUT, 1ms)` 대기.
  - 대형 패킷에서 `max_batch`를 소켓 버퍼 기준으로 자동 제한(실질적으로 large packet은 batch 축소).
- 효과:
  - 초기 측정: `64KB` 비율 `37.71%`, `FAIL`
  - 반영 후: `64KB` 비율 `132.76%`, `PASS`
  - 기준 파일:
    - `core/tests/scenario/stream/result/size_compare_20260215_010511/summary.csv`
    - `core/tests/scenario/stream/result/size_compare_after_gather8192_20260215_020245/summary.csv`

3. I/O 토폴로지 자동값 재조정
- 변경: `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp`
- 내용: `io_threads>=24`에서 auto `server_shards=8`, auto `client_workers=2`
- 효과:
  - 64KB 조합 스윕에서 `client_workers=2`가 가장 안정적으로 `PASS` 유지
  - `client_workers>=3`에서 고 throughput이 나와도 `drain_timeout` 또는 `incomplete_ratio`로 `FAIL` 빈발
  - 기준 파일: `core/tests/scenario/stream/result/size64k_combo_sweep_20260215_014036/summary.csv`

4. 수신 파서 복사 경로 경량화
- 변경: `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp`
- 내용: 수신 청크 전체를 누적/erase하던 방식에서, 미완성 tail만 유지하는 방식으로 변경.
- 효과:
  - 고부하 구간에서 packet assembly 비용 완화 및 안정성 개선에 기여
  - 특히 4KB/64KB에서 변동폭과 drain 안정성 개선에 유효

### 13.3 효과가 제한적이었던 시도

- `send_batch` 단독 증가는 `4KB`에서 상한 효과만 보임:
  - `1 -> 64` 증가 시 throughput `546,250 -> 1,200,589.50 msg/s`
  - 그러나 단독으로는 `cppserver 4KB` parity에 미달
  - 기준 파일: `core/tests/scenario/stream/result/size4k_batch_sweep_20260215_011111/summary.csv`

- `sndbuf/rcvbuf`를 `1MB`로 확대해도 `4KB` 개선폭은 제한적:
  - `ss=8,cw=2` 기준 `1,420,882.50 -> 1,406,571.00 msg/s` (유의미한 개선 없음)
  - 기준 파일:
    - `core/tests/scenario/stream/result/size4k_current_ss8_cw2_probe_20260215_015327/metrics.csv`
    - `core/tests/scenario/stream/result/size4k_current_ss8_cw2_buf1m_probe_20260215_015601/metrics.csv`

### 13.4 최종 결과 (동일 시나리오, `ccu=10000`, `inflight=30`, `io_threads=32`)

결과 파일: `core/tests/scenario/stream/result/size_compare_after_gather8192_20260215_020245/summary.csv`

| size | cppserver msg/s | zlink msg/s | zlink/cppserver | 판정 |
|---|---:|---:|---:|---|
| 64 | 4,758,501.00 | 10,998,858.00 | 231.14% | PASS |
| 256 | 4,406,904.10 | 8,744,193.00 | 198.42% | PASS |
| 1024 | 4,174,260.90 | 4,750,371.00 | 113.80% | PASS |
| 4096 | 1,829,124.20 | 1,810,347.00 | 98.97% | PASS |
| 65536 | 79,846.30 | 106,000.00 | 132.76% | PASS |

판정 기준:
- `zlink/cppserver >= 95%`이면 목표 충족으로 판정.
- 위 5개 사이즈 전 구간 목표 충족.

### 13.5 반영 커밋

- `e1aecf50`: `perf: stabilize stream 64k path and tune zlink scenario workers`
- `580fbb82`: `perf: raise stream gather threshold for 4k parity`

## 14. 관련 링크

### 14.1 내부 문서

- [고성능 stream 소켓 스펙](./high-performance-stream-socket-specification.ko.md)
- [기존 stream 소켓 스펙](./stream-socket-specification.ko.md)
- [STREAM CS fastpath 설계안](./stream-cs-fastpath-cppserver-based.ko.md)
- [STREAM 5-stack parity/성능 정리](./stream-4stack-parity-and-zlink-improvement.ko.md)

### 14.2 내부 코드 경로

- [ASIO 엔진](../../core/src/engine/asio/asio_engine.cpp)
- [ASIO RAW 엔진](../../core/src/engine/asio/asio_raw_engine.cpp)
- [TCP transport](../../core/src/transports/tcp/tcp_transport.cpp)
- [STREAM 소켓](../../core/src/sockets/stream.cpp)
- [STREAM fast encoder](../../core/src/protocol/stream_fast_encoder.cpp)
- [STREAM fast decoder](../../core/src/protocol/stream_fast_decoder.cpp)
- [zlink stream 시나리오 코드](../../core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp)
- [zlink stream 시나리오 스크립트](../../core/tests/scenario/stream/zlink/run_stream_scenarios.sh)
- [cppserver 시나리오 스크립트](../../core/tests/scenario/stream/cppserver/run_stream_scenarios.sh)

### 14.3 외부 참고

- [Boost.Asio 공식 문서](https://www.boost.org/doc/libs/release/doc/html/boost_asio.html)
- [CppServer GitHub](https://github.com/chronoxor/CppServer)
- [libzmq GitHub](https://github.com/zeromq/libzmq)
