# bindings 라이브러리 출시 전 최종 성능 확인표 (2026-06-19)

> 이 문서는 bindings 라이브러리 7.1.0 배포 전 마지막 성능 확인을 위한
> 실행표다. 성능 목표, 판정 기준, public API 원칙은
> [`bindings-library-performance-improvement-plan.ko.md`](bindings-library-performance-improvement-plan.ko.md)를
> 따른다. 이 문서는 기존 측정값을 이어받아 통과로 표시하지 않고, 7.1.0 기준으로
> 새로 확인한 결과만 기록한다.

## 1. 확인 목적

이번 확인의 목적은 새 기능 개발이 아니라, 7.1.0 core runtime으로 갱신된 bindings
라이브러리가 배포 전에 성능 목표를 계속 만족하는지 확인하는 것이다. 성능이 목표보다
낮게 나오면 먼저 측정 조건이 C 기준과 같은지 확인하고, 조건이 맞는데도 낮으면 해당
binding 라이브러리의 public API 내부 구현을 개선한다.

이 문서는 최종 판단표만 담당한다. 후보별 긴 분석 로그, 실험 결과, 원복 사유는
필요할 때 `doc/perf/perf/log/` 아래에 별도 라운드 로그로 남긴다.

## 2. 고정 입력

| 항목 | 값 |
|------|----|
| 대상 core version | `7.1.0` |
| 대상 tag | `core/v7.1.0` |
| 대상 bindings version | `7.1.0` |
| C 기준 경로 | `bindings/c/perf/baseline/` |
| 기준 정책 | `doc/perf/PERF_POLICY.md`, `doc/perf/PERF_SINGLE_TEST_POLICY.md`, `doc/perf/PERF_MULTI_TEST_POLICY.md` |
| 실행 계획 | `doc/perf/perf/bindings-library-performance-improvement-plan.ko.md` |
| 상태 값 | `미측정`, `통과(비율%)`, `미달(비율%)`, `보류(비율%)`, `해당 없음` |

7.1.0 최종 확인에서는 6.x/7.0.0 시절 baseline이나 언어별 full 결과를 최종 통과
근거로 쓰지 않는다. 과거 결과는 병목 후보를 찾기 위한 참고 자료로만 사용한다.

## 3. 실행 전 체크리스트

| 순서 | 확인 항목 | 상태 | 메모 |
|------|-----------|------|------|
| 1 | `VERSION`과 public header가 `7.1.0`인지 확인한다. | `완료` | `VERSION`, `core/include/zlink.h`, `core/include/zlink/common.h` 모두 `7.1.0` 확인. |
| 2 | `cmake --build core/build`로 실제 `core/build` runtime을 최신 source 기준으로 다시 만든다. | `완료` | `core/build/lib/libzlink.so.7.1.0` 기준으로 다시 빌드했다. |
| 3 | `bindings/update_zlink_libs.sh` 또는 release sync 결과로 각 binding native library가 `7.1.0`인지 확인한다. | `대기` | C 기준 재측정 뒤 binding native library를 다시 동기화한다. |
| 4 | C single full baseline을 7.1.0 기준으로 확보한다. | `완료` | `perf_c_single_linux_20260619_131028_prerelease_7_1_0_fresh_c_single.txt`, `status=complete`, 결과 라인 `1020/1020`. |
| 5 | C multi full baseline을 7.1.0 기준으로 확보한다. | `보강 완료` | full run은 `STREAM/tls` 4건 때문에 `partial`이었고, 실패 조합 재실행은 `complete`, 결과 라인 `20/20`이다. |
| 6 | 각 언어 full run 전에 같은 runner 조건의 fail-fast smoke report를 남긴다. | `진행 중` | C++ single/multi smoke 완료. .NET single smoke 완료. 다른 항목은 미측정. |
| 7 | report의 `Perf runtime libzlink: ...`가 의도한 runtime을 가리키는지 확인한다. | `진행 중` | 이후 report는 `core/build/lib/libzlink.so.7.1.0`을 기준으로 확인한다. |
| 8 | `Effective Options`, auto-HWM `MsgUnit(B)`, routed payload borrow 조건을 C와 대조한다. | `진행 중` | C++ multi smoke는 `routed_echo_borrow_payload=tcp`, multi size set `64,256,1024,4096,65536,131072`로 실행했다. |

## 4. 측정 범위

Single suite는 기존 기본 size set을 유지한다.

| Suite | Transport | Pattern | Message size |
|-------|-----------|---------|--------------|
| Single | `tcp`, `ws`, `wss`, `tls` | `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `SPOT` | `64`, `256`, `1024`, `65536`, `131072`, `262144` |
| Single C++/.NET 추가 | `inproc`, `ipc` | `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER` | `64`, `256`, `1024`, `65536`, `131072`, `262144` |
| Multi | `tcp`, `ws`, `wss`, `tls` | `MULTI_DEALER_DEALER`, `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`, `MULTI_PUBSUB`, `MULTI_SPOT`, `MULTI_SPOT_REQREP`, `MULTI_SPOT_SENDSEND`, `MULTI_STREAM` | `64`, `256`, `1024`, `4096`, `65536`, `131072` |

`MULTI_STREAM`은 기존 계획과 같이 `64`, `256`, `1024`, `65536`만 판정 대상으로 삼고
`4096`, `131072`은 `해당 없음`으로 둔다. C++/.NET single의 `inproc | SPOT`,
`ipc | SPOT`도 full single 결과가 없으면 `해당 없음`으로 둔다.

## 5. 기준 파일 표

| 항목 | 기준 파일 | 상태 | 메모 |
|------|-----------|------|------|
| C single 7.1.0 full | `bindings/c/perf/baseline/perf_c_single_linux_20260619_131028_prerelease_7_1_0_fresh_c_single.txt` | `완료` | `status=complete`, 결과 라인 `1020/1020`, runtime `core/build/lib/libzlink.so.7.1.0`. |
| C multi 7.1.0 full | `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260619_133323_prerelease_7_1_0_fresh_c_multi.txt` | `보강 완료` | full run은 `status=partial`, `success=180`, `fail=4`, 결과 라인 `900/920`. 실패한 `MULTI_STREAM tls 64,256,1024,65536`은 `perf_c_multi_linux_20260619_140559_prerelease_7_1_0_fresh_c_multi_recheck_stream_tls.txt`에서 `status=complete`, 결과 라인 `20/20`으로 재실행 통과했다. |
| C single 7.0.0 이전 full | `bindings/c/perf/baseline/perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt` | `이전 측정` | `status=complete`, 결과 라인 `1020/1020`. 7.1.0 최종 통과 근거로 쓰지 않는다. |
| C multi 7.0.0 이전 full | `bindings/c/perf/baseline/perf_c_multi_linux_20260619_062932.txt` | `이전 측정` | `status=complete`, 결과 라인 `960/960`. 7.1.0 최종 통과 근거로 쓰지 않는다. |
| C single 제한 재측정 | `미측정` | `미측정` | C 기준 outlier 또는 측정 오차 의심 조합에만 사용한다. |
| C multi 제한 재측정 | `미측정` | `미측정` | full baseline을 대체하지 않고 보강 근거로만 사용한다. |

## 6. 언어별 최종 판정표

아래 표는 출시 판단의 최상위 표다. 각 언어의 상세 결과가 하나라도 `미달`, `보류`,
`미측정`이면 `출시 판단`은 `대기`로 둔다. `보류`는 완료가 아니며, 필요한 public API
계약 또는 별도 설계 항목을 메모해야 한다.

| 순서 | 언어 | perf 경로 | Single smoke | Single full | Multi smoke | Multi full | 미달/보류 상세 | 출시 판단 |
|------|------|-----------|--------------|-------------|-------------|------------|----------------|-----------|
| 1 | C++ | `bindings/cpp/perf` | `완료` | `완료` | `완료` | `완료` | `제한 재측정 후 반복 미달 후보 7건, 해소 후보 5건, SPOT 1건 비교 불가` | `대기` |
| 2 | .NET | `bindings/dotnet/perf` | `완료` | `미측정` | `미측정` | `미측정` | `single smoke complete, full/multi 미측정` | `대기` |
| 3 | Java | `bindings/java/perf` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `대기` |
| 4 | Node | `bindings/node/perf` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `대기` |
| 5 | Go | `bindings/go/perf` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `대기` |
| 6 | Rust | `bindings/rust/perf` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `대기` |
| 7 | Python | `bindings/python/perf` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `대기` |

## 7. 상세 미달/보류 추적표

언어별 full run에서 목표를 만족하지 못한 조합은 이 표에 먼저 모은다. 개인 PC에서
측정 중이므로 이 표의 `미달`은 최종 병목 판정이 아니라 제한 재측정 대상이다. 같은
조합을 C 기준과 binding 결과로 개별 재측정해 CPU 부하, full-run 간섭, C 기준 outlier를
분리한 뒤에도 반복되는 조합만 실제 개선 대상으로 확정한다.

| 언어 | Suite | Transport | Pattern | Size | 상태 | 조치 | 다음 확인 |
|------|-------|-----------|---------|------|------|------|-----------|
| C++ | Single | `inproc` | `ROUTER_ROUTER` | `131072` | `해소(96.9%)` | 제한 재측정 통과 | 추적 해제 후보 |
| C++ | Single | `inproc` | `ROUTER_ROUTER` | `262144` | `해소(96.8%)` | 제한 재측정 통과 | 추적 해제 후보 |
| C++ | Single | `ipc` | `DEALER_ROUTER` | `65536` | `반복 미달(55.0%)` | 원인 분석 필요 | C++ DEALER_ROUTER large 경로 검토 |
| C++ | Single | `ipc` | `DEALER_ROUTER` | `131072` | `반복 미달(52.4%)` | 원인 분석 필요 | C++ DEALER_ROUTER large 경로 검토 |
| C++ | Single | `ipc` | `DEALER_ROUTER` | `262144` | `반복 미달(50.0%)` | 원인 분석 필요 | C++ DEALER_ROUTER large 경로 검토 |
| C++ | Single | `tcp` | `DEALER_ROUTER` | `65536` | `반복 미달(65.7%)` | 원인 분석 필요 | C++ DEALER_ROUTER large 경로 검토 |
| C++ | Single | `tcp` | `DEALER_ROUTER` | `131072` | `반복 미달(54.7%)` | 원인 분석 필요 | C++ DEALER_ROUTER large 경로 검토 |
| C++ | Single | `tcp` | `DEALER_ROUTER` | `262144` | `반복 미달(61.7%)` | 원인 분석 필요 | C++ DEALER_ROUTER large 경로 검토 |
| C++ | Single | `ws` | `DEALER_ROUTER` | `65536` | `해소(75.1%)` | 제한 재측정 통과 | 추적 해제 후보 |
| C++ | Single | `ws` | `DEALER_ROUTER` | `131072` | `해소(72.1%)` | 제한 재측정 통과 | 추적 해제 후보 |
| C++ | Single | `ws` | `DEALER_ROUTER` | `262144` | `반복 미달(67.2%)` | 원인 분석 필요 | C++ DEALER_ROUTER large 경로 검토 |
| C++ | Single | `wss` | `SPOT` | `262144` | `비교 불가` | C 기준 제한 재측정 실패 | SPOT 대형 단독 재측정 필요 |
| C++ | Multi | `wss` | `MULTI_SPOT` | `256` | `해소(102.0%)` | 제한 재측정 통과 | 추적 해제 후보 |

근거 파일:

- 최초 C++ Single 미달 후보는 C 기준 `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ 결과 `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt`를 비교했다.
- 최초 C++ Multi 미달 후보는 C 기준 `perf_c_multi_linux_20260619_062932.txt`와 C++ 결과 `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt`를 비교했다.
- Single 제한 재측정은 C `perf_c_single_linux_20260619_102507_prerelease_7_0_0_c_single_recheck_cpp_misses.txt`와 C++ `perf_cpp_single_linux_20260619_103007_prerelease_7_0_0_cpp_single_recheck_cpp_misses.txt`를 비교했다. C 파일은 SPOT 일부 large 조합 실패 때문에 `partial`이지만, DEALER/ROUTER 후보 셀에는 비교 가능한 결과가 있다.
- Multi 제한 재측정은 C `perf_c_multi_linux_20260619_103503_prerelease_7_0_0_c_multi_recheck_cpp_wss_spot_256.txt`와 C++ `perf_cpp_multi_linux_20260619_103530_prerelease_7_0_0_cpp_multi_recheck_cpp_wss_spot_256.txt`를 비교했다.
- `해소`는 제한 재측정에서 목표선을 만족했다는 뜻이다. `반복 미달`은 같은 조건 제한 재측정에서도 목표선을 만족하지 못했다는 뜻이다.

상태를 `보류`로 바꾸려면 아래 세 가지를 모두 채운다.

- public API 변경 없이 가능한 내부 구현 개선 후보를 최소 한 번 이상 측정했다.
- 측정 조건 불일치, timeout, stale runtime, C 기준 outlier가 아니라는 점을 확인했다.
- 필요한 public API 계약 또는 별도 설계 항목을 메모했다.

## 7.1 Smoke 관찰 로그

| 언어 | Suite | 결과 파일 | 상태 | 메모 |
|------|-------|-----------|------|------|
| C++ | Single smoke | `bindings/cpp/perf/results/single/report/perf_cpp_single_linux_20260619_083619_prerelease_7_0_0_cpp_single_smoke.txt` | `완료` | `status=complete`, 결과 라인 `1020/1020`. |
| C++ | Multi smoke | `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260619_090026_prerelease_7_0_0_cpp_multi_smoke_retry.txt` | `partial` | `MULTI_SPOT_SENDSEND tls 256B`에서 client exit `-11` 1회 관찰, `750/840`. 단독 `tls 256B` 20회 반복은 모두 통과했다. |
| C++ | Multi smoke 재시도 | `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260619_091346_prerelease_7_0_0_cpp_multi_smoke_retry2.txt` | `완료` | `status=complete`, 결과 라인 `960/960`. 이전 SIGSEGV 지점인 `MULTI_SPOT_SENDSEND tls 256B` 통과. |
| C++ | Multi full | `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` | `완료` | `status=complete`, 결과 라인 `960/960`, exit 0. |
| .NET | Single smoke | `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260619_104240_prerelease_7_0_0_dotnet_single_smoke.txt` | `완료` | `status=complete`, 결과 라인 `1020/1020`, exit 0. |

## 7.2 개선 후보 실험 로그

| 언어 | 후보 | 대상 | Before | After | 판정 | 메모 |
|------|------|------|--------|-------|------|------|
| C++ | direct dealer send public overload | `DEALER_ROUTER tcp/ws/ipc 65536,131072,262144` | 제한 재측정 대비 `48.1~75.1%` | 제한 재측정 대비 `48.1~74.3%` | `기각` | 공개 API를 늘렸지만 after 변화가 -3.6%~+2.4% 범위라 성능 개선으로 볼 수 없어 되돌렸다. |

근거 파일:

- Before: `bindings/cpp/perf/results/single/report/perf_cpp_single_linux_20260619_103007_prerelease_7_0_0_cpp_single_recheck_cpp_misses.txt`
- After: `bindings/cpp/perf/results/single/report/perf_cpp_single_linux_20260619_104048_prerelease_7_0_0_cpp_single_after_direct_dealer_send.txt`
- 비교 C 기준: `bindings/c/perf/results/single/report/perf_c_single_linux_20260619_102507_prerelease_7_0_0_c_single_recheck_cpp_misses.txt`

## 7.3 C multi 실패 재확인 로그

`bindings/c/perf/results/multi/report/perf_c_multi_linux_20260619_122446.txt`는
`MULTI_DEALER_DEALER wss 65536/131072B`와 `MULTI_STREAM ws`에서 partial로 끝났다.
실패 조합만 분리 재실행해 아래처럼 판정했다.

| 대상 | 원본 실패 | 재실행 파일 | 재실행 상태 | 판정 |
|------|-----------|-------------|-------------|------|
| `MULTI_DEALER_DEALER wss 65536,131072` | `malloc_consolidate(): unaligned fastbin chunk detected` | `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260619_130220_prerelease_7_0_0_c_multi_recheck_122446_dealer_dealer_wss_large.txt` | `complete`, `10/10` | 단독 재실행에서 재현되지 않았다. 원본 full-run 중 일시 종료로 본다. |
| `MULTI_STREAM ws 64,256,1024,4096,65536,131072` | `non_zero_exit_2_size_64`로 전체 size 실패처럼 기록 | `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260619_130237_prerelease_7_0_0_c_multi_recheck_122446_stream_ws.txt` | `partial`, `25/30` | 실제 반복 실패는 `131072B` 하나였다. |
| `MULTI_STREAM ws 131072` | 디버그 재실행에서 `timeout_error=6166` | `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260619_130408_prerelease_7_0_0_c_multi_recheck_122446_stream_ws_131072_debug.txt` | `partial`, `25/30` | 연결/전송 오류가 아니라 10,000 client의 128KiB WS echo가 completion wait 안에 drain되지 않은 것이다. |
| `MULTI_STREAM ws` 정책 정렬 후 | `4096/131072B`는 기존 계획상 `해당 없음` | `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260619_130633_prerelease_7_0_0_c_multi_recheck_122446_stream_ws_policy_aligned.txt` | `complete`, `20/20` | runner 기본 STREAM size를 `64,256,1024,65536`으로 맞춘 뒤 실패가 없어졌다. |

이 재확인은 최신 source 기준으로 `core/build`가 자동 재빌드된 뒤 수행됐다. 재실행 report의
runtime은 `core/build/lib/libzlink.so.7.1.0`이다.

## 8. 언어별 상세 측정 상태표

아래 표는 기존 성능 개선 계획 문서와 같은 형식으로 기록한다. 각 size 칸은 같은 조건의 C 기준 throughput 대비 binding throughput 비율이다. 목표를 만족하면 `통과(비율%)`, 목표보다 낮으면 `미달(비율%)`, 아직 같은 조건의 7.0.0 full 결과가 없으면 `미측정`으로 둔다.

Single suite size는 `64`, `256`, `1024`, `65536`, `131072`, `262144`이다. Multi suite size는 `64`, `256`, `1024`, `4096`, `65536`, `131072`이며, `MULTI_STREAM`의 `4096`, `131072`은 기존 계획과 같이 `해당 없음`이다.

### 8.1 C++

#### 8.1.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `통과(99.8%)` | `통과(99.8%)` | `통과(98.2%)` | `통과(99.9%)` | `통과(99.7%)` | `통과(99.8%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `tcp` | `PUBSUB` | `통과(108.0%)` | `통과(120.0%)` | `통과(127.1%)` | `통과(454.8%)` | `통과(558.0%)` | `통과(597.6%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `tcp` | `DEALER_DEALER` | `통과(100.6%)` | `통과(100.0%)` | `통과(98.5%)` | `통과(99.8%)` | `통과(99.8%)` | `통과(99.9%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `tcp` | `DEALER_ROUTER` | `통과(94.8%)` | `통과(99.8%)` | `통과(99.9%)` | `미달(57.2%)` | `미달(44.8%)` | `미달(45.0%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `tcp` | `ROUTER_ROUTER` | `통과(105.4%)` | `통과(109.6%)` | `통과(101.5%)` | `통과(99.4%)` | `통과(99.5%)` | `통과(101.2%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `tcp` | `SPOT` | `통과(106.4%)` | `통과(98.5%)` | `통과(98.7%)` | `통과(101.4%)` | `통과(91.5%)` | `통과(102.6%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ws` | `PAIR` | `통과(99.9%)` | `통과(100.7%)` | `통과(100.6%)` | `통과(99.8%)` | `통과(99.8%)` | `통과(99.7%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ws` | `PUBSUB` | `통과(98.8%)` | `통과(106.3%)` | `통과(116.4%)` | `통과(223.7%)` | `통과(301.8%)` | `통과(445.9%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ws` | `DEALER_DEALER` | `통과(99.8%)` | `통과(99.8%)` | `통과(96.1%)` | `통과(99.8%)` | `통과(100.3%)` | `통과(100.3%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ws` | `DEALER_ROUTER` | `통과(97.3%)` | `통과(93.6%)` | `통과(101.1%)` | `미달(68.4%)` | `미달(64.3%)` | `미달(60.5%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ws` | `ROUTER_ROUTER` | `통과(106.9%)` | `통과(97.8%)` | `통과(97.5%)` | `통과(99.9%)` | `통과(94.9%)` | `통과(99.2%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ws` | `SPOT` | `통과(108.5%)` | `통과(98.2%)` | `통과(101.4%)` | `통과(98.0%)` | `통과(102.3%)` | `통과(83.9%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `wss` | `PAIR` | `통과(100.7%)` | `통과(99.7%)` | `통과(100.9%)` | `통과(98.9%)` | `통과(96.3%)` | `통과(99.0%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `wss` | `PUBSUB` | `통과(106.9%)` | `통과(101.6%)` | `통과(117.0%)` | `통과(106.3%)` | `통과(96.4%)` | `통과(97.8%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `wss` | `DEALER_DEALER` | `통과(99.8%)` | `통과(99.7%)` | `통과(98.0%)` | `통과(100.2%)` | `통과(95.0%)` | `통과(99.0%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `wss` | `DEALER_ROUTER` | `통과(100.9%)` | `통과(94.6%)` | `통과(100.7%)` | `통과(89.1%)` | `통과(87.9%)` | `통과(90.0%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `wss` | `ROUTER_ROUTER` | `통과(105.9%)` | `통과(97.7%)` | `통과(99.4%)` | `통과(94.1%)` | `통과(97.4%)` | `통과(93.3%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `wss` | `SPOT` | `통과(99.2%)` | `통과(78.8%)` | `통과(90.4%)` | `통과(101.4%)` | `통과(98.1%)` | `미달(49.4%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `tls` | `PAIR` | `통과(99.9%)` | `통과(100.2%)` | `통과(97.9%)` | `통과(97.7%)` | `통과(98.9%)` | `통과(99.8%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `tls` | `PUBSUB` | `통과(113.8%)` | `통과(115.0%)` | `통과(121.1%)` | `통과(119.2%)` | `통과(129.8%)` | `통과(134.0%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `tls` | `DEALER_DEALER` | `통과(100.2%)` | `통과(99.6%)` | `통과(97.0%)` | `통과(98.1%)` | `통과(99.7%)` | `통과(100.5%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `tls` | `DEALER_ROUTER` | `통과(96.5%)` | `통과(95.8%)` | `통과(102.4%)` | `통과(89.0%)` | `통과(88.8%)` | `통과(84.8%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `tls` | `ROUTER_ROUTER` | `통과(107.1%)` | `통과(111.5%)` | `통과(98.5%)` | `통과(88.5%)` | `통과(92.1%)` | `통과(95.9%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `tls` | `SPOT` | `통과(104.1%)` | `통과(99.7%)` | `통과(99.1%)` | `통과(97.8%)` | `통과(99.0%)` | `통과(103.3%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `inproc` | `PAIR` | `통과(101.8%)` | `통과(93.5%)` | `통과(101.8%)` | `통과(99.7%)` | `통과(99.8%)` | `통과(100.2%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `inproc` | `PUBSUB` | `통과(108.9%)` | `통과(103.5%)` | `통과(105.1%)` | `통과(1126.4%)` | `통과(984.3%)` | `통과(384.8%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `inproc` | `DEALER_DEALER` | `통과(116.6%)` | `통과(94.6%)` | `통과(82.4%)` | `통과(100.1%)` | `통과(100.4%)` | `통과(99.7%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `inproc` | `DEALER_ROUTER` | `통과(95.8%)` | `통과(101.6%)` | `통과(100.6%)` | `통과(130.2%)` | `통과(137.5%)` | `통과(118.2%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `inproc` | `ROUTER_ROUTER` | `통과(104.5%)` | `통과(98.1%)` | `통과(99.6%)` | `통과(86.1%)` | `미달(62.9%)` | `미달(17.5%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `inproc` | `SPOT` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ipc` | `PAIR` | `통과(99.7%)` | `통과(99.7%)` | `통과(91.1%)` | `통과(99.8%)` | `통과(99.9%)` | `통과(100.8%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ipc` | `PUBSUB` | `통과(106.2%)` | `통과(128.2%)` | `통과(115.9%)` | `통과(457.6%)` | `통과(538.9%)` | `통과(589.0%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ipc` | `DEALER_DEALER` | `통과(99.6%)` | `통과(99.6%)` | `통과(93.3%)` | `통과(100.8%)` | `통과(99.8%)` | `통과(99.8%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ipc` | `DEALER_ROUTER` | `통과(96.9%)` | `통과(94.4%)` | `통과(100.3%)` | `미달(52.3%)` | `미달(45.2%)` | `미달(45.4%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ipc` | `ROUTER_ROUTER` | `통과(106.2%)` | `통과(108.2%)` | `통과(97.1%)` | `통과(94.3%)` | `통과(95.4%)` | `통과(100.1%)` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |
| `ipc` | `SPOT` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | C single baseline `perf_c_single_linux_20260619_081342_prerelease_7_0_0_final_c_single.txt`와 C++ single full `perf_cpp_single_linux_20260619_092918_prerelease_7_0_0_cpp_single_full.txt` 비교. |

#### 8.1.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `통과(96.4%)` | `통과(106.3%)` | `통과(112.9%)` | `통과(109.9%)` | `통과(113.9%)` | `통과(115.2%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(100.6%)` | `통과(99.1%)` | `통과(100.1%)` | `통과(100.7%)` | `통과(106.9%)` | `통과(95.5%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(96.2%)` | `통과(94.7%)` | `통과(95.3%)` | `통과(93.8%)` | `통과(90.6%)` | `통과(97.3%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tcp` | `MULTI_PUBSUB` | `통과(98.3%)` | `통과(98.8%)` | `통과(100.3%)` | `통과(110.8%)` | `통과(105.6%)` | `통과(97.4%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tcp` | `MULTI_SPOT` | `통과(87.8%)` | `통과(95.7%)` | `통과(99.3%)` | `통과(93.4%)` | `통과(99.5%)` | `통과(99.1%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(97.8%)` | `통과(95.0%)` | `통과(95.8%)` | `통과(96.5%)` | `통과(111.5%)` | `통과(107.6%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(120.1%)` | `통과(119.4%)` | `통과(120.6%)` | `통과(102.1%)` | `통과(96.0%)` | `통과(88.4%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tcp` | `MULTI_STREAM` | `통과(122.1%)` | `통과(118.7%)` | `통과(118.0%)` | `해당 없음` | `통과(94.5%)` | `해당 없음` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(90.9%)` | `통과(108.2%)` | `통과(112.6%)` | `통과(117.5%)` | `통과(112.8%)` | `통과(109.4%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(100.1%)` | `통과(95.6%)` | `통과(99.6%)` | `통과(91.9%)` | `통과(91.2%)` | `통과(103.6%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(94.8%)` | `통과(89.3%)` | `통과(92.2%)` | `통과(91.9%)` | `통과(79.7%)` | `통과(94.8%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `ws` | `MULTI_PUBSUB` | `통과(97.1%)` | `통과(107.0%)` | `통과(106.2%)` | `통과(112.1%)` | `통과(131.5%)` | `통과(98.3%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `ws` | `MULTI_SPOT` | `통과(96.7%)` | `통과(121.5%)` | `통과(103.5%)` | `통과(101.4%)` | `통과(103.8%)` | `통과(94.3%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(98.9%)` | `통과(92.1%)` | `통과(103.3%)` | `통과(102.8%)` | `통과(103.5%)` | `통과(98.3%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(107.3%)` | `통과(98.5%)` | `통과(89.9%)` | `통과(101.8%)` | `통과(89.6%)` | `통과(95.5%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `ws` | `MULTI_STREAM` | `통과(124.6%)` | `통과(124.6%)` | `통과(133.6%)` | `해당 없음` | `통과(102.7%)` | `해당 없음` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(98.3%)` | `통과(107.2%)` | `통과(108.3%)` | `통과(108.2%)` | `통과(99.0%)` | `통과(106.0%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(100.3%)` | `통과(103.0%)` | `통과(102.9%)` | `통과(108.7%)` | `통과(101.0%)` | `통과(102.6%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(100.5%)` | `통과(99.4%)` | `통과(102.7%)` | `통과(101.6%)` | `통과(105.6%)` | `통과(107.9%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `wss` | `MULTI_PUBSUB` | `통과(99.0%)` | `통과(97.8%)` | `통과(107.6%)` | `통과(121.4%)` | `통과(84.3%)` | `통과(95.5%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `wss` | `MULTI_SPOT` | `통과(104.2%)` | `미달(28.2%)` | `통과(86.5%)` | `통과(156.5%)` | `통과(102.7%)` | `통과(111.9%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(95.4%)` | `통과(96.7%)` | `통과(99.6%)` | `통과(101.3%)` | `통과(106.7%)` | `통과(108.6%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(110.6%)` | `통과(106.8%)` | `통과(99.7%)` | `통과(100.2%)` | `통과(95.9%)` | `통과(103.1%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `wss` | `MULTI_STREAM` | `통과(109.1%)` | `통과(108.3%)` | `통과(105.6%)` | `해당 없음` | `통과(98.0%)` | `해당 없음` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(96.8%)` | `통과(112.0%)` | `통과(112.8%)` | `통과(125.3%)` | `통과(103.1%)` | `통과(104.6%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(101.7%)` | `통과(100.8%)` | `통과(102.4%)` | `통과(104.3%)` | `통과(106.7%)` | `통과(106.5%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(103.4%)` | `통과(98.8%)` | `통과(100.1%)` | `통과(100.3%)` | `통과(101.2%)` | `통과(100.0%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tls` | `MULTI_PUBSUB` | `통과(104.2%)` | `통과(102.5%)` | `통과(107.0%)` | `통과(121.0%)` | `통과(109.0%)` | `통과(100.8%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tls` | `MULTI_SPOT` | `통과(89.1%)` | `통과(90.1%)` | `통과(100.3%)` | `통과(102.2%)` | `통과(105.5%)` | `통과(103.1%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(95.0%)` | `통과(89.6%)` | `통과(94.0%)` | `통과(94.6%)` | `통과(96.4%)` | `통과(101.5%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(107.2%)` | `통과(114.6%)` | `통과(122.9%)` | `통과(101.4%)` | `통과(97.3%)` | `통과(104.5%)` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |
| `tls` | `MULTI_STREAM` | `통과(114.3%)` | `통과(108.2%)` | `통과(109.2%)` | `해당 없음` | `통과(98.5%)` | `해당 없음` | C multi baseline `perf_c_multi_linux_20260619_062932.txt`와 C++ multi full `perf_cpp_multi_linux_20260619_095144_prerelease_7_0_0_cpp_multi_full.txt` 비교. |

### 8.2 .NET

#### 8.2.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `inproc` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `inproc` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `inproc` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `inproc` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `inproc` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `inproc` | `SPOT` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ipc` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ipc` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ipc` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ipc` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ipc` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ipc` | `SPOT` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |

#### 8.2.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |

### 8.3 Java

#### 8.3.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |

#### 8.3.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |

### 8.4 Node

#### 8.4.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |

#### 8.4.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |

### 8.5 Go

#### 8.5.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |

#### 8.5.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |

### 8.6 Rust

#### 8.6.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |

#### 8.6.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |

### 8.7 Python

#### 8.7.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tcp` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `ws` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `wss` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |
| `tls` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C single baseline과 비교한다. |

#### 8.7.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 4096 | 65536 | 131072 | 결과 파일 / 메모 |
|---|---|---|---|---|---|---|---|---|
| `tcp` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tcp` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `ws` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `wss` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |
| `tls` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `해당 없음` | `미측정` | `해당 없음` | 7.0.0 기준 full 결과 확보 뒤 C multi baseline과 비교한다. |

## 9. 언어별 평균 성능표

아래 표는 최종 full 결과에서 `통과(비율%)`, `미달(비율%)`, `보류(비율%)` 값을 모아
계산한다. `해당 없음`과 `미측정`은 제외한다. 평균만으로 통과 여부를 판단하지 않고,
p10과 최저 10% 평균을 함께 본다.

| 언어 | 측정 셀 수 | 평균 | 중앙값 | p10 | 최저 10% 평균 | Single 평균 | Multi 평균 | 메모 |
|------|------------|------|--------|-----|---------------|-------------|------------|------|
| C++ | `388` | `114.3%` | `100.0%` | `90.9%` | `74.4%` | `124.8%` | `102.7%` | full run 기준. 미달 후보는 제한 재측정 후 최종 판정. |
| .NET | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` |
| Java | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` |
| Node | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` |
| Go | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` |
| Rust | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` |
| Python | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` |

## 10. 실행 순서

최종 확인은 아래 순서로 진행한다. 한 언어가 끝나기 전에 다음 언어 full run으로 넘어가지
않는다. 단, C 기준 baseline 확보는 모든 언어보다 먼저 한 번 수행한다.

| 순서 | 작업 | 명령 / 기준 | 완료 조건 |
|------|------|-------------|-----------|
| 1 | core runtime rebuild | `cmake --build core/build` | `core/build` 아래 `libzlink`가 7.0.0이고 source보다 최신이다. |
| 2 | C single baseline | `bindings/c/perf/run_benchmarks.sh` | `status=complete`, 결과 라인 `1020/1020`. |
| 3 | C multi baseline | `bindings/c/perf/run_benchmarks_multi.sh --msg-sizes 64,256,1024,4096,65536,131072` | `status=complete`, 결과 라인 `960/960`. |
| 4 | C++ smoke/full | `bindings/cpp/perf/run_benchmarks*.sh` | Single/Multi 상세표에 미달 또는 미측정이 없다. |
| 5 | .NET smoke/full | `bindings/dotnet/perf/run_benchmarks*.sh` | Single/Multi 상세표에 미달 또는 미측정이 없다. |
| 6 | Java smoke/full | `bindings/java/perf/run_benchmarks*.sh` | Single/Multi 상세표에 미달 또는 미측정이 없다. |
| 7 | Node smoke/full | `bindings/node/perf/run_benchmarks*.sh` | Single/Multi 상세표에 미달 또는 미측정이 없다. |
| 8 | Go smoke/full | `bindings/go/perf/run_benchmarks*.sh` | Single/Multi 상세표에 미달 또는 미측정이 없다. |
| 9 | Rust smoke/full | `bindings/rust/perf/run_benchmarks*.sh` | Single/Multi 상세표에 미달 또는 미측정이 없다. |
| 10 | Python smoke/full | `bindings/python/perf/run_benchmarks*.sh` | Single/Multi 상세표에 미달 또는 미측정이 없다. |
| 11 | 최종 요약 | 이 문서 §6, §7, §8, §9 갱신 | 모든 언어 `출시 판단`이 `통과`이거나, 남은 `보류`의 별도 설계/릴리스 판단이 명시되어 있다. |

## 11. 최종 릴리스 판단

| 항목 | 상태 | 근거 |
|------|------|------|
| C 7.0.0 기준 확보 | `완료` | §5 |
| 모든 언어 smoke 통과 | `진행 중` | C++ smoke 완료, .NET single smoke 완료. .NET multi와 나머지 언어 미측정. |
| 모든 언어 full complete | `미측정` | §6 |
| 미달 조합 0건 | `미측정` | §7 |
| 보류 조합 처리 방침 명시 | `미측정` | §7 |
| 출시 가능 여부 | `대기` | 위 항목이 모두 닫힌 뒤 갱신한다. |
