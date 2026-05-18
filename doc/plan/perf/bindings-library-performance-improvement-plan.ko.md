# bindings 라이브러리 성능 개선 계획

> 이 문서는 bindings 라이브러리 성능을 C 기준 대비 목표 비율까지 끌어올리기 위한
> 실행 계획이다. 이전 측정 기록은 이 문서에 보관하지 않고, 매 라운드의 결과 파일과
> 최종 요약만 별도로 남긴다.

## 1. 범위와 목표

이번 작업은 perf 자체를 빠르게 만드는 일이 아니라, 각 언어 binding 라이브러리가
public API를 통해 내는 실제 성능을 개선하는 일이다. perf는 `doc/perf` 정책에 맞게
이미 작성되어 있다고 보고, perf 자체의 측정 버그가 확인된 경우를 제외하면
수정하지 않는다.

목표 비율은 같은 suite, pattern, transport, message size, metric 조합에서
`bindings/c/perf` 결과를 기준으로 계산한다.

C 기준은 core 내부 이론 성능이 아니라, `bindings/c/perf`가 public C API로 측정한
C binding 라이브러리의 일반적인 성능이다. 기준으로 삼는 C 결과는 같은 기본 옵션으로
실행한 최근 full 측정 결과여야 하며, 특정 실험이나 debug 재현을 위한 일회성 결과는
기준으로 쓰지 않는다. C 결과 자체가 비정상적으로 낮거나 높아 보이면 같은 조건으로
재측정해 일반적인 범위를 먼저 확인한다.

| 순서 | 언어 | perf 경로 |
|------|------|-----------|
| 1 | C++ | `bindings/cpp/perf` |
| 2 | .NET | `bindings/dotnet/perf` |
| 3 | Java | `bindings/java/perf` |
| 4 | Node | `bindings/node/perf` |
| 5 | Rust | `bindings/rust/perf` |
| 6 | Go | `bindings/go/perf` |
| 7 | Python | `bindings/python/perf` |

목표 비율은 size 하나로만 정하지 않는다. 최근 C++ full 비교에서는 size보다
pattern 차이가 더 컸다. 예를 들어 single `PAIR`, `PUBSUB`, `SPOT`은 C와 비슷하거나
더 빠른 조합이 많았지만, routed pattern인 `DEALER_ROUTER`, `ROUTER_ROUTER`는 일부
transport와 큰 메시지에서 크게 낮아졌다.

다만 `ROUTER_ROUTER` 또는 `MULTI_ROUTER_ROUTER`가 현재 특정 binding에서 낮게 나온
결과를 그대로 낮은 목표 기준으로 인정하지 않는다. 같은 suite, transport, size에서
C의 `ROUTER_ROUTER`와 `DEALER_ROUTER` 차이가 작다면 해당 binding도 그 차이에
가까워야 한다. 즉 routed router 성능은 C 대비 절대 비율뿐 아니라 같은 binding의
`DEALER_ROUTER` 대비 상대 비율로도 검증한다. C++ `MULTI_ROUTER_ROUTER`처럼
`MULTI_DEALER_ROUTER` 대비 과도하게 낮은 결과는 목표 완화 근거가 아니라 binding
라이브러리 병목 또는 버그 후보로 본다.

따라서 완료 판단은 pattern 그룹별 범위를 먼저 적용하고, size는 보조 기준으로 본다.
아래 표의 왼쪽 값은 최소 통과 기준이고, 오른쪽 값은 안정권 기준이다. 64KB 이상 큰
메시지는 같은 pattern 그룹 안에서 낮은 쪽 기준을 적용하고, 64B~1024B 작은 메시지는
높은 쪽 기준에 가까워지는 것을 목표로 한다.

Node와 Python은 별도 근거 없이 큰 차이를 두지 않는다. 두 binding 모두 동적
런타임과 native 경계 비용이 큰 그룹으로 보고 같은 목표 범위를 적용한다. Rust는
C++보다 높은 기준으로 두지 않는다. 둘 다 native binding 그룹으로 보며, public API
래퍼 비용을 감안하더라도 managed runtime binding보다는 높은 기준을 적용한다.

| Pattern 그룹 | 포함 pattern | C++/Rust | .NET/Java | Go | Node/Python |
|--------------|--------------|----------|-----------|----|-------------|
| 단순 one-way | `PAIR`, `PUBSUB`, `DEALER_DEALER`, `MULTI_PUBSUB`, `MULTI_STREAM` | 80~90% | 63~73% | 53~63% | 35~43% |
| routed one-way | `DEALER_ROUTER`, `ROUTER_ROUTER` | 70~83% | 55~67% | 47~57% | 33~40% |
| multi routed echo | `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER` | 65~77% | 50~63% | 40~53% | 30~37% |
| SPOT 계열 | `SPOT`, `MULTI_SPOT`, `MULTI_SPOT_REQREP`, `MULTI_SPOT_SENDSEND` | 75~90% | 60~70% | 50~60% | 33~40% |

`ROUTER_ROUTER` 계열은 추가로 아래 상대 기준을 적용한다.

- C의 `ROUTER_ROUTER / DEALER_ROUTER` 비율을 같은 suite, transport, size에서 계산한다.
- 대상 binding의 `ROUTER_ROUTER / DEALER_ROUTER` 비율이 C의 상대 비율보다 크게
  낮으면, 절대 목표 비율을 넘더라도 완료로 보지 않는다.
- 상대 비율 허용 오차는 측정 오차를 감안해 C++/Rust는 10%p, .NET/Java/Go는 15%p,
  Node/Python은 20%p로 본다.

latency, latency_p95, latency_p99는 throughput 목표를 만족하더라도 C 대비 과도하게
악화되면 완료로 보지 않는다. 레이턴시 악화가 보이면 같은 조합을 다시 측정하고,
binding 내부 병목인지 perf 측정 오류인지 먼저 구분한다.

## 2. 고정 원칙

- 성능 개선 대상은 perf가 아니라 각 언어 binding 라이브러리다.
- perf는 버그가 있거나 `doc/perf` 원칙을 위배했을 때만 수정한다.
- binding perf는 `bindings/c/perf`와 같은 의미를 측정해야 한다.
- C perf와 다른 의미를 만드는 실험은 하지 않는다. worker 수, client 수, transport,
  pattern, message size, duration, timeout, HWM, socket buffer, borrow/copy 정책은
  C와 대상 binding이 같은 조건일 때만 비교 근거로 사용한다.
- binding perf hot path는 해당 언어의 public API를 사용해야 한다.
- 내부 API, private API, native helper, C API 직접 호출로 수치를 만드는 방식은
  인정하지 않는다.
- 수치 달성만을 위해 perf 전용 public API나 zero-copy 우회 API를 추가하지 않는다.
- public API 계약이 잘못 구현되었거나 필수 계약이 빠진 것이 확인되지 않았다면
  인터페이스를 추가하거나 바꾸지 않는다. 성능 미달은 기존 public API 내부 구현을
  개선해서 해결해야 하며, 공개 인터페이스 변경의 근거가 될 수 없다.
- public API 변경이 꼭 필요하면 먼저 어떤 계약이 잘못되었는지 spec과 테스트로
  확인한 뒤 수정한다. 이 경우에도 perf 수치 달성이 아니라 공개 계약 정정이 변경
  이유여야 한다.
- 성능 목표를 달성하려면 public API 추가나 변경이 필요하다고 판단되는 경우에는
  해당 변경을 바로 구현하지 않는다. 먼저 필요한 계약, 이유, 대안, 영향을 계획
  문서에 적고 사용자에게 승인 요청을 한 뒤 대기한다. 승인 대기 중에는 그 항목을
  `보류`로 표시하고, 현재 언어에 `미달` 또는 `미측정` 항목이 남아 있는지 확인한다.
- .NET 이후 언어에서도 public API 변경은 최후 수단이다. 기존 public API 내부 최적화
  후보를 먼저 모두 검토하고, 큰 개선 가능성이 명확하지 않으면 public API 변경
  프로토타입도 만들지 않는다. 테스트 의미가 달라지는 실험이나 큰 개선 가능성이 낮은
  인터페이스 변경 실험은 하지 않는다. public API 변경 후보가 목표 달성에 의미 있는
  개선을 만들 가능성이 높다고 판단되는 경우에만 제한 프로토타입으로 확인한다.
  프로토타입 변경은
  측정 직후 원복해야 하며, 개선이 확인된 경우에도 필요한 계약, 예상 호출 방식, 영향
  범위, 측정 결과를 문서에 남기고 사용자 승인 전에는 정식 반영하지 않는다.
- 버그가 확인되면 perf에서 우회하지 않고 버그를 먼저 수정한다.
- 버그 수정 전에는 해당 동작을 재현하는 회귀테스트를 먼저 작성한다.
- binding 버그이면 해당 언어 binding 라이브러리에서 수정한다.
- core 버그이면 core에서 수정한 뒤 `bindings/dev_sync_local_core_libs.sh`로
  bindings에 local core library를 다시 배포한다.
- public API의 실제 계약은 각 binding의 public contract source를 기준으로 판단한다.
  예를 들어 .NET은 `bindings/dotnet/src/Zlink/Contracts/`가 public API 계약의
  단일 기준이다. `doc/spec/bindings/README.md`와 각 언어별
  `doc/spec/bindings/<lang>/README.md`는 API 목록이 아니라 public API 작성,
  배치, internal boundary 검토 규칙으로만 사용한다. perf 프로젝트가 `internal`
  surface나 `InternalsVisibleTo`에 의존하면 안 된다.
- Auto-HWM message unit처럼 binding spec에서 typed option facade로 제공해야 한다고
  정한 기능은 그 규칙을 따른다. raw option bag이나 perf 전용 helper로 우회하지 않는다.
- `doc/perf/PERF_POLICY.md`,
  `doc/perf/PERF_SINGLE_TEST_POLICY.md`,
  `doc/perf/PERF_MULTI_TEST_POLICY.md`를 항상 따른다.

## 3. 실행 방식

각 binding의 공식 perf 스크립트를 그대로 사용한다.

- single: `bindings/<lang>/perf/run_binding_single.sh`
- multi: `bindings/<lang>/perf/run_binding_multi.sh`
- C 기준: `bindings/c/perf/run_benchmarks.sh`,
  `bindings/c/perf/run_benchmarks_multi.sh`

스크립트에 설정된 기본값을 바꾸지 않는다. 비교 범위를 좁힐 때만
`--transports`와 `--pattern`으로 특정 transport와 특정 pattern을 지정한다.
제한 측정은 transport를 먼저 고정하고, 그 안에서 message size 일부 또는 전체를
확인한다. 예를 들어 작은 message size만 골라 `tcp,ws,wss,tls`를 한 번에 돌리는
방식은 쓰지 않는다. 먼저 `tcp`만 대상으로 pattern과 message size 일부 또는 전체를
비교한다. `tcp`에서 목표 비율을 만족한 뒤에만 같은 방식으로 `ws`, `wss`, `tls`
순서로 넘어간다. transport를 바꿀 때마다 C 기준과 대상 binding 결과의 옵션을 다시
대조한다.
제한 측정으로 C와 대상 binding을 다시 비교할 때는 결과 파일의 `Effective Options`를
먼저 대조한다. suite, pattern, transport, message size, duration, client 수,
timeout, HWM, socket buffer 설정이 같은지 확인해야 한다. auto-HWM을 사용하는 경우에는
`Auto-HWM Detail`, `Auto-HWM spotnode`, `Auto-HWM spot handles`에 보이는 모든
`MsgUnit(B)`가 해당 message size와 같은지도 확인한다. 예를 들어 64B 테스트에서
`MsgUnit(B)=4096`이 보이면 그 결과는 비교 기준으로 쓰지 않는다. SPOT 계열은 데이터
소켓뿐 아니라 제어용 SpotNode와 SPOT handle도 같은 message size를 사용해야 한다.
이 값이 다르면 HWM slot 수와 socket buffer 크기가 달라져 처리량 비교가 왜곡될 수 있다.
또한 routed echo 계열처럼 C perf가 특정 transport에서 payload를 빌려 쓰는 경우에는
`Effective Options`의 `routed_echo_borrow_payload` 값도 함께 확인한다. 이 값이 다르면
메시지 복사 비용이 비교에 섞이므로, 결과를 binding 자체 성능으로 단정하지 않는다.

C는 개선 대상 언어가 아니라 비교군이다. 따라서 첫 비교에서는 C perf를 매번 새로
실행하지 않고, `bindings/c/perf/baseline/` 아래의 최근 full 측정 결과를 사용한다.
이 기준 파일은 같은 기본 옵션으로 실행한 결과여야 하며, 특정 실험이나 debug 재현을
위해 제한 실행한 결과는 기준으로 쓰지 않는다.

측정 오차가 의심되거나 C 기준 파일의 특정 조합이 비정상적으로 보일 때만, 비교 범위를
같은 transport와 pattern으로 제한해서 C와 대상 binding을 각각 다시 측정한다. 이때도
C는 새 기준을 만들기 위한 보조 측정일 뿐이며, 동시에 여러 C perf를 계속 돌리지 않는다.

한 라운드는 아래 순서로 진행한다.

1. `bindings/c/perf/baseline/`의 최근 full C 결과에서 같은 suite, pattern, transport,
   message size, metric 값을 찾는다.
2. 대상 binding perf를 언어별로 하나만 실행한다.
3. C 대비 비율을 계산한다.
4. 미달 조합의 병목을 binding 라이브러리에서 찾는다.
5. binding 라이브러리를 수정한다.
6. 같은 조합의 대상 binding perf를 다시 측정한다.
7. 측정 오차가 의심되면 같은 transport와 pattern으로 C와 대상 binding을 제한
   재측정한 뒤 다시 비교한다.
8. 목표를 넘을 때까지 반복한다.

single과 multi는 같은 원칙으로 반복한다. 한 번에 전체 matrix를 돌리지 않고,
`--transports`와 `--pattern`으로 조합을 좁혀 원인과 개선 효과를 확인한 뒤 다음
조합으로 이동한다. 제한 측정 순서는 transport 우선이다. `tcp`의 미달 조합이 남아 있으면
`ws`, `wss`, `tls` 측정으로 넘어가지 않는다. `tcp`가 통과한 뒤 다음 transport로
넘어갈 때도 한 번에 하나의 transport만 선택해서 C 기준과 대상 binding을 비교한다.

언어별 작업은 한 번에 한 언어만 진행한다. 진행 순서는 C++, .NET, Java, Node, Rust,
Go, Python이다. 현재 언어의 모든 대상 transport, pattern, size가 `통과` 또는 `보류`
상태가 되기 전에는 다음 언어로 넘어가지 않는다. `미달` 또는 `미측정` 항목이 하나라도
남아 있으면 현재 언어 작업을 계속한다.

공식 perf 실행은 기본적으로 하나만 실행한다. 측정 오차 확인을 위해 C와 대상 binding을
제한 재측정해야 할 때도 전체 공식 perf 실행 수는 두 개를 넘기지 않는다. 같은 suite,
pattern, transport, message size 조합을 중복으로 동시에 실행하지 않는다.

## 4. 직접 진행 절차

이 작업은 측정, 병목 분석, 코드 수정, 재측정, 문서 갱신을 직접 수행한다.

상태 값은 아래 네 가지로만 기록한다.

- `미측정`: 아직 같은 조건의 C 기준과 대상 binding 결과를 비교하지 않았다.
- `통과`: 목표 비율, 상대 기준, latency 조건, `Effective Options`, `MsgUnit(B)` 조건을
  모두 만족한다.
- `미달`: 유효 비교에서 목표를 만족하지 못했고, 아직 내부 개선 후보를 더 확인해야 한다.
- `보류`: 유효 비교에서 목표 미달이지만, public API 변경 없이 가능한 내부 개선 후보를
  더 찾지 못했다. 보류 항목은 승인 후보와 근거를 함께 기록해야 한다.

현재 언어에서 `미달` 또는 `미측정` 항목이 남아 있으면 다음 언어로 넘어가지 않는다.
`보류`는 완료가 아니지만, 더 진행하려면 public API 변경 승인이 필요한 상태로 본다.

매 라운드마다 아래를 직접 확인한다.

- C 기준과 binding 결과가 같은 suite, transport, pattern, size를 비교했는지
- `tcp`의 모든 대상 pattern/size가 `통과` 또는 `보류`가 되었는지
- `tcp`에 `미달` 또는 `미측정`이 남은 상태에서 `ws`, `wss`, `tls`로 넘어가지 않았는지
- 제한 재측정 결과의 `Effective Options`와 auto-HWM `MsgUnit(B)`가 서로 같은지
- C 기준으로 `bindings/c/perf/baseline/`의 최근 full 결과를 먼저 사용했는지
- C 재측정은 측정 오차나 비정상 기준이 의심되는 제한 조합에서만 실행했는지
- perf 수정이 필요한 경우 실제 버그나 정책 위반 근거가 있는지
- 수정이 binding public API 내부 구현 개선인지
- 새로 발견한 결과를 상태 표와 로그 문서에 반영했는지

실행 중 문제가 발견되면 같은 언어 안에서 원인을 리뷰하고, 회귀테스트 작성, 버그 수정,
재측정, 문서 갱신을 반복한다. 측정 실패, 기준 불일치, perf 정책 위반, binding/core
버그, 목표 기준의 모호함이 모두 사라질 때까지 해당 항목을 `통과`나 `보류`로 표시하지
않는다.

## 5. 현재 상태 표

이 표는 최신 판정만 유지한다. 상세 측정 기록은 `doc/plan/perf/log/` 아래에 둔다.

### 5.1 언어 진행 상태

| 순서 | 언어 | 현재 transport | 전체 상태 | 다음 작업 |
|------|------|----------------|-----------|-----------|
| 1 | C++ | `tls` | 보류 포함 완료 | .NET `tcp` 시작 |
| 2 | .NET | `tcp` | 진행 중 | `MULTI_SPOT/tcp/64` 내부 개선 |
| 3 | Java | `tcp` | 대기 | .NET의 `미달` 해소 후 시작 |
| 4 | Node | `tcp` | 대기 | Java의 `미달` 해소 후 시작 |
| 5 | Rust | `tcp` | 대기 | Node의 `미달` 해소 후 시작 |
| 6 | Go | `tcp` | 대기 | Rust의 `미달` 해소 후 시작 |
| 7 | Python | `tcp` | 대기 | Go의 `미달` 해소 후 시작 |

### 5.2 C++ 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | `MULTI_ROUTER_ROUTER` | `65536` | `보류` | `52.8%` | `perf_cpp_multi_linux_20260518_114136_codex_cpp_tcp_rr_64_after_raw_revert.txt` |
| `tcp` | `MULTI_ROUTER_ROUTER` | `131072` | `통과` | `66.8%` | `perf_cpp_multi_linux_20260518_112701_codex_cpp_tcp_rr_large_local_send_msg.txt` |
| `ws` | `MULTI_DEALER_DEALER` | `64` | `보류` | `76.5%` | public API 변경 없이 추가 내부 후보 없음 |
| `ws` | `MULTI_DEALER_DEALER` | `256` | `통과` | `96.4%` | C current 기준 |
| `ws` | `MULTI_DEALER_DEALER` | `1024` | `통과` | `93.5%` | C current 기준 |
| `ws` | `MULTI_DEALER_DEALER` | `65536` | `통과` | `94.8%` | C current 기준 |
| `ws` | `MULTI_DEALER_DEALER` | `131072` | `통과` | `101.6%` | C current 기준 |
| `ws` | `MULTI_DEALER_DEALER` | `262144` | `보류` | `78.8%` | public API 변경 없이 추가 내부 후보 없음 |
| `ws` | `MULTI_DEALER_ROUTER` | `64` | `통과` | `88.1%` | C current 기준 |
| `ws` | `MULTI_DEALER_ROUTER` | `256` | `통과` | `87.7%` | C current 기준 |
| `ws` | `MULTI_DEALER_ROUTER` | `1024` | `통과` | `92.6%` | C current 기준 |
| `ws` | `MULTI_DEALER_ROUTER` | `65536` | `보류` | `59.0%` | public API 변경 없이 추가 내부 후보 없음 |
| `ws` | `MULTI_DEALER_ROUTER` | `131072` | `통과` | `70.9%` | C current 기준 |
| `ws` | `MULTI_DEALER_ROUTER` | `262144` | `통과` | `66.0%` | C current 기준 |
| `ws` | `MULTI_ROUTER_ROUTER` | `64` | `통과` | `95.6%` | C current 기준 |
| `ws` | `MULTI_ROUTER_ROUTER` | `256` | `통과` | `94.7%` | C current 기준 |
| `ws` | `MULTI_ROUTER_ROUTER` | `1024` | `통과` | `93.1%` | C current 기준 |
| `ws` | `MULTI_ROUTER_ROUTER` | `65536` | `보류` | `60.3%` | public API 변경 없이 추가 내부 후보 없음 |
| `ws` | `MULTI_ROUTER_ROUTER` | `131072` | `보류` | `60.8%` | public API 변경 없이 추가 내부 후보 없음 |
| `ws` | `MULTI_ROUTER_ROUTER` | `262144` | `통과` | `100.6%` | C current 기준 |
| `ws` | `MULTI_PUBSUB` | `64` | `통과` | `95.8%` | C current 기준 |
| `ws` | `MULTI_PUBSUB` | `256` | `통과` | `98.2%` | C current 기준 |
| `ws` | `MULTI_PUBSUB` | `1024` | `통과` | `92.6%` | `perf_cpp_multi_linux_20260518_124911_codex_cpp_ws_pubsub_1024_debug.txt` |
| `ws` | `MULTI_PUBSUB` | `65536,131072,262144` | `통과` | `87.6%~113.9%` | `perf_cpp_multi_linux_20260518_124924_codex_cpp_ws_pubsub_large_recheck.txt` |
| `ws` | `MULTI_SPOT` | 전체 대상 | `보류` | - | 실행 실패, `MsgUnit(B)=4096`; SPOT pub/sub auto-HWM message unit typed facade 승인 필요 |
| `ws` | `MULTI_SPOT_REQREP` | 전체 대상 | `보류` | - | 실행 실패, `MsgUnit(B)=4096`; SPOT pub/sub auto-HWM message unit typed facade 승인 필요 |
| `ws` | `MULTI_SPOT_SENDSEND` | 전체 대상 | `보류` | - | 실행 실패, `MsgUnit(B)=4096`; SPOT pub/sub auto-HWM message unit typed facade 승인 필요 |
| `ws` | `MULTI_STREAM` | `64,256,1024,65536` | `통과` | `80.3%~98.9%` | `perf_cpp_multi_linux_20260518_124314_codex_cpp_ws_full_status.txt` |
| `wss` | `MULTI_DEALER_DEALER` | `64` | `보류` | `73.7%` | 제한 C 기준, public API 변경 없이 추가 내부 후보 없음 |
| `wss` | `MULTI_DEALER_DEALER` | `256` | `통과` | `99.7%` | 제한 C: `perf_c_multi_linux_20260518_133226_codex_c_wss_dd_recheck_compare.txt` |
| `wss` | `MULTI_DEALER_DEALER` | `1024,65536` | `통과` | `83.7%~88.2%` | `perf_cpp_multi_linux_20260518_132049_codex_cpp_wss_full_status.txt` |
| `wss` | `MULTI_DEALER_DEALER` | `131072` | `통과` | `92.6%` | 제한 C: `perf_c_multi_linux_20260518_133226_codex_c_wss_dd_recheck_compare.txt` |
| `wss` | `MULTI_DEALER_DEALER` | `262144` | `보류` | - | C++ timeout, C 제한 측정은 성공 |
| `wss` | `MULTI_DEALER_ROUTER` | `64,256,1024,65536,131072,262144` | `통과` | `84.4%~95.5%` | `perf_cpp_multi_linux_20260518_132049_codex_cpp_wss_full_status.txt` |
| `wss` | `MULTI_ROUTER_ROUTER` | `64,256,1024,65536,131072,262144` | `통과` | `83.0%~93.5%` | `perf_cpp_multi_linux_20260518_132049_codex_cpp_wss_full_status.txt` |
| `wss` | `MULTI_PUBSUB` | `64,1024` | `통과` | `89.4%~104.3%` | 제한 C: `perf_c_multi_linux_20260518_133255_codex_c_wss_pubsub_recheck_compare.txt` |
| `wss` | `MULTI_PUBSUB` | `256` | `보류` | `78.3%` | typed publish 후보 timeout, public subscribe hot path 승인 필요 |
| `wss` | `MULTI_PUBSUB` | `65536` | `보류` | `67.2%` | typed publish 후보 timeout, public subscribe hot path 승인 필요 |
| `wss` | `MULTI_PUBSUB` | `131072,262144` | `보류` | - | C++ timeout, C 제한 측정은 성공 |
| `wss` | `MULTI_SPOT` | 전체 대상 | `보류` | - | 실행 실패, `MsgUnit(B)=4096`; SPOT pub/sub auto-HWM message unit typed facade 승인 필요 |
| `wss` | `MULTI_SPOT_REQREP` | 전체 대상 | `보류` | - | 실행 실패, `MsgUnit(B)=4096`; SPOT pub/sub auto-HWM message unit typed facade 승인 필요 |
| `wss` | `MULTI_SPOT_SENDSEND` | 전체 대상 | `보류` | - | 실행 실패, `MsgUnit(B)=4096`; SPOT pub/sub auto-HWM message unit typed facade 승인 필요 |
| `wss` | `MULTI_STREAM` | `64,256,1024,65536` | `통과` | `90.0%~100.1%` | `perf_cpp_multi_linux_20260518_132049_codex_cpp_wss_full_status.txt` |
| `tls` | `MULTI_DEALER_DEALER` | `64` | `보류` | `75.5%` | 제한 C 기준, public API 변경 없이 추가 내부 후보 없음 |
| `tls` | `MULTI_DEALER_DEALER` | `256,1024,65536` | `통과` | `83.4%~88.4%` | `perf_cpp_multi_linux_20260518_133640_codex_cpp_tls_full_status.txt` |
| `tls` | `MULTI_DEALER_DEALER` | `131072` | `보류` | `51.6%` | 제한 C 기준, public API 변경 없이 추가 내부 후보 없음 |
| `tls` | `MULTI_DEALER_DEALER` | `262144` | `보류` | - | C++ timeout, C 제한 측정은 성공 |
| `tls` | `MULTI_DEALER_ROUTER` | `64,256,1024,65536,131072` | `통과` | `83.6%~89.1%` | `perf_cpp_multi_linux_20260518_133640_codex_cpp_tls_full_status.txt` |
| `tls` | `MULTI_DEALER_ROUTER` | `262144` | `통과` | `88.2%` | 제한 C: `perf_c_multi_linux_20260518_141339_codex_c_tls_dr_262_recheck_compare.txt` |
| `tls` | `MULTI_ROUTER_ROUTER` | `64,256,1024,65536,131072` | `통과` | `89.3%~94.8%` | `perf_cpp_multi_linux_20260518_133640_codex_cpp_tls_full_status.txt` |
| `tls` | `MULTI_ROUTER_ROUTER` | `262144` | `통과` | `112.5%` | 제한 C: `perf_c_multi_linux_20260518_141403_codex_c_tls_rr_262_recheck_compare.txt` |
| `tls` | `MULTI_PUBSUB` | `64,256,1024` | `통과` | `80.2%~85.5%` | 제한 C와 full 기준 |
| `tls` | `MULTI_PUBSUB` | `65536` | `보류` | `70.2%` | public subscribe hot path 승인 필요 |
| `tls` | `MULTI_PUBSUB` | `131072` | `통과` | `97.8%` | 제한 C: `perf_c_multi_linux_20260518_140850_codex_c_tls_pubsub_recheck_compare.txt` |
| `tls` | `MULTI_PUBSUB` | `262144` | `보류` | - | C++ timeout, C 제한 측정은 성공 |
| `tls` | `MULTI_SPOT` | 전체 대상 | `보류` | - | 실행 실패, `MsgUnit(B)=4096`; SPOT pub/sub auto-HWM message unit typed facade 승인 필요 |
| `tls` | `MULTI_SPOT_REQREP` | 전체 대상 | `보류` | - | 실행 실패, `MsgUnit(B)=4096`; SPOT pub/sub auto-HWM message unit typed facade 승인 필요 |
| `tls` | `MULTI_SPOT_SENDSEND` | 전체 대상 | `보류` | - | 실행 실패, `MsgUnit(B)=4096`; SPOT pub/sub auto-HWM message unit typed facade 승인 필요 |
| `tls` | `MULTI_STREAM` | `64,256,1024,65536` | `통과` | `94.8%~103.2%` | `perf_cpp_multi_linux_20260518_133640_codex_cpp_tls_full_status.txt` |

### 5.3 .NET 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | `MULTI_DEALER_DEALER` | `64` | `보류` | `53.7%` | builder inline 후보 후에도 미달, 추가 내부 후보 없음 |
| `tcp` | `MULTI_DEALER_DEALER` | `256` | `보류` | `62.5%` | 제한 재측정에서도 미달, 추가 내부 후보 없음 |
| `tcp` | `MULTI_DEALER_DEALER` | `1024` | `통과` | `77.6%` | 제한 재측정 |
| `tcp` | `MULTI_DEALER_DEALER` | `65536` | `통과` | `105.4%` | full tcp |
| `tcp` | `MULTI_DEALER_DEALER` | `131072` | `통과` | `94.9%` | full tcp |
| `tcp` | `MULTI_DEALER_DEALER` | `262144` | `통과` | `83.2%` | full tcp |
| `tcp` | `MULTI_DEALER_ROUTER` | `64` | `통과` | `62.5%` | `routed_echo_borrow_payload=tcp` 정렬 |
| `tcp` | `MULTI_DEALER_ROUTER` | `256` | `통과` | `64.8%` | `routed_echo_borrow_payload=tcp` 정렬 |
| `tcp` | `MULTI_DEALER_ROUTER` | `1024` | `통과` | `65.8%` | `routed_echo_borrow_payload=tcp` 정렬 |
| `tcp` | `MULTI_DEALER_ROUTER` | `65536` | `통과` | `80.6%` | `routed_echo_borrow_payload=tcp` 정렬 |
| `tcp` | `MULTI_DEALER_ROUTER` | `131072` | `통과` | `103.6%` | `routed_echo_borrow_payload=tcp` 정렬 |
| `tcp` | `MULTI_DEALER_ROUTER` | `262144` | `통과` | `130.6%` | `routed_echo_borrow_payload=tcp` 정렬 |
| `tcp` | `MULTI_ROUTER_ROUTER` | `64` | `통과` | `54.9%` | 상대 기준 허용 범위 |
| `tcp` | `MULTI_ROUTER_ROUTER` | `256` | `통과` | `56.0%` | 상대 기준 허용 범위 |
| `tcp` | `MULTI_ROUTER_ROUTER` | `1024` | `통과` | `56.9%` | 제한 재측정, 상대 기준 허용 범위 |
| `tcp` | `MULTI_ROUTER_ROUTER` | `65536` | `통과` | `73.9%` | 상대 기준 허용 범위 |
| `tcp` | `MULTI_ROUTER_ROUTER` | `131072` | `통과` | `93.1%` | 상대 기준 허용 범위 |
| `tcp` | `MULTI_ROUTER_ROUTER` | `262144` | `통과` | `145.7%` | 상대 기준 허용 범위 |
| `tcp` | `MULTI_PUBSUB` | `64` | `통과` | `67.0%` | builder inline 후보 후 통과 |
| `tcp` | `MULTI_PUBSUB` | `256` | `통과` | `71.7%` | 제한 재측정 |
| `tcp` | `MULTI_PUBSUB` | `1024` | `통과` | `103.4%` | 제한 재측정 |
| `tcp` | `MULTI_PUBSUB` | `65536` | `통과` | `84.5%` | full tcp |
| `tcp` | `MULTI_PUBSUB` | `131072` | `통과` | `91.2%` | full tcp |
| `tcp` | `MULTI_PUBSUB` | `262144` | `통과` | `121.0%` | full tcp |
| `tcp` | `MULTI_SPOT` | `64` | `보류` | `51.5%` | 같은 조건 내부 후보 실패, public API 변경 실험은 계속하지 않음 |
| `tcp` | `MULTI_SPOT` | `256` | `보류` | `38.4%` | 같은 SPOT publish/subscribe hot path, 추가 내부 후보 없음 |
| `tcp` | `MULTI_SPOT` | `1024` | `보류` | `49.9%` | 같은 SPOT publish/subscribe hot path, 추가 내부 후보 없음 |
| `tcp` | `MULTI_SPOT` | `65536` | `보류` | `35.8%` | 같은 SPOT publish/subscribe hot path, 추가 내부 후보 없음 |
| `tcp` | `MULTI_SPOT` | `131072` | `보류` | `30.0%` | 같은 SPOT publish/subscribe hot path, 추가 내부 후보 없음 |
| `tcp` | `MULTI_SPOT` | `262144` | `보류` | `23.3%` | 같은 SPOT publish/subscribe hot path, 추가 내부 후보 없음 |
| `tcp` | `MULTI_SPOT_REQREP` | `64` | `통과` | `78.0%` | full tcp |
| `tcp` | `MULTI_SPOT_REQREP` | `256` | `통과` | `69.6%` | full tcp |
| `tcp` | `MULTI_SPOT_REQREP` | `1024` | `통과` | `62.7%` | full tcp |
| `tcp` | `MULTI_SPOT_REQREP` | `65536` | `보류` | - | 제한 재측정도 timeout, 조건 변경 없이 추가 내부 후보 없음 |
| `tcp` | `MULTI_SPOT_REQREP` | `131072` | `통과` | `86.4%` | full tcp |
| `tcp` | `MULTI_SPOT_REQREP` | `262144` | `보류` | - | 제한 재측정도 timeout, 조건 변경 없이 추가 내부 후보 없음 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `64` | `통과` | `69.5%` | 제한 재측정 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `256` | `통과` | `72.7%` | 제한 재측정 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `1024` | `통과` | `72.9%` | 제한 재측정 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `65536` | `보류` | - | 제한 재측정도 timeout, 조건 변경 없이 추가 내부 후보 없음 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `131072` | `통과` | `91.3%` | full tcp |
| `tcp` | `MULTI_SPOT_SENDSEND` | `262144` | `보류` | - | full tcp는 통과했으나 제한 재측정 timeout |
| `tcp` | `MULTI_STREAM` | `64` | `통과` | `91.8%` | full tcp |
| `tcp` | `MULTI_STREAM` | `256` | `통과` | `86.4%` | full tcp |
| `tcp` | `MULTI_STREAM` | `1024` | `통과` | `84.0%` | full tcp |
| `tcp` | `MULTI_STREAM` | `65536` | `통과` | `104.1%` | full tcp |
| `ws` | `MULTI_DEALER_DEALER` | `64` | `보류` | `59.8%` | builder inline 후에도 목표 미달, 추가 내부 후보 없음 |
| `ws` | `MULTI_DEALER_DEALER` | `256` | `보류` | `55.7%` | builder inline 후에도 목표 미달, 추가 내부 후보 없음 |
| `ws` | `MULTI_DEALER_DEALER` | `1024` | `통과` | `67.5%` | full ws |
| `ws` | `MULTI_DEALER_DEALER` | `65536` | `통과` | `85.5%` | full ws |
| `ws` | `MULTI_DEALER_DEALER` | `131072` | `보류` | `57.0%` | 같은 one-way hot path, 추가 내부 후보 없음 |
| `ws` | `MULTI_DEALER_DEALER` | `262144` | `통과` | `80.5%` | full ws |
| `ws` | `MULTI_DEALER_ROUTER` | `64,256,1024,65536,131072,262144` | `통과` | `51.1%~95.3%` | full ws |
| `ws` | `MULTI_ROUTER_ROUTER` | `64,256,1024,65536` | `보류` | `47.1%~48.3%` | 상대 기준 미달, 추가 내부 후보 없음 |
| `ws` | `MULTI_ROUTER_ROUTER` | `131072,262144` | `통과` | `66.1%~82.8%` | full ws |
| `ws` | `MULTI_PUBSUB` | `64,256,1024` | `보류` | `42.9%~56.9%` | builder inline 후에도 목표 미달, 추가 내부 후보 없음 |
| `ws` | `MULTI_PUBSUB` | `65536,131072,262144` | `통과` | `64.7%~73.8%` | full ws |
| `ws` | `MULTI_SPOT` | `64,256,1024,65536,131072,262144` | `보류` | `33.7%~51.6%` | 제한 C 기준, 같은 SPOT hot path 추가 내부 후보 없음 |
| `ws` | `MULTI_SPOT_REQREP` | `64,256,1024,131072` | `통과` | `73.9%~98.5%` | 제한 C 기준 |
| `ws` | `MULTI_SPOT_REQREP` | `65536,262144` | `보류` | - | .NET timeout, C 제한 측정 성공 |
| `ws` | `MULTI_SPOT_SENDSEND` | `64,256,1024,131072` | `통과` | `63.5%~94.4%` | 제한 C 기준 |
| `ws` | `MULTI_SPOT_SENDSEND` | `65536,262144` | `보류` | - | .NET timeout, C 제한 측정 성공 |
| `ws` | `MULTI_STREAM` | `64,256,1024,65536` | `통과` | `89.2%~96.7%` | full ws |
| `wss` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미달 때문에 보류 |
| `tls` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미달 때문에 보류 |

### 5.4 Java 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | `MULTI_PUBSUB` | `64` | `미달` | `59.0%` | `perf_java_multi_linux_20260518_111324_codex_java_mpubsub_tcp64_after_sendbuilder_single_storage.txt` |
| `tcp` | `MULTI_PUBSUB` | `256` | `통과` | `65.2%` | `perf_java_multi_linux_20260518_110614_codex_java_mpubsub_tcp64_256_after_reuse_topicmsg.txt` |
| `tcp` | `MULTI_DEALER_ROUTER` | `65536` | `미달` | `46.7%` | `perf_java_multi_linux_20260518_111220_codex_java_mdr_tcp65536_131072_after_router_single_fastpath.txt` |
| `tcp` | `MULTI_DEALER_ROUTER` | `131072` | `통과` | `56.6%` | `perf_java_multi_linux_20260518_111220_codex_java_mdr_tcp65536_131072_after_router_single_fastpath.txt` |
| `tcp` | SPOT 계열 | `64` | `미달` | - | `MsgUnit(B)=4096` 불일치 |
| `ws` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미달 때문에 보류 |
| `wss` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미달 때문에 보류 |
| `tls` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미달 때문에 보류 |

### 5.5 Node 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | `PUBSUB` | `64` | `통과` | `37.06%` | `perf_node_single_linux_20260518_111604.txt` |
| `tcp` | `PUBSUB` | `256` | `통과` | `36.30%` | `perf_node_single_linux_20260518_111503.txt` |
| `tcp` | 그 외 대상 | 전체 대상 | `미측정` | - | 전체 완료 아님 |
| `ws` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 전체 완료 전 보류 |
| `wss` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 전체 완료 전 보류 |
| `tls` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 전체 완료 전 보류 |

### 5.6 Rust 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | 전체 대상 | 전체 대상 | `미측정` | - | C++ -> .NET -> Java -> Node 이후 진행 |
| `ws` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 전체 완료 전 보류 |
| `wss` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 전체 완료 전 보류 |
| `tls` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 전체 완료 전 보류 |

### 5.7 Go 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | `PAIR` | `64` | `통과` | `79.80%` | `perf_go_single_linux_20260518_115650_codex_go_tcp64_single_recv_into.txt` |
| `tcp` | `DEALER_DEALER` | `64` | `통과` | `78.05%` | `perf_go_single_linux_20260518_115650_codex_go_tcp64_single_recv_into.txt` |
| `tcp` | `DEALER_ROUTER` | `64` | `미달` | `45.12%` | `perf_go_single_linux_20260518_115650_codex_go_tcp64_single_recv_into.txt` |
| `tcp` | `ROUTER_ROUTER` | `64` | `통과` | `50.49%` | `perf_go_single_linux_20260518_115650_codex_go_tcp64_single_recv_into.txt` |
| `tcp` | `PUBSUB` | `64` | `미달` | `9.78%` | `perf_go_single_linux_20260518_120037_codex_go_tcp64_pubsub_adopt_recv.txt` |
| `tcp` | `SPOT` | `64` | `미달` | `29.65%` | `perf_go_single_linux_20260518_115650_codex_go_tcp64_single_recv_into.txt` |
| `ws` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미달 때문에 보류 |
| `wss` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미달 때문에 보류 |
| `tls` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미달 때문에 보류 |

### 5.8 Python 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | `MULTI_DEALER_DEALER` | `64` | `미달` | `4.55%` | `perf_python_multi_linux_20260518_115738_codex_python_tcp64_owned_recv.txt` |
| `tcp` | `MULTI_PUBSUB` | `64` | `미달` | `4.42%` | `perf_python_multi_linux_20260518_115738_codex_python_tcp64_owned_recv.txt` |
| `tcp` | `MULTI_DEALER_ROUTER` | `64` | `미달` | `9.98%` | `perf_python_multi_linux_20260518_115738_codex_python_tcp64_owned_recv.txt` |
| `tcp` | `MULTI_ROUTER_ROUTER` | `64` | `미달` | `8.47%` | `perf_python_multi_linux_20260518_115738_codex_python_tcp64_owned_recv.txt` |
| `tcp` | `MULTI_STREAM` | `64` | `미달` | `0.82%` | `perf_python_multi_linux_20260518_115738_codex_python_tcp64_owned_recv.txt` |
| `tcp` | `MULTI_SPOT_REQREP` | `64` | `미달` | `0.24%` | `MsgUnit(B)=4096` 불일치 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `64` | `미달` | `3.53%` | `MsgUnit(B)=4096` 불일치 |
| `tcp` | `MULTI_SPOT` | `64` | `미달` | - | client timeout |
| `ws` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미달 때문에 보류 |
| `wss` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미달 때문에 보류 |
| `tls` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미달 때문에 보류 |

## 6. 완료 기준

아래 조건을 모두 만족하면 해당 언어 binding 작업을 완료한다.

- single과 multi의 대상 조합이 모두 목표 비율 이상이다.
- perf 결과가 `doc/perf` 정책과 `bindings/c/perf` 의미를 유지한다.
- perf 코드를 수정했다면 버그 또는 정책 위반 근거가 남아 있다.
- binding 라이브러리 변경에 필요한 테스트가 통과한다.
- 실행 중 발견된 이슈가 모두 리뷰되었고, 필요한 테스트와 수정이 끝났다.
- 이 문서가 실제 실행 절차와 판단 기준을 최신 상태로 반영한다.
- 결과 파일 경로와 C 대비 비율 요약이 최종 보고에 포함된다.

모든 대상 언어가 완료되면 최종 요약에는 언어별 최저 비율, 남은 예외, 수정한 파일,
실행한 perf 명령을 함께 기록한다.
