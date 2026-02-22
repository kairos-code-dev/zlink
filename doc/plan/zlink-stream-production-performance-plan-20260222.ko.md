# ZLINK STREAM 운영 성능 개선 실행 명세 (모호성 제거판)

- 작성일: 2026-02-22
- 적용 범위: `core/src` 운영 코드만
- 금지: 벤치 전용 분기, 벤치 전용 env 의존, API 브레이킹

## 0) 고정 의사결정

1. 공개 API 시그니처는 유지한다.
- `zlink_stream_start()`, `zlink_stream_stop()`, `zlink_stream_send()`
- `zlink_stream_on_packets_fn` 시그니처 유지

2. 성능 목표는 아래 2개 스택에 대해 동시에 적용한다.
- `zlink` (LEN32BE off)
- `zlink-len32be` (LEN32BE on)

3. 성능 판정은 `throughput` 기준으로 한다.
- 비교 대상: `cppserver`
- 조건: size `64`, `1024`, `65536` 각각에서 `median_tps(zlink*) >= median_tps(cppserver)`

4. 벤치 실행 전략은 고정한다.
- `core/bench/benchwithstreamcompare/run_benchmarks.sh`만 사용
- `--ccu 10000 --inflight 1 --runs 3 --warmup 2 --duration 5 --server-io-threads 4 --client-io-threads 4`
- phases는 `both` 고정

5. 성능 비교의 baseline은 작업 시작 직전 1회 채집한다.
- baseline 커맨드는 본 문서 3-2와 동일
- baseline 산출물 경로: `core/bench/benchwithstreamcompare/results/<timestamp>/summary.json`

## 1) 변경 금지 항목

- [ ] `core/bench/` 하위에서 특정 stack만 유리해지는 성능 특례를 넣지 않는다.
- [ ] 측정 무결성 수정(파싱 버그, 리포트 계산 버그, 실패 판정 버그)만 예외적으로 허용한다.
- [ ] 테스트 완화를 위해 timeout/검증 로직을 느슨하게 하지 않는다.
- [ ] `core/include/zlink.h` 공개 함수 시그니처를 바꾸지 않는다.
- [ ] 특정 벤치에서만 동작하는 특수 경로를 `core/src`에 넣지 않는다.

## 1-1) 동시성/소유권 규약 (필수)

- [ ] RID reassembly state(`stream_reassembly_state_t`)는 socket owner thread에서만 읽기/쓰기 한다.
- [ ] `xpipe_terminated()` / `stream_dispatch_stop()`에서 state 정리는 owner thread 직렬화 순서로만 수행한다.
- [ ] 타 스레드에서 state에 직접 접근/삭제하지 않는다.
- [ ] owner thread 직렬화가 깨지는 변경은 금지한다.
- [ ] owner thread 직렬화 경로를 코드로 명시한다.
  - I/O 이벤트 진입: `session_base_t::push_msg()` -> `socket_base_t::stream_dispatch_msg_from_io()`
  - 종료 이벤트: `xpipe_terminated()`는 owner thread 이벤트 루프 경유
  - stop 요청: `stream_dispatch_stop()` 호출은 owner thread와 경쟁하지 않도록 제어

## 2) 구현 작업 패키지 (순서 고정)

### WP-0. 회귀 테스트 선행 (먼저 수행)

대상 파일:
- `core/tests/test_stream_socket.cpp`
- 필요 시 `core/tests/test_stream_fastpath.cpp`

선행 테스트 항목:
- [ ] LEN32BE 단건 프레임
- [ ] LEN32BE 헤더 분할 수신
- [ ] LEN32BE 바디 분할 수신
- [ ] LEN32BE 다중 프레임 단일 read
- [ ] 콜백 수명 계약(콜백 반환 후 포인터 무효) 검증

완료 판정:
- [ ] WP-1~WP-4 코드 변경 전에 위 테스트가 CI/로컬에서 재현 가능 상태로 존재
- [ ] 테스트 실패 시 원인 로그가 명확히 출력됨

### WP-1. LEN32BE 수신 경로를 단일 상태기계로 재작성

대상 파일:
- `core/src/sockets/stream.hpp`
- `core/src/sockets/stream.cpp`
- `core/src/core/pipe.hpp`
- `core/src/core/pipe.cpp`

대상 함수:
- `zlink::stream_t::dispatch_len32be()`

구현 정의(고정):
- [ ] LEN32BE 입력 프레임은 `4-byte big-endian payload_len + payload_stream_bytes`로 정의한다.
- [ ] 상태기계는 먼저 4바이트 길이만 읽고(`payload_len`), 그 길이만큼 `msg_t`를 1회 할당한다.
- [ ] 이후 들어오는 stream 조각은 같은 `msg_t` 버퍼에 누적 저장한다.
- [ ] `written == payload_len`이 되는 순간, 완성된 **동일 `msg_t` 객체**를 콜백 인자로 전달한다.
- [ ] 콜백 전달 전에 중간 `msg_t` 재생성/재포장/재복사 경로를 두지 않는다.
- [ ] 콜백 `msg_t` 데이터는 payload만 포함한다(4-byte 길이 헤더는 콜백 데이터에 포함하지 않음).
- [ ] LEN32BE 콜백 호출 정책은 기존 배치 API를 유지한다.
  - 단일 read에서 완성된 패킷이 여러 개면 `msgs_[] + msg_count_`로 1회 전달한다.
  - 완성 패킷이 1개면 `msg_count_ == 1`로 전달한다.
  - 구현 임의로 "항상 단건 콜백"으로 바꾸지 않는다.

콜백 수명 계약(필수):
- [ ] 콜백에 전달된 `msg_t`는 콜백 반환 시점까지만 유효하다.
- [ ] 콜백에서 `msg_t`를 close/move/copy-ownership 하지 않는다.
- [ ] 콜백이 포인터/버퍼 주소를 반환 후까지 보관하지 않는다.
- [ ] 콜백 반환 직후 dispatcher가 전달한 `msg_t`를 close/reset 한다.
- [ ] 콜백 non-zero 반환 시에도 `msg_t` 정리 후 dispatch stop 순서로 처리한다.

필수 구현 조건:
- [ ] RID state는 아래 필드만 사용한다.
  - `header[4]`
  - `header_written`
  - `msg_t assembling`
  - `payload_len`
  - `written`
  - `active`
- [ ] header 4바이트 완성 시 `assembling.init_size(payload_len)`를 **1회만** 호출한다.
- [ ] 이후 수신 조각은 `assembling.data() + written`에 직접 복사한다.
- [ ] `written == payload_len`이면 완성 패킷으로 callback 전달 목록에 넣고 즉시 state reset 한다.
- [ ] 기존 중간 패킷 이동 체인을 제거한다.
  - `single_packet` 경로 제거
  - `append_msg_batch()` 의존 제거
- [ ] `dispatch_len32be()` hot-path에서 socket-global `unordered_map + mutex` 조회를 제거한다.
  - 구현 방식: `pipe`에 직접 연결된 state(=pipe-local state) 사용
  - 예외: start/stop/terminate 시점의 관리성 접근만 허용

완료 판정:
- [ ] `dispatch_len32be()`에서 완성 패킷당 추가 동적 재할당(`malloc/realloc`)이 발생하지 않음
- [ ] `dispatch_len32be()`에서 완성 패킷을 다른 `msg_t`로 재포장하는 중간 move 체인이 없음
- [ ] 분할 수신(헤더/바디 분할) 테스트 통과
- [ ] LEN32BE hot-path에서 전역 reassembly map 조회가 없음(함수명과 무관한 행위 기준)
- [ ] `dispatch_len32be()` hot-path에서 전역 reassembly mutex 획득 없음(행위 기준)

### WP-2. LEN32BE 외 STREAM 공통 hot-path 단순화

대상 파일:
- `core/src/sockets/stream.cpp`

대상 함수:
- `xsend()`
- `xstream_dispatch_msg()`
- `resolve_dispatch_routing_id_fast()`

필수 구현 조건:
- [ ] `xsend()` 2-part 경로에서 RID frame 처리 후 payload frame 전송까지 불필요 상태 전환을 제거한다.
- [ ] `_current_out`가 유효할 때 중복 검사를 줄인다.
- [ ] `xstream_dispatch_msg()`는 느린 RID 해석 경로 호출을 “RID=0인 경우”로 한정한다.
- [ ] control-event 판별은 데이터 경로를 해치지 않도록 최소 분기로 배치한다.

완료 판정:
- [ ] `xsend()`에서 payload frame 전송 전 중복 `check_write()` 호출이 없음
- [ ] `xstream_dispatch_msg()`에서 fast-path 우선 순서가 `pipe rid -> msg rid -> slow path`로 고정됨

### WP-3. callback 내부 송신 경로 복사 최소화

대상 파일:
- `core/src/sockets/stream.cpp`
- `core/src/api/zlink.cpp`

대상 함수:
- `stream_dispatch_send_from_io()`
- `zlink_stream_send()`

필수 구현 조건:
- [ ] callback 컨텍스트(IO thread)에서 송신 시 불필요한 중간 버퍼 할당/복사를 제거한다.
- [ ] 안전 조건 불충족 시 기존 경로로 즉시 fallback 한다.
- [ ] 동작 의미(반환값/에러코드)는 기존과 동일하게 유지한다.
- [ ] callback 컨텍스트 판별 조건은 아래 식으로 고정한다.
  - `g_stream_dispatch_tls.socket == this`
  - `g_stream_dispatch_tls.pipe != NULL`
  - `g_stream_dispatch_tls.routing_id == routing_id`
- [ ] fallback 경로 진입 조건은 명시적으로 유지한다.
  - routing_id 불일치
  - callback 외부 스레드
  - write/flush 실패
- [ ] `stream_dispatch_send_from_io()`는 메시지마다 `flush()`하지 않는다.
  - 동일 `out-pipe` 기준으로 write 누적 후 1회 flush 수행
  - out-pipe가 다르면 pipe별로 각 1회 flush 허용
- [ ] LEN32BE 송신 시 `4-byte length`와 payload 조립 과정의 불필요 memcpy를 줄인다.
  - 가능하면 header와 payload를 분리 write하거나 gather 가능한 경로를 우선 사용

완료 판정:
- [ ] callback echo 경로에서 기존 경로 대비 추가 중간 버퍼(`std::vector`/`malloc`) 생성이 없음
- [ ] 기존 API 동작 회귀 없음 (`core/tests/test_stream_socket.cpp`)
- [ ] 동일 out-pipe에 대해 flush 호출 횟수가 `메시지 수`가 아니라 `1회`임을 코드/로그로 확인

### WP-4. ASIO STREAM 버퍼 성장 정책 정렬

대상 파일:
- `core/src/engine/asio/asio_engine.cpp`
- `core/src/engine/asio/asio_raw_engine.cpp`
- `core/src/protocol/raw_decoder.cpp`
- `core/src/protocol/decoder_allocators.cpp`

대상 로직:
- `prime_stream_decoder_read_target()`
- `maybe_grow_stream_decoder_read_target()`
- stream encoder/decode target size 결정 경로

필수 구현 조건:
- [ ] 초기값은 작게 시작하고 필요 시 점진 확장한다.
- [ ] 상한은 반드시 `rcvbuf/sndbuf/maxmsgsize`로 제한한다.
- [ ] 반복적인 확장/축소 진동을 줄인다.
- [ ] 메모리 폭증을 유발하는 전역 고정 대버퍼 도입 금지
- [ ] `input_stopped/pending` 경로에서 per-read 임시 `std::vector` 복사를 줄인다.
  - pending pool 재사용 우선
  - 불가피한 복사 경로는 read당 1회로 제한

완료 판정:
- [ ] baseline 대비 `throughput:zlink:64`의 `median_tps`가 `-3%` 이내
- [ ] baseline 대비 `throughput:zlink:1024`의 `median_tps`가 `-3%` 이내
- [ ] baseline 대비 `throughput:zlink:65536`의 `median_tps`가 `-3%` 이내
- [ ] baseline 대비 `throughput:zlink-len32be:64`의 `median_tps`가 `-3%` 이내
- [ ] baseline 대비 `throughput:zlink-len32be:1024`의 `median_tps`가 `-3%` 이내
- [ ] baseline 대비 `throughput:zlink-len32be:65536`의 `median_tps`가 `-3%` 이내
- [ ] 서버 RSS 급증 없음(기존 대비 비정상 증가 없음)
- [ ] `asio_engine.cpp`의 pending 경로에서 불필요 임시 vector 생성 지점 감소 확인

### WP-5. 회귀 테스트 보강 (후속)

대상 파일:
- `core/tests/test_stream_socket.cpp`
- 필요 시 `core/tests/test_stream_fastpath.cpp`

추가 테스트 항목:
- [ ] LEN32BE 단건 프레임
- [ ] LEN32BE 헤더 분할 수신
- [ ] LEN32BE 바디 분할 수신
- [ ] LEN32BE 다중 프레임 단일 read
- [ ] non-LEN32BE 기존 2-part 동작 회귀 없음

완료 판정:
- [ ] 추가 테스트 전부 PASS
- [ ] 기존 STREAM 관련 테스트 전부 PASS

## 3) 고정 검증 절차

### 3-1. 빌드 및 테스트

```bash
cmake -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build -j"$(nproc)"
ctest --test-dir core/build --output-on-failure
```

### 3-2. 벤치 실행

```bash
./core/bench/benchwithstreamcompare/run_benchmarks.sh \
  --stack zlink,zlink-len32be,cppserver \
  --size 64,1024,65536 \
  --phases both \
  --ccu 10000 \
  --inflight 1 \
  --runs 3 \
  --warmup 2 \
  --duration 5 \
  --client-io-threads 4 \
  --server-io-threads 4
```

### 3-2-b. 결과 파일 확인 위치 (고정)

- `core/bench/benchwithstreamcompare/results/<timestamp>/summary.json`
- `core/bench/benchwithstreamcompare/results/<timestamp>/comparison.md`
- `core/bench/benchwithstreamcompare/results/<timestamp>/metrics.csv`

### 3-3. 성능 합격 기준

1. `summary.json`에서 `throughput:zlink:<size>`와 `throughput:cppserver:<size>`의 `median_tps` 비교
- [ ] 64: `zlink >= cppserver`
- [ ] 1024: `zlink >= cppserver`
- [ ] 65536: `zlink >= cppserver`

2. `summary.json`에서 `throughput:zlink-len32be:<size>`와 `throughput:cppserver:<size>` 비교
- [ ] 64: `zlink-len32be >= cppserver`
- [ ] 1024: `zlink-len32be >= cppserver`
- [ ] 65536: `zlink-len32be >= cppserver`

3. 안정성
- [ ] `pass_rate == 1.0`
- [ ] `confidence == STABLE_CHECKED`
- [ ] `mismatch_total_all == 0`

4. 편차 한계
- [ ] `cv_pct <= 5.0` (throughput 기준, 모든 size)

## 4) 산출물 체크리스트

- [ ] 코드 변경 PR
- [ ] 테스트 로그 (`ctest`)
- [ ] 벤치 결과 디렉토리 (`core/bench/benchwithstreamcompare/results/<timestamp>`)
- [ ] `comparison.md`와 `summary.json` 첨부
- [ ] 본 문서의 모든 체크박스 상태 업데이트

## 5) 완료(DoD)

- [ ] WP-0 ~ WP-5 완료
- [ ] 변경이 `core/src` 중심이며 벤치 전용 코드 변경 없음
- [ ] 기능 회귀 없음
- [ ] 성능 목표 충족 (`zlink`, `zlink-len32be` 각각 모든 size에서 `cppserver` 이상)

## 5-1) 실행 순서 (고정)

1. [ ] WP-0 선행: LEN32BE 분할/다중프레임/수명계약 테스트를 먼저 추가
2. [ ] WP-1 수행: LEN32BE 상태기계 + pipe-local state 적용
3. [ ] WP-2 수행: LEN32BE 외 common hot-path 단순화
4. [ ] WP-3 수행: callback 송신/flush 최적화
5. [ ] WP-4 수행: ASIO pending 복사/성장 정책 조정
6. [ ] WP-5 수행: 후속 회귀/보강 테스트 추가
7. [ ] 전체 테스트 + 벤치 게이트 통과 확인

## 6) 문서 이행 체크 (코드 리뷰용)

- [ ] `core/src/sockets/stream.cpp`에서 `dispatch_len32be()`의 핵심 상태 필드가 문서 2-WP1과 일치
- [ ] `core/src/sockets/stream.cpp`에서 "완성 패킷당 추가 동적 재할당 없음" 확인
- [ ] `core/src/sockets/stream.cpp`에서 LEN32BE hot-path의 map/mutex 조회 제거 확인
- [ ] `core/src/sockets/stream.cpp`에서 `xsend()`/`xstream_dispatch_msg()` fast-path 순서가 문서 2-WP2와 일치
- [ ] `core/src/engine/asio/asio_engine.cpp`의 buffer growth 상한이 `rcvbuf/sndbuf/maxmsgsize`로 제한됨
- [ ] `core/src/engine/asio/asio_engine.cpp`의 pending 경로 임시 vector 복사 감소 확인
- [ ] LEN32BE 콜백 호출 정책(배치 유지, msg_count 정확성)이 문서와 일치
- [ ] 콜백 수명 계약(반환 후 무효, dispatcher 정리 책임)이 테스트/코드로 보장됨
