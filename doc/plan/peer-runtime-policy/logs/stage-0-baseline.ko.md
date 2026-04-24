# 단계 0 기준선 로그

- 시작 시각: 2026-04-24T15:50:32+09:00
- 기준 commit: 62319cfea7304f527538bac16208abda45264014
- 시작 git status:
```
?? doc/draft/auto-hwm.ko.md
?? doc/draft/peer-disconnect-rid.ko.md
?? doc/draft/peer-weight.ko.md
```
- 읽은 문서: AGENTS.md, 실행 계획, 세 draft 첫 구현 계약, core/tests/README.md, bindings/c/perf/README.md, README perf 규칙

## 기준선 검증 결과
- `cmake --build core/build -j$(nproc)`: 통과
- `ctest -L unittest`: 통과, 20/20
- `ctest -L integration`: 통과, 59/59
- `ctest -L e2e`: 통과, 2/2
- `ctest -L regression`: 통과, 16/16
- perf PAIR report: `bindings/c/perf/results/single/report/perf_c_single_linux_20260424_155229.txt`, status=complete
- perf SPOT_REQREP report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_155236.txt`, status=complete, runtime `core/build/lib/libzlink.so.5.3.4`
- perf SPOT_SENDSEND report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_155244.txt`, status=complete, runtime `core/build/lib/libzlink.so.5.3.4`

## 단계 0 완료
- 기준선 실패 없음.
