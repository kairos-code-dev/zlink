# Rebuild Feature Parity Hot Path Notes

이 문서는 rebuilding 작업에서 실제로 재발 방지 규칙으로 유지해야 하는
hot-path 항목만 기록한다.

## 1. Multipart part-array materialization 금지

- 범위:
  `send`, `recv`, `callback`, `pub`, `subscribe`의 multipart 경로
- 금지:
  메시지를 caller나 callback에 넘길 때마다 `zlink_msg_t[]` 같은 part array를
  새로 heap allocation해서 반환하는 구현
- 기본 규칙:
  - 메시지 소유권은 그대로 caller/callback 쪽으로 넘긴다.
  - part array container는 내부 `thread_local` 재사용을 기본으로 한다.
  - 즉 `ownership transfer`와 `array allocation policy`를 분리해서 본다.
- 이유:
  - 이전 `/home/hep7/project/kairos/zlink/core` 작업에서
    callback multipart export가 per-call array allocation을 밟으면서
    큰 성능 병목을 만든 적이 있다.
  - 이 프로젝트는 그 실수를 반복하지 않기 위해
    처음부터 `thread_local` 재사용을 기본 정책으로 둔다.
- 실행 규칙:
  - 이 경로를 건드리는 micro-slice는
    focused perf와 확장 perf를 같은 iteration 안에 확인한다.
  - 기준선 복귀에 직접 기여한 수정이면 이 문서를 같이 갱신한다.
