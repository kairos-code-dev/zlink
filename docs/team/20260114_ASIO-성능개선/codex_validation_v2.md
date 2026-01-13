# ASIO 성능 개선 계획 검증 (v2)

이 문서는 `docs/team/20260114_ASIO-성능개선/plan.md`와
`docs/team/20260114_ASIO-성능개선/libzmq_analysis.md`의 제안을 기준으로,
현 코드 기준 실현 가능성과 위험 요소를 점검한 결과이다.

## 1) 코드 레벨에서 3단계 Phase 구현 가능 여부

**판정: 구현 가능하나, 출력 경로 리팩터링이 전제 조건**

- Phase 1/2는 `src/asio/asio_engine.cpp`의 `process_output()`가 현재
  `_write_buffer`로 복사하고 `_outpos/_outsize`를 0으로 초기화하는 구조라,
  **동기 write + 부분 전송 처리**로 전환하려면 출력 버퍼 상태 관리가 크게 바뀌어야 한다.
  (참고: `src/asio/asio_engine.cpp:588`)
- Phase 1의 speculative write는 `restart_output()`에서 동기 전송을 시도해야 하므로
  `_write_pending`이 이미 true인 경우의 **중복 write 방지 가드**가 필수다.
  (현재는 `start_async_write()`에서만 가드함)
- Phase 2는 encoder 포인터 직접 사용으로 전환해야 하므로,
  **동기 경로에서는 복사 제거**가 가능하지만, **async 전환 시에는 복사/수명보장 로직 필요**.
- Phase 3은 `i_asio_transport` 인터페이스 확장 후 각 transport 구현 추가로 가능하나,
  WebSocket 계열은 frame 기반 특성 때문에 `write_some()` 의미를 재정의해야 한다.

요약하면, **핵심 리팩터링 지점은 `process_output()`와 `_outpos/_outsize` 상태 처리**이며,
이를 정리하면 3단계는 코드 수준에서 실현 가능하다.

## 2) Speculative Write 도입 시 상태 전이 충돌 위험

**판정: 충돌 위험 있음 (특히 `_write_pending`/`_output_stopped` 상태 관리)**

리스크 포인트:
- `restart_output()`는 현재 `_output_stopped`만 검사하고 `start_async_write()`를 호출한다.
  여기에 speculative write를 넣으면 **async write 진행 중에도 동기 write가 재진입**할 수 있다.
  (참고: `src/asio/asio_engine.cpp:649`)
- 상태 플래그가 `_write_pending` 하나뿐이라, **동기/비동기 전환 중 상태 꼬임**이 발생할 수 있다.
  예: 동기 write가 부분 전송 후 would_block을 만나 async로 전환하는 동안,
  다른 경로에서 `restart_output()`가 호출되면 중복 write 위험.
- `asio_ws_engine_t`는 `_write_pending`이 true일 때 `start_async_write()`를 막지만,
  speculative write 도입 시 동일한 가드가 필요하며, WS/WSS는 **동시 write 금지** 제약이 더 엄격하다.

완화 조건:
- speculative_write 진입 시 `_write_pending` 검사 및 단일 write-in-flight 보장.
- would_block 전환 시 `_write_pending`을 즉시 세팅하고 추가 write를 막는 규칙 명문화.
- `_output_stopped`는 **실제 buffer 비어 있음**을 나타내도록 재정의/유지해야 한다.

## 3) Encoder 버퍼 직접 사용 시 lifetime 문제

**판정: 동기 경로는 안전, async 경로는 추가 보호 필요**

- encoder는 내부 버퍼(`_buf`) 또는 메시지 payload 포인터를 직접 반환한다.
  포인터는 **다음 encode 호출 또는 메시지 close 시 무효**가 될 수 있다.
  (참고: `src/encoder.hpp:47`)
- 현재 `asio_engine_t`는 항상 `_write_buffer`로 복사해 lifetime을 보호하고 있다.
  (참고: `src/asio/asio_engine.cpp:628`)
- Phase 2에서 복사를 제거하면, **async 전환 시 반드시 복사하거나 메시지 생명주기를 고정**해야 한다.
  그렇지 않으면 async 완료 전 데이터가 덮이거나 해제될 수 있다.
- `asio_ws_engine_t`는 이미 encoder 포인터를 async_write에 넘기고 있으며,
  `_write_pending` 동안 `process_output()`을 호출하지 않도록 설계되어 있어
  **현재 구조에서는 사실상 안전한 편**이지만, speculative write 추가 시 동일한 보장이 필요하다.

결론: **동기 경로는 즉시 전송으로 안전**, async 전환은 복사 또는 메시지 보존이 필요하다.

## 4) Transport 인터페이스 확장의 실현 가능성

**판정: TCP/SSL은 straightforward, WS/WSS는 의미 조정 필요**

- `i_asio_transport`에 `write_some()`을 추가하는 것은 구조적으로 가능하다.
  (참고: `src/asio/i_asio_transport.hpp:32`)
- TCP (POSIX/Windows): `stream_descriptor::write_some` 또는 `socket::write_some`로 구현 가능.
- SSL: `ssl_stream::write_some`로 구현 가능하나, 논블로킹 소켓에서 `would_block` 처리를 확인해야 한다.
- WS/WSS: Beast의 WebSocket은 **메시지 프레임 단위**이며 `write`가 전체 frame 전송을 시도한다.
  `write_some()` 의미를 **"한 프레임 전송 시도"**로 재정의해야 현실적이다.
  또한 sync `write`는 내부적으로 여러 단계 I/O를 수행할 수 있어
  **실제 non-blocking 동작 및 would_block 반환 여부를 검증해야 한다**.

요약: **인터페이스 확장은 가능하나, WS/WSS는 설계 해석과 검증이 필요**하다.

## 5) 각 Phase별 벤치마크 검증 기준 적절성

**판정: 최종 목표 기준으로는 적절, Phase별 검증 기준은 보강 필요**

현재 기준:
- `taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20`
- p50/p99 latency, throughput, CPU
- libzmq 대비 +-10% 이내

보강 필요 사항:
- Phase별로 목표가 다르므로 **"이전 Phase 대비 개선" 기준**이 추가되어야 한다.
  - Phase 1: 짧은 메시지 p99 latency 개선이 핵심이므로, **baseline 대비 개선 폭**을 명시해야 함.
  - Phase 2: memcpy 제거 효과는 throughput/CPU 개선이므로 **CPU/throughput 개선 기준**을 추가해야 함.
  - Phase 3: transport별 동작 범위를 넓히는 것이므로 **TCP/SSL/WS/WSS 별 기능 검증**을 포함해야 함.
- `run_benchmarks.sh`는 빌드까지 재수행하므로, **동일 빌드 옵션/하드웨어 조건 고정**이 중요하다.
- "libzmq 대비 +-10%"만으로는 **단계별 성능 회복 여부가 불명확**할 수 있다.

결론: **최종 단계 기준은 유지하되, Phase별 세부 기준을 추가하는 것이 적절**하다.

---

## 종합 결론

- 3단계 Phase는 **구조적으로 구현 가능**하나, `process_output()`/`_outpos` 처리 로직을
  libzmq 흐름에 맞게 재설계해야 한다.
- Speculative Write 도입은 상태 전이 충돌 위험이 있으므로
  **단일 write-in-flight 규칙과 would_block 전환 규칙**을 명시해야 한다.
- Encoder 버퍼 직접 사용은 **동기 경로에 한해 안전**, async 전환 시에는 복사/보존이 필요하다.
- Transport 확장은 가능하지만 **WS/WSS의 frame 기반 특성 때문에 의미 재정의와 검증이 필수**다.
- 벤치 기준은 최종 목표로는 적절하나, **Phase별 성공 기준을 추가**해야 실제 검증력이 생긴다.
