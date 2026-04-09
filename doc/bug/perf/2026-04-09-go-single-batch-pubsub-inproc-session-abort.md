# Go single smoke `PUBSUB/inproc` 뒤 `session_base.cpp:217` abort 기록 정정

## 현재 판정

이 문서에 적힌 `PUBSUB/inproc` 뒤 core `session_base.cpp:217` abort는
2026-04-09 최신 작업 트리 기준으로 다시 재현되지 않았다.

즉 이 문서는 현재 active bug를 설명하는 문서가 아니라,
당시 한 차례 관찰됐던 기록을 정정해 남기는 문서다.

## 다시 확인한 내용

같은 날 다시 확인한 direct repro에서는 `PUBSUB/inproc`가 정상 종료됐다.

```bash
cd /home/hep7/project/kairos/zlink
cd bindings/go && go run ./perf/single --pattern PUBSUB --transport inproc --msg-size 64 --duration 1
```

관찰 결과는 정상 `RESULT` 5줄이었다.

그 뒤 추가 검증을 이어 가면서 확인한 실제 문제는
`PUBSUB/inproc` 뒤 core abort가 아니라,
single perf loop에서 간헐적으로 나타나는 Go perf runtime 불안정성이었다.

이 문제는 이후 `bindings/go/perf` 쪽 수정으로 계속 추적 중이며,
이 문서의 abort 가설과는 별개로 다뤄야 한다.

## 결론

- 판정: 현재 기준 재현 안 됨
- 상태: active core bug 아님
- 조치: 이 문서는 정정 문서로만 유지

