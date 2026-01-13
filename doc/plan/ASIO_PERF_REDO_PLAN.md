## 왜 libzmq 대비 성능 차이가 났나?
- **기존 libzmq**는 `send()` → kernel `send`로 직통이고, 짧은 TCP/IPC 메시지를 지연 없이 전달하는 단순 흐름이다.
- **ASIO 기반 zlink**는 `process_output()`에서 `_options.out_batch_size`만큼 encoder 데이터를 채우는 루프/복사 경로가 기본이라, 짧은 메시지에서도 batching 비용이 들어가 latency가 증가한다.
- encoder → `_write_buffer` → `async_write_some`으로 여러 계층을 거치다 보니 호출 횟수와 복사가 증가하고, libzmq처럼 곧장 `send`로 내려가지 않아 throughput/latency가 밀린다.
- handshake/zero-copy 등 최적화는 대형 메시지나 연결 초기에서나 유의미하고, 실제 반복된 짧은 메시지 흐름에는 영향이 없다.  
  
## 목표  
1. TCP/IPC 짧은 메시지 경로를 다시 단순화해서 latency를	libzmq 수준으로 회복한다.  
2. batching/compaction/handshake 플래그를 **per-socket**으로 끌 수 있는 옵션을 제공하여 필요 없는 경로는 아예 비활성화한다.  
3. 매 조치마다 `taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20`로 측정해, latency/throughput이 개선됐는지를 숫자로 같이 기록한다.  

## 구체적 실행 방안
1. **즉시성 전용 쓰기 경로(out_batch_size 기반 분기)**
   - `src/asio/asio_engine.cpp`의 `process_output`에서 짧은 메시지인 경우 `_options.out_batch_size`만큼 채우는 루프를 거치지 않고 즉시 쓰기 경로로 분기한다.
   - 분기 기준은 `_outsize` 등 실제 접근 가능한 상태로 정의한다.
   - async write의 수명 보장을 위해 **멤버 버퍼**에 복사 후 전송한다 (stack buffer 사용 금지).

2. **out_batch_size 기반 튜닝 및 옵션화**
   - `_options.out_batch_size`를 latency-critical 환경에서 1 또는 작은 값으로 조정 가능하도록 문서화한다.
   - 기본값 8192는 유지하고, trade-off를 명시한다.

3. **소켓 차원 토글 & telemetry**
   - 새 소켓 옵션 `ZMQ_ASIO_WRITE_BATCHING`을 추가해 latency 개선 경로 선택을 가능하게 한다.
   - `zmq_debug`에 배치 관련 카운터를 추가한다.

4. **ASIO WebSocket 엔진 포함**
   - `src/asio/asio_ws_engine.cpp`에도 동일한 분기/멤버 버퍼 수명 관리 방식을 적용한다.

5. **테스트 계획**
   - 즉시성 경로의 수명 문제가 없음을 검증하는 테스트 추가
   - WS 엔진 경로 동작 확인 테스트 추가

6. **실제 병목 검증 단계**
   - 프로파일링으로 `_options.out_batch_size` 루프/복사 경로가 실제 병목인지 확인
   - CPU/메모리 사용률 기록

7. **측정과 기록**
   - 각 변경 후 `taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20` 수행
   - 결과를 `benchwithzmq/benchmark_result.txt` 및 `benchwithzmq/zlink_BENCHMARK_RESULTS.md`에 기록
   - p50/p99 latency, throughput, 메시지 크기, 옵션 값, 커밋 해시 포함

## 기록 및 공유
- 새 계획 문서에는 실패 사례 링크를 문단 하단에 두고, 진행 상황은 `doc/plan/FAILED_PERFORMANCE_WORK/` 하위 문서 또는 README에 정리한다.  
- 위의 타이밍/토글 세팅이 실제 latency/throughput을 개선하는지 확인되면 그 결과만 따로 `doc/plan/ASIO_PERF_REDO_RESULTS.md`로 정리해 리뷰에 붙인다.

## 참고 코드 위치
1. **`src/asio/asio_engine.cpp` `process_output`** (src/asio/asio_engine.cpp:588)
   현재는 `_options.out_batch_size`만큼 encoder 데이터를 채우는 루프가 있고, 모든 데이터를 `_write_buffer`로 복사한 뒤 `async_write_some`을 호출한다.
   ```cpp
   if (_outsize == 0) {
       _outsize = _encoder->encode (&_outpos, 0);
   }
   while (_outsize < static_cast<size_t> (_options.out_batch_size)) {
       unsigned char *bufptr = _outpos + _outsize;
       size_t n = _encoder->encode (&bufptr,
                                    _options.out_batch_size - _outsize);
       _outsize += n;
   }

   const size_t out_batch_size =
     static_cast<size_t> (_options.out_batch_size);
   const size_t target =
     _outsize > out_batch_size ? _outsize : out_batch_size;
   _write_buffer.resize (_outsize);
   memcpy (_write_buffer.data (), _outpos, _outsize);
   ```
   짧은 메시지에서는 `_outsize`가 작고 즉시 전송 가능한 경우 루프를 건너뛰고 멤버 버퍼로 1회 복사해 전송한다.

2. **`include/zmq.h`, `src/options.hpp`, `src/options.cpp`, `src/socket_base.cpp`**
   새 소켓 옵션 정의 및 저장/적용. 기존 패턴(옵션 ID 추가 → options 저장 → socket_base 적용)에 맞춰 수정.

3. **성공/실패 기록 링크**
   과거 `feature/perf-optimization` 시도 문서는 `doc/plan/FAILED_PERFORMANCE_WORK/` 아래에 보관되어 있다.
   대표 문서: `doc/plan/FAILED_PERFORMANCE_WORK/PERF_OPTIMIZATION_IMPLEMENTATION_PLAN.md`
