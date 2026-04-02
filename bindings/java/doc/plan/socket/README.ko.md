# Java Socket Ralphloop

이 디렉터리는 Java socket surface split 작업을
`core/tools/ralphloop/` 기반으로 반복 실행하기 위한 랄프 세트다.

구성:

- [`2026-03-27-java-socket-surface-detailed-design.ko.md`](/home/hep7/project/kairos/zlink/bindings/java/plan/socket/2026-03-27-java-socket-surface-detailed-design.ko.md)
  - 보조 상세 설계 스펙
- [`java-socket-surface-execution-guide.ko.md`](/home/hep7/project/kairos/zlink/bindings/java/plan/socket/java-socket-surface-execution-guide.ko.md)
  - 유일한 실행 authority 문서
  - 마지막 `Slice 6. POSD 후속 리팩토링`까지 완료하고, 더 이상 설명 가능한
    리팩토링 대상이 없을 때 종료
- [`run_java_socket_surface_execution.sh`](/home/hep7/project/kairos/zlink/bindings/java/plan/socket/run_java_socket_surface_execution.sh)
  - `ralphloop` supervisor wrapper
- `logs/`
  - 실행 로그 디렉터리

기본 실행:

```bash
./bindings/java/plan/socket/run_java_socket_surface_execution.sh
```

smoke 확인:

```bash
./bindings/java/plan/socket/run_java_socket_surface_execution.sh --max-iterations 0
```

병렬 실행이 필요하면 `--logs-dir`, `--gate-label`을 분리한다.
