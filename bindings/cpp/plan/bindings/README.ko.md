# C++ Bindings Core Alignment 문서

이 디렉토리는 `bindings/cpp`를 최신 `core` public surface에 맞춰 정렬하는
실행 문서를 담는다.

이 문서 세트의 직접 범위는 `samples`와 `tests/contract`까지다.
기존 `bindings/cpp/perf/**`는 별도 작업으로 둔다.

문서:

- [`2026-03-26-cpp-core-alignment-plan.ko.md`](./2026-03-26-cpp-core-alignment-plan.ko.md)
  메인 설계 / 실행 authority 문서다.
  public surface, 고정 결정, 단계별 구현 범위, 검증 절차를 정의한다.
- [`cpp-bindings-alignment-execution-guide.ko.md`](./cpp-bindings-alignment-execution-guide.ko.md)
  메인 플랜을 실제 실행 순서와 완료 판정 기준으로 고정한 실행 문서다.
- [`run_cpp_bindings_alignment_execution.sh`](./run_cpp_bindings_alignment_execution.sh)
  bindings 작업 전용 랄프 루프 실행 래퍼다.
  내부적으로 [`core/tools/run_codex_execution_guide_loop.sh`](../../../../core/tools/run_codex_execution_guide_loop.sh)
  를 호출한다.
  실행 wrapper에는 별도 lock을 두지 않는다.
  병렬 실행이 필요하면 `--logs-dir`, `--gate-label`을 분리해서 사용한다.

권장 사용 순서:

1. 메인 설계와 정책은
   [`2026-03-26-cpp-core-alignment-plan.ko.md`](./2026-03-26-cpp-core-alignment-plan.ko.md)
   에서 확인한다.
2. 실제 착수 순서와 종료 판정은
   [`cpp-bindings-alignment-execution-guide.ko.md`](./cpp-bindings-alignment-execution-guide.ko.md)
   에서 확인한다.
3. 자동 실행이 필요하면
   [`run_cpp_bindings_alignment_execution.sh`](./run_cpp_bindings_alignment_execution.sh)
   를 사용한다.
4. 스크립트 smoke 점검은
   `./bindings/cpp/plan/bindings/run_cpp_bindings_alignment_execution.sh --max-iterations 0`
   로 수행한다. 정상 동작이면 종료 코드는 `0`이다.
