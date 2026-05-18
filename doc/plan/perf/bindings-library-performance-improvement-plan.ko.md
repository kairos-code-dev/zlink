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
가까워야 하는지 진단 기준으로 확인한다. 절대 목표 기준을 통과한 항목은 상대 기준만으로
`미달`로 내리지 않는다. C++ `MULTI_ROUTER_ROUTER`처럼 `MULTI_DEALER_ROUTER` 대비
과도하게 낮은 결과는 목표 완화 근거가 아니라 binding 라이브러리 병목 또는 버그 후보로
본다.

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

`ROUTER_ROUTER` 계열은 추가로 아래 상대 기준을 진단에 사용한다.

- C의 `ROUTER_ROUTER / DEALER_ROUTER` 비율을 같은 suite, transport, size에서 계산한다.
- 대상 binding의 `ROUTER_ROUTER / DEALER_ROUTER` 비율이 C의 상대 비율보다 크게
  낮으면 병목 후보로 기록한다. 단, 절대 목표 비율을 넘으면 상태는 `통과`로 둔다.
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

`MsgUnit(B)` 불일치, `Effective Options` 불일치, timeout은 그 자체로 `보류` 사유가
아니다. 이런 항목은 먼저 비교 조건을 맞추거나 perf 정책 위반 여부를 확인해야 한다.
비교 조건이 아직 맞지 않아도 예비 수치가 목표보다 낮고 같은 public API hot path에
내부 개선 후보가 남아 있으면 `미달`로 둔다. `MsgUnit(B)` 불일치는 `통과`를 막는
조건이며, 개선 작업을 중단할 근거가 아니다.

측정 timeout은 timeout 값을 늘리거나 같은 조합을 반복 실행해서 해결하지 않는다.
retry, 추가 sleep, worker/client 수 조정처럼 실패를 숨기거나 테스트 의미를 바꾸는
우회도 사용하지 않는다.
공식 perf의 duration, operation timeout, runner result timeout, ready timeout 값은
C와 같은 의미를 보존해야 하며, 실패를 숨기기 위해 키우면 안 된다. 결과 라인이 나오지
않는 조합은 `미달`로 두고 server/client 로그를 확인한 뒤, active loop, stop/drain,
poller wakeup, pending reply 처리, backpressure 처리처럼 수치를 못 내게 만든 구현
원인을 수정해야 한다. 같은 조건에서 수치가 나올 때까지 다음 조합이나 다음 언어로
넘어가지 않는다. 공식 판정 표에는 timeout/no result를 최종 근거로 남기지 않고,
같은 조건에서 처리량 또는 latency 숫자가 나온 뒤에 `통과`, `미달`, `보류` 중 하나로
판정한다.

public API 변경 없이는 비교 조건을 완전히 맞추기 어렵다고 보여도 바로 `보류`로
넘기지 않는다. 기존 public API 내부 구현, lifecycle/setup 순서, buffer 재사용,
callback/dispatch 경로, 불필요한 allocation/copy, poll loop를 먼저 검토하고 최소
하나 이상의 의미 보존 후보를 측정해야 한다. `보류`는 그 후보들이 실패했거나
효과가 없고, 추가로 필요한 public API 계약과 영향까지 문서에 적은 뒤에만 표시한다.

`보류`를 남기기 전에 아래 금지 사례에 걸리지 않는지 먼저 확인한다.

- `MsgUnit(B)`가 C와 다르면 `보류`가 아니라 조건 정렬 전 `미달` 또는 `미측정`이다.
- timeout은 재현 조건, server/client 로그, 같은 조건의 C 제한 결과를 확인하기 전에는
  `보류`가 아니다.
- public API 제한은 마지막 판단 근거다. 내부 구현을 적어도 한 번 이상 수정하고 같은
  조건으로 재측정하지 않았다면 `보류`가 아니라 `미달`이다.
- 절대 목표 기준을 통과한 항목은 교차 언어 비교만으로 `미달`로 내리지 않는다.
- 교차 언어 비교는 `보류` 판단을 검증하는 보조 기준으로만 사용한다. C++에서 `보류`로
  닫으려는 항목이 같은 조건의 .NET/Java보다 낮으면 보류하지 말고 내부 구현을 재검토한다.
  .NET과 Java는 같은 managed runtime 목표 그룹이므로 서로 비교한다. 한쪽이 같은 조건이나
  가까운 하위 pattern에서 뚜렷하게 더 높은 수치를 내면 다른 쪽을 `추가 내부 후보 없음`으로
  보류하지 않고 callback/dispatch, poll loop, buffer ownership, message copy, HWM 적용
  차이를 먼저 분석하고 같은 조건으로 재측정한다.
- 새 public API 후보가 기존 공개 계약 테스트에서 막힌다는 사실만으로 `보류`로 넘기지
  않는다. 기존 public API의 option 전달, typed option facade, setup 순서, 내부
  auto-HWM 전파, perf 정책 위반 가능성을 먼저 확인하고 같은 조건으로 재측정해야 한다.
- public API 변경 실험은 큰 개선 가능성이 명확할 때만 수행한다. 단순한 가능성이나
  작은 개선 예상만으로 public API 변경 실험을 하거나, 반대로 public API 제한을 이유로
  내부 개선 검토를 멈추지 않는다.

현재 언어에서 `미달` 또는 `미측정` 항목이 남아 있으면 다음 언어로 넘어가지 않는다.
`보류`는 완료가 아니지만, 더 진행하려면 public API 변경 승인이 필요한 상태로 본다.

상태 표 안에서 요약 행과 상세 행이 서로 맞지 않으면 상세 행을 우선한다. 상세 행에
`미달` 또는 `미측정`이 하나라도 남아 있으면 그 언어는 완료가 아니다. 5.1의 언어 진행
상태는 각 언어별 상세 표에서 산출한 결과여야 하며, 상세 표보다 느슨한 완료 판단을
적으면 안 된다.

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
언어별 상태 표는 모두 `Transport`, `Pattern`, `Size(B)`, `Status`, `C 대비`, `결과`
열을 사용한다. `결과` 칸에는 근거, 다음 분석 대상, 결과 파일만 적고, 진행 순서 때문에
아직 실행하지 못한 항목을 `보류`처럼 표현하지 않는다. 아직 같은 조건으로 비교하지
않았으면 `미측정`, 유효 수치가 목표보다 낮으면 `미달`로 둔다.

### 5.1 언어 진행 상태

| 순서 | 언어 | 현재 transport | 전체 상태 | 다음 작업 |
|------|------|----------------|-----------|-----------|
| 1 | C++ | `ws` | 진행 중 | SPOT 계열 `MsgUnit(B)` 정렬과 기존 public API 내부 구현 개선 가능성 재검토 |
| 2 | .NET | `tcp` | 진행 대기, 미달 있음 | C++ 미달 해소 뒤 `MULTI_SPOT`, small one-way, `PUBSUB`, `ROUTER_ROUTER` 미달을 Java와 재비교 |
| 3 | Java | `tcp` | 진행 대기, 미달 있음 | .NET 미달 해소 뒤 `MULTI_SPOT`, `wss/tls` `MULTI_SPOT_SENDSEND` 미달 재검토 |
| 4 | Node | `tcp` | 진행 대기, 미측정 있음 | Java의 `미달` 해소 후 `tcp` 미측정 조합부터 시작 |
| 5 | Rust | `tcp` | 진행 대기, 미측정 있음 | Node의 `미달`/`미측정` 해소 후 `tcp`부터 시작 |
| 6 | Go | `tcp` | 진행 대기, 미달/미측정 있음 | Rust의 `미달`/`미측정` 해소 후 `tcp` 미달 조합부터 시작 |
| 7 | Python | `tcp` | 진행 대기, 미달/미측정 있음 | Go의 `미달`/`미측정` 해소 후 `tcp` 미달 조합부터 시작 |

### 5.2 C++ 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | `MULTI_ROUTER_ROUTER` | `65536` | `미달` | `52.8%` | C++ routed echo 목표와 다른 transport 통과 수치 대비 낮다. raw revert 이후 경로를 .NET/Java와 비교해 재검토 |
| `tcp` | `MULTI_ROUTER_ROUTER` | `131072` | `통과` | `66.8%` | `perf_cpp_multi_linux_20260518_112701_codex_cpp_tcp_rr_large_local_send_msg.txt` |
| `ws` | `MULTI_DEALER_DEALER` | `64` | `미달` | `76.5%` | 같은 pattern의 256~131072는 93.5%~101.6%로 통과한다. small-message 경로를 재검토 |
| `ws` | `MULTI_DEALER_DEALER` | `256` | `통과` | `96.4%` | C current 기준 |
| `ws` | `MULTI_DEALER_DEALER` | `1024` | `통과` | `93.5%` | C current 기준 |
| `ws` | `MULTI_DEALER_DEALER` | `65536` | `통과` | `94.8%` | C current 기준 |
| `ws` | `MULTI_DEALER_DEALER` | `131072` | `통과` | `101.6%` | C current 기준 |
| `ws` | `MULTI_DEALER_DEALER` | `262144` | `미달` | `78.8%` | 같은 pattern의 중간 size는 통과한다. large-message 종료/flush/backpressure 경로를 재검토 |
| `ws` | `MULTI_DEALER_ROUTER` | `64` | `통과` | `88.1%` | C current 기준 |
| `ws` | `MULTI_DEALER_ROUTER` | `256` | `통과` | `87.7%` | C current 기준 |
| `ws` | `MULTI_DEALER_ROUTER` | `1024` | `통과` | `92.6%` | C current 기준 |
| `ws` | `MULTI_DEALER_ROUTER` | `65536` | `미달` | `59.0%` | 같은 pattern의 인접 size와 C++ 목표 대비 낮다. routed payload copy/borrow 경로를 재검토 |
| `ws` | `MULTI_DEALER_ROUTER` | `131072` | `통과` | `70.9%` | C current 기준 |
| `ws` | `MULTI_DEALER_ROUTER` | `262144` | `통과` | `66.0%` | C current 기준 |
| `ws` | `MULTI_ROUTER_ROUTER` | `64` | `통과` | `95.6%` | C current 기준 |
| `ws` | `MULTI_ROUTER_ROUTER` | `256` | `통과` | `94.7%` | C current 기준 |
| `ws` | `MULTI_ROUTER_ROUTER` | `1024` | `통과` | `93.1%` | C current 기준 |
| `ws` | `MULTI_ROUTER_ROUTER` | `65536` | `미달` | `60.3%` | 같은 pattern의 64~1024와 262144는 통과한다. routed echo 상대 기준과 size별 경로를 재검토 |
| `ws` | `MULTI_ROUTER_ROUTER` | `131072` | `미달` | `60.8%` | 같은 pattern의 64~1024와 262144는 통과한다. routed echo 상대 기준과 size별 경로를 재검토 |
| `ws` | `MULTI_ROUTER_ROUTER` | `262144` | `통과` | `100.6%` | C current 기준 |
| `ws` | `MULTI_PUBSUB` | `64` | `통과` | `95.8%` | C current 기준 |
| `ws` | `MULTI_PUBSUB` | `256` | `통과` | `98.2%` | C current 기준 |
| `ws` | `MULTI_PUBSUB` | `1024` | `통과` | `92.6%` | `perf_cpp_multi_linux_20260518_124911_codex_cpp_ws_pubsub_1024_debug.txt` |
| `ws` | `MULTI_PUBSUB` | `65536,131072,262144` | `통과` | `87.6%~113.9%` | `perf_cpp_multi_linux_20260518_124924_codex_cpp_ws_pubsub_large_recheck.txt` |
| `ws` | `MULTI_SPOT` | 전체 대상 | `미달` | - | `MsgUnit(B)` 정렬 전 판정. 공개 API 추가로 보류하지 말고 기존 public API option 전달, SpotNode/Spot handle setup, 내부 auto-HWM 전파를 먼저 분석하고 재측정 |
| `ws` | `MULTI_SPOT_REQREP` | 전체 대상 | `미달` | - | `MsgUnit(B)` 정렬 전 판정. 공개 API 추가로 보류하지 말고 기존 public API option 전달, SpotNode/Spot handle setup, 내부 auto-HWM 전파를 먼저 분석하고 재측정 |
| `ws` | `MULTI_SPOT_SENDSEND` | 전체 대상 | `미달` | - | `MsgUnit(B)` 정렬 전 판정. 공개 API 추가로 보류하지 말고 기존 public API option 전달, SpotNode/Spot handle setup, 내부 auto-HWM 전파를 먼저 분석하고 재측정 |
| `ws` | `MULTI_STREAM` | `64,256,1024,65536` | `통과` | `80.3%~98.9%` | `perf_cpp_multi_linux_20260518_124314_codex_cpp_ws_full_status.txt` |
| `wss` | `MULTI_DEALER_DEALER` | `64` | `미달` | `73.7%` | 같은 pattern의 256~262144는 통과한다. small-message WSS 경로를 재검토 |
| `wss` | `MULTI_DEALER_DEALER` | `256` | `통과` | `99.7%` | 제한 C: `perf_c_multi_linux_20260518_133226_codex_c_wss_dd_recheck_compare.txt` |
| `wss` | `MULTI_DEALER_DEALER` | `1024,65536` | `통과` | `83.7%~88.2%` | `perf_cpp_multi_linux_20260518_132049_codex_cpp_wss_full_status.txt` |
| `wss` | `MULTI_DEALER_DEALER` | `131072` | `통과` | `92.6%` | 제한 C: `perf_c_multi_linux_20260518_133226_codex_c_wss_dd_recheck_compare.txt` |
| `wss` | `MULTI_DEALER_DEALER` | `262144` | `통과` | - | stop token round-robin 종료 수정 후 complete: `perf_cpp_multi_linux_20260518_201313_codex_cpp_wss_dd262144_stop_roundrobin.txt` |
| `wss` | `MULTI_DEALER_ROUTER` | `64,256,1024,65536,131072,262144` | `통과` | `84.4%~95.5%` | `perf_cpp_multi_linux_20260518_132049_codex_cpp_wss_full_status.txt` |
| `wss` | `MULTI_ROUTER_ROUTER` | `64,256,1024,65536,131072,262144` | `통과` | `83.0%~93.5%` | `perf_cpp_multi_linux_20260518_132049_codex_cpp_wss_full_status.txt` |
| `wss` | `MULTI_PUBSUB` | `64,1024` | `통과` | `89.4%~104.3%` | 제한 C: `perf_c_multi_linux_20260518_133255_codex_c_wss_pubsub_recheck_compare.txt` |
| `wss` | `MULTI_PUBSUB` | `256` | `미달` | `78.3%` | 같은 pattern의 64/1024/131072/262144는 통과한다. subscribe hot path를 public API 변경 전 내부 구현으로 재검토 |
| `wss` | `MULTI_PUBSUB` | `65536` | `미달` | `67.2%` | 같은 pattern의 64/1024/131072/262144는 통과한다. subscribe hot path를 public API 변경 전 내부 구현으로 재검토 |
| `wss` | `MULTI_PUBSUB` | `131072,262144` | `통과` | - | client active deadline 종료 수정 후 complete: `perf_cpp_multi_linux_20260518_201950_codex_cpp_wss_pubsub_large_deadline_exit.txt` |
| `wss` | `MULTI_SPOT` | 전체 대상 | `미달` | - | `MsgUnit(B)` 정렬 전 판정. 공개 API 추가로 보류하지 말고 기존 public API option 전달, SpotNode/Spot handle setup, 내부 auto-HWM 전파를 먼저 분석하고 재측정 |
| `wss` | `MULTI_SPOT_REQREP` | 전체 대상 | `미달` | - | `MsgUnit(B)` 정렬 전 판정. 공개 API 추가로 보류하지 말고 기존 public API option 전달, SpotNode/Spot handle setup, 내부 auto-HWM 전파를 먼저 분석하고 재측정 |
| `wss` | `MULTI_SPOT_SENDSEND` | 전체 대상 | `미달` | - | `MsgUnit(B)` 정렬 전 판정. 공개 API 추가로 보류하지 말고 기존 public API option 전달, SpotNode/Spot handle setup, 내부 auto-HWM 전파를 먼저 분석하고 재측정 |
| `wss` | `MULTI_STREAM` | `64,256,1024,65536` | `통과` | `90.0%~100.1%` | `perf_cpp_multi_linux_20260518_132049_codex_cpp_wss_full_status.txt` |
| `tls` | `MULTI_DEALER_DEALER` | `64` | `미달` | `75.5%` | 같은 pattern의 256~262144는 통과한다. small-message TLS 경로를 재검토 |
| `tls` | `MULTI_DEALER_DEALER` | `256,1024,65536` | `통과` | `83.4%~88.4%` | `perf_cpp_multi_linux_20260518_133640_codex_cpp_tls_full_status.txt` |
| `tls` | `MULTI_DEALER_DEALER` | `131072` | `미달` | `51.6%` | 같은 pattern의 256/1024/65536/262144는 통과한다. large-message TLS 경로를 재검토 |
| `tls` | `MULTI_DEALER_DEALER` | `262144` | `통과` | - | stop token round-robin 종료 수정 후 complete: `perf_cpp_multi_linux_20260518_201325_codex_cpp_tls_dd262144_stop_roundrobin.txt` |
| `tls` | `MULTI_DEALER_ROUTER` | `64,256,1024,65536,131072` | `통과` | `83.6%~89.1%` | `perf_cpp_multi_linux_20260518_133640_codex_cpp_tls_full_status.txt` |
| `tls` | `MULTI_DEALER_ROUTER` | `262144` | `통과` | `88.2%` | 제한 C: `perf_c_multi_linux_20260518_141339_codex_c_tls_dr_262_recheck_compare.txt` |
| `tls` | `MULTI_ROUTER_ROUTER` | `64,256,1024,65536,131072` | `통과` | `89.3%~94.8%` | `perf_cpp_multi_linux_20260518_133640_codex_cpp_tls_full_status.txt` |
| `tls` | `MULTI_ROUTER_ROUTER` | `262144` | `통과` | `112.5%` | 제한 C: `perf_c_multi_linux_20260518_141403_codex_c_tls_rr_262_recheck_compare.txt` |
| `tls` | `MULTI_PUBSUB` | `64,256,1024` | `통과` | `80.2%~85.5%` | 제한 C와 full 기준 |
| `tls` | `MULTI_PUBSUB` | `65536` | `미달` | `70.2%` | 같은 pattern의 64/256/1024/131072/262144는 통과한다. subscribe hot path를 public API 변경 전 내부 구현으로 재검토 |
| `tls` | `MULTI_PUBSUB` | `131072` | `통과` | `97.8%` | 제한 C: `perf_c_multi_linux_20260518_140850_codex_c_tls_pubsub_recheck_compare.txt` |
| `tls` | `MULTI_PUBSUB` | `262144` | `통과` | - | client active deadline 종료 수정 후 complete: `perf_cpp_multi_linux_20260518_202011_codex_cpp_tls_pubsub262144_deadline_exit.txt` |
| `tls` | `MULTI_SPOT` | 전체 대상 | `미달` | - | `MsgUnit(B)` 정렬 전 판정. 공개 API 추가로 보류하지 말고 기존 public API option 전달, SpotNode/Spot handle setup, 내부 auto-HWM 전파를 먼저 분석하고 재측정 |
| `tls` | `MULTI_SPOT_REQREP` | 전체 대상 | `미달` | - | `MsgUnit(B)` 정렬 전 판정. 공개 API 추가로 보류하지 말고 기존 public API option 전달, SpotNode/Spot handle setup, 내부 auto-HWM 전파를 먼저 분석하고 재측정 |
| `tls` | `MULTI_SPOT_SENDSEND` | 전체 대상 | `미달` | - | `MsgUnit(B)` 정렬 전 판정. 공개 API 추가로 보류하지 말고 기존 public API option 전달, SpotNode/Spot handle setup, 내부 auto-HWM 전파를 먼저 분석하고 재측정 |
| `tls` | `MULTI_STREAM` | `64,256,1024,65536` | `통과` | `94.8%~103.2%` | `perf_cpp_multi_linux_20260518_133640_codex_cpp_tls_full_status.txt` |

### 5.3 .NET 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | `MULTI_DEALER_DEALER` | `64` | `미달` | `53.7%` | Java 같은 one-way small-message 수치와 차이가 크다. builder 이후 send/receive allocation 경로 재검토 |
| `tcp` | `MULTI_DEALER_DEALER` | `256` | `미달` | `62.5%` | Java 같은 one-way small-message 수치와 차이가 크다. builder 이후 send/receive allocation 경로 재검토 |
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
| `tcp` | `MULTI_PUBSUB` | `262144` | `통과` | `172.5%` | timeout 재현 후 재측정 complete: `perf_dotnet_multi_linux_20260518_185545_codex_dotnet_pubsub262144_recheck.txt` |
| `tcp` | `MULTI_SPOT` | `64` | `미달` | `51.5%` | `REQREP`/`SENDSEND`는 통과하므로 같은 SPOT 내부 구현 비교가 필요하다. publish/subscribe callback, poll loop, buffer ownership, message copy 차이를 분석하고 재측정 |
| `tcp` | `MULTI_SPOT` | `256` | `미달` | `38.4%` | `REQREP`/`SENDSEND`는 통과하므로 같은 SPOT 내부 구현 비교가 필요하다. publish/subscribe callback, poll loop, buffer ownership, message copy 차이를 분석하고 재측정 |
| `tcp` | `MULTI_SPOT` | `1024` | `미달` | `49.9%` | `REQREP`/`SENDSEND`는 통과하므로 같은 SPOT 내부 구현 비교가 필요하다. publish/subscribe callback, poll loop, buffer ownership, message copy 차이를 분석하고 재측정 |
| `tcp` | `MULTI_SPOT` | `65536` | `미달` | `35.8%` | `REQREP`/`SENDSEND`는 통과하므로 같은 SPOT 내부 구현 비교가 필요하다. publish/subscribe callback, poll loop, buffer ownership, message copy 차이를 분석하고 재측정 |
| `tcp` | `MULTI_SPOT` | `131072` | `미달` | `30.0%` | `REQREP`/`SENDSEND`는 통과하므로 같은 SPOT 내부 구현 비교가 필요하다. publish/subscribe callback, poll loop, buffer ownership, message copy 차이를 분석하고 재측정 |
| `tcp` | `MULTI_SPOT` | `262144` | `미달` | `23.3%` | `REQREP`/`SENDSEND`는 통과하므로 같은 SPOT 내부 구현 비교가 필요하다. publish/subscribe callback, poll loop, buffer ownership, message copy 차이를 분석하고 재측정 |
| `tcp` | `MULTI_SPOT_REQREP` | `64` | `통과` | `70.6%` | direct callback, progress pump keepalive, `WrapBytes`: `perf_dotnet_multi_linux_20260518_192515_codex_dotnet_reqrep_wrapbytes_small_recheck.txt` |
| `tcp` | `MULTI_SPOT_REQREP` | `256` | `통과` | `63.6%` | direct callback, progress pump keepalive, `WrapBytes` |
| `tcp` | `MULTI_SPOT_REQREP` | `1024` | `통과` | `64.1%` | callback fallback scan 제거 후 통과: `perf_dotnet_multi_linux_20260518_194125_codex_dotnet_reqrep1024_no_fallback_scan.txt` |
| `tcp` | `MULTI_SPOT_REQREP` | `65536` | `통과` | `64.7%` | HWM 안정 active slot과 blocking reply 정렬: `perf_dotnet_multi_linux_20260518_192658_codex_dotnet_reqrep_wrapbytes_large_final.txt` |
| `tcp` | `MULTI_SPOT_REQREP` | `131072` | `통과` | `103.4%` | HWM cap active slot 적용 후 complete |
| `tcp` | `MULTI_SPOT_REQREP` | `262144` | `통과` | `117.1%` | HWM cap active slot 적용 후 complete |
| `tcp` | `MULTI_SPOT_SENDSEND` | `64` | `통과` | `69.5%` | 제한 재측정 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `256` | `통과` | `72.7%` | 제한 재측정 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `1024` | `통과` | `72.9%` | 제한 재측정 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `65536` | `통과` | `93.1%` | in-flight HWM 정렬 후 timeout 해소: `perf_dotnet_multi_linux_20260518_185532_codex_dotnet_sendsend65536_half_hwm_slots.txt` |
| `tcp` | `MULTI_SPOT_SENDSEND` | `131072` | `통과` | `91.3%` | full tcp |
| `tcp` | `MULTI_SPOT_SENDSEND` | `262144` | `통과` | `156.3%` | full tcp와 제한 재측정 기준 통과 |
| `tcp` | `MULTI_STREAM` | `64` | `통과` | `91.8%` | full tcp |
| `tcp` | `MULTI_STREAM` | `256` | `통과` | `86.4%` | full tcp |
| `tcp` | `MULTI_STREAM` | `1024` | `통과` | `84.0%` | full tcp |
| `tcp` | `MULTI_STREAM` | `65536` | `통과` | `104.1%` | full tcp |
| `ws` | `MULTI_DEALER_DEALER` | `64` | `미달` | `59.8%` | Java 같은 pattern과 비교해 builder 이후 send/receive allocation 경로 재검토 |
| `ws` | `MULTI_DEALER_DEALER` | `256` | `미달` | `55.7%` | Java 같은 pattern과 비교해 builder 이후 send/receive allocation 경로 재검토 |
| `ws` | `MULTI_DEALER_DEALER` | `1024` | `통과` | `67.5%` | full ws |
| `ws` | `MULTI_DEALER_DEALER` | `65536` | `통과` | `85.5%` | full ws |
| `ws` | `MULTI_DEALER_DEALER` | `131072` | `미달` | `57.0%` | 같은 pattern의 65536/262144와 Java 수치 대비 낮은지 size별 buffer/backpressure 경로 재검토 |
| `ws` | `MULTI_DEALER_DEALER` | `262144` | `통과` | `80.5%` | full ws |
| `ws` | `MULTI_DEALER_ROUTER` | `64,256,1024,65536,131072,262144` | `통과` | `51.1%~95.3%` | full ws |
| `ws` | `MULTI_ROUTER_ROUTER` | `64,256,1024,65536` | `미달` | `47.1%~48.3%` | .NET/Java 같은 managed 목표 그룹 안에서 router-router dispatch와 payload 경로 재검토 |
| `ws` | `MULTI_ROUTER_ROUTER` | `131072,262144` | `통과` | `66.1%~82.8%` | full ws |
| `ws` | `MULTI_PUBSUB` | `64,256,1024` | `미달` | `42.9%~56.9%` | Java PUBSUB 대비 낮은지 확인하고 subscribe callback, receive allocation, poll loop 재검토 |
| `ws` | `MULTI_PUBSUB` | `65536,131072,262144` | `통과` | `64.7%~73.8%` | full ws |
| `ws` | `MULTI_SPOT` | `64,256,1024,65536,131072,262144` | `미달` | `33.7%~51.6%` | `REQREP`/`SENDSEND`와 Java 구현 대비 낮다. publish/subscribe hot path 내부 차이를 분석하고 재측정 |
| `ws` | `MULTI_SPOT_REQREP` | `64,256,1024,131072` | `통과` | `73.9%~98.5%` | 제한 C 기준 |
| `ws` | `MULTI_SPOT_REQREP` | `65536,262144` | `통과` | `66.6%~79.2%` | timeout 원인 해소 후 complete: `perf_dotnet_multi_linux_20260518_202052_codex_dotnet_ws_reqrep_large_nooutput_recheck.txt` |
| `ws` | `MULTI_SPOT_SENDSEND` | `64,256,1024,131072` | `통과` | `63.5%~94.4%` | 제한 C 기준 |
| `ws` | `MULTI_SPOT_SENDSEND` | `65536,262144` | `통과` | `70.8%~88.0%` | timeout 원인 해소 후 complete, 262144는 제한 C 재측정 기준: `perf_dotnet_multi_linux_20260518_202112_codex_dotnet_ws_sendsend_large_nooutput_recheck.txt` |
| `ws` | `MULTI_STREAM` | `64,256,1024,65536` | `통과` | `89.2%~96.7%` | full ws |
| `wss` | `MULTI_DEALER_DEALER` | `64,256` | `미달` | `55.7%~60.4%` | Java 같은 pattern과 비교해 small-message WSS send/receive 경로 재검토 |
| `wss` | `MULTI_DEALER_DEALER` | `1024,65536,131072,262144` | `통과` | `73.1%~93.8%` | full wss |
| `wss` | `MULTI_DEALER_ROUTER` | 전체 대상 | `통과` | `54.8%~95.4%` | full wss |
| `wss` | `MULTI_ROUTER_ROUTER` | 전체 대상 | `통과` | `50.4%~93.6%` | full wss, 상대 기준 허용 범위 |
| `wss` | `MULTI_PUBSUB` | `64,256,1024` | `미달` | `42.8%~61.4%` | Java PUBSUB 대비 낮은지 확인하고 subscribe callback, receive allocation, poll loop 재검토 |
| `wss` | `MULTI_PUBSUB` | `65536,131072,262144` | `통과` | `73.7%~92.8%` | full wss |
| `wss` | `MULTI_SPOT` | `64,262144` | `미달` | `47.2%~48.4%` | `REQREP`/`SENDSEND`와 Java 구현 대비 낮다. publish/subscribe hot path 내부 차이를 분석하고 재측정 |
| `wss` | `MULTI_SPOT` | `256,1024,65536,131072` | `통과` | `103.5%~303.9%` | 제한 C 기준 |
| `wss` | `MULTI_SPOT_REQREP` | `64,256,1024,131072,262144` | `통과` | `70.2%~96.0%` | 제한 C 기준 |
| `wss` | `MULTI_SPOT_REQREP` | `65536` | `통과` | `93.2%` | timeout 원인 해소 후 complete: `perf_dotnet_multi_linux_20260518_202129_codex_dotnet_wss_reqrep65536_nooutput_recheck.txt` |
| `wss` | `MULTI_SPOT_SENDSEND` | `64,256,1024` | `통과` | `64.5%~69.2%` | 제한 C 기준 |
| `wss` | `MULTI_SPOT_SENDSEND` | `65536,131072,262144` | `통과` | `90.8%~91.1%` | timeout 원인 해소 후 complete: `perf_dotnet_multi_linux_20260518_202141_codex_dotnet_wss_sendsend_large_nooutput_recheck.txt` |
| `wss` | `MULTI_STREAM` | `64,256,1024,65536` | `통과` | `86.7%~91.9%` | full wss |
| `tls` | `MULTI_DEALER_DEALER` | `64,256` | `미달` | `54.1%~60.2%` | Java 같은 pattern과 비교해 small-message TLS send/receive 경로 재검토 |
| `tls` | `MULTI_DEALER_DEALER` | `1024,65536,131072,262144` | `통과` | `67.2%~85.3%` | full tls |
| `tls` | `MULTI_DEALER_ROUTER` | 전체 대상 | `통과` | `53.7%~91.6%` | full tls |
| `tls` | `MULTI_ROUTER_ROUTER` | `64,256,1024` | `미달` | `45.9%~48.8%` | .NET/Java 같은 managed 목표 그룹 안에서 router-router dispatch와 payload 경로 재검토 |
| `tls` | `MULTI_ROUTER_ROUTER` | `65536,131072,262144` | `통과` | `86.8%~96.1%` | full tls |
| `tls` | `MULTI_PUBSUB` | `64,256,1024,65536,262144` | `미달` | `38.7%~62.4%` | Java PUBSUB 대비 낮은지 확인하고 subscribe callback, receive allocation, poll loop 재검토 |
| `tls` | `MULTI_PUBSUB` | `131072` | `통과` | `85.7%` | full tls |
| `tls` | `MULTI_SPOT` | `64,65536,131072,262144` | `미달` | `43.2%~48.9%` | `REQREP`/`SENDSEND`와 Java 구현 대비 낮다. publish/subscribe hot path 내부 차이를 분석하고 재측정 |
| `tls` | `MULTI_SPOT` | `256,1024` | `통과` | `61.6%~77.4%` | 제한 C 기준 |
| `tls` | `MULTI_SPOT_REQREP` | `64,256,1024,262144` | `통과` | `63.5%~96.7%` | 제한 C 기준 |
| `tls` | `MULTI_SPOT_REQREP` | `65536,131072` | `통과` | `87.4%~92.2%` | timeout 원인 해소 후 complete: `perf_dotnet_multi_linux_20260518_202204_codex_dotnet_tls_reqrep_large_nooutput_recheck.txt` |
| `tls` | `MULTI_SPOT_SENDSEND` | `64,256,1024,131072,262144` | `통과` | `63.1%~295.0%` | 제한 C 기준 |
| `tls` | `MULTI_SPOT_SENDSEND` | `65536` | `통과` | `85.2%` | timeout 원인 해소 후 complete: `perf_dotnet_multi_linux_20260518_202221_codex_dotnet_tls_sendsend65536_nooutput_recheck.txt` |
| `tls` | `MULTI_STREAM` | `64,256,1024,65536` | `통과` | `83.5%~94.9%` | 제한 tail 측정 |

### 5.4 Java 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | `MULTI_DEALER_DEALER` | 전체 대상 | `통과` | `68.2%~92.7%` | `perf_java_multi_linux_20260518_160351_codex_java_tcp_full_status.txt` |
| `tcp` | `MULTI_DEALER_ROUTER` | 전체 대상 | `통과` | `55.3%~87.0%` | 1024 timeout 재현 없음: `perf_java_multi_linux_20260518_195221_codex_java_dealer_router_small_after_spot_internal.txt` |
| `tcp` | `MULTI_ROUTER_ROUTER` | `64,256,1024` | `통과` | `62.9%~64.3%` | 절대 목표 기준 통과 |
| `tcp` | `MULTI_ROUTER_ROUTER` | `65536,131072,262144` | `통과` | `54.5%~106.6%` | 상대 기준 허용 범위 |
| `tcp` | `MULTI_PUBSUB` | 전체 대상 | `통과` | `90.6%~256.7%` | 최신 full tcp |
| `tcp` | `MULTI_SPOT` | `64,256,1024,65536,131072,262144` | `미달` | `32.8%~51.4%` | `REQREP`/`SENDSEND`는 통과하므로 같은 SPOT publish/subscribe 내부 구현 차이를 .NET과 재검토 |
| `tcp` | `MULTI_SPOT_REQREP` | `64,256,1024` | `통과` | `67.3%~84.9%` | progress pump 재사용과 단일 submit loop 후 통과 |
| `tcp` | `MULTI_SPOT_REQREP` | `65536,131072,262144` | `통과` | `77.3%~107.7%` | 최신 full tcp 기준 timeout 없이 통과 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `64,256,1024` | `통과` | `75.6%~80.9%` | 단일 poll loop와 `MsgUnit(B)` 정렬 후 제한 C 기준 통과 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `65536,131072,262144` | `통과` | `81.1%~131.3%` | HWM 기반 in-flight 제한과 active stop count 정렬 후 complete |
| `tcp` | `MULTI_STREAM` | 전체 대상 | `통과` | `102.5%~135.1%` | 최신 full tcp |
| `ws` | `MULTI_SPOT_SENDSEND` | `65536,262144` | `통과` | `72.0%~95.3%` | copy 제거, `System.nanoTime()` 적용, active slot HWM 정렬. C 제한: `perf_c_multi_linux_20260518_205954_codex_c_ws_sendsend_large_java_compare.txt`, Java: `perf_java_multi_linux_20260518_210350_codex_java_ws_sendsend_large_final_nano.txt` |
| `ws` | `MULTI_SPOT_SENDSEND` | `131072` | `통과` | `68.5%` | size 단독 제한 측정 기준: `perf_java_multi_linux_20260518_210336_codex_java_ws_sendsend131072_nano_time.txt` |
| `ws` | 그 외 미출력 조합 | 해당 size | `통과` | - | 제한 재측정 complete: `perf_java_multi_linux_20260518_203045_codex_java_ws_dd1024_isolate_before_fix.txt`, `perf_java_multi_linux_20260518_203142_codex_java_ws_pubsub262144_deadline_exit.txt`, `perf_java_multi_linux_20260518_203213_codex_java_ws_reqrep64_debug.txt` |
| `wss` | `MULTI_SPOT_SENDSEND` | `65536` | `통과` | `87.6%` | `perf_java_multi_linux_20260518_210512_codex_java_wss_sendsend65536_final_nano.txt` |
| `wss` | `MULTI_SPOT_SENDSEND` | `131072` | `미달` | `43.6%` | timeout은 stdin reader 공유로 해소. active slot/public `wrapDirect` 실험 효과 없음. C 제한: `perf_c_multi_linux_20260518_210806_codex_c_wss_sendsend131072_java_compare.txt`, Java best: `perf_java_multi_linux_20260518_210732_codex_java_wss_sendsend131072_slots_2_probe.txt` |
| `wss` | `MULTI_SPOT_SENDSEND` | `262144` | `미달` | `0.0%~90.8%` | 최신 단독 재측정 필요. 이전 full은 미달 수치, .NET은 통과하므로 Java 내부 경로 계속 분석 |
| `wss` | 그 외 미출력 조합 | 해당 size | `통과` | - | 제한 재측정 complete: `perf_java_multi_linux_20260518_204413_codex_java_wss_pubsub131072_isolate.txt`, `perf_java_multi_linux_20260518_204422_codex_java_wss_sendsend_small_isolate.txt`, `perf_java_multi_linux_20260518_204519_codex_java_wss_sendsend1024_recheck_after_debug.txt` |
| `tls` | `MULTI_SPOT_SENDSEND` | `65536` | `통과` | `86.8%` | `perf_java_multi_linux_20260518_210940_codex_java_tls_sendsend65536_final_nano.txt` |
| `tls` | `MULTI_SPOT_SENDSEND` | `131072,262144` | `미달` | `11.1%~17.6%` | `perf_java_multi_linux_20260518_210951_codex_java_tls_sendsend131072_final_nano.txt`, `perf_java_multi_linux_20260518_211000_codex_java_tls_sendsend262144_final_nano.txt` |
| `tls` | `MULTI_ROUTER_ROUTER` | `131072,262144` | `통과` | - | server active deadline 종료 수정 후 complete: `perf_java_multi_linux_20260518_205313_codex_java_tls_rr_large_deadline_exit.txt` |
| `tls` | 그 외 대상 | 해당 size | `통과` | - | full tls와 제한 재측정 기준 timeout/no-result 없음 |

### 5.5 Node 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | `PUBSUB` | `64` | `통과` | `37.06%` | `perf_node_single_linux_20260518_111604.txt` |
| `tcp` | `PUBSUB` | `256` | `통과` | `36.30%` | `perf_node_single_linux_20260518_111503.txt` |
| `tcp` | 그 외 대상 | 전체 대상 | `미측정` | - | 진행 순서 도달 후 `tcp`에서 먼저 측정 |
| `ws` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미측정/미달 해소 후 측정 |
| `wss` | 전체 대상 | 전체 대상 | `미측정` | - | `ws` 미측정/미달 해소 후 측정 |
| `tls` | 전체 대상 | 전체 대상 | `미측정` | - | `wss` 미측정/미달 해소 후 측정 |

### 5.6 Rust 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | 전체 대상 | 전체 대상 | `미측정` | - | 진행 순서 도달 후 `tcp`에서 먼저 측정 |
| `ws` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미측정/미달 해소 후 측정 |
| `wss` | 전체 대상 | 전체 대상 | `미측정` | - | `ws` 미측정/미달 해소 후 측정 |
| `tls` | 전체 대상 | 전체 대상 | `미측정` | - | `wss` 미측정/미달 해소 후 측정 |

### 5.7 Go 상태

| Transport | Pattern | Size(B) | Status | C 대비 | 결과 |
|-----------|---------|---------|--------|--------|------|
| `tcp` | `PAIR` | `64` | `통과` | `79.80%` | `perf_go_single_linux_20260518_115650_codex_go_tcp64_single_recv_into.txt` |
| `tcp` | `DEALER_DEALER` | `64` | `통과` | `78.05%` | `perf_go_single_linux_20260518_115650_codex_go_tcp64_single_recv_into.txt` |
| `tcp` | `DEALER_ROUTER` | `64` | `미달` | `45.12%` | `perf_go_single_linux_20260518_115650_codex_go_tcp64_single_recv_into.txt` |
| `tcp` | `ROUTER_ROUTER` | `64` | `통과` | `50.49%` | `perf_go_single_linux_20260518_115650_codex_go_tcp64_single_recv_into.txt` |
| `tcp` | `PUBSUB` | `64` | `미달` | `9.78%` | `perf_go_single_linux_20260518_120037_codex_go_tcp64_pubsub_adopt_recv.txt` |
| `tcp` | `SPOT` | `64` | `미달` | `29.65%` | `perf_go_single_linux_20260518_115650_codex_go_tcp64_single_recv_into.txt` |
| `tcp` | 그 외 대상 | 전체 대상 | `미측정` | - | 진행 순서 도달 후 `tcp` 미달 조합과 함께 측정 |
| `ws` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미측정/미달 해소 후 측정 |
| `wss` | 전체 대상 | 전체 대상 | `미측정` | - | `ws` 미측정/미달 해소 후 측정 |
| `tls` | 전체 대상 | 전체 대상 | `미측정` | - | `wss` 미측정/미달 해소 후 측정 |

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
| `tcp` | 그 외 대상 | 전체 대상 | `미측정` | - | 진행 순서 도달 후 `tcp` 미달 조합과 함께 측정 |
| `ws` | 전체 대상 | 전체 대상 | `미측정` | - | `tcp` 미측정/미달 해소 후 측정 |
| `wss` | 전체 대상 | 전체 대상 | `미측정` | - | `ws` 미측정/미달 해소 후 측정 |
| `tls` | 전체 대상 | 전체 대상 | `미측정` | - | `wss` 미측정/미달 해소 후 측정 |

## 6. 완료 기준

아래 조건을 모두 만족하면 해당 언어 binding 작업을 완료한다.

- single과 multi의 대상 조합이 모두 목표 비율 이상이다.
- 상세 상태 표에 `미측정` 또는 `미달`이 하나도 남아 있지 않다.
- perf 결과가 `doc/perf` 정책과 `bindings/c/perf` 의미를 유지한다.
- perf 코드를 수정했다면 버그 또는 정책 위반 근거가 남아 있다.
- binding 라이브러리 변경에 필요한 테스트가 통과한다.
- 실행 중 발견된 이슈가 모두 리뷰되었고, 필요한 테스트와 수정이 끝났다.
- 이 문서가 실제 실행 절차와 판단 기준을 최신 상태로 반영한다.
- 결과 파일 경로와 C 대비 비율 요약이 최종 보고에 포함된다.

모든 대상 언어가 완료되면 최종 요약에는 언어별 최저 비율, 남은 예외, 수정한 파일,
실행한 perf 명령을 함께 기록한다.
