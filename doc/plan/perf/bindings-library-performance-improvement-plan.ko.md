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
| 5 | Go | `bindings/go/perf` |
| 6 | Rust | `bindings/rust/perf` |
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
  pattern, message size, duration, timeout, socket buffer, borrow/copy 정책은
  C와 대상 binding이 같은 조건일 때만 비교 근거로 사용한다. HWM 관련 확인은
  auto-HWM 활성 여부와 size별 `MsgUnit(B)` 일치 여부로 제한한다.
- binding perf hot path는 해당 언어의 public API를 사용해야 한다.
- 내부 API, private API, native helper, C API 직접 호출로 수치를 만드는 방식은
  인정하지 않는다.
- 수치 달성만을 위해 perf 전용 public API나 zero-copy 우회 API를 추가하지 않는다.
- public API 계약이 잘못 구현되었거나 C public API가 제공하는 필수 계약이 binding
  public API에 빠진 것이 확인되면, 해당 public API 추가나 수정을 금지하지 않는다.
  이 경우 누락이나 오구현 자체가 수정 근거다.
- public API 추가나 수정이 필요하면 먼저 어떤 C 공개 계약을 감싸는지, 어떤 binding
  계약이 빠졌거나 잘못되었는지, 필요한 회귀/API 테스트가 무엇인지 계획 문서에 적고
  같은 언어 작업 안에서 테스트와 구현을 진행한다. C API에 없는 새 의미를 만드는
  경우에만 별도 draft/spec 검토 대상으로 분리한다.
- C public API와 무관한 새 인터페이스는 성능 목표만으로 만들지 않는다. 이 경우 성능
  미달은 기존 public API 내부 구현을 개선해서 먼저 해결해야 하며, 공개 인터페이스
  변경의 근거가 될 수 없다.
- .NET 이후 언어에서도 public API 변경은 최후 수단이다. 기존 public API 내부 최적화
  후보를 먼저 모두 검토하고, 큰 개선 가능성이 명확하지 않으면 public API 변경
  프로토타입도 만들지 않는다. 테스트 의미가 달라지는 실험이나 큰 개선 가능성이 낮은
  인터페이스 변경 실험은 하지 않는다. public API 변경 후보가 목표 달성에 의미 있는
  개선을 만들 가능성이 높다고 판단되는 경우에만 제한 프로토타입으로 확인한다.
  프로토타입 변경은 측정 직후 원복해야 하며, 개선이 확인된 경우에는 필요한 계약,
  예상 호출 방식, 영향 범위, 측정 결과를 문서에 남긴 뒤 정식 API 작업으로 분리한다.
- 버그가 확인되면 perf에서 우회하지 않고 버그를 먼저 수정한다.
- 버그 수정 전에는 해당 동작을 재현하는 회귀테스트를 먼저 작성한다.
- binding 버그이면 해당 언어 binding 라이브러리에서 수정한다.
- core 버그이면 core에서 수정한 뒤
  `/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh`로 bindings에 local
  core library를 다시 배포한다.
- core public API를 새로 추가하거나 수정하는 항목은 core 구현과 core 테스트를 먼저
  완료한다. 그 다음 `/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh`를
  실행해 bindings local core library와 vendored C header를 갱신하고, 그 뒤에 각 binding
  코드를 수정한다.
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
timeout, socket buffer 설정이 같은지 확인해야 한다. HWM은 numeric `SNDHWM`/`RCVHWM`
값을 통과 기준이나 튜닝 대상으로 삼지 않는다. auto-HWM 활성 여부와
`Auto-HWM Detail`, `Auto-HWM spotnode`, `Auto-HWM spot handles`에 보이는 모든
`MsgUnit(B)`가 해당 message size와 같은지만 확인한다. 예를 들어 64B 테스트에서
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

언어별 작업은 한 번에 한 언어만 진행한다. 진행 순서는 C++, .NET, Java, Node, Go,
Rust, Python이다. 현재 언어의 모든 대상 transport, pattern, size가 `통과` 또는 `보류`
상태가 되기 전에는 다음 언어로 넘어가지 않는다. `미달` 또는 `미측정` 항목이 하나라도
남아 있으면 현재 언어 작업을 계속한다.

공식 perf 실행은 기본적으로 하나만 실행한다. 측정 오차 확인을 위해 C와 대상 binding을
제한 재측정해야 할 때도 전체 공식 perf 실행 수는 두 개를 넘기지 않는다. 같은 suite,
pattern, transport, message size 조합을 중복으로 동시에 실행하지 않는다.

## 4. 직접 진행 절차

이 작업은 측정, 병목 분석, 코드 수정, 재측정, 문서 갱신을 직접 수행한다.

상태 값은 아래 네 가지로만 기록한다. C 대비 비율을 계산한 측정치는 상태만 쓰지 않고
`통과(85%)`, `미달(72%)`, `보류(30%)`처럼 상태와 비율을 한 칸에 함께 적는다.
아직 측정하지 않은 칸은 `미측정`으로 두고, 정책상 측정 대상이 아닌 칸은 `해당 없음`으로
둔다.

- `미측정`: 아직 같은 조건의 C 기준과 대상 binding 결과를 비교하지 않았다.
- `통과`: 목표 비율, 상대 기준, latency 조건, `Effective Options`, `MsgUnit(B)` 조건을
  모두 만족한다.
- `미달`: 유효 비교에서 목표를 만족하지 못했고, 아직 내부 개선 후보를 더 확인해야 한다.
- `보류`: 유효 비교에서 목표 미달이지만, public API 변경 없이 가능한 내부 개선 후보를
  더 찾지 못했다. 보류 항목은 추가 또는 수정이 필요한 public API와 근거를 함께
  기록해야 한다.

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
같은 조건에서 처리량 또는 latency 숫자가 나온 뒤에 `통과(비율%)`, `미달(비율%)`,
`보류(비율%)` 중 하나로 판정한다.

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
`보류`는 완료가 아니며, public API 추가/수정 항목이 남아 별도 구현 단위로 이어가야
하는 상태로 본다. C API에 이미 있는 계약을 binding public API가 빠뜨린 경우에는
대기하지 말고 public API 추가/수정 대상으로 기록한 뒤 회귀/API 테스트부터 작성한다.

상태 표 안에서 요약 행과 상세 행이 서로 맞지 않으면 상세 행을 우선한다. 상세 행에
`미달` 또는 `미측정`이 하나라도 남아 있으면 그 언어는 완료가 아니다. 6.1의 언어 진행
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

## 5. Public API 확인 기준

이 섹션은 새 측정 라운드에서 public API 문제를 판정하는 기준만 둔다. 이미 코드에
반영된 작업은 추가/수정 대상 목록에서 제거한다. 상태표의 `미측정` 칸을 채우다가 병목이
나오면 먼저 같은 조건의 C perf와 비교하고, 내부 구현이나 perf 사용 경로를 고친 뒤에도
C 공개 계약과 같은 의미를 binding public API로 표현할 수 없을 때만 새 public API 항목으로
기록한다.

### 5.1 이미 반영된 항목

context-level auto-HWM message unit option은 현재 코드에 반영된 항목이다. 따라서 이
문서에서는 더 이상 별도 rollout 작업으로 추적하지 않는다.

현재 확인해야 하는 기준은 아래와 같다.

- core/C에는 `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES = 18`과
  `ZLINK_CTX_AUTO_HWM_MSG_UNIT_BYTES_DFLT = 0`이 존재한다.
- binding의 일반 사용 경로는 context option이다. 각 언어 perf는 socket별 message unit
  facade가 아니라 context option으로 size별 message unit을 설정해야 한다.
- socket/SpotNode/Spot별 message unit facade를 되살리지 않는다. 새 측정에서
  `MsgUnit(B)` 불일치가 다시 나오면 새 API rollout이 아니라 회귀나 perf 사용 경로 문제로
  먼저 다룬다.
- `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES = 0x3034`는 C handle-level 저수준 계약으로 남는다.
  이 값이 남아 있다는 이유만으로 binding의 일반 사용 surface에 socket별 facade를 다시
  추가하지 않는다.

새 측정에서 이 기준이 깨진 언어가 있으면 해당 언어 상태표의 해당 칸을 `미달`로 표시하고,
결과 파일 / 메모 칸에 빠진 API 또는 잘못된 perf 사용 경로를 적는다.

### 5.2 새 측정 중 확인할 항목

아래 항목은 "지금 바로 새 의미를 만든다"는 뜻이 아니다. C API의 공개 primitive는
존재하지만, 해당 언어 public API가 이미 같은 의미를 제공하는지 먼저 확인한다. 이미
제공하면 perf 사용 경로만 고치고, 제공하지 않으면 회귀/API 테스트를 먼저 작성한 뒤
public API 추가 대상으로 분리한다.

| 항목 | C API 기준 | 확인 방식 |
|------|------------|-----------|
| writable/owned message | `zlink_msg_init_size`, `zlink_msg_data`, `zlink_msg_size`, `zlink_msg_close`, `zlink_msg_init_data` | 반복 송신 경로가 매번 불필요한 복사나 할당을 하는지 확인한다. C++처럼 이미 writable message가 있으면 새 API 대상이 아니다. .NET, Java, Node는 writable buffer를 public API로 안전하게 노출하는 경로가 있는지 먼저 확인한다. |
| routed single-part send/recv | `zlink_send_part_rid`, `zlink_router_request_part`, `zlink_router_reply_part`, `zlink_router_recv_part`, `zlink_spot_recv_part`, `zlink_router_send_spot_part` | ROUTER/DEALER/SPOT routed 경로에서 multi-part wrapper 없이 single part를 보내고 받을 수 있는지 확인한다. 같은 의미의 public API가 있으면 perf만 그 경로로 고친다. |
| single-part subscribe receive | `zlink_subscribe_part`, `zlink_spot_subscribe_part` | PUBSUB/SPOT receive 경로에서 caller가 결과 객체나 message buffer를 재사용할 수 있는지 확인한다. 재사용 의미가 C의 `zlink_msg_t` 재사용과 다르면 API gap으로 기록한다. |
| stream frame send | `zlink_stream_send_bound_actor_part`, `zlink_stream_packet_handler` | `MULTI_STREAM` 병목이 C stream callback/part 계약 누락인지 먼저 대조한다. 언어 binding이 이미 같은 의미를 제공하면 새 API를 만들지 않고 perf 사용 경로를 고친다. |

## 6. 신규 측정 상태 표

이 표는 기존 측정 기록을 판정 근거로 재사용하지 않고, 새 측정을 시작하기 위한 상태표다. 이전 결과 파일은 참고 자료일 뿐이며, 아래 칸은 새 라운드에서 같은 조건의 C 기준과 대상 binding 결과를 다시 비교하기 전까지 모두 `미측정`으로 둔다.

표 구조는 모든 언어에서 같다. 행은 transport와 pattern을 고정하고, 열은 message size를 고정한다. 각 size 칸은 해당 transport/pattern/size 조합의 상태를 뜻한다. `MULTI_STREAM`은 정책상 `64,256,1024,65536`만 측정하므로 `131072`, `262144`는 `해당 없음`으로 둔다.

상태 칸에는 `미측정`, `통과(비율%)`, `미달(비율%)`, `보류(비율%)`, `해당 없음` 형식만
쓴다. 예를 들어 C 대비 85%로 목표를 만족하면 `통과(85%)`, 내부 개선 후보가 소진된
30% 항목이면 `보류(30%)`로 적는다. 새 측정 뒤에는 같은 칸에 상태와 C 대비 비율을 함께
적고, 오른쪽 `결과 파일 / 메모` 칸에 결과 파일과 필요한 근거를 적는다. 크기별 결과
파일이나 사유가 다르면 행을 size별로 쪼개도 되지만, 쪼갠 뒤에도 transport/pattern/size
조합이 빠지면 안 된다.

### 6.1 언어 진행 상태

| 순서 | 언어 | perf 경로 | Single 상태 | Multi 상태 | 다음 작업 |
|------|------|-----------|-------------|------------|-----------|
| 1 | C++ | `bindings/cpp/perf` | `tcp/ws/wss/tls 통과` | `tcp/ws/wss/tls 재측정 완료, 일부 행 미달` | 2026-05-21 재측정 결과를 6.2.2 대표 표에 반영 |
| 2 | .NET | `bindings/dotnet/perf` | `tcp/ws/wss/tls 통과` | `tcp/ws/wss/tls 재측정 완료` | 2026-05-21 poller slot API 반영 뒤 재측정 결과를 6.3.2 대표 표에 반영 |
| 3 | Java | `bindings/java/perf` | `tcp/ws/wss/tls 통과` | `tcp/ws/wss/tls 재측정 완료, full-run partial 행은 제한 재측정으로 보강` | 2026-05-21 `ROUTER_ROUTER` stop token 처리와 poll mask 수정 뒤 결과를 6.4.2 대표 표에 반영 |
| 4 | Node | `bindings/node/perf` | `tcp/ws single routed large 보류, wss PAIR 64B 보류, tls PAIR 64B 및 DEALER_DEALER 64B 보류 외 통과` | `tcp/ws/wss/tls 재측정 완료, tcp full-run partial 행은 제한 재측정으로 보강` | 2026-05-21 tcp 재측정 결과를 6.5.2 대표 표에 반영 |
| 5 | Go | `bindings/go/perf` | `tcp single 통과, ws single latency 미달, wss/tls 미측정` | `미측정` | Go `ws` single latency 병목 분리 |
| 6 | Rust | `bindings/rust/perf` | `미측정` | `미측정` | 새 측정 라운드에서 `tcp`부터 transport 우선으로 측정 |
| 7 | Python | `bindings/python/perf` | `미측정` | `미측정` | 새 측정 라운드에서 `tcp`부터 transport 우선으로 측정 |

### 6.2 C++ 상태

#### 6.2.1 Single suite

| Transport | Pattern | 64 | 1024 | 16384 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `PAIR` | `통과(101%)` | `통과(100%)` | `통과(119%)` | `통과(121%)` | `통과(122%)` | `통과(135%)` | C: `perf_c_single_linux_20260519_182557_codex_c_tcp_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_182647_codex_cpp_tcp_single_smoke_all_c_setup_20260519.txt` |
| `tcp` | `PUBSUB` | `통과(120%)` | `통과(114%)` | `통과(131%)` | `통과(228%)` | `통과(541%)` | `통과(561%)` | C: `perf_c_single_linux_20260519_182557_codex_c_tcp_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_182647_codex_cpp_tcp_single_smoke_all_c_setup_20260519.txt` |
| `tcp` | `DEALER_DEALER` | `통과(99%)` | `통과(156%)` | `통과(103%)` | `통과(99%)` | `통과(99%)` | `통과(99%)` | C: `perf_c_single_linux_20260519_182557_codex_c_tcp_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_182647_codex_cpp_tcp_single_smoke_all_c_setup_20260519.txt` |
| `tcp` | `DEALER_ROUTER` | `통과(96%)` | `통과(96%)` | `통과(100%)` | `통과(71%)` | `통과(74%)` | `통과(119%)` | C: `perf_c_single_linux_20260519_182557_codex_c_tcp_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_182647_codex_cpp_tcp_single_smoke_all_c_setup_20260519.txt`; large 반복 확인: `perf_cpp_single_linux_20260519_182803_codex_cpp_dr_large_repeat3_20260519.txt`. C++ setup을 C `DEALER_ROUTER`와 같은 monitor 순서로 정렬 |
| `tcp` | `ROUTER_ROUTER` | `통과(108%)` | `통과(106%)` | `통과(104%)` | `통과(93%)` | `통과(95%)` | `통과(105%)` | C: `perf_c_single_linux_20260519_182557_codex_c_tcp_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_182647_codex_cpp_tcp_single_smoke_all_c_setup_20260519.txt`; 64KB 반복 확인: `perf_cpp_single_linux_20260519_182905_codex_cpp_rr65536_repeat3_20260519.txt` |
| `tcp` | `SPOT` | `통과(110%)` | `통과(104%)` | `통과(100%)` | `통과(95%)` | `통과(85%)` | `통과(79%)` | C: `perf_c_single_linux_20260519_182557_codex_c_tcp_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_182647_codex_cpp_tcp_single_smoke_all_c_setup_20260519.txt`; large 반복 확인: `perf_cpp_single_linux_20260519_182840_codex_cpp_spot_large_repeat3_20260519.txt` |
| `ws` | `PAIR` | `통과(100%)` | `통과(103%)` | `통과(100%)` | `통과(101%)` | `통과(101%)` | `통과(101%)` | C: `perf_c_single_linux_20260519_183218_codex_c_ws_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_183308_codex_cpp_ws_single_smoke_all_20260519.txt` |
| `ws` | `PUBSUB` | `통과(86%)` | `통과(115%)` | `통과(121%)` | `통과(192%)` | `통과(251%)` | `통과(391%)` | C: `perf_c_single_linux_20260519_183218_codex_c_ws_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_183308_codex_cpp_ws_single_smoke_all_20260519.txt` |
| `ws` | `DEALER_DEALER` | `통과(99%)` | `통과(98%)` | `통과(98%)` | `통과(99%)` | `통과(99%)` | `통과(99%)` | C: `perf_c_single_linux_20260519_183218_codex_c_ws_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_183308_codex_cpp_ws_single_smoke_all_20260519.txt` |
| `ws` | `DEALER_ROUTER` | `통과(92%)` | `통과(97%)` | `통과(97%)` | `통과(103%)` | `통과(99%)` | `통과(101%)` | C: `perf_c_single_linux_20260519_183218_codex_c_ws_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_183308_codex_cpp_ws_single_smoke_all_20260519.txt` |
| `ws` | `ROUTER_ROUTER` | `통과(99%)` | `통과(99%)` | `통과(100%)` | `통과(84%)` | `통과(95%)` | `통과(105%)` | C: `perf_c_single_linux_20260519_183218_codex_c_ws_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_183308_codex_cpp_ws_single_smoke_all_20260519.txt` |
| `ws` | `SPOT` | `통과(111%)` | `통과(106%)` | `통과(95%)` | `통과(93%)` | `통과(82%)` | `통과(98%)` | C: `perf_c_single_linux_20260519_183218_codex_c_ws_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_183308_codex_cpp_ws_single_smoke_all_20260519.txt` |
| `wss` | `PAIR` | `통과(100%)` | `통과(100%)` | `통과(99%)` | `통과(100%)` | `통과(99%)` | `통과(88%)` | C: `perf_c_single_linux_20260519_183421_codex_c_wss_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_185145_codex_cpp_wss_single_smoke_all_final_20260519.txt` |
| `wss` | `PUBSUB` | `통과(100%)` | `통과(117%)` | `통과(99%)` | `통과(107%)` | `통과(93%)` | `통과(89%)` | C: `perf_c_single_linux_20260519_183421_codex_c_wss_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_185145_codex_cpp_wss_single_smoke_all_final_20260519.txt` |
| `wss` | `DEALER_DEALER` | `통과(101%)` | `통과(98%)` | `통과(101%)` | `통과(101%)` | `통과(103%)` | `통과(99%)` | C: `perf_c_single_linux_20260519_183421_codex_c_wss_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_185145_codex_cpp_wss_single_smoke_all_final_20260519.txt` |
| `wss` | `DEALER_ROUTER` | `통과(98%)` | `통과(99%)` | `통과(96%)` | `통과(93%)` | `통과(97%)` | `통과(100%)` | C: `perf_c_single_linux_20260519_183421_codex_c_wss_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_185145_codex_cpp_wss_single_smoke_all_final_20260519.txt` |
| `wss` | `ROUTER_ROUTER` | `통과(88%)` | `통과(95%)` | `통과(91%)` | `통과(100%)` | `통과(98%)` | `통과(122%)` | C: `perf_c_single_linux_20260519_183421_codex_c_wss_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_185145_codex_cpp_wss_single_smoke_all_final_20260519.txt` |
| `wss` | `SPOT` | `통과(106%)` | `통과(99%)` | `통과(185%)` | `통과(103%)` | `통과(106%)` | `통과(99%)` | C full: `perf_c_single_linux_20260519_183421_codex_c_wss_single_smoke_all_20260519.txt`; C++ full: `perf_cpp_single_linux_20260519_185145_codex_cpp_wss_single_smoke_all_final_20260519.txt`; 256KB은 full smoke 74%, 같은 조건 repeat3에서 C `perf_c_single_linux_20260519_183618_codex_c_wss_single_spot_large_repeat3_20260519.txt` 대비 C++ `perf_cpp_single_linux_20260519_184454_codex_cpp_wss_single_spot_borrow_publish_repeat3_20260519.txt` 99% |
| `tls` | `PAIR` | `통과(100%)` | `통과(106%)` | `통과(99%)` | `통과(104%)` | `통과(100%)` | `통과(99%)` | C: `perf_c_single_linux_20260519_184702_codex_c_tls_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_184924_codex_cpp_tls_single_smoke_all_20260519.txt` |
| `tls` | `PUBSUB` | `통과(103%)` | `통과(123%)` | `통과(120%)` | `통과(121%)` | `통과(132%)` | `통과(129%)` | C: `perf_c_single_linux_20260519_184702_codex_c_tls_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_184924_codex_cpp_tls_single_smoke_all_20260519.txt` |
| `tls` | `DEALER_DEALER` | `통과(98%)` | `통과(93%)` | `통과(98%)` | `통과(100%)` | `통과(98%)` | `통과(99%)` | C: `perf_c_single_linux_20260519_184702_codex_c_tls_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_184924_codex_cpp_tls_single_smoke_all_20260519.txt` |
| `tls` | `DEALER_ROUTER` | `통과(94%)` | `통과(102%)` | `통과(99%)` | `통과(94%)` | `통과(93%)` | `통과(99%)` | C: `perf_c_single_linux_20260519_184702_codex_c_tls_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_184924_codex_cpp_tls_single_smoke_all_20260519.txt` |
| `tls` | `ROUTER_ROUTER` | `통과(93%)` | `통과(100%)` | `통과(96%)` | `통과(94%)` | `통과(98%)` | `통과(98%)` | C: `perf_c_single_linux_20260519_184702_codex_c_tls_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_184924_codex_cpp_tls_single_smoke_all_20260519.txt` |
| `tls` | `SPOT` | `통과(115%)` | `통과(97%)` | `통과(98%)` | `통과(99%)` | `통과(98%)` | `통과(98%)` | C: `perf_c_single_linux_20260519_184702_codex_c_tls_single_smoke_all_20260519.txt`; C++: `perf_cpp_single_linux_20260519_184924_codex_cpp_tls_single_smoke_all_20260519.txt` |

#### 6.2.2 Multi suite

2026-05-21 재측정 결과로 대표 표를 갱신했다. 판정은 `doc/perf` 기준처럼 C `bindings/c/perf`와 같은 suite/pattern/transport/size의 throughput 비율로 계산한다. HWM은 튜닝 값으로 쓰지 않고, auto-HWM 활성 여부와 size별 `MsgUnit(B)` 일치 여부만 확인한다.

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `MULTI_DEALER_DEALER` | `통과(89.1%)` | `통과(104.6%)` | `통과(109.2%)` | `통과(100.8%)` | `통과(95.0%)` | `통과(97.7%)` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(89.6%)` | `통과(91.9%)` | `통과(92.6%)` | `미달(60.2%)` | `통과(86.5%)` | `통과(92.1%)` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(91.2%)` | `통과(91.5%)` | `통과(90.1%)` | `통과(104.4%)` | `미달(59.3%)` | `통과(76.8%)` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_PUBSUB` | `통과(88.2%)` | `통과(92.5%)` | `통과(108.8%)` | `통과(104.7%)` | `미달(73.7%)` | `통과(136.9%)` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_SPOT` | `통과(93.1%)` | `미달(74.5%)` | `통과(75.4%)` | `통과(93.0%)` | `통과(98.1%)` | `통과(158.0%)` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(101.2%)` | `통과(103.8%)` | `통과(102.4%)` | `통과(112.0%)` | `통과(110.2%)` | `통과(117.3%)` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_165846_codex_cpp_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(110.5%)` | `통과(114.3%)` | `통과(110.1%)` | `통과(115.6%)` | `통과(121.8%)` | `통과(103.4%)` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_STREAM` | `통과(103.9%)` | `통과(99.1%)` | `통과(100.9%)` | `통과(113.5%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(89.8%)` | `통과(96.5%)` | `통과(89.0%)` | `통과(89.2%)` | `미달(77.9%)` | `통과(100.8%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(82.1%)` | `통과(80.5%)` | `통과(84.5%)` | `통과(81.3%)` | `통과(82.6%)` | `통과(106.7%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(88.3%)` | `통과(86.4%)` | `통과(81.4%)` | `통과(112.4%)` | `통과(121.0%)` | `통과(97.0%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_PUBSUB` | `통과(92.2%)` | `통과(90.0%)` | `통과(91.9%)` | `통과(96.2%)` | `통과(99.4%)` | `미달(70.2%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT` | `통과(95.9%)` | `통과(98.5%)` | `통과(96.8%)` | `통과(94.4%)` | `통과(96.9%)` | `통과(95.7%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(94.1%)` | `통과(96.2%)` | `통과(89.4%)` | `통과(105.8%)` | `통과(101.0%)` | `통과(84.6%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_165846_codex_cpp_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(92.1%)` | `통과(78.3%)` | `통과(79.3%)` | `통과(100.2%)` | `통과(96.4%)` | `통과(144.4%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_STREAM` | `통과(99.8%)` | `통과(98.7%)` | `통과(99.0%)` | `통과(89.7%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(91.8%)` | `통과(101.6%)` | `미달(76.9%)` | `미달(58.2%)` | `미달(75.9%)` | `통과(106.6%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(86.0%)` | `통과(83.6%)` | `통과(86.4%)` | `통과(85.0%)` | `통과(81.0%)` | `통과(82.3%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(83.0%)` | `통과(86.0%)` | `통과(84.0%)` | `통과(76.2%)` | `통과(70.4%)` | `통과(71.6%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_PUBSUB` | `통과(86.1%)` | `통과(83.8%)` | `통과(82.0%)` | `미달(69.5%)` | `미달(75.2%)` | `미달(71.8%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_SPOT` | `통과(232.4%)` | `통과(462.8%)` | `미달(13.4%)` | `통과(91.2%)` | `통과(109.5%)` | `통과(108.3%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(94.5%)` | `통과(93.4%)` | `통과(92.0%)` | `통과(107.4%)` | `통과(101.7%)` | `통과(100.0%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_165846_codex_cpp_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(93.6%)` | `통과(93.3%)` | `통과(80.6%)` | `통과(98.8%)` | `통과(84.6%)` | `통과(95.5%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_STREAM` | `통과(88.1%)` | `통과(87.3%)` | `통과(85.7%)` | `미달(66.1%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(91.3%)` | `통과(108.7%)` | `통과(92.8%)` | `통과(91.8%)` | `통과(85.2%)` | `통과(96.0%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(92.8%)` | `통과(89.8%)` | `통과(89.0%)` | `통과(84.8%)` | `통과(90.7%)` | `통과(90.3%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(95.6%)` | `통과(94.0%)` | `통과(90.6%)` | `미달(57.7%)` | `통과(143.7%)` | `미달(39.0%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_PUBSUB` | `통과(80.8%)` | `통과(81.2%)` | `통과(85.9%)` | `통과(81.9%)` | `미달(79.1%)` | `미달(78.4%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT` | `통과(104.5%)` | `통과(99.9%)` | `통과(162.4%)` | `통과(101.1%)` | `통과(99.1%)` | `통과(93.4%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(92.5%)` | `통과(82.9%)` | `통과(90.4%)` | `통과(95.1%)` | `통과(98.6%)` | `통과(98.9%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_165846_codex_cpp_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(86.8%)` | `통과(92.2%)` | `통과(78.3%)` | `통과(88.4%)` | `통과(94.1%)` | `통과(94.3%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_STREAM` | `통과(82.5%)` | `통과(84.7%)` | `미달(77.7%)` | `통과(97.2%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |

측정 결과 파일:

- C 기준: `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`, `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`, `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`, `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`, `perf_c_multi_linux_20260519_234246_codex_c_tls_stream64_debug_recheck_20260519.txt`, `perf_c_multi_linux_20260519_235205_codex_c_tls_stream256_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234152_codex_c_tls_stream1024_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234308_codex_c_tls_stream65536_recheck_20260519.txt`, `perf_c_multi_linux_20260521_130140_codex_c_tcp_rr_large_repeat_for_cpp_20260521.txt`, `perf_c_multi_linux_20260521_130951_codex_c_ws_rr_large_for_cpp_20260521.txt`, `perf_c_multi_linux_20260521_132328_codex_c_wss_dd_rr_large_for_cpp_20260521.txt`, `perf_c_multi_linux_20260521_133217_codex_c_tls_sendsend262144_for_cpp_20260521.txt`, `perf_c_multi_linux_20260521_133338_codex_c_tls_rr_stream_large_for_cpp_20260521.txt`, `perf_c_multi_linux_20260521_133503_codex_c_tls_stream_large_single_for_cpp_20260521.txt`
- C++ 측정: `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`, `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`, `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`, `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`, `perf_cpp_multi_linux_20260521_133213_codex_cpp_tls_sendsend262144_repro_20260521.txt`, `perf_cpp_multi_linux_20260521_133233_codex_cpp_tls_stream_large_recheck_20260521.txt`, `perf_cpp_multi_linux_20260521_133335_codex_cpp_tls_rr_stream_large_recheck_20260521.txt`, `perf_cpp_multi_linux_20260521_165846_codex_cpp_multi_spot_reqrep_pollcompletion_full_20260521.txt`
- C++ 최신 재측정에서는 일부 ws/wss/tls 행이 기존 대표 표보다 낮게 나와 미달로 표시했다. 기준 보강이 필요한 항목은 별도 재측정 대상으로 남긴다.

### 6.3 .NET 상태

#### 6.3.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `PAIR` | `통과(100%)` | `통과(75%)` | `통과(118%)` | `통과(99%)` | `통과(99%)` | `통과(101%)` | C: `perf_c_single_linux_20260520_000429_codex_c_tcp_single_duration5_for_dotnet_20260520.txt`; .NET: `perf_dotnet_single_linux_20260520_001758_codex_dotnet_tcp_single_smoke_all_after_dr_poller_drain_20260520.txt` |
| `tcp` | `PUBSUB` | `통과(88%)` | `통과(89%)` | `통과(114%)` | `통과(101%)` | `통과(101%)` | `통과(100%)` | C/.NET: 위 파일 |
| `tcp` | `DEALER_DEALER` | `통과(97%)` | `통과(74%)` | `통과(117%)` | `통과(100%)` | `통과(101%)` | `통과(101%)` | C/.NET: 위 파일 |
| `tcp` | `DEALER_ROUTER` | `통과(75%)` | `통과(79%)` | `통과(85%)` | `통과(79%)` | `통과(98%)` | `통과(91%)` | C/.NET: 위 파일. `DEALER` explicit routing id와 recv deadline count를 제거하고, active phase는 wire stop token까지 유지한다. ROUTER 수신은 signal-driven wait 뒤 `DontWait` drain으로 처리해 C와 같은 stop-token 종료 의미를 유지하면서 blocking routed recv 병목을 제거했다. 제한 확인: `perf_dotnet_single_linux_20260520_001728_codex_dotnet_tcp_single_dr_large_poller_drain_20260520.txt` |
| `tcp` | `ROUTER_ROUTER` | `통과(89%)` | `통과(91%)` | `통과(89%)` | `통과(90%)` | `통과(78%)` | `통과(94%)` | C/.NET: 위 파일. `ROUTER_ROUTER / DEALER_ROUTER` 상대 기준은 절대 기준 통과 항목의 진단 보조로만 사용 |
| `tcp` | `SPOT` | `통과(176%)` | `통과(132%)` | `통과(109%)` | `통과(125%)` | `통과(103%)` | `통과(102%)` | C/.NET: 위 파일. .NET `PerfSpot`에 single auto-HWM message unit 적용을 추가했고 `test_single_spot_auto_hwm_msgunit_matches_size.sh`로 size별 `MsgUnit(B)` 일치를 확인 |
| `ws` | `PAIR` | `통과(96%)` | `통과(72%)` | `통과(103%)` | `통과(98%)` | `통과(99%)` | `통과(100%)` | C: `perf_c_single_linux_20260520_002250_codex_c_ws_single_for_dotnet_20260520.txt`; .NET: `perf_dotnet_single_linux_20260520_002601_codex_dotnet_ws_single_smoke_all_20260520.txt` |
| `ws` | `PUBSUB` | `통과(85%)` | `통과(83%)` | `통과(91%)` | `통과(100%)` | `통과(100%)` | `통과(100%)` | C/.NET: 위 파일 |
| `ws` | `DEALER_DEALER` | `통과(97%)` | `통과(72%)` | `통과(102%)` | `통과(100%)` | `통과(100%)` | `통과(99%)` | C/.NET: 위 파일 |
| `ws` | `DEALER_ROUTER` | `통과(78%)` | `통과(80%)` | `통과(89%)` | `통과(85%)` | `통과(93%)` | `통과(102%)` | C/.NET: 위 파일 |
| `ws` | `ROUTER_ROUTER` | `통과(85%)` | `통과(84%)` | `통과(90%)` | `통과(88%)` | `통과(94%)` | `통과(102%)` | C/.NET: 위 파일 |
| `ws` | `SPOT` | `통과(166%)` | `통과(118%)` | `통과(97%)` | `통과(120%)` | `통과(102%)` | `통과(101%)` | C/.NET: 위 파일 |
| `wss` | `PAIR` | `통과(97%)` | `통과(68%)` | `통과(101%)` | `통과(104%)` | `통과(93%)` | `통과(102%)` | C: `perf_c_single_linux_20260520_003001_codex_c_wss_single_for_dotnet_20260520.txt`; .NET: `perf_dotnet_single_linux_20260520_003309_codex_dotnet_wss_single_smoke_all_20260520.txt` |
| `wss` | `PUBSUB` | `통과(91%)` | `통과(82%)` | `통과(97%)` | `통과(99%)` | `통과(98%)` | `통과(100%)` | C/.NET: 위 파일 |
| `wss` | `DEALER_DEALER` | `통과(97%)` | `통과(70%)` | `통과(104%)` | `통과(106%)` | `통과(93%)` | `통과(98%)` | C/.NET: 위 파일 |
| `wss` | `DEALER_ROUTER` | `통과(76%)` | `통과(80%)` | `통과(98%)` | `통과(93%)` | `통과(99%)` | `통과(118%)` | C/.NET: 위 파일 |
| `wss` | `ROUTER_ROUTER` | `통과(81%)` | `통과(84%)` | `통과(97%)` | `통과(95%)` | `통과(98%)` | `통과(118%)` | C/.NET: 위 파일 |
| `wss` | `SPOT` | `통과(161%)` | `통과(106%)` | `통과(467%)` | `통과(515%)` | `통과(197%)` | `통과(369%)` | C/.NET: 위 파일. auto-HWM 적용과 size별 `MsgUnit(B)` 일치를 확인 |
| `tls` | `PAIR` | `통과(97%)` | `통과(74%)` | `통과(125%)` | `통과(99%)` | `통과(101%)` | `통과(100%)` | C: `perf_c_single_linux_20260520_003724_codex_c_tls_single_for_dotnet_20260520.txt`; .NET: `perf_dotnet_single_linux_20260520_004037_codex_dotnet_tls_single_smoke_all_20260520.txt` |
| `tls` | `PUBSUB` | `통과(91%)` | `통과(83%)` | `통과(117%)` | `통과(99%)` | `통과(99%)` | `통과(100%)` | C/.NET: 위 파일 |
| `tls` | `DEALER_DEALER` | `통과(97%)` | `통과(74%)` | `통과(130%)` | `통과(96%)` | `통과(100%)` | `통과(101%)` | C/.NET: 위 파일 |
| `tls` | `DEALER_ROUTER` | `통과(77%)` | `통과(76%)` | `통과(99%)` | `통과(107%)` | `통과(105%)` | `통과(107%)` | C/.NET: 위 파일 |
| `tls` | `ROUTER_ROUTER` | `통과(90%)` | `통과(92%)` | `통과(96%)` | `통과(97%)` | `통과(99%)` | `통과(97%)` | C/.NET: 위 파일 |
| `tls` | `SPOT` | `통과(160%)` | `통과(124%)` | `통과(102%)` | `통과(102%)` | `통과(99%)` | `통과(99%)` | C/.NET: 위 파일. auto-HWM 적용과 size별 `MsgUnit(B)` 일치를 확인 |

#### 6.3.2 Multi suite

2026-05-21 재측정 결과로 대표 표를 갱신했다. 판정은 `doc/perf` 기준처럼 C `bindings/c/perf`와 같은 suite/pattern/transport/size의 throughput 비율로 계산한다. HWM은 튜닝 값으로 쓰지 않고, auto-HWM 활성 여부와 size별 `MsgUnit(B)` 일치 여부만 확인한다.

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `MULTI_DEALER_DEALER` | `통과(69.9%)` | `통과(82.2%)` | `통과(87.4%)` | `통과(102.8%)` | `통과(101.4%)` | `통과(103.0%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; .NET `perf_dotnet_multi_linux_20260521_145328_codex_dotnet_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(61.4%)` | `통과(62.0%)` | `통과(61.0%)` | `통과(70.7%)` | `통과(78.9%)` | `통과(96.1%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; .NET `perf_dotnet_multi_linux_20260521_145328_codex_dotnet_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(53.1%)` | `통과(54.5%)` | `통과(53.9%)` | `통과(65.7%)` | `통과(87.3%)` | `통과(100.6%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; .NET `perf_dotnet_multi_linux_20260521_145328_codex_dotnet_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_PUBSUB` | `통과(75.6%)` | `통과(73.9%)` | `통과(148.1%)` | `통과(87.2%)` | `통과(85.2%)` | `통과(109.8%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; .NET `perf_dotnet_multi_linux_20260521_145328_codex_dotnet_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_SPOT` | `통과(73.4%)` | `통과(117.2%)` | `통과(63.9%)` | `통과(101.5%)` | `통과(95.0%)` | `통과(76.8%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; .NET `perf_dotnet_multi_linux_20260521_145328_codex_dotnet_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(64.2%)` | `통과(64.7%)` | `통과(65.0%)` | `통과(60.7%)` | `통과(77.4%)` | `통과(103.0%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; .NET `perf_dotnet_multi_linux_20260521_170326_codex_dotnet_multi_spot_reqrep_pollcompletion_full2_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `미달(58.9%)` | `미달(58.2%)` | `미달(57.4%)` | `통과(88.6%)` | `통과(91.1%)` | `통과(125.6%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; .NET `perf_dotnet_multi_linux_20260521_145328_codex_dotnet_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_STREAM` | `통과(94.4%)` | `통과(95.0%)` | `통과(90.5%)` | `통과(95.2%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; .NET `perf_dotnet_multi_linux_20260521_145328_codex_dotnet_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(69.6%)` | `통과(82.8%)` | `통과(97.9%)` | `통과(106.0%)` | `통과(109.0%)` | `통과(174.3%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(64.2%)` | `통과(65.0%)` | `통과(66.1%)` | `통과(68.2%)` | `통과(98.2%)` | `통과(126.0%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(60.4%)` | `통과(59.8%)` | `통과(59.1%)` | `통과(60.3%)` | `통과(87.0%)` | `통과(114.3%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_PUBSUB` | `통과(78.7%)` | `통과(77.3%)` | `통과(77.7%)` | `통과(131.4%)` | `통과(147.4%)` | `통과(133.8%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT` | `통과(71.1%)` | `통과(69.6%)` | `통과(63.9%)` | `통과(103.1%)` | `통과(94.4%)` | `통과(67.3%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT_REQREP` | `미달(47.7%)` | `통과(71.2%)` | `통과(74.4%)` | `통과(63.3%)` | `통과(99.8%)` | `통과(90.9%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_170326_codex_dotnet_multi_spot_reqrep_pollcompletion_full2_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `미달(59.1%)` | `미달(59.6%)` | `통과(64.6%)` | `통과(107.0%)` | `통과(97.0%)` | `통과(95.9%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_STREAM` | `통과(90.7%)` | `통과(97.2%)` | `통과(92.4%)` | `통과(100.0%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(68.6%)` | `통과(88.2%)` | `통과(73.1%)` | `통과(95.7%)` | `통과(97.9%)` | `통과(105.3%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(66.1%)` | `통과(61.0%)` | `통과(62.2%)` | `통과(91.3%)` | `통과(93.5%)` | `통과(93.8%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(57.4%)` | `통과(55.6%)` | `통과(56.8%)` | `통과(92.2%)` | `통과(96.1%)` | `통과(99.1%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_PUBSUB` | `통과(68.9%)` | `통과(69.0%)` | `통과(81.5%)` | `통과(83.9%)` | `통과(96.0%)` | `통과(115.0%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_SPOT` | `통과(180.1%)` | `통과(261.4%)` | `미달(53.5%)` | `통과(67.5%)` | `통과(72.7%)` | `미달(59.2%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(67.4%)` | `통과(72.1%)` | `통과(81.1%)` | `통과(100.2%)` | `통과(91.6%)` | `통과(87.0%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_170326_codex_dotnet_multi_spot_reqrep_pollcompletion_full2_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `미달(57.0%)` | `미달(58.2%)` | `통과(66.5%)` | `통과(100.0%)` | `통과(96.0%)` | `통과(91.9%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_STREAM` | `통과(84.0%)` | `통과(86.7%)` | `통과(87.2%)` | `통과(88.7%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(69.6%)` | `통과(88.9%)` | `통과(85.9%)` | `통과(85.2%)` | `통과(94.2%)` | `통과(97.4%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(66.1%)` | `통과(60.6%)` | `통과(61.3%)` | `통과(85.6%)` | `통과(93.3%)` | `통과(95.1%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(56.2%)` | `통과(56.2%)` | `통과(56.0%)` | `통과(79.7%)` | `통과(92.6%)` | `통과(98.2%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_PUBSUB` | `통과(69.1%)` | `통과(66.8%)` | `통과(77.4%)` | `통과(80.2%)` | `통과(90.7%)` | `통과(96.8%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT` | `통과(66.4%)` | `통과(85.7%)` | `통과(108.6%)` | `통과(97.9%)` | `통과(90.5%)` | `통과(79.1%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(64.2%)` | `통과(63.2%)` | `통과(68.9%)` | `통과(78.9%)` | `통과(90.6%)` | `통과(91.5%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_170326_codex_dotnet_multi_spot_reqrep_pollcompletion_full2_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `미달(53.6%)` | `미달(58.2%)` | `미달(59.3%)` | `통과(91.0%)` | `통과(96.6%)` | `통과(93.4%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_STREAM` | `통과(89.9%)` | `통과(85.6%)` | `통과(84.0%)` | `통과(86.6%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. STREAM small/large C 보강 파일은 아래 목록 참조. |

측정 결과 파일:

- C 기준: `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`, `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`, `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`, `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`, `perf_c_multi_linux_20260519_234246_codex_c_tls_stream64_debug_recheck_20260519.txt`, `perf_c_multi_linux_20260519_235205_codex_c_tls_stream256_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234152_codex_c_tls_stream1024_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234308_codex_c_tls_stream65536_recheck_20260519.txt`
- .NET 측정: `perf_dotnet_multi_linux_20260521_145328_codex_dotnet_tcp_multi_remeasure_20260521.txt`, `perf_dotnet_multi_linux_20260521_150044_codex_dotnet_tcp_spot_sendsend262144_recheck_20260521.txt`, `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`, `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`, `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`, `perf_dotnet_multi_linux_20260521_170326_codex_dotnet_multi_spot_reqrep_pollcompletion_full2_20260521.txt`
- 현재 표의 일부 small SPOT 계열 미달은 최신 재측정값을 그대로 반영한 것이다. 통과로 바꾸기 위한 sleep/backoff나 HWM 숫자 튜닝은 적용하지 않았다.

### 6.4 Java 상태

#### 6.4.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `PAIR` | `통과(97.7%)` | `통과(97.4%)` | `통과(125.1%)` | `통과(118.1%)` | `통과(118.3%)` | `통과(131.4%)` | C: `perf_c_single_linux_20260519_182557_codex_c_tcp_single_smoke_all_20260519.txt`; Java: `perf_java_single_linux_20260520_065810_codex_java_tcp_single_smoke_20260520.txt`. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `PUBSUB` | `통과(89.7%)` | `통과(92.5%)` | `통과(107.3%)` | `통과(98.7%)` | `통과(97.2%)` | `통과(97.9%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `DEALER_DEALER` | `통과(97.5%)` | `통과(151.1%)` | `통과(123.0%)` | `통과(97.3%)` | `통과(97.0%)` | `통과(97.0%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `DEALER_ROUTER` | `통과(83.4%)` | `통과(82.6%)` | `통과(90.2%)` | `통과(111.1%)` | `통과(95.6%)` | `통과(110.9%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `ROUTER_ROUTER` | `통과(96.8%)` | `통과(99.0%)` | `통과(81.6%)` | `통과(108.6%)` | `통과(105.1%)` | `통과(102.4%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `SPOT` | `통과(192.1%)` | `통과(144.6%)` | `통과(131.9%)` | `통과(84.9%)` | `통과(67.9%)` | `통과(60.6%)` | C: `perf_c_single_linux_20260519_182557_codex_c_tcp_single_smoke_all_20260519.txt`; Java repeat3: `perf_java_single_linux_20260520_070601_codex_java_tcp_single_spot_direct_spin_repeat3_20260520.txt`. Java SPOT은 C와 같은 의미가 되도록 stop token 송신을 별도 stop publisher로 분리하고, active 메시지는 복사 없이 public API로 직접 publish한다. all-size smoke의 131072B 이상치는 isolated recheck `perf_java_single_linux_20260520_070545_codex_java_tcp_single_spot_131072_direct_spin_recheck_20260520.txt`와 repeat3로 배제했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `PAIR` | `통과(97.7%)` | `통과(98.6%)` | `통과(141.6%)` | `통과(98.8%)` | `통과(98.7%)` | `통과(98.6%)` | C: `perf_c_single_linux_20260519_183218_codex_c_ws_single_smoke_all_20260519.txt`; 256B C 제한: `perf_c_single_linux_20260520_085311_codex_c_ws_single_256_for_java_20260520.txt`; Java: `perf_java_single_linux_20260520_084654_codex_java_ws_single_smoke_after_routed_fix_20260520.txt`. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `PUBSUB` | `통과(83.1%)` | `통과(89.0%)` | `통과(124.1%)` | `통과(99.0%)` | `통과(99.1%)` | `통과(98.7%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `DEALER_DEALER` | `통과(97.1%)` | `통과(98.8%)` | `통과(134.9%)` | `통과(97.8%)` | `통과(97.3%)` | `통과(97.4%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `DEALER_ROUTER` | `통과(68.4%)` | `통과(93.4%)` | `통과(118.6%)` | `통과(217.0%)` | `통과(146.5%)` | `통과(137.7%)` | C 파일은 위 PAIR 행과 같다. Java: `perf_java_single_linux_20260520_085454_codex_java_ws_single_dr_all_sizes_blocking_active_20260520.txt`. active와 stop token 전송을 C `perf_dealer_router.cpp`와 같은 blocking send 의미로 맞췄다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `ROUTER_ROUTER` | `통과(89.4%)` | `통과(107.2%)` | `통과(114.1%)` | `통과(196.9%)` | `통과(144.6%)` | `통과(146.1%)` | C 파일은 위 PAIR 행과 같다. Java: `perf_java_single_linux_20260520_085110_codex_java_ws_single_rr_all_sizes_isolated_20260520.txt`. ROUTER-ROUTER는 C처럼 양쪽 routing id와 mandatory를 설정하고 PING/PONG으로 target route를 확인한 뒤 active와 stop token을 blocking send 의미로 보낸다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `SPOT` | `통과(194.1%)` | `통과(155.2%)` | `통과(153.7%)` | `통과(115.9%)` | `통과(120.3%)` | `통과(134.6%)` | C 파일은 위 PAIR 행과 같다. Java: `perf_java_single_linux_20260520_085151_codex_java_ws_single_spot_all_sizes_isolated_20260520.txt`. 전체 matrix 실행 중 후반부 timeout이 있었으나 패턴 단위 smoke에서는 전 size 통과해 partial matrix 결과는 판정 근거에서 제외했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `PAIR` | `통과(98.3%)` | `통과(99.0%)` | `통과(143.8%)` | `통과(133.7%)` | `통과(121.6%)` | `통과(103.5%)` | C: `perf_c_single_linux_20260519_183421_codex_c_wss_single_smoke_all_20260519.txt`; 256B C 제한: `perf_c_single_linux_20260520_093213_codex_c_wss_single_256_for_java_20260520.txt`; Java: `perf_java_single_linux_20260520_092846_codex_java_wss_single_smoke_20260520.txt`. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `PUBSUB` | `통과(82.5%)` | `통과(95.2%)` | `통과(148.6%)` | `통과(123.7%)` | `통과(110.1%)` | `통과(100.6%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `DEALER_DEALER` | `통과(98.0%)` | `통과(99.4%)` | `통과(150.3%)` | `통과(131.6%)` | `통과(122.2%)` | `통과(101.6%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `DEALER_ROUTER` | `통과(69.8%)` | `통과(90.6%)` | `통과(136.6%)` | `통과(158.0%)` | `통과(186.8%)` | `통과(208.5%)` | C/Java 파일은 위 PAIR 행과 같다. ws single에서 정렬한 active/stop blocking send 의미를 그대로 사용한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `ROUTER_ROUTER` | `통과(83.2%)` | `통과(104.2%)` | `통과(136.9%)` | `통과(158.9%)` | `통과(190.5%)` | `통과(211.7%)` | C/Java 파일은 위 PAIR 행과 같다. ws single에서 정렬한 ROUTER-ROUTER PING/PONG target route 확인과 active/stop blocking send 의미를 그대로 사용한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `SPOT` | `통과(202.0%)` | `통과(148.9%)` | `통과(147.0%)` | `통과(114.4%)` | `통과(101.7%)` | `통과(139.0%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `PAIR` | `통과(98.3%)` | `통과(98.6%)` | `통과(134.3%)` | `통과(109.3%)` | `통과(101.9%)` | `통과(101.0%)` | C: `perf_c_single_linux_20260519_184702_codex_c_tls_single_smoke_all_20260519.txt`; 256B C 제한: `perf_c_single_linux_20260520_093917_codex_c_tls_single_256_for_java_20260520.txt`; Java: `perf_java_single_linux_20260520_093346_codex_java_tls_single_smoke_20260520.txt`. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `PUBSUB` | `통과(81.0%)` | `통과(86.8%)` | `통과(136.0%)` | `통과(102.4%)` | `통과(100.0%)` | `통과(98.4%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `DEALER_DEALER` | `통과(96.8%)` | `통과(98.7%)` | `통과(132.7%)` | `통과(106.9%)` | `통과(100.5%)` | `통과(101.0%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `DEALER_ROUTER` | `통과(68.6%)` | `통과(83.5%)` | `통과(125.2%)` | `통과(172.0%)` | `통과(165.7%)` | `통과(171.4%)` | C/Java 파일은 위 PAIR 행과 같다. ws/wss single에서 정렬한 active/stop blocking send 의미를 그대로 사용한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `ROUTER_ROUTER` | `통과(87.5%)` | `통과(105.7%)` | `통과(122.0%)` | `통과(171.3%)` | `통과(171.2%)` | `통과(165.6%)` | C/Java 파일은 위 PAIR 행과 같다. ws/wss single에서 정렬한 ROUTER-ROUTER PING/PONG target route 확인과 active/stop blocking send 의미를 그대로 사용한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `SPOT` | `통과(200.8%)` | `통과(147.7%)` | `통과(148.3%)` | `통과(210.3%)` | `통과(96.1%)` | `통과(124.1%)` | C/Java 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |

#### 6.4.2 Multi suite

2026-05-21 재측정 결과로 대표 표를 갱신했다. 판정은 `doc/perf` 기준처럼 C `bindings/c/perf`와 같은 suite/pattern/transport/size의 throughput 비율로 계산한다. HWM은 튜닝 값으로 쓰지 않고, auto-HWM 활성 여부와 size별 `MsgUnit(B)` 일치 여부만 확인한다.

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `MULTI_DEALER_DEALER` | `통과(73.2%)` | `통과(91.9%)` | `통과(95.5%)` | `미달(59.2%)` | `통과(64.2%)` | `통과(68.0%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(87.6%)` | `통과(74.2%)` | `통과(77.1%)` | `통과(50.5%)` | `통과(69.3%)` | `통과(92.9%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(60.1%)` | `통과(61.0%)` | `통과(60.3%)` | `통과(61.0%)` | `통과(72.7%)` | `통과(96.2%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_PUBSUB` | `통과(77.3%)` | `통과(79.1%)` | `통과(96.1%)` | `통과(151.4%)` | `통과(143.2%)` | `통과(157.5%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_SPOT` | `통과(108.6%)` | `통과(88.8%)` | `통과(82.9%)` | `통과(75.6%)` | `통과(73.4%)` | `통과(83.7%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(69.4%)` | `통과(74.0%)` | `통과(77.7%)` | `통과(95.4%)` | `통과(109.5%)` | `통과(145.6%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_170615_codex_java_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(84.0%)` | `통과(82.0%)` | `통과(81.7%)` | `통과(81.2%)` | `통과(79.7%)` | `통과(63.3%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. large C 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_STREAM` | `통과(98.8%)` | `통과(96.4%)` | `통과(88.3%)` | `통과(107.3%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(69.0%)` | `통과(67.2%)` | `통과(93.6%)` | `미달(61.2%)` | `미달(46.9%)` | `통과(91.1%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; Java `perf_java_multi_linux_20260521_140504_codex_java_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(71.0%)` | `통과(68.7%)` | `통과(71.7%)` | `미달(49.3%)` | `통과(67.5%)` | `통과(84.0%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; Java `perf_java_multi_linux_20260521_140504_codex_java_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(58.3%)` | `통과(62.2%)` | `통과(61.1%)` | `통과(54.8%)` | `통과(73.2%)` | `통과(88.1%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; Java `perf_java_multi_linux_20260521_140504_codex_java_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_PUBSUB` | `통과(85.5%)` | `통과(73.7%)` | `통과(88.5%)` | `통과(135.2%)` | `통과(134.3%)` | `통과(146.7%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; Java `perf_java_multi_linux_20260521_140504_codex_java_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT` | `통과(101.5%)` | `통과(101.2%)` | `통과(111.0%)` | `통과(89.6%)` | `통과(79.1%)` | `통과(75.1%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; Java `perf_java_multi_linux_20260521_140504_codex_java_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(83.8%)` | `통과(79.6%)` | `통과(75.8%)` | `통과(92.9%)` | `통과(102.4%)` | `통과(101.5%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; Java full `perf_java_multi_linux_20260521_170615_codex_java_multi_spot_reqrep_pollcompletion_full_20260521.txt`, ws 64B 보강 `perf_java_multi_linux_20260521_171039_codex_java_multi_spot_reqrep_ws64_recheck_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(76.6%)` | `통과(74.3%)` | `통과(77.5%)` | `통과(99.7%)` | `통과(63.9%)` | `통과(77.4%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; Java `perf_java_multi_linux_20260521_140504_codex_java_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_STREAM` | `통과(100.9%)` | `통과(105.6%)` | `통과(101.2%)` | `통과(106.1%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; Java `perf_java_multi_linux_20260521_140504_codex_java_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(74.3%)` | `통과(81.0%)` | `통과(68.1%)` | `통과(94.0%)` | `통과(88.4%)` | `통과(91.1%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; Java `perf_java_multi_linux_20260521_141952_codex_java_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(70.2%)` | `통과(69.9%)` | `통과(73.4%)` | `통과(92.3%)` | `통과(95.9%)` | `통과(106.1%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; Java `perf_java_multi_linux_20260521_141952_codex_java_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(57.2%)` | `통과(56.8%)` | `통과(57.4%)` | `통과(72.8%)` | `통과(57.2%)` | `통과(99.4%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; Java `perf_java_multi_linux_20260521_141952_codex_java_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_PUBSUB` | `통과(72.8%)` | `통과(77.1%)` | `통과(85.7%)` | `통과(89.5%)` | `통과(100.9%)` | `통과(103.7%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; Java `perf_java_multi_linux_20260521_141952_codex_java_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_SPOT` | `통과(265.8%)` | `통과(390.2%)` | `미달(12.4%)` | `미달(53.6%)` | `미달(58.5%)` | `미달(48.8%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; Java `perf_java_multi_linux_20260521_141952_codex_java_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(78.1%)` | `통과(80.4%)` | `통과(78.8%)` | `통과(100.6%)` | `통과(96.0%)` | `통과(94.2%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; Java `perf_java_multi_linux_20260521_170615_codex_java_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(68.2%)` | `통과(71.7%)` | `통과(77.7%)` | `통과(95.6%)` | `통과(96.7%)` | `통과(89.9%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; Java `perf_java_multi_linux_20260521_141952_codex_java_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_STREAM` | `통과(94.9%)` | `통과(91.9%)` | `통과(93.7%)` | `통과(92.3%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; Java `perf_java_multi_linux_20260521_141952_codex_java_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(74.7%)` | `통과(99.6%)` | `통과(81.2%)` | `통과(97.5%)` | `통과(82.9%)` | `통과(79.8%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(72.4%)` | `통과(71.7%)` | `통과(71.5%)` | `통과(58.4%)` | `통과(92.1%)` | `통과(90.2%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(58.9%)` | `통과(58.8%)` | `통과(58.8%)` | `통과(62.0%)` | `통과(63.9%)` | `통과(69.6%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_PUBSUB` | `통과(75.0%)` | `통과(73.9%)` | `통과(82.1%)` | `통과(90.2%)` | `통과(95.9%)` | `통과(95.4%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT` | `통과(124.0%)` | `통과(127.8%)` | `통과(173.8%)` | `통과(88.7%)` | `통과(92.2%)` | `통과(95.8%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(76.8%)` | `미달(57.0%)` | `통과(71.1%)` | `통과(85.7%)` | `통과(93.0%)` | `통과(93.3%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_170615_codex_java_multi_spot_reqrep_pollcompletion_full_20260521.txt`. 256B는 throughput 비율이 Java SPOT 계열 최소 기준 60% 아래라 미달로 둔다. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(64.5%)` | `통과(69.3%)` | `통과(66.9%)` | `통과(89.7%)` | `통과(88.9%)` | `통과(80.9%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_STREAM` | `통과(94.4%)` | `통과(93.5%)` | `통과(95.2%)` | `통과(92.0%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |

측정 결과 파일:

- C 기준: `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`, `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`, `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`, `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`, `perf_c_multi_linux_20260519_234246_codex_c_tls_stream64_debug_recheck_20260519.txt`, `perf_c_multi_linux_20260519_235205_codex_c_tls_stream256_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234152_codex_c_tls_stream1024_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234308_codex_c_tls_stream65536_recheck_20260519.txt`, `perf_c_multi_linux_20260520_232413_codex_c_tcp_multi_sendsend_current_all_for_node_20260520.txt`, `perf_c_multi_linux_20260521_142745_codex_c_wss_spot262144_for_java_20260521.txt`
- Java 측정: `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`, `perf_java_multi_linux_20260521_135027_codex_java_tcp_spot262144_recheck3_20260521.txt`, `perf_java_multi_linux_20260521_140328_codex_java_tcp_dr_pollset_mask_20260521.txt`, `perf_java_multi_linux_20260521_141820_codex_java_tcp_rr_stop_reliable_20260521.txt`, `perf_java_multi_linux_20260521_140504_codex_java_ws_multi_remeasure_20260521.txt`, `perf_java_multi_linux_20260521_141913_codex_java_ws_spot262144_recheck_20260521.txt`, `perf_java_multi_linux_20260521_141952_codex_java_wss_multi_remeasure_20260521.txt`, `perf_java_multi_linux_20260521_142852_codex_java_wss_spot262144_recheck2_20260521.txt`, `perf_java_multi_linux_20260521_142852_codex_java_wss_spot_reqrep262144_recheck_20260521.txt`, `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`, `perf_java_multi_linux_20260521_143917_codex_java_tls_spot_reqrep262144_recheck_20260521.txt`, `perf_java_multi_linux_20260521_143926_codex_java_tls_spot_sendsend262144_recheck_20260521.txt`, `perf_java_multi_linux_20260521_143939_codex_java_tls_spot262144_recheck2_20260521.txt`, `perf_java_multi_linux_20260521_170615_codex_java_multi_spot_reqrep_pollcompletion_full_20260521.txt`, `perf_java_multi_linux_20260521_171039_codex_java_multi_spot_reqrep_ws64_recheck_20260521.txt`
- 현재 표의 일부 small SPOT 계열 미달은 최신 재측정값을 그대로 반영한 것이다. 통과로 바꾸기 위한 sleep/backoff나 HWM 숫자 튜닝은 적용하지 않았다.

### 6.5 Node 상태

#### 6.5.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `PAIR` | `통과(37.4%)` | `통과(38.9%)` | `통과(54.1%)` | `통과(62.7%)` | `통과(67.2%)` | `통과(55.2%)` | C: `perf_c_single_linux_20260520_000429_codex_c_tcp_single_duration5_for_dotnet_20260520.txt`; Node: `perf_node_single_linux_20260520_115303_codex_node_tcp_single_full_reuse_recv_final_20260520.txt`. 이전 full smoke `perf_node_single_linux_20260520_105505_codex_node_tcp_single_smoke_20260520.txt`, PAIR all-size `perf_node_single_linux_20260520_105627_codex_node_tcp_single_pair_all_recheck_20260520.txt`, `perf_node_single_linux_20260520_112838_codex_node_tcp_single_smoke_autoslots_20260520.txt`는 stop token 앞 backlog drain timeout 또는 partial이라 통과 근거에서 제외한다. generic single sender에서 C에 없는 post-active phase-2 payload를 제거하고, stop token retry를 bounded 처리했다. receiver는 caller-provided `Received`/`TopicMessage` 저장소를 재사용한다. active sender flow-control gate와 active retry sleep은 C single 의미와 달라 제거했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `PUBSUB` | `통과(35.6%)` | `통과(42.1%)` | `통과(51.8%)` | `통과(64.7%)` | `통과(62.0%)` | `통과(53.0%)` | C/Node 파일은 위 PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `DEALER_DEALER` | `통과(36.0%)` | `통과(36.4%)` | `통과(51.0%)` | `통과(64.0%)` | `통과(60.1%)` | `통과(38.9%)` | C/Node 파일은 위 PAIR 행과 같다. 이전 all-size `perf_node_single_linux_20260520_105912_codex_node_tcp_single_dealer_dealer_all_20260520.txt`는 256B timeout이라 통과 근거에서 제외한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `DEALER_ROUTER` | `통과(66.9%)` | `통과(71.8%)` | `통과(70.0%)` | `보류(15.4%)` | `보류(12.5%)` | `보류(11.4%)` | C: `perf_c_single_linux_20260520_000429_codex_c_tcp_single_duration5_for_dotnet_20260520.txt`; Node: `perf_node_single_linux_20260520_140736_codex_node_tcp_single_routed_current_all_20260520.txt`. C의 `zlink_router_recv_part` 의미에 맞춰 Node `RouterSocket.recvPayloadInto`가 native 단일 파트 receive helper를 직접 호출하게 했다. sender worker는 C처럼 receiver가 wire stop token을 본 뒤 닫히도록 lifetime을 맞춰 262144B timeout/no-data를 제거했다. `DEALER` 송신은 기존 public `sendFrom(buffer)` 단일 파트 경로를 사용한다. active sender flow-control gate, active retry sleep, flow-credit batching은 C single 의미와 달라 제거했다. routed large는 timeout 없이 유효 수치를 얻었지만 Node/Python routed one-way 최소 기준 33% 아래다. 남은 개선 후보는 C public API의 단일 메시지 호출 의미를 벗어나는 batch drain이나 perf 전용 control API라 적용하지 않고 보류한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `ROUTER_ROUTER` | `통과(79.2%)` | `통과(73.8%)` | `통과(68.5%)` | `보류(12.7%)` | `보류(11.4%)` | `보류(12.5%)` | C/Node 파일은 위 DEALER_ROUTER 행과 같다. 기존 public `send(rid).message(...).submit()` 표면은 유지하면서 내부 blocking single-part 전송을 C의 `zlink_send_part_rid` helper로 정렬했다. large 수치는 timeout 없이 확보했지만 최소 기준 33% 아래다. 남은 차이는 JS 경계와 메시지별 public receive/send 호출 비용이 지배적이며, C public API와 다른 batch drain이나 perf 전용 control API는 적용하지 않고 보류한다. auto-HWM 활성과 size별 receiver `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `SPOT` | `통과(88.3%)` | `통과(72.1%)` | `통과(64.2%)` | `통과(139.1%)` | `통과(116.9%)` | `통과(114.3%)` | C: `perf_c_single_linux_20260520_000429_codex_c_tcp_single_duration5_for_dotnet_20260520.txt`; Node: `perf_node_single_linux_20260520_140615_codex_node_tcp_single_spot_payloadinto_all_20260520.txt`. `Spot.publishFrom(topic, buffer, flags)`는 C의 `zlink_spot_publish_part` 단일 파트 helper와 같은 native Buffer 경로로 보낸다. `Spot.subscribePayloadInto(buffer, flags)`를 추가해 C의 `zlink_spot_subscribe_part`처럼 caller buffer에 단일 part payload를 복사하고 topic/routing metadata를 반환한다. 기존 `TopicMessage` 재사용만으로는 large가 통과하지 못해 반영하지 않았다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `PAIR` | `통과(36.1%)` | `통과(37.1%)` | `통과(71.5%)` | `통과(84.1%)` | `통과(56.0%)` | `통과(99.2%)` | C: `perf_c_single_linux_20260521_042759_codex_c_ws_single_current_for_node_20260521.txt`; Node full: `perf_node_single_linux_20260521_044735_codex_node_ws_single_current_20260521.txt`; Node 64B/131072B 제한 재측정: `perf_node_single_linux_20260521_044900_codex_node_ws_single_pair_recheck_20260521.txt`. full run의 131072B 흔들림은 제한 repeat5 complete 파일로 다시 확인했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `PUBSUB` | `통과(73.4%)` | `통과(38.7%)` | `통과(60.8%)` | `통과(99.8%)` | `통과(99.3%)` | `통과(38.1%)` | C: `perf_c_single_linux_20260521_042759_codex_c_ws_single_current_for_node_20260521.txt`; Node full: `perf_node_single_linux_20260521_044735_codex_node_ws_single_current_20260521.txt`; Node 64B direct payload 재측정: `perf_node_single_linux_20260521_045324_codex_node_ws_single_pubsub64_payloadinto_20260521.txt`; Node 262144B 제한 재측정: `perf_node_single_linux_20260521_045115_codex_node_ws_single_pubsub_dd_recheck_20260521.txt`. single PUBSUB도 C의 single-part publish/subscribe 의미에 맞춰 internal `publishDirect`와 `subscribePayloadInto` 경로로 정렬했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `DEALER_DEALER` | `통과(35.4%)` | `통과(36.8%)` | `통과(71.6%)` | `통과(99.9%)` | `통과(98.7%)` | `통과(99.5%)` | C: `perf_c_single_linux_20260521_042759_codex_c_ws_single_current_for_node_20260521.txt`; Node full: `perf_node_single_linux_20260521_044735_codex_node_ws_single_current_20260521.txt`; Node 64B/262144B 제한 재측정: `perf_node_single_linux_20260521_045115_codex_node_ws_single_pubsub_dd_recheck_20260521.txt`. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `DEALER_ROUTER` | `통과(68.3%)` | `통과(77.5%)` | `통과(108.4%)` | `보류(32.2%)` | `보류(22.3%)` | `보류(21.4%)` | C: `perf_c_single_linux_20260521_042759_codex_c_ws_single_current_for_node_20260521.txt`; Node full: `perf_node_single_linux_20260521_044735_codex_node_ws_single_current_20260521.txt`; Node large repeat5: `perf_node_single_linux_20260521_045618_codex_node_ws_single_routed_large_recheck_20260521.txt`. large는 timeout 없이 유효 수치를 얻었지만 Node/Python routed one-way 최소 기준 33% 아래라 보류한다. C public API와 다른 batch drain이나 perf 전용 control API는 적용하지 않는다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `ROUTER_ROUTER` | `통과(70.6%)` | `통과(78.5%)` | `통과(105.1%)` | `보류(30.5%)` | `보류(21.9%)` | `보류(21.3%)` | C: `perf_c_single_linux_20260521_042759_codex_c_ws_single_current_for_node_20260521.txt`; Node full: `perf_node_single_linux_20260521_044735_codex_node_ws_single_current_20260521.txt`; Node large repeat5: `perf_node_single_linux_20260521_045618_codex_node_ws_single_routed_large_recheck_20260521.txt`. large는 timeout 없이 유효 수치를 얻었지만 최소 기준 33% 아래라 보류한다. 기존 public routed send 표면은 유지하고, C public API와 다른 batch drain이나 perf 전용 control API는 적용하지 않는다. auto-HWM 활성과 size별 receiver `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `SPOT` | `통과(87.8%)` | `통과(70.9%)` | `통과(72.0%)` | `통과(216.0%)` | `통과(204.1%)` | `통과(110.2%)` | C: `perf_c_single_linux_20260521_042759_codex_c_ws_single_current_for_node_20260521.txt`; Node: `perf_node_single_linux_20260521_044735_codex_node_ws_single_current_20260521.txt`. `Spot.publishFrom`/`Spot.subscribePayloadInto` 단일 payload 경로로 C 의미와 맞췄다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `PAIR` | `보류(33.8%)` | `통과(36.6%)` | `통과(102.4%)` | `통과(128.9%)` | `통과(110.0%)` | `통과(100.9%)` | C full: `perf_c_single_linux_20260521_045850_codex_c_wss_single_current_for_node_20260521.txt`; Node full: `perf_node_single_linux_20260521_051840_codex_node_wss_single_current_20260521.txt`; 64B C 제한 재측정: `perf_c_single_linux_20260521_052435_codex_c_wss_single_pair64_recheck_for_node_20260521.txt`; 64B Node 제한 재측정: `perf_node_single_linux_20260521_052427_codex_node_wss_single_pair64_recheck_20260521.txt`. 64B는 timeout 없이 유효 수치를 얻었지만 Node/Python 단순 one-way 최소 기준 35% 아래다. public `recvInto` 재사용 후보는 `perf_node_single_linux_20260521_052635_codex_node_wss_single_pair64_recvinto_recheck_20260521.txt`에서 run 변동으로 median이 14.5%까지 떨어져 반영하지 않았다. C public API와 다른 batch drain이나 시작 제어 API는 적용하지 않고 보류한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `PUBSUB` | `통과(68.9%)` | `통과(90.6%)` | `통과(149.6%)` | `통과(106.0%)` | `통과(110.8%)` | `통과(109.5%)` | C full: `perf_c_single_linux_20260521_045850_codex_c_wss_single_current_for_node_20260521.txt`; Node full: `perf_node_single_linux_20260521_051840_codex_node_wss_single_current_20260521.txt`; Node 64B/65536B 제한 재측정: `perf_node_single_linux_20260521_052336_codex_node_wss_single_pubsub_payloadinto_blocking_recheck_20260521.txt`. `subscribePayloadInto` receive loop를 C처럼 첫 receive는 blocking, burst drain은 `DontWait`로 정렬해 65536B median 저하를 해소했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `DEALER_DEALER` | `통과(35.0%)` | `통과(36.9%)` | `통과(101.5%)` | `통과(130.1%)` | `통과(116.1%)` | `통과(100.9%)` | C/Node full 파일은 위 wss PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `DEALER_ROUTER` | `통과(63.7%)` | `통과(75.9%)` | `통과(146.8%)` | `통과(74.5%)` | `통과(77.4%)` | `통과(78.6%)` | C/Node full 파일은 위 wss PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `ROUTER_ROUTER` | `통과(70.7%)` | `통과(78.3%)` | `통과(136.9%)` | `통과(75.4%)` | `통과(75.8%)` | `통과(78.4%)` | C/Node full 파일은 위 wss PAIR 행과 같다. `ROUTER_ROUTER / DEALER_ROUTER` 상대 기준은 절대 기준 통과 항목의 진단 보조로만 사용한다. auto-HWM 활성과 size별 receiver `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `SPOT` | `통과(84.9%)` | `통과(65.5%)` | `통과(455.4%)` | `통과(171.7%)` | `통과(167.4%)` | `통과(172.2%)` | C/Node full 파일은 위 wss PAIR 행과 같다. `Spot.publishFrom`/`Spot.subscribePayloadInto` 단일 payload 경로로 C 의미와 맞췄다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `PAIR` | `보류(33.5%)` | `통과(35.8%)` | `통과(77.9%)` | `통과(86.3%)` | `통과(101.6%)` | `통과(102.2%)` | C full: `perf_c_single_linux_20260521_052855_codex_c_tls_single_current_for_node_20260521.txt`; Node full: `perf_node_single_linux_20260521_054834_codex_node_tls_single_current_20260521.txt`; Node 64B/131072B 제한 재측정: `perf_node_single_linux_20260521_055052_codex_node_tls_single_pair_dd_recheck_20260521.txt`. 64B는 timeout 없이 유효 수치를 얻었지만 Node/Python 단순 one-way 최소 기준 35% 아래다. wss와 같은 public `recvInto` 후보는 안정적인 통과 근거를 만들지 못했고, C public API와 다른 batch drain이나 시작 제어 API는 적용하지 않고 보류한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `PUBSUB` | `통과(67.5%)` | `통과(82.1%)` | `통과(145.1%)` | `통과(75.1%)` | `통과(99.7%)` | `통과(100.3%)` | C/Node full 파일은 위 tls PAIR 행과 같다. `subscribePayloadInto` receive loop는 C처럼 첫 receive blocking, burst drain `DontWait` 구조를 사용한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `DEALER_DEALER` | `보류(32.9%)` | `통과(36.3%)` | `통과(78.8%)` | `통과(101.1%)` | `통과(100.1%)` | `통과(101.7%)` | C/Node full 파일은 위 tls PAIR 행과 같다. 64B와 131072B는 `perf_node_single_linux_20260521_055052_codex_node_tls_single_pair_dd_recheck_20260521.txt`로 제한 재측정했다. 131072B는 통과 확인됐고, 64B는 timeout 없이 유효 수치를 얻었지만 단순 one-way 최소 기준 35% 아래라 보류한다. C public API와 다른 batch drain이나 시작 제어 API는 적용하지 않는다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `DEALER_ROUTER` | `통과(63.1%)` | `통과(70.7%)` | `통과(110.5%)` | `통과(54.9%)` | `통과(53.7%)` | `통과(51.3%)` | C/Node full 파일은 위 tls PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `ROUTER_ROUTER` | `통과(72.3%)` | `통과(77.3%)` | `통과(113.2%)` | `통과(53.9%)` | `통과(51.8%)` | `통과(53.0%)` | C/Node full 파일은 위 tls PAIR 행과 같다. `ROUTER_ROUTER / DEALER_ROUTER` 상대 기준은 절대 기준 통과 항목의 진단 보조로만 사용한다. auto-HWM 활성과 size별 receiver `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `SPOT` | `통과(85.4%)` | `통과(68.6%)` | `통과(74.1%)` | `통과(162.7%)` | `통과(161.8%)` | `통과(171.1%)` | C/Node full 파일은 위 tls PAIR 행과 같다. `Spot.publishFrom`/`Spot.subscribePayloadInto` 단일 payload 경로로 C 의미와 맞췄다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |

#### 6.5.2 Multi suite

2026-05-21 재측정 결과로 대표 표를 갱신했다. 판정은 `doc/perf` 기준처럼 C `bindings/c/perf`와 같은 suite/pattern/transport/size의 throughput 비율로 계산한다. HWM은 튜닝 값으로 쓰지 않고, auto-HWM 활성 여부와 size별 `MsgUnit(B)` 일치 여부만 확인한다.

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `MULTI_DEALER_DEALER` | `통과(77.0%)` | `통과(91.5%)` | `통과(78.4%)` | `통과(73.5%)` | `통과(71.3%)` | `통과(65.5%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(48.0%)` | `통과(48.2%)` | `통과(45.4%)` | `통과(31.3%)` | `통과(46.9%)` | `통과(64.2%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(34.1%)` | `통과(34.9%)` | `통과(34.3%)` | `통과(31.8%)` | `통과(45.5%)` | `통과(72.0%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_PUBSUB` | `통과(35.8%)` | `통과(39.3%)` | `통과(82.9%)` | `통과(56.3%)` | `통과(54.9%)` | `통과(64.5%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_SPOT` | `통과(45.8%)` | `통과(94.4%)` | `통과(64.3%)` | `통과(70.5%)` | `통과(79.2%)` | `통과(97.8%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(54.3%)` | `통과(50.4%)` | `통과(44.9%)` | `통과(59.9%)` | `통과(63.7%)` | `통과(70.2%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_171325_codex_node_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(93.0%)` | `통과(90.9%)` | `통과(96.7%)` | `통과(108.8%)` | `통과(205.4%)` | `통과(140.7%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_STREAM` | `통과(72.4%)` | `통과(73.7%)` | `통과(73.1%)` | `통과(91.1%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(99.4%)` | `통과(87.2%)` | `통과(77.2%)` | `통과(60.8%)` | `통과(57.5%)` | `통과(89.6%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(86.7%)` | `통과(85.1%)` | `통과(84.8%)` | `통과(53.2%)` | `통과(57.1%)` | `통과(65.6%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(36.3%)` | `통과(34.3%)` | `통과(33.7%)` | `통과(30.7%)` | `통과(51.5%)` | `통과(66.3%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_PUBSUB` | `통과(44.3%)` | `통과(40.4%)` | `통과(46.7%)` | `통과(67.4%)` | `통과(62.1%)` | `통과(85.3%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT` | `통과(72.6%)` | `통과(71.6%)` | `통과(66.5%)` | `통과(48.7%)` | `통과(71.6%)` | `통과(99.3%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(62.1%)` | `통과(57.0%)` | `통과(56.8%)` | `통과(81.1%)` | `통과(75.3%)` | `통과(87.0%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_171325_codex_node_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(97.5%)` | `통과(98.0%)` | `통과(100.4%)` | `통과(112.9%)` | `통과(116.3%)` | `통과(168.9%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_STREAM` | `통과(70.2%)` | `통과(72.2%)` | `통과(75.0%)` | `통과(92.9%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(101.6%)` | `통과(83.0%)` | `통과(66.6%)` | `통과(54.0%)` | `통과(62.2%)` | `통과(65.2%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(79.3%)` | `통과(76.5%)` | `통과(73.5%)` | `통과(55.5%)` | `통과(60.9%)` | `통과(60.0%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(37.6%)` | `통과(34.0%)` | `통과(34.0%)` | `통과(52.5%)` | `통과(57.1%)` | `통과(57.1%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_PUBSUB` | `통과(36.4%)` | `통과(38.3%)` | `통과(53.7%)` | `통과(53.5%)` | `통과(54.1%)` | `통과(58.8%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_SPOT` | `통과(85.0%)` | `통과(253.3%)` | `통과(155.3%)` | `통과(46.9%)` | `통과(48.8%)` | `통과(40.0%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(62.1%)` | `통과(57.9%)` | `통과(64.8%)` | `통과(95.8%)` | `통과(94.4%)` | `통과(93.0%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_171325_codex_node_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(92.8%)` | `통과(96.2%)` | `통과(108.4%)` | `통과(107.7%)` | `통과(109.3%)` | `통과(103.1%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_STREAM` | `통과(92.6%)` | `통과(93.1%)` | `통과(93.1%)` | `통과(97.0%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(100.9%)` | `통과(86.7%)` | `통과(74.8%)` | `통과(56.5%)` | `통과(55.2%)` | `통과(54.8%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(88.5%)` | `통과(86.3%)` | `통과(84.3%)` | `통과(52.7%)` | `통과(55.7%)` | `통과(56.2%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(37.0%)` | `통과(35.0%)` | `통과(34.9%)` | `통과(47.3%)` | `통과(58.0%)` | `통과(56.3%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_PUBSUB` | `통과(35.3%)` | `통과(36.9%)` | `통과(47.5%)` | `통과(53.9%)` | `통과(58.6%)` | `통과(59.3%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT` | `통과(70.1%)` | `통과(92.6%)` | `통과(106.3%)` | `통과(98.2%)` | `통과(110.6%)` | `통과(150.7%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(60.5%)` | `통과(54.0%)` | `통과(49.1%)` | `통과(86.4%)` | `통과(87.9%)` | `통과(89.5%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_171325_codex_node_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(89.7%)` | `통과(93.2%)` | `통과(96.2%)` | `통과(100.9%)` | `통과(103.5%)` | `통과(153.7%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_STREAM` | `통과(90.8%)` | `통과(86.4%)` | `통과(91.9%)` | `통과(102.4%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |

측정 결과 파일:

- C 기준: `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`, `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`, `perf_c_multi_linux_20260520_234521_codex_c_ws_multi_spot64_256_repro_20260520.txt`, `perf_c_multi_linux_20260521_001957_codex_c_ws_multi_spot1024_262144_recheck_for_node_20260521.txt`, `perf_c_multi_linux_20260520_235328_codex_c_ws_multi_spot65536_repeat3_seq_20260520.txt`, `perf_c_multi_linux_20260520_235741_codex_c_ws_multi_spot131072_repro_debug_20260520.txt`, `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`, `perf_c_multi_linux_20260521_015925_codex_c_wss_multi_pubsub_all_for_node_20260521.txt`, `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`, `perf_c_multi_linux_20260520_232413_codex_c_tcp_multi_sendsend_current_all_for_node_20260520.txt`, `perf_c_multi_linux_20260521_011758_codex_c_ws_multi_sendsend_all_for_node_20260521.txt`
- Node 측정: `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`, `perf_node_multi_linux_20260521_145134_codex_node_tcp_spot131072_recheck_20260521.txt`, `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`, `perf_node_multi_linux_20260521_003015_codex_node_ws_multi_spot_large_recheck_for_node_20260521.txt`, `perf_node_multi_linux_20260521_004553_codex_node_ws_multi_dd_native_sendloop_all_20260521.txt`, `perf_node_multi_linux_20260521_005520_codex_node_ws_multi_routed_pubsub_recheck_20260521.txt`, `perf_node_multi_linux_20260521_011145_codex_node_ws_multi_dr_native_echo_loop_all_20260521.txt`, `perf_node_multi_linux_20260521_011737_codex_node_ws_multi_spotreq_sendsend_stream_20260521.txt`, `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`, `perf_node_multi_linux_20260521_021710_codex_node_wss_multi_spot256_repro_20260521.txt`, `perf_node_multi_linux_20260521_022729_codex_node_wss_multi_routed_group_20260521.txt`, `perf_node_multi_linux_20260521_023558_codex_node_wss_multi_spot_rest_20260521.txt`, `perf_node_multi_linux_20260521_024207_codex_node_wss_multi_spotreq_sendsend_stream_20260521.txt`, `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`, `perf_node_multi_linux_20260521_034012_codex_node_tls_multi_pubsub64_256_publish_direct_20260521.txt`, `perf_node_multi_linux_20260521_171325_codex_node_multi_spot_reqrep_pollcompletion_full_20260521.txt`
- Node `MULTI_SPOT_REQREP`는 poll completion 반영 뒤 full matrix 결과로 갱신했다. 실패 은폐용 sleep/backoff나 public API 우회는 추가하지 않았다.

### 6.6 Go 상태

#### 6.6.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `PAIR` | `통과(80.7%)` | `통과(73.4%)` | `통과(126.2%)` | `통과(234.8%)` | `통과(249.1%)` | `통과(281.4%)` | C: `perf_c_single_linux_20260521_055144_codex_c_tcp_single_current_for_go_20260521.txt`; Go: `perf_go_single_linux_20260521_060049_codex_go_tcp_single_current_20260521.txt`. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `PUBSUB` | `통과(59.6%)` | `통과(70.3%)` | `통과(91.4%)` | `통과(97.2%)` | `통과(95.2%)` | `통과(90.0%)` | C: `perf_c_single_linux_20260521_055144_codex_c_tcp_single_current_for_go_20260521.txt`; Go: `perf_go_single_linux_20260521_063732_codex_go_tcp_single_pubsub_spot_all_subscribepart_20260521.txt`. `SubSocket.SubscribePart(out, topicBuffer, flags)`를 추가해 C `zlink_subscribe_part`와 같은 단일 part receive 의미로 맞췄다. timeout은 없고 auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. 짧은 topic C 문자열 변환 제거, `TopicMessage` 저장소 재사용, 단일 part publish 직접 메서드 후보는 제한 재측정 `perf_go_single_linux_20260521_061807_codex_go_tcp_single_pubsub_spot_small_cstring_20260521.txt`, `perf_go_single_linux_20260521_062306_codex_go_tcp_single_pubsub_small_reuse_topic_message_20260521.txt`, `perf_go_single_linux_20260521_062619_codex_go_tcp_single_pubsub_spot_small_publishpart_20260521.txt`에서 개선 근거가 없어 반영하지 않았다. |
| `tcp` | `DEALER_DEALER` | `통과(81.7%)` | `통과(74.0%)` | `통과(122.1%)` | `통과(226.0%)` | `통과(246.1%)` | `통과(277.5%)` | C/Go full 파일은 위 PAIR 행과 같다. full run의 131072B `exit_nonzero`는 제한 재측정 `perf_go_single_linux_20260521_061453_codex_go_tcp_single_dealer_dealer_131072_debug_20260521.txt`에서 complete로 확인했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `DEALER_ROUTER` | `통과(50.9%)` | `통과(50.6%)` | `통과(57.2%)` | `통과(44.0%)` | `통과(43.9%)` | `통과(43.2%)` | C/Go full 파일은 위 PAIR 행과 같다. full run의 65536B `exit_nonzero`는 제한 재측정 `perf_go_single_linux_20260521_061433_codex_go_tcp_single_dealer_router_65536_debug_20260521.txt`에서 complete로 확인했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `ROUTER_ROUTER` | `통과(55.4%)` | `통과(54.6%)` | `통과(58.6%)` | `통과(43.8%)` | `통과(40.4%)` | `통과(45.8%)` | C/Go 파일은 위 PAIR 행과 같다. routed one-way 기준을 통과했고 auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `SPOT` | `통과(152.0%)` | `통과(121.6%)` | `통과(112.4%)` | `통과(82.8%)` | `통과(81.5%)` | `통과(71.0%)` | C: `perf_c_single_linux_20260521_055144_codex_c_tcp_single_current_for_go_20260521.txt`; Go: `perf_go_single_linux_20260521_065622_codex_go_tcp_single_spot_sender_yield_20260521.txt`. `Spot.SubscribePart(out, topicBuffer, flags)`를 추가해 C `zlink_spot_subscribe_part`와 같은 단일 part receive 의미로 맞췄다. active publish는 C처럼 `DONTWAIT`를 사용하고 backpressure 때 1ms 대기한다. SPOT active receive는 C처럼 poller 없이 `DONTWAIT` drain과 yield를 사용한다. Go sender는 C의 별도 sender/receiver thread 진행 의미를 맞추기 위해 성공 send 뒤 `runtime.Gosched()`로 receiver goroutine에 양보한다. C SPOT은 `NODROP`을 설정하지 않으므로 Go SPOT에서도 `SetNoDrop(true)`를 제거했다. timeout은 없고 auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. 짧은 topic C 문자열 변환 제거와 단일 part publish 직접 메서드 후보는 제한 재측정 `perf_go_single_linux_20260521_061807_codex_go_tcp_single_pubsub_spot_small_cstring_20260521.txt`, `perf_go_single_linux_20260521_062619_codex_go_tcp_single_pubsub_spot_small_publishpart_20260521.txt`에서 개선 근거가 없어 반영하지 않았다. |
| `ws` | `PAIR` | `통과(73.7%)` | `미달(66.6%)` | `통과(88.6%)` | `통과(97.1%)` | `통과(95.5%)` | `통과(91.4%)` | C: `perf_c_single_linux_20260521_090653_codex_c_ws_single_current_after_core_rebuild_for_go_20260521.txt`; Go: `perf_go_single_linux_20260521_085525_codex_go_ws_single_full_after_routed_timestamp_20260521.txt`. 256B는 throughput 비율은 목표권이지만 latency가 C 대비 139.0x라 미달로 둔다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `PUBSUB` | `미달(58.5%)` | `미달(57.3%)` | `통과(89.2%)` | `통과(97.0%)` | `통과(95.6%)` | `통과(90.1%)` | C/Go 파일은 위 PAIR 행과 같다. 64B와 256B는 throughput 비율은 목표권이지만 latency가 각각 C 대비 86.0x, 63.9x라 미달로 둔다. receive hot path는 C처럼 첫 수신 blocking, 이후 `DONTWAIT` burst drain이며 auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `DEALER_DEALER` | `통과(73.9%)` | `미달(66.7%)` | `통과(95.4%)` | `통과(97.5%)` | `통과(95.7%)` | `통과(91.4%)` | C/Go 파일은 위 PAIR 행과 같다. 256B는 throughput 비율은 목표권이지만 latency가 C 대비 149.7x라 미달로 둔다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `DEALER_ROUTER` | `통과(56.1%)` | `통과(58.1%)` | `통과(74.9%)` | `통과(83.5%)` | `통과(97.8%)` | `통과(81.3%)` | C/Go 파일은 위 PAIR 행과 같다. Go routed active phase를 C `perf_dealer_router.cpp`처럼 sender goroutine의 blocking send와 receiver의 blocking `RecvPart` stop-token 루프로 맞췄다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `ROUTER_ROUTER` | `통과(65.4%)` | `통과(55.9%)` | `통과(67.5%)` | `통과(90.9%)` | `통과(94.6%)` | `통과(81.7%)` | C/Go 파일은 위 PAIR 행과 같다. ROUTER-ROUTER도 C처럼 PING/PONG으로 target route를 확인한 뒤 active와 stop token을 blocking send 의미로 보낸다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `SPOT` | `미달(144.5%)` | `미달(118.4%)` | `통과(110.3%)` | `통과(103.4%)` | `통과(94.1%)` | `통과(83.6%)` | C/Go 파일은 위 PAIR 행과 같다. 64B와 256B는 throughput은 C보다 높지만 latency가 각각 C 대비 527.4x, 38.5x라 미달로 둔다. SPOT은 C와 같은 `DONTWAIT` publish/backpressure 대기 의미와 `SubscribePart` 수신 경로를 유지하며 auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |

#### 6.6.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |
| `ws` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |
| `wss` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |
| `tls` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |

### 6.7 Rust 상태

#### 6.7.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |

#### 6.7.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |
| `ws` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |
| `wss` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |
| `tls` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |

### 6.8 Python 상태

#### 6.8.1 Single suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `PAIR` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |

#### 6.8.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tcp` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |
| `ws` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `ws` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |
| `wss` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `wss` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |
| `tls` | `MULTI_DEALER_DEALER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_DEALER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_ROUTER_ROUTER` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_PUBSUB` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_SPOT` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_SPOT_REQREP` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_SPOT_SENDSEND` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | `미측정` | 신규 측정 대기 |
| `tls` | `MULTI_STREAM` | `미측정` | `미측정` | `미측정` | `미측정` | `해당 없음` | `해당 없음` | 신규 측정 대기 |

## 7. 완료 기준

아래 조건을 모두 만족하면 해당 언어 binding 작업을 완료한다.

- single과 multi의 대상 조합이 모두 목표 비율 이상이다.
- 상세 상태 표에 `미측정` 또는 `미달`이 하나도 남아 있지 않다.
- perf 결과가 `doc/perf` 정책과 `bindings/c/perf` 의미를 유지한다.
- perf 코드를 수정했다면 버그 또는 정책 위반 근거가 남아 있다.
- binding 라이브러리 변경에 필요한 테스트가 통과한다.
- 실행 중 발견된 이슈가 모두 리뷰되었고, 필요한 테스트와 수정이 끝났다.
- 이 문서가 실제 실행 절차와 판단 기준을 최신 상태로 반영한다.
- 결과 파일 경로와 C 대비 비율 요약이 최종 보고에 포함된다.

모든 대상 언어가 완료되면 최종 요약에는 언어별 최저 비율, 남은 예외, 수정한 파일, 실행한 perf 명령을 함께 기록한다.
