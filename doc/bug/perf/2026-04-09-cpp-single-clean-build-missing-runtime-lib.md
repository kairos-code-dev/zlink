# C++ clean-build 뒤 runtime library 누락 기록 정정

## 현재 판정

이 문서에 적힌 `clean-build` 뒤 `libzlink.so.5` 누락은
2026-04-09 최신 작업 트리 기준으로 다시 재현되지 않았다.

즉 이 문서는 현재 active bug를 설명하는 문서가 아니라,
당시 한 차례 관찰됐던 기록을 정정해 남기는 문서다.

## 다시 확인한 내용

최신 검증에서는 아래 명령이 정상적으로 끝났다.

```bash
cd /home/hep7/project/kairos/zlink
./bindings/cpp/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64 --clean-build
```

검증 결과:

- core clean build 정상 완료
- `core/build/lib/libzlink.so`, `libzlink.so.5` 생성 확인
- single smoke report 정상 생성
- 최신 report는 `complete` 상태로 종료

따라서 현재 기준으로는
`clean-build` 뒤 runtime shared library가 빠져서
`non_zero_exit_127` 이 난다는 가설을 유지할 수 없다.

## 결론

- 판정: 현재 기준 재현 안 됨
- 상태: active core bug 아님
- 조치: 이 문서는 정정 문서로만 유지
