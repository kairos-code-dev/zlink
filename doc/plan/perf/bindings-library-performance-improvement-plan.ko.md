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

Node 개선 라운드는 위 표의 Node/Python 최소 통과 기준보다 5%p 높은 값을 우선 목표로
본다. 따라서 `MULTI_PUBSUB` 같은 단순 one-way는 최소 40%, `MULTI_ROUTER_ROUTER`
같은 multi routed echo는 최소 35%, routed one-way와 SPOT 계열은 각각 최소 38%를
사용해 신규 미달 후보를 고른다. Python 판정 기준은 이 Node 개선 라운드로 함께 올리지
않는다.

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
| 1 | C++ | `bindings/cpp/perf` | `tcp/ws/wss/tls 통과` | `tcp/ws/wss/tls 재측정 완료` | 2026-05-21 재측정 결과를 6.2.2 대표 표에 반영 |
| 2 | .NET | `bindings/dotnet/perf` | `tcp/ws/wss/tls 통과` | `tcp/ws/wss/tls 재측정 완료` | 2026-05-21 poller slot API 반영 뒤 재측정 결과를 6.3.2 대표 표에 반영 |
| 3 | Java | `bindings/java/perf` | `tcp/ws/wss/tls 통과` | `tcp/ws/wss/tls 재측정 완료, full-run partial 행은 제한 재측정으로 보강` | 2026-05-21 `ROUTER_ROUTER` stop token 처리와 poll mask 수정 뒤 결과를 6.4.2 대표 표에 반영 |
| 4 | Node | `bindings/node/perf` | `tcp/ws single routed large 보류, wss PAIR 64B 보류, tls PAIR 64B 및 DEALER_DEALER 64B 보류 외 통과` | `tcp/ws/wss/tls 재측정 완료, tcp full-run partial 행은 제한 재측정으로 보강` | 2026-05-21 tcp 재측정 결과를 6.5.2 대표 표에 반영 |
| 5 | Go | `bindings/go/perf` | `tcp/tls single 통과, ws/wss single latency 보류, wss SPOT 262144B 보류` | `tcp/ws/wss/tls 측정 완료` | Go multi `wss/tls` 미달 항목 개선 |
| 6 | Rust | `bindings/rust/perf` | `미측정` | `미측정` | 새 측정 라운드에서 `tcp`부터 transport 우선으로 측정 |
| 7 | Python | `bindings/python/perf` | `미측정` | `미측정` | 새 측정 라운드에서 `tcp`부터 transport 우선으로 측정 |

#### 6.1.1 언어별 평균 성능

아래 지표는 현재 측정값이 있는 C++, .NET, Java, Node, Go를 계산한다. 각 언어의
Single/Multi 상태표에서 `통과(비율%)`, `미달(비율%)`, `보류(비율%)` 형식의 측정 셀을
모두 모아 C 대비 throughput 비율을 계산한다. `해당 없음`과 `미측정`은 제외한다.

단순 평균은 높은 outlier에 쉽게 끌려간다. 그래서 중앙값, p10, 최저 10% 평균을 함께 본다.
p10은 하위 10% 경계값이고, 최저 10% 평균은 가장 느린 구간의 체감 위험을 보기 위한
보조 지표다.

| 언어 | 측정 셀 수 | 평균 | 중앙값 | p10 | 최저 10% 평균 | Single 평균 | Multi 평균 |
|------|------------|------|--------|-----|---------------|-------------|------------|
| C++ | 328 | 104.8% | 98.0% | 83.0% | 79.3% | 112.9% | 98.4% |
| .NET | 328 | 93.3% | 91.0% | 63.9% | 59.9% | 103.1% | 85.7% |
| Java | 328 | 101.4% | 95.5% | 67.2% | 60.6% | 119.2% | 87.5% |
| Node | 328 | 76.1% | 71.5% | 37.5% | 29.3% | 78.7% | 74.1% |
| Go | 332 | 68.7% | 62.5% | 29.2% | 14.2% | 90.8% | 51.8% |

#### 6.1.2 C 대비 고성능 outlier 재검토

C 대비 성능이 크게 높은 항목은 그대로 좋은 결과로 확정하지 않는다. 특히 120% 이상
항목은 아래 순서로 다시 본다.

1. 같은 날짜, 같은 core/build, 같은 transport/pattern/size 조건으로 C 기준을 재측정한다.
2. active window, stop token, drain grace, latency sample, client 수, MsgUnit(B)이 C/perf와
   같은 의미인지 확인한다.
3. binding 쪽 최적화가 public API 내부 최적화인지, perf 전용 의미 변경인지 구분한다.
4. public API 내부 최적화라면 C/perf 또는 다른 binding에도 적용 가능한지 후보로 남긴다.

현재 표에서 120% 이상 outlier는 아래 그룹에 몰려 있다.

| 언어 | 주요 outlier 그룹 | 최대값 | 1차 해석 | 후속 확인 |
|------|------------------|--------|----------|-----------|
| C++ | Single `PUBSUB`, Multi `MULTI_SPOT` | 재측정 후 88.1% (`wss MULTI_SPOT 1024B`) | 기존 573.0%는 오래된 C 기준 파일 영향이 컸다. 같은 `core/build` 제한 재측정에서 C가 1608.5 Kmsg/s, C++가 1416.5 Kmsg/s였다. | C++ `PUBSUB` large outlier도 같은 방식으로 C 기준을 먼저 갱신한다. |
| .NET | Single `SPOT`, Multi `MULTI_SPOT` | 의미 정렬 후 63.5% (`wss SPOT 65536B`) | 기존 515.0%는 .NET single SPOT에 C에 없는 기본 in-flight cap이 들어간 영향이 있었다. 기본 cap을 제거하고 public `SubscribePart` 수신 경로로 C의 single-part subscribe 의미에 맞추니 C 8.31 Kmsg/s 대비 .NET 5.27 Kmsg/s가 됐다. | C에 없는 perf-only credit 제한은 기본 경로에 넣지 않는다. single SPOT은 public API 내부 수신 경로 개선으로 통과했다. |
| Java | Single routed/spot 계열, Multi `MULTI_SPOT` | 재측정 후 113.1% (`wss MULTI_SPOT 256B`) | 기존 390.2%는 C 기준 파일 시점 차이가 컸다. 같은 조건 제한 재측정에서 C가 5424.3 Kmsg/s, Java가 6134.2 Kmsg/s였다. | 남은 120% 이상 single routed/spot outlier는 같은 조건 C 재측정 뒤 JIT/fast path 영향만 분리한다. |
| Node | Single `SPOT`, Multi `MULTI_SPOT_SENDSEND` | 재측정 후 296.4% (`wss SPOT 1024B`) | C 기준을 갱신해도 Node single SPOT은 높게 남았다. 현재 Node single SPOT은 한 이벤트 루프에서 publish 후 inline drain을 수행하므로 C의 별도 publisher/receiver thread 의미와 다를 수 있다. | 이 셀은 통과로 확정하지 않고 보류한다. Node single SPOT은 C와 같은 의미의 송신/수신 분리 구조를 설계한 뒤 다시 측정한다. |
| Go | Single large one-way, Single `SPOT`, Multi `MULTI_STREAM`/`MULTI_SPOT` | 현재 281.4% (`tcp PAIR 262144B`), multi에서는 160.8% (`tls MULTI_SPOT 65536B`) | Go `tcp` large outlier는 2026-05-21 C 기준 파일을 사용한 값이라 같은 조건 C 제한 재측정으로 먼저 확인해야 한다. `wss SPOT 64B`는 2026-05-22 같은 조건 C/Go 재측정에서도 186.5%로 높게 남았다. `tls SPOT 64B/256B`도 같은 조건 C/Go 재측정에서 156.8%, 125.6%로 C보다 높게 나왔다. `ws MULTI_STREAM 262144B`는 같은 조건 C 제한 재측정 `perf_c_multi_linux_20260522_062037_codex_c_ws_multi_stream_large_for_go_20260522.txt` 대비 Go `perf_go_multi_linux_20260522_060003_codex_go_ws_multi_current_20260522.txt`에서 149.3%다. `tls MULTI_SPOT 65536B/131072B`는 같은 조건 C `perf_c_multi_linux_20260522_071155_codex_c_tls_multi_for_go_20260522.txt` 대비 Go `perf_go_multi_linux_20260522_072100_codex_go_tls_multi_current_20260522.txt`에서 160.8%, 139.8%다. | `wss/tls SPOT`은 C와 같은 sender/receiver 분리, `DONTWAIT` publish, `SubscribePart` 수신 의미를 유지한다. `MULTI_STREAM`은 shared C reference client를 쓰므로 측정 surface가 Go STREAM server라는 점을 반영해 server hot path 차이를 먼저 본다. `tls MULTI_SPOT`은 Go worker drain이 C보다 backlog를 더 공격적으로 소화하는지 확인하고, 적용 가능한 최적화가 있으면 C/perf와 다른 바인딩으로 역반영 가능한지 검토한다. tcp large outlier는 C 기준을 갱신한 뒤 public API 내부 최적화인지 다시 본다. |

2026-05-21 outlier 적용 결과:

- C++ `wss MULTI_SPOT 1024B`: C
  `perf_c_multi_linux_20260521_210632_codex_c_wss_multi_spot1024_outlier_apply_20260521.txt`,
  C++ `perf_cpp_multi_linux_20260521_210739_codex_cpp_wss_multi_spot1024_outlier_apply_20260521.txt`
  기준으로 88.1%다.
- .NET `wss SPOT 65536B`: C
  `perf_c_single_linux_20260521_210757_codex_c_wss_single_spot65536_outlier_apply_20260521.txt`,
  .NET `perf_dotnet_single_linux_20260522_005548_codex_dotnet_wss_single_spot65536_subscribe_part_20260522.txt`
  기준으로 63.5%다. C에 없는 기본 in-flight cap은 제거했고, 수신 hot path는 public `SubscribePart`로 C의 single-part subscribe 의미에 맞췄다.
- Java `wss MULTI_SPOT 256B`: C
  `perf_c_multi_linux_20260521_210845_codex_c_wss_multi_spot256_outlier_apply_20260521.txt`,
  Java `perf_java_multi_linux_20260521_210909_codex_java_wss_multi_spot256_outlier_apply_20260521.txt`
  기준으로 113.1%다.
- Node `wss SPOT 1024B`: C
  `perf_c_single_linux_20260521_210903_codex_c_wss_single_spot1024_outlier_apply_20260521.txt`,
  Node `perf_node_single_linux_20260521_210938_codex_node_wss_single_spot1024_outlier_apply_20260521.txt`
  기준으로 296.4%다. C와 같은 의미가 확인될 때까지 보류로 둔다.
- Node single SPOT의 worker 송신/receiver drain 분리도 검토했지만, JS worker 송신자가
  receiver drain보다 빠르게 active backlog를 크게 만든 뒤 stop token 관찰 전 backlog drain에
  묶였다. 이를 in-flight cap으로 막으면 C에 없는 perf-only credit 제한이 되므로 적용하지
  않았다. Node single SPOT은 public API만으로 C의 별도 native sender/receiver thread 의미를
  재현할 수 있는지 더 검토해야 한다.

잠정 이식 후보는 다음과 같다.

- **no-data 경로에서 예외를 만들지 않기**: Node `recvPayloadInto(...DontWait)` 개선처럼
  hot path에서 no-data를 값으로 돌려 예외 생성 비용을 없애는 방식은 다른 binding에서도
  public API 의미를 해치지 않는지 검토할 수 있다.
- **결과 객체 materialization 축소**: Java `Spot.subscribe` routing-id scratch cache,
  Node raw result의 불필요한 `routingId: null` 생략처럼 반복 수신에서 매번 새 객체나
  byte[]를 만들지 않는 최적화는 binding 공통 후보가 된다.
- **poller 내부 index/cache**: C++ `poller_t` socket-only modify cache와 Java `Poller`
  handle index cache는 public API를 바꾸지 않는 내부 최적화다. .NET Poller도 같은
  선형 탐색 hot path가 있는지 별도 검토한다.
- **C-style 단일 poll loop 유지**: .NET `MULTI_SPOT_SENDSEND`처럼 C/perf와 다른
  POLLOUT 중심 대기는 성능과 의미를 모두 흔들 수 있다. binding perf는 C와 같은
  signal-driven poll loop를 먼저 맞춘다.

위 후보는 성능을 올리기 위한 임의 변경이 아니라, C/perf와 의미가 같은지 확인된 뒤에만
반영한다. C보다 크게 나온 셀은 다음 측정 라운드에서 우선 재검증 대상으로 잡는다.

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
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(89.6%)` | `통과(91.9%)` | `통과(92.6%)` | `통과(123.3%)` | `통과(86.5%)` | `통과(92.1%)` | 65536B는 C `perf_c_multi_linux_20260521_190248_codex_c_tcp_multi_dr65536_for_cpp_recheck_20260521.txt` 대비 C++ `perf_cpp_multi_linux_20260521_192618_codex_cpp_tcp_routed_poller_order_recheck_20260521.txt`에서 통과했다. C++ public `poller_t` socket-only `modify()` 내부가 전체 poll item 재구성 대신 기존 등록 순서를 보존한 항목 갱신을 하도록 바꿨다. 나머지는 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(91.2%)` | `통과(91.5%)` | `통과(90.1%)` | `통과(79.2%)` | `통과(76.8%)` | `통과(76.8%)` | 65536B는 C `perf_c_multi_linux_20260521_192650_codex_c_tcp_rr_for_cpp_order_recheck_20260521.txt` 대비 C++ `perf_cpp_multi_linux_20260521_194038_codex_cpp_tcp_rr_server_direct_poll_recheck_20260521.txt`에서 통과했다. 131072B는 같은 조건 C `perf_c_multi_linux_20260521_194106_codex_c_tcp_rr131072_for_cpp_direct_poll_recheck_20260521.txt` 대비 C++ `perf_cpp_multi_linux_20260521_194121_codex_cpp_tcp_rr131072_server_direct_poll_single_recheck_20260521.txt`에서 통과했다. C++ ROUTER_ROUTER 서버는 C relay server와 같은 단일 `zlink_poll(..., -1)` 대기로 맞췄다. 나머지는 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. |
| `tcp` | `MULTI_PUBSUB` | `통과(88.2%)` | `통과(92.5%)` | `통과(108.8%)` | `통과(104.7%)` | `통과(132.7%)` | `통과(136.9%)` | 131072B는 C 대표 `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt` 대비 C++ `perf_cpp_multi_linux_20260521_191818_codex_cpp_pubsub_stream_recheck_20260521.txt`에서 통과했다. 나머지는 C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. |
| `tcp` | `MULTI_SPOT` | `통과(93.1%)` | `통과(122.7%)` | `통과(75.4%)` | `통과(93.0%)` | `통과(98.1%)` | `통과(158.0%)` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. 256B는 같은 조건 제한 재측정 C `perf_c_multi_linux_20260521_190130_codex_c_tcp_multi_spot256_for_cpp_recheck_20260521.txt` 대비 C++ `perf_cpp_multi_linux_20260521_190202_codex_cpp_tcp_multi_spot256_recheck_20260521.txt`에서 통과했다. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(101.2%)` | `통과(103.8%)` | `통과(102.4%)` | `통과(112.0%)` | `통과(110.2%)` | `통과(117.3%)` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_165846_codex_cpp_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(110.5%)` | `통과(114.3%)` | `통과(110.1%)` | `통과(115.6%)` | `통과(121.8%)` | `통과(103.4%)` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_STREAM` | `통과(103.9%)` | `통과(99.1%)` | `통과(100.9%)` | `통과(113.5%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(89.8%)` | `통과(96.5%)` | `통과(89.0%)` | `통과(89.2%)` | `통과(100.5%)` | `통과(100.8%)` | 131072B는 C 대표 `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt` 대비 C++ `perf_cpp_multi_linux_20260521_191441_codex_cpp_routed_poller_cache_recheck_20260521.txt`에서 통과했다. C++ public `poller_t` socket-only `modify()` 캐시 갱신을 적용했다. 나머지는 C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(82.1%)` | `통과(80.5%)` | `통과(84.5%)` | `통과(81.3%)` | `통과(82.6%)` | `통과(106.7%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(88.3%)` | `통과(86.4%)` | `통과(81.4%)` | `통과(112.4%)` | `통과(121.0%)` | `통과(97.0%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_PUBSUB` | `통과(92.2%)` | `통과(90.0%)` | `통과(91.9%)` | `통과(96.2%)` | `통과(99.4%)` | `통과(124.9%)` | 262144B는 C 대표 `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt` 대비 C++ `perf_cpp_multi_linux_20260521_191818_codex_cpp_pubsub_stream_recheck_20260521.txt`에서 통과했다. 나머지는 C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. |
| `ws` | `MULTI_SPOT` | `통과(95.9%)` | `통과(98.5%)` | `통과(96.8%)` | `통과(94.4%)` | `통과(96.9%)` | `통과(95.7%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(94.1%)` | `통과(96.2%)` | `통과(89.4%)` | `통과(105.8%)` | `통과(101.0%)` | `통과(84.6%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_165846_codex_cpp_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(92.1%)` | `통과(78.3%)` | `통과(79.3%)` | `통과(100.2%)` | `통과(96.4%)` | `통과(144.4%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_STREAM` | `통과(99.8%)` | `통과(98.7%)` | `통과(99.0%)` | `통과(89.7%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(91.8%)` | `통과(101.6%)` | `통과(106.1%)` | `통과(98.0%)` | `통과(94.4%)` | `통과(106.6%)` | 1024/65536/131072B는 C 대표 `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt` 대비 C++ `perf_cpp_multi_linux_20260521_191441_codex_cpp_routed_poller_cache_recheck_20260521.txt`에서 통과했다. C++ public `poller_t` socket-only `modify()` 캐시 갱신을 적용했다. 나머지는 C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(86.0%)` | `통과(83.6%)` | `통과(86.4%)` | `통과(85.0%)` | `통과(81.0%)` | `통과(82.3%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(83.0%)` | `통과(86.0%)` | `통과(84.0%)` | `통과(76.2%)` | `통과(70.4%)` | `통과(71.6%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_PUBSUB` | `통과(86.1%)` | `통과(83.8%)` | `통과(82.0%)` | `통과(80.8%)` | `통과(81.1%)` | `통과(103.7%)` | 65536/131072B는 같은 조건 C 재측정 `perf_c_multi_linux_20260521_193207_codex_c_wss_pubsub_for_cpp_recheck_20260521.txt` 대비 C++ `perf_cpp_multi_linux_20260521_191818_codex_cpp_pubsub_stream_recheck_20260521.txt`에서 통과했다. 262144B는 C 대표 `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt` 대비 같은 C++ 파일에서 통과했다. |
| `wss` | `MULTI_SPOT` | `통과(232.4%)` | `통과(462.8%)` | `통과(88.1%)` | `통과(91.2%)` | `통과(109.5%)` | `통과(108.3%)` | 1024B는 같은 조건 재측정 C `perf_c_multi_linux_20260521_210632_codex_c_wss_multi_spot1024_outlier_apply_20260521.txt` 대비 C++ `perf_cpp_multi_linux_20260521_210739_codex_cpp_wss_multi_spot1024_outlier_apply_20260521.txt`로 갱신했다. 나머지는 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(94.5%)` | `통과(93.4%)` | `통과(92.0%)` | `통과(107.4%)` | `통과(101.7%)` | `통과(100.0%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_165846_codex_cpp_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(93.6%)` | `통과(93.3%)` | `통과(80.6%)` | `통과(98.8%)` | `통과(84.6%)` | `통과(95.5%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_STREAM` | `통과(88.1%)` | `통과(87.3%)` | `통과(85.7%)` | `통과(81.5%)` | `해당 없음` | `해당 없음` | 65536B는 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt` 대비 C++ `perf_cpp_multi_linux_20260521_191818_codex_cpp_pubsub_stream_recheck_20260521.txt`에서 통과했다. 나머지는 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; C++ `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(91.3%)` | `통과(108.7%)` | `통과(92.8%)` | `통과(91.8%)` | `통과(85.2%)` | `통과(96.0%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(92.8%)` | `통과(89.8%)` | `통과(89.0%)` | `통과(84.8%)` | `통과(90.7%)` | `통과(90.3%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(95.6%)` | `통과(94.0%)` | `통과(90.6%)` | `통과(93.7%)` | `통과(143.7%)` | `통과(95.2%)` | 65536/262144B는 C 대표 `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt` 대비 C++ `perf_cpp_multi_linux_20260521_191441_codex_cpp_routed_poller_cache_recheck_20260521.txt`에서 통과했다. C++ public `poller_t` socket-only `modify()` 캐시 갱신을 적용했다. 나머지는 C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. |
| `tls` | `MULTI_PUBSUB` | `통과(80.8%)` | `통과(81.2%)` | `통과(85.9%)` | `통과(81.9%)` | `통과(82.5%)` | `통과(88.5%)` | 131072/262144B는 C 대표 `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt` 대비 C++ `perf_cpp_multi_linux_20260521_191818_codex_cpp_pubsub_stream_recheck_20260521.txt`에서 통과했다. 나머지는 C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. |
| `tls` | `MULTI_SPOT` | `통과(104.5%)` | `통과(99.9%)` | `통과(162.4%)` | `통과(101.1%)` | `통과(99.1%)` | `통과(93.4%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(92.5%)` | `통과(82.9%)` | `통과(90.4%)` | `통과(95.1%)` | `통과(98.6%)` | `통과(98.9%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_165846_codex_cpp_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(86.8%)` | `통과(92.2%)` | `통과(78.3%)` | `통과(88.4%)` | `통과(94.1%)` | `통과(94.3%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_STREAM` | `통과(82.5%)` | `통과(84.7%)` | `통과(99.8%)` | `통과(97.2%)` | `해당 없음` | `해당 없음` | 1024B는 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt` 대비 C++ `perf_cpp_multi_linux_20260521_191818_codex_cpp_pubsub_stream_recheck_20260521.txt`에서 통과했다. 나머지는 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; C++ `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`. |

측정 결과 파일:

- C 기준: `perf_c_multi_linux_20260519_194121_codex_c_tcp_multi_smoke_all_after_spot_recv_guard_20260519.txt`, `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`, `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`, `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`, `perf_c_multi_linux_20260519_234246_codex_c_tls_stream64_debug_recheck_20260519.txt`, `perf_c_multi_linux_20260519_235205_codex_c_tls_stream256_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234152_codex_c_tls_stream1024_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234308_codex_c_tls_stream65536_recheck_20260519.txt`, `perf_c_multi_linux_20260521_130140_codex_c_tcp_rr_large_repeat_for_cpp_20260521.txt`, `perf_c_multi_linux_20260521_130951_codex_c_ws_rr_large_for_cpp_20260521.txt`, `perf_c_multi_linux_20260521_132328_codex_c_wss_dd_rr_large_for_cpp_20260521.txt`, `perf_c_multi_linux_20260521_133217_codex_c_tls_sendsend262144_for_cpp_20260521.txt`, `perf_c_multi_linux_20260521_133338_codex_c_tls_rr_stream_large_for_cpp_20260521.txt`, `perf_c_multi_linux_20260521_133503_codex_c_tls_stream_large_single_for_cpp_20260521.txt`
- C++ 측정: `perf_cpp_multi_linux_20260521_123539_codex_cpp_tcp_multi_remeasure_20260521.txt`, `perf_cpp_multi_linux_20260521_130227_codex_cpp_ws_multi_remeasure_20260521.txt`, `perf_cpp_multi_linux_20260521_131047_codex_cpp_wss_multi_remeasure_20260521.txt`, `perf_cpp_multi_linux_20260521_132452_codex_cpp_tls_multi_remeasure_20260521.txt`, `perf_cpp_multi_linux_20260521_133213_codex_cpp_tls_sendsend262144_repro_20260521.txt`, `perf_cpp_multi_linux_20260521_133233_codex_cpp_tls_stream_large_recheck_20260521.txt`, `perf_cpp_multi_linux_20260521_133335_codex_cpp_tls_rr_stream_large_recheck_20260521.txt`, `perf_cpp_multi_linux_20260521_165846_codex_cpp_multi_spot_reqrep_pollcompletion_full_20260521.txt`, `perf_cpp_multi_linux_20260521_174933_codex_cpp_wss_multi_spot1024_recheck_20260521.txt`, `perf_cpp_multi_linux_20260521_191441_codex_cpp_routed_poller_cache_recheck_20260521.txt`, `perf_cpp_multi_linux_20260521_191818_codex_cpp_pubsub_stream_recheck_20260521.txt`, `perf_cpp_multi_linux_20260521_192618_codex_cpp_tcp_routed_poller_order_recheck_20260521.txt`, `perf_cpp_multi_linux_20260521_194038_codex_cpp_tcp_rr_server_direct_poll_recheck_20260521.txt`, `perf_cpp_multi_linux_20260521_194121_codex_cpp_tcp_rr131072_server_direct_poll_single_recheck_20260521.txt`
- C++ 보강 C 기준: `perf_c_multi_linux_20260521_190248_codex_c_tcp_multi_dr65536_for_cpp_recheck_20260521.txt`, `perf_c_multi_linux_20260521_192650_codex_c_tcp_rr_for_cpp_order_recheck_20260521.txt`, `perf_c_multi_linux_20260521_193207_codex_c_wss_pubsub_for_cpp_recheck_20260521.txt`, `perf_c_multi_linux_20260521_194106_codex_c_tcp_rr131072_for_cpp_direct_poll_recheck_20260521.txt`
- C++ 최신 재측정에서는 서버 poll loop와 routed echo large 보강 뒤 `tcp/ws/wss/tls` multi 대표 표의 미달 항목이 해소됐다.

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
| `wss` | `SPOT` | `통과(161%)` | `통과(106%)` | `통과(467%)` | `통과(63.5%)` | `통과(197%)` | `통과(369%)` | 65536B는 C `perf_c_single_linux_20260521_210757_codex_c_wss_single_spot65536_outlier_apply_20260521.txt` 대비 .NET `perf_dotnet_single_linux_20260522_005548_codex_dotnet_wss_single_spot65536_subscribe_part_20260522.txt`로 갱신했다. C에 없는 기본 in-flight cap을 제거하고 수신 hot path를 public `SubscribePart`로 바꿔 C의 single-part subscribe 의미에 맞췄다. auto-HWM 적용과 size별 `MsgUnit(B)` 일치를 확인했다. 나머지는 C/.NET 위 파일. |
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
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(64.4%)` | `통과(62.3%)` | `통과(63.2%)` | `통과(88.6%)` | `통과(91.1%)` | `통과(125.6%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; .NET `perf_dotnet_multi_linux_20260521_145328_codex_dotnet_tcp_multi_remeasure_20260521.txt`. 64/256/1024B는 C `perf_c_multi_linux_20260521_173900_codex_c_spot_sendsend_small_for_dotnet_probe_20260521.txt` 대비 .NET `perf_dotnet_multi_linux_20260521_173735_codex_dotnet_spot_sendsend_small_probe_20260521.txt`로 보강했다. |
| `tcp` | `MULTI_STREAM` | `통과(94.4%)` | `통과(95.0%)` | `통과(90.5%)` | `통과(95.2%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; .NET `perf_dotnet_multi_linux_20260521_145328_codex_dotnet_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(69.6%)` | `통과(82.8%)` | `통과(97.9%)` | `통과(106.0%)` | `통과(109.0%)` | `통과(174.3%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(64.2%)` | `통과(65.0%)` | `통과(66.1%)` | `통과(68.2%)` | `통과(98.2%)` | `통과(126.0%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(60.4%)` | `통과(59.8%)` | `통과(59.1%)` | `통과(60.3%)` | `통과(87.0%)` | `통과(114.3%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_PUBSUB` | `통과(78.7%)` | `통과(77.3%)` | `통과(77.7%)` | `통과(131.4%)` | `통과(147.4%)` | `통과(133.8%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT` | `통과(71.1%)` | `통과(69.6%)` | `통과(63.9%)` | `통과(103.1%)` | `통과(94.4%)` | `통과(67.3%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(73.7%)` | `통과(71.2%)` | `통과(74.4%)` | `통과(63.3%)` | `통과(99.8%)` | `통과(90.9%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_170326_codex_dotnet_multi_spot_reqrep_pollcompletion_full2_20260521.txt`. 64B는 C `perf_c_multi_linux_20260521_173721_codex_c_ws_spot_reqrep64_for_dotnet_no_managed_timer_20260521.txt` 대비 .NET `perf_dotnet_multi_linux_20260521_173705_codex_dotnet_ws_spot_reqrep64_no_managed_timer_20260521.txt`로 보강했다. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(67.4%)` | `통과(82.1%)` | `통과(69.0%)` | `통과(107.0%)` | `통과(97.0%)` | `통과(95.9%)` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 64/256/1024B는 C `perf_c_multi_linux_20260521_173900_codex_c_spot_sendsend_small_for_dotnet_probe_20260521.txt` 대비 .NET `perf_dotnet_multi_linux_20260521_173735_codex_dotnet_spot_sendsend_small_probe_20260521.txt`로 보강했다. |
| `ws` | `MULTI_STREAM` | `통과(90.7%)` | `통과(97.2%)` | `통과(92.4%)` | `통과(100.0%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(68.6%)` | `통과(88.2%)` | `통과(73.1%)` | `통과(95.7%)` | `통과(97.9%)` | `통과(105.3%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(66.1%)` | `통과(61.0%)` | `통과(62.2%)` | `통과(91.3%)` | `통과(93.5%)` | `통과(93.8%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(57.4%)` | `통과(55.6%)` | `통과(56.8%)` | `통과(92.2%)` | `통과(96.1%)` | `통과(99.1%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_PUBSUB` | `통과(68.9%)` | `통과(69.0%)` | `통과(81.5%)` | `통과(83.9%)` | `통과(96.0%)` | `통과(115.0%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_SPOT` | `통과(71.7%)` | `통과(261.4%)` | `통과(409.7%)` | `통과(67.5%)` | `통과(72.7%)` | `통과(60.4%)` | 64/1024B는 같은 조건 재측정 C `perf_c_multi_linux_20260521_195558_codex_c_dotnet_spot_remaining_recheck_20260521.txt` 대비 .NET `perf_dotnet_multi_linux_20260521_195924_codex_dotnet_spot_remaining_recheck_20260521.txt`에서 통과했다. 262144B는 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt` 대비 .NET `perf_dotnet_multi_linux_20260521_193257_codex_dotnet_spot_after_completion_poller_recheck_20260521.txt`로 갱신했다. 나머지는 .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(67.4%)` | `통과(72.1%)` | `통과(81.1%)` | `통과(100.2%)` | `통과(91.6%)` | `통과(87.0%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_170326_codex_dotnet_multi_spot_reqrep_pollcompletion_full2_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(63.2%)` | `통과(63.1%)` | `통과(77.6%)` | `통과(100.0%)` | `통과(96.0%)` | `통과(92.9%)` | 64/1024B는 같은 조건 재측정 C `perf_c_multi_linux_20260521_195558_codex_c_dotnet_spot_remaining_recheck_20260521.txt` 대비 .NET `perf_dotnet_multi_linux_20260521_204616_codex_dotnet_spot_sendsend_pollin_cstyle_recheck_20260521.txt`로 갱신했다. `SendToSpot(Message)`는 public 원본 보존 계약을 유지하면서 clone 객체 생성 없이 native copy를 바로 submit하도록 내부 경로를 줄였다. sendsend active poll loop는 C처럼 `POLLIN`만 등록하고 50ms 한도 poll 뒤 submit을 재시도하도록 맞췄다. 262144B는 C `perf_c_multi_linux_20260521_174214_codex_c_spot_sendsend_tls_small_for_dotnet_recheck_20260521.txt`, `perf_c_multi_linux_20260521_173900_codex_c_spot_sendsend_small_probe_20260521.txt` 대비 .NET `perf_dotnet_multi_linux_20260521_193257_codex_dotnet_spot_after_completion_poller_recheck_20260521.txt`로 갱신했다. 나머지는 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. |
| `wss` | `MULTI_STREAM` | `통과(84.0%)` | `통과(86.7%)` | `통과(87.2%)` | `통과(88.7%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; .NET `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(69.6%)` | `통과(88.9%)` | `통과(85.9%)` | `통과(85.2%)` | `통과(94.2%)` | `통과(97.4%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(66.1%)` | `통과(60.6%)` | `통과(61.3%)` | `통과(85.6%)` | `통과(93.3%)` | `통과(95.1%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(56.2%)` | `통과(56.2%)` | `통과(56.0%)` | `통과(79.7%)` | `통과(92.6%)` | `통과(98.2%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_PUBSUB` | `통과(69.1%)` | `통과(66.8%)` | `통과(77.4%)` | `통과(80.2%)` | `통과(90.7%)` | `통과(96.8%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT` | `통과(71.5%)` | `통과(85.7%)` | `통과(64.8%)` | `통과(97.9%)` | `통과(90.5%)` | `통과(79.1%)` | 64/1024B는 같은 조건 재측정 C `perf_c_multi_linux_20260521_195558_codex_c_dotnet_spot_remaining_recheck_20260521.txt` 대비 .NET `perf_dotnet_multi_linux_20260521_195924_codex_dotnet_spot_remaining_recheck_20260521.txt`로 갱신했다. 나머지는 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(64.2%)` | `통과(63.2%)` | `통과(68.9%)` | `통과(78.9%)` | `통과(90.6%)` | `통과(91.5%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_170326_codex_dotnet_multi_spot_reqrep_pollcompletion_full2_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(61.1%)` | `통과(61.1%)` | `통과(64.5%)` | `통과(91.0%)` | `통과(96.6%)` | `통과(86.8%)` | 64/1024B는 같은 조건 재측정 C `perf_c_multi_linux_20260521_195558_codex_c_dotnet_spot_remaining_recheck_20260521.txt` 대비 .NET `perf_dotnet_multi_linux_20260521_204616_codex_dotnet_spot_sendsend_pollin_cstyle_recheck_20260521.txt`로 갱신했다. `SendToSpot(Message)` native-copy submit 내부 최적화와 C-style sendsend active poll loop 적용 뒤 64B와 1024B 모두 최신 재측정값 기준 통과했다. 262144B는 C `perf_c_multi_linux_20260521_174214_codex_c_spot_sendsend_tls_small_for_dotnet_recheck_20260521.txt`, `perf_c_multi_linux_20260521_173900_codex_c_spot_sendsend_small_probe_20260521.txt` 대비 .NET `perf_dotnet_multi_linux_20260521_193257_codex_dotnet_spot_after_completion_poller_recheck_20260521.txt`로 갱신했다. 나머지는 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. |
| `tls` | `MULTI_STREAM` | `통과(89.9%)` | `통과(85.6%)` | `통과(84.0%)` | `통과(86.6%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; .NET `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`. STREAM small/large C 보강 파일은 아래 목록 참조. |

측정 결과 파일:

- C 기준: `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`, `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`, `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`, `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`, `perf_c_multi_linux_20260519_234246_codex_c_tls_stream64_debug_recheck_20260519.txt`, `perf_c_multi_linux_20260519_235205_codex_c_tls_stream256_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234152_codex_c_tls_stream1024_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234308_codex_c_tls_stream65536_recheck_20260519.txt`, `perf_c_multi_linux_20260521_195558_codex_c_dotnet_spot_remaining_recheck_20260521.txt`
- .NET 측정: `perf_dotnet_multi_linux_20260521_145328_codex_dotnet_tcp_multi_remeasure_20260521.txt`, `perf_dotnet_multi_linux_20260521_150044_codex_dotnet_tcp_spot_sendsend262144_recheck_20260521.txt`, `perf_dotnet_multi_linux_20260521_150054_codex_dotnet_ws_multi_remeasure_20260521.txt`, `perf_dotnet_multi_linux_20260521_150725_codex_dotnet_wss_multi_remeasure_20260521.txt`, `perf_dotnet_multi_linux_20260521_151429_codex_dotnet_tls_multi_remeasure_20260521.txt`, `perf_dotnet_multi_linux_20260521_170326_codex_dotnet_multi_spot_reqrep_pollcompletion_full2_20260521.txt`, `perf_dotnet_multi_linux_20260521_173705_codex_dotnet_ws_spot_reqrep64_no_managed_timer_20260521.txt`, `perf_dotnet_multi_linux_20260521_173735_codex_dotnet_spot_sendsend_small_probe_20260521.txt`, `perf_dotnet_multi_linux_20260521_174138_codex_dotnet_spot_sendsend_tls_small_recheck_20260521.txt`, `perf_dotnet_multi_linux_20260521_193257_codex_dotnet_spot_after_completion_poller_recheck_20260521.txt`, `perf_dotnet_multi_linux_20260521_195924_codex_dotnet_spot_remaining_recheck_20260521.txt`, `perf_dotnet_multi_linux_20260521_201737_codex_dotnet_spot_sendsend_copied_native_recheck_20260521.txt`, `perf_dotnet_multi_linux_20260521_204616_codex_dotnet_spot_sendsend_pollin_cstyle_recheck_20260521.txt`
- .NET SPOT callback request는 native timeout과 `POLLCOMPLETION` poll loop가 완료를 책임지므로 binding 내부 per-request managed timer를 제거했다. public API는 바꾸지 않았다. `SendToSpot(Message)`는 public 원본 보존 계약을 유지한 채 내부 native copy submit 경로를 줄였다. `MULTI_SPOT_SENDSEND`는 C와 같이 active poller를 `POLLIN` 중심으로 두고 50ms 한도 poll 뒤 submit을 재시도하도록 맞춘 뒤 `wss 64B`, `tls 64B`, `tls 1024B` 미달을 해소했다. 통과로 바꾸기 위한 sleep/backoff나 HWM 숫자 튜닝은 적용하지 않았다.

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
| `tcp` | `MULTI_DEALER_DEALER` | `통과(73.2%)` | `통과(91.9%)` | `통과(95.5%)` | `통과(78.9%)` | `통과(75.6%)` | `통과(68.0%)` | 65536/131072B는 같은 조건 재측정 C `perf_c_multi_linux_20260521_194538_codex_c_java_routed_miss_recheck_20260521.txt` 대비 Java `perf_java_multi_linux_20260521_194652_codex_java_routed_miss_recheck_20260521.txt`에서 통과했다. 나머지는 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(87.6%)` | `통과(74.2%)` | `통과(77.1%)` | `통과(55.6%)` | `통과(58.6%)` | `통과(92.9%)` | 65536/131072B는 같은 조건 재측정 C `perf_c_multi_linux_20260521_194538_codex_c_java_routed_miss_recheck_20260521.txt` 대비 Java `perf_java_multi_linux_20260521_194652_codex_java_routed_miss_recheck_20260521.txt`에서 통과했다. 나머지는 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(60.1%)` | `통과(61.0%)` | `통과(60.3%)` | `통과(61.0%)` | `통과(72.7%)` | `통과(96.2%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_PUBSUB` | `통과(77.3%)` | `통과(79.1%)` | `통과(96.1%)` | `통과(151.4%)` | `통과(143.2%)` | `통과(157.5%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_SPOT` | `통과(108.6%)` | `통과(88.8%)` | `통과(82.9%)` | `통과(75.6%)` | `통과(73.4%)` | `통과(83.7%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(69.4%)` | `통과(74.0%)` | `통과(77.7%)` | `통과(95.4%)` | `통과(109.5%)` | `통과(145.6%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_170615_codex_java_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(84.0%)` | `통과(82.0%)` | `통과(81.7%)` | `통과(81.2%)` | `통과(79.7%)` | `통과(63.3%)` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. large C 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_STREAM` | `통과(98.8%)` | `통과(96.4%)` | `통과(88.3%)` | `통과(107.3%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`; Java `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(69.0%)` | `통과(67.2%)` | `통과(93.6%)` | `통과(63.5%)` | `통과(65.5%)` | `통과(91.1%)` | 65536B는 같은 조건 재측정 C `perf_c_multi_linux_20260521_194538_codex_c_java_routed_miss_recheck_20260521.txt` 대비 Java `perf_java_multi_linux_20260521_194652_codex_java_routed_miss_recheck_20260521.txt`에서 통과했다. 131072B는 C 파일은 같고 Java 단독 재측정 `perf_java_multi_linux_20260521_203945_codex_java_ws_dd131072_poller_index_cache_recheck_20260521.txt`에서 통과했다. `DealerSocket.send()`는 public API를 바꾸지 않고 캡처 lambda 기반 공통 builder 대신 socket 직접 builder를 쓰도록 내부 호출 오버헤드를 줄였다. `PerfMultiDealerDealer` 수신 기록은 공통 direct active-latency 기록 API를 써서 header record 할당을 없앴고, public `Poller` 내부에 socket/spot handle index cache를 추가해 `modify()`의 선형 탐색 비용을 줄였다. full-copy 제거 프로브 `perf_java_multi_linux_20260521_194928_codex_java_ws_dd131072_direct_alloc_probe_20260521.txt`, 직접 stamp 프로브 `perf_java_multi_linux_20260521_201712_codex_java_ws_dd131072_direct_stamp_recheck_20260521.txt`, reusable message send 프로브 `perf_java_multi_linux_20260521_203729_codex_java_ws_dd131072_reusable_message_send_recheck_20260521.txt`, direct no-wait submit 프로브 `perf_java_multi_linux_20260521_203828_codex_java_ws_dd131072_dealer_direct_nowait_submit_recheck_20260521.txt`는 더 느려 반영하지 않았다. 나머지는 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; Java `perf_java_multi_linux_20260521_140504_codex_java_ws_multi_remeasure_20260521.txt`. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(71.0%)` | `통과(68.7%)` | `통과(71.7%)` | `통과(58.9%)` | `통과(59.0%)` | `통과(84.0%)` | 65536/131072B는 같은 조건 재측정 C `perf_c_multi_linux_20260521_194538_codex_c_java_routed_miss_recheck_20260521.txt` 대비 Java `perf_java_multi_linux_20260521_194652_codex_java_routed_miss_recheck_20260521.txt`에서 통과했다. 나머지는 대표 C `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`; Java `perf_java_multi_linux_20260521_140504_codex_java_ws_multi_remeasure_20260521.txt`. |
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
| `wss` | `MULTI_SPOT` | `통과(265.8%)` | `통과(113.1%)` | `통과(349.7%)` | `통과(128.6%)` | `통과(63.2%)` | `통과(60.1%)` | 256B는 같은 조건 재측정 C `perf_c_multi_linux_20260521_210845_codex_c_wss_multi_spot256_outlier_apply_20260521.txt` 대비 Java `perf_java_multi_linux_20260521_210909_codex_java_wss_multi_spot256_outlier_apply_20260521.txt`로 갱신했다. 65536/262144B는 같은 조건 재측정 C `perf_c_multi_linux_20260521_194954_codex_c_wss_spot_large_for_java_recheck2_20260521.txt` 대비 Java all-size 재측정 `perf_java_multi_linux_20260521_200127_codex_java_wss_spot_large_spinwait_recheck_20260521.txt`, 262144B 단독 `perf_java_multi_linux_20260521_200350_codex_java_wss_spot262144_spinwait_single_recheck_20260521.txt`로 갱신했다. 131072B는 C `perf_c_multi_linux_20260521_201904_codex_c_wss_spot131072_for_java_current_recheck_20260521.txt` 대비 Java `perf_java_multi_linux_20260521_203035_codex_java_wss_spot131072_rid_cache_recheck_20260521.txt`에서 통과했다. Java public `Poller.add(Spot, ..., POLLOUT)` 내부를 spot-pub poller primitive에 연결하고, publisher active backpressure 대기를 public Poller+Timer 단일 대기로 바꿨다. `Spot.subscribe` fast path는 scratch routing-id cache를 사용해 반복 routing id의 byte[]/RoutingId 생성을 줄였다. recv worker idle sleep/backoff는 `Thread.onSpinWait()`으로 제거했다. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(78.1%)` | `통과(80.4%)` | `통과(78.8%)` | `통과(100.6%)` | `통과(96.0%)` | `통과(94.2%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; Java `perf_java_multi_linux_20260521_170615_codex_java_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(68.2%)` | `통과(71.7%)` | `통과(77.7%)` | `통과(95.6%)` | `통과(96.7%)` | `통과(89.9%)` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; Java `perf_java_multi_linux_20260521_141952_codex_java_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_STREAM` | `통과(94.9%)` | `통과(91.9%)` | `통과(93.7%)` | `통과(92.3%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`; Java `perf_java_multi_linux_20260521_141952_codex_java_wss_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(74.7%)` | `통과(99.6%)` | `통과(81.2%)` | `통과(97.5%)` | `통과(82.9%)` | `통과(79.8%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(72.4%)` | `통과(71.7%)` | `통과(71.5%)` | `통과(58.4%)` | `통과(92.1%)` | `통과(90.2%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(58.9%)` | `통과(58.8%)` | `통과(58.8%)` | `통과(62.0%)` | `통과(63.9%)` | `통과(69.6%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_PUBSUB` | `통과(75.0%)` | `통과(73.9%)` | `통과(82.1%)` | `통과(90.2%)` | `통과(95.9%)` | `통과(95.4%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT` | `통과(124.0%)` | `통과(127.8%)` | `통과(173.8%)` | `통과(88.7%)` | `통과(92.2%)` | `통과(95.8%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(76.8%)` | `통과(67.0%)` | `통과(71.1%)` | `통과(85.7%)` | `통과(93.0%)` | `통과(93.3%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_170615_codex_java_multi_spot_reqrep_pollcompletion_full_20260521.txt`. 256B는 C `perf_c_multi_linux_20260521_174430_codex_c_tls_spot_reqrep256_for_java_deadline_timer_pollcompletion_20260521.txt` 대비 Java `perf_java_multi_linux_20260521_175312_codex_java_tls_spot_reqrep256_deadline_timer_no_1ms_wait_20260521.txt`로 보강했다. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(64.5%)` | `통과(69.3%)` | `통과(66.9%)` | `통과(89.7%)` | `통과(88.9%)` | `통과(80.9%)` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_STREAM` | `통과(94.4%)` | `통과(93.5%)` | `통과(95.2%)` | `통과(92.0%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`; Java `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |

측정 결과 파일:

- C 기준: `perf_c_multi_linux_20260521_135152_codex_c_tcp_multi_for_java_current_20260521.txt`, `perf_c_multi_linux_20260519_211916_codex_c_ws_multi_smoke_all_retry_20260519.txt`, `perf_c_multi_linux_20260519_221051_codex_c_wss_multi_smoke_all_20260519.txt`, `perf_c_multi_linux_20260519_233310_codex_c_tls_multi_smoke_all_20260519_current.txt`, `perf_c_multi_linux_20260519_234246_codex_c_tls_stream64_debug_recheck_20260519.txt`, `perf_c_multi_linux_20260519_235205_codex_c_tls_stream256_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234152_codex_c_tls_stream1024_recheck_20260519.txt`, `perf_c_multi_linux_20260519_234308_codex_c_tls_stream65536_recheck_20260519.txt`, `perf_c_multi_linux_20260520_232413_codex_c_tcp_multi_sendsend_current_all_for_node_20260520.txt`, `perf_c_multi_linux_20260521_142745_codex_c_wss_spot262144_for_java_20260521.txt`, `perf_c_multi_linux_20260521_194538_codex_c_java_routed_miss_recheck_20260521.txt`, `perf_c_multi_linux_20260521_194954_codex_c_wss_spot_large_for_java_recheck2_20260521.txt`, `perf_c_multi_linux_20260521_201904_codex_c_wss_spot131072_for_java_current_recheck_20260521.txt`
- Java 측정: `perf_java_multi_linux_20260521_133627_codex_java_tcp_multi_remeasure_20260521.txt`, `perf_java_multi_linux_20260521_135027_codex_java_tcp_spot262144_recheck3_20260521.txt`, `perf_java_multi_linux_20260521_140328_codex_java_tcp_dr_pollset_mask_20260521.txt`, `perf_java_multi_linux_20260521_141820_codex_java_tcp_rr_stop_reliable_20260521.txt`, `perf_java_multi_linux_20260521_140504_codex_java_ws_multi_remeasure_20260521.txt`, `perf_java_multi_linux_20260521_141913_codex_java_ws_spot262144_recheck_20260521.txt`, `perf_java_multi_linux_20260521_141952_codex_java_wss_multi_remeasure_20260521.txt`, `perf_java_multi_linux_20260521_142852_codex_java_wss_spot262144_recheck2_20260521.txt`, `perf_java_multi_linux_20260521_142852_codex_java_wss_spot_reqrep262144_recheck_20260521.txt`, `perf_java_multi_linux_20260521_142934_codex_java_tls_multi_remeasure_20260521.txt`, `perf_java_multi_linux_20260521_143917_codex_java_tls_spot_reqrep262144_recheck_20260521.txt`, `perf_java_multi_linux_20260521_143926_codex_java_tls_spot_sendsend262144_recheck_20260521.txt`, `perf_java_multi_linux_20260521_143939_codex_java_tls_spot262144_recheck2_20260521.txt`, `perf_java_multi_linux_20260521_170615_codex_java_multi_spot_reqrep_pollcompletion_full_20260521.txt`, `perf_java_multi_linux_20260521_171039_codex_java_multi_spot_reqrep_ws64_recheck_20260521.txt`, `perf_java_multi_linux_20260521_175312_codex_java_tls_spot_reqrep256_deadline_timer_no_1ms_wait_20260521.txt`, `perf_java_multi_linux_20260521_174453_codex_java_wss_multi_spot_recheck_20260521.txt`, `perf_java_multi_linux_20260521_174635_codex_java_wss_multi_spot65536_ready_recheck_20260521.txt`, `perf_java_multi_linux_20260521_184941_codex_java_wss_multi_spot_pub_poller_recheck_20260521.txt`, `perf_java_multi_linux_20260521_194652_codex_java_routed_miss_recheck_20260521.txt`, `perf_java_multi_linux_20260521_194802_codex_java_ws_dd131072_single_recheck_20260521.txt`, `perf_java_multi_linux_20260521_195158_codex_java_wss_spot_large_recheck2_20260521.txt`, `perf_java_multi_linux_20260521_201532_codex_java_ws_dd131072_dealer_send_builder_recheck_20260521.txt`, `perf_java_multi_linux_20260521_201824_codex_java_wss_spot131072_after_dealer_builder_recheck_20260521.txt`, `perf_java_multi_linux_20260521_203035_codex_java_wss_spot131072_rid_cache_recheck_20260521.txt`, `perf_java_multi_linux_20260521_203945_codex_java_ws_dd131072_poller_index_cache_recheck_20260521.txt`
- Java `MULTI_SPOT_REQREP`는 `POLLCOMPLETION` poller에 active deadline timer를 함께 등록해 C처럼 completion 또는 deadline event로만 깨어나도록 수정했다. 1ms timeout poll은 제거했다. `wss MULTI_SPOT` publisher는 public `Poller.add(Spot, ..., POLLOUT)`이 spot-pub poller primitive를 쓰도록 보강한 뒤 public Poller+Timer 대기로 재측정했다. recv worker idle sleep/backoff도 제거했다. `Spot.subscribe` fast path routing-id cache와 `Poller` handle index cache, `PerfMultiDealerDealer` direct latency 기록 적용 뒤 `ws MULTI_DEALER_DEALER 131072B`와 `wss MULTI_SPOT 131072B` 미달을 해소했다. 통과로 바꾸기 위한 sleep/backoff나 HWM 숫자 튜닝은 적용하지 않았다.

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
| `wss` | `SPOT` | `통과(84.9%)` | `통과(65.5%)` | `보류(296.4%)` | `통과(171.7%)` | `통과(167.4%)` | `통과(172.2%)` | 1024B는 같은 조건 재측정 C `perf_c_single_linux_20260521_210903_codex_c_wss_single_spot1024_outlier_apply_20260521.txt` 대비 Node `perf_node_single_linux_20260521_210938_codex_node_wss_single_spot1024_outlier_apply_20260521.txt`에서 여전히 높다. 현재 Node single SPOT은 publish 후 inline drain 구조라 C의 별도 sender/receiver thread 의미와 다를 수 있어 통과로 확정하지 않는다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. 나머지는 C/Node full 파일은 위 wss PAIR 행과 같다. |
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
| `tcp` | `MULTI_ROUTER_ROUTER` | `통과(36.1%)` | `통과(36.7%)` | `통과(35.6%)` | `통과(54.3%)` | `통과(45.5%)` | `통과(72.0%)` | 64/256/1024B는 C `perf_c_multi_linux_20260521_180927_codex_c_multi_rr_threshold5_for_node_20260521.txt` 대비 Node public API 경로 `perf_node_multi_linux_20260521_183956_codex_node_multi_rr_public_borrowed_send_recheck_20260521.txt`로 보강했다. 65536B는 같은 C 기준 대비 `perf_node_multi_linux_20260521_185753_codex_node_tcp_rr65536_recv_no_throw_recheck_20260521.txt`에서 통과했다. 131072/262144B는 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. public routed send 내부가 C 단일 part borrowed primitive를 쓰고, public `recvPayloadInto(...DontWait)` 내부 no-data 예외 비용을 제거했다. |
| `tcp` | `MULTI_PUBSUB` | `통과(41.9%)` | `통과(51.5%)` | `통과(82.9%)` | `통과(56.3%)` | `통과(54.9%)` | `통과(64.5%)` | 64/256B는 C `perf_c_multi_linux_20260521_180012_codex_c_multi_pubsub_small_for_node_threshold5_20260521.txt` 대비 Node public publish/subscribe 경로 `perf_node_multi_linux_20260521_184302_codex_node_multi_pubsub_public_publish_borrowed_recheck_20260521.txt`로 보강했다. 나머지는 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. no-data 예외 제거, metric payload 직접 기록, public publish operation 내부 borrowed Buffer 경로를 적용했다. |
| `tcp` | `MULTI_SPOT` | `통과(45.8%)` | `통과(94.4%)` | `통과(64.3%)` | `통과(70.5%)` | `통과(79.2%)` | `통과(97.8%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(54.3%)` | `통과(50.4%)` | `통과(44.9%)` | `통과(59.9%)` | `통과(63.7%)` | `통과(70.2%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_171325_codex_node_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `통과(93.0%)` | `통과(90.9%)` | `통과(96.7%)` | `통과(108.8%)` | `통과(205.4%)` | `통과(140.7%)` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tcp` | `MULTI_STREAM` | `통과(72.4%)` | `통과(73.7%)` | `통과(73.1%)` | `통과(91.1%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`; Node `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_DEALER` | `통과(99.4%)` | `통과(87.2%)` | `통과(77.2%)` | `통과(60.8%)` | `통과(57.5%)` | `통과(89.6%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(86.7%)` | `통과(85.1%)` | `통과(84.8%)` | `통과(53.2%)` | `통과(57.1%)` | `통과(65.6%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(39.9%)` | `통과(38.3%)` | `통과(37.5%)` | `통과(44.7%)` | `통과(51.5%)` | `통과(66.3%)` | 64/256/1024/65536B는 C `perf_c_multi_linux_20260521_180927_codex_c_multi_rr_threshold5_for_node_20260521.txt` 대비 Node public API 경로 `perf_node_multi_linux_20260521_183956_codex_node_multi_rr_public_borrowed_send_recheck_20260521.txt`로 보강했다. 131072/262144B는 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. public routed send 내부가 C 단일 part borrowed primitive를 쓰도록 고쳤다. |
| `ws` | `MULTI_PUBSUB` | `통과(44.3%)` | `통과(40.4%)` | `통과(46.7%)` | `통과(67.4%)` | `통과(62.1%)` | `통과(85.3%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT` | `통과(72.6%)` | `통과(71.6%)` | `통과(66.5%)` | `통과(48.7%)` | `통과(71.6%)` | `통과(99.3%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(62.1%)` | `통과(57.0%)` | `통과(56.8%)` | `통과(81.1%)` | `통과(75.3%)` | `통과(87.0%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_171325_codex_node_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `통과(97.5%)` | `통과(98.0%)` | `통과(100.4%)` | `통과(112.9%)` | `통과(116.3%)` | `통과(168.9%)` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `ws` | `MULTI_STREAM` | `통과(70.2%)` | `통과(72.2%)` | `통과(75.0%)` | `통과(92.9%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`; Node `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_DEALER` | `통과(101.6%)` | `통과(83.0%)` | `통과(66.6%)` | `통과(54.0%)` | `통과(62.2%)` | `통과(65.2%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(79.3%)` | `통과(76.5%)` | `통과(73.5%)` | `통과(55.5%)` | `통과(60.9%)` | `통과(60.0%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(38.1%)` | `통과(39.1%)` | `통과(39.4%)` | `통과(60.7%)` | `통과(57.1%)` | `통과(57.1%)` | 64/256/1024/65536B는 C `perf_c_multi_linux_20260521_180927_codex_c_multi_rr_threshold5_for_node_20260521.txt` 대비 Node public API 경로 `perf_node_multi_linux_20260521_183956_codex_node_multi_rr_public_borrowed_send_recheck_20260521.txt`로 보강했다. 131072/262144B는 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. public routed send 내부가 C 단일 part borrowed primitive를 쓰도록 고쳤다. |
| `wss` | `MULTI_PUBSUB` | `통과(43.5%)` | `통과(43.9%)` | `통과(53.7%)` | `통과(53.5%)` | `통과(54.1%)` | `통과(58.8%)` | 64/256B는 C `perf_c_multi_linux_20260521_180012_codex_c_multi_pubsub_small_for_node_threshold5_20260521.txt` 대비 Node public publish/subscribe 경로 `perf_node_multi_linux_20260521_184302_codex_node_multi_pubsub_public_publish_borrowed_recheck_20260521.txt`로 보강했다. 나머지는 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. no-data 예외 제거, metric payload 직접 기록, public publish operation 내부 borrowed Buffer 경로를 적용했다. |
| `wss` | `MULTI_SPOT` | `통과(85.0%)` | `통과(253.3%)` | `통과(155.3%)` | `통과(46.9%)` | `통과(48.8%)` | `통과(40.0%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(62.1%)` | `통과(57.9%)` | `통과(64.8%)` | `통과(95.8%)` | `통과(94.4%)` | `통과(93.0%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_171325_codex_node_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `통과(92.8%)` | `통과(96.2%)` | `통과(108.4%)` | `통과(107.7%)` | `통과(109.3%)` | `통과(103.1%)` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `wss` | `MULTI_STREAM` | `통과(92.6%)` | `통과(93.1%)` | `통과(93.1%)` | `통과(97.0%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_DEALER` | `통과(100.9%)` | `통과(86.7%)` | `통과(74.8%)` | `통과(56.5%)` | `통과(55.2%)` | `통과(54.8%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(88.5%)` | `통과(86.3%)` | `통과(84.3%)` | `통과(52.7%)` | `통과(55.7%)` | `통과(56.2%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_ROUTER_ROUTER` | `통과(38.6%)` | `통과(39.0%)` | `통과(39.4%)` | `통과(55.1%)` | `통과(58.0%)` | `통과(56.3%)` | 64/256/1024/65536B는 C `perf_c_multi_linux_20260521_180927_codex_c_multi_rr_threshold5_for_node_20260521.txt` 대비 Node public API 경로 `perf_node_multi_linux_20260521_183956_codex_node_multi_rr_public_borrowed_send_recheck_20260521.txt`로 보강했다. 131072/262144B는 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. public routed send 내부가 C 단일 part borrowed primitive를 쓰도록 고쳤다. |
| `tls` | `MULTI_PUBSUB` | `통과(43.1%)` | `통과(42.6%)` | `통과(47.5%)` | `통과(53.9%)` | `통과(58.6%)` | `통과(59.3%)` | 64B는 C `perf_c_multi_linux_20260521_190630_codex_c_tls_pubsub64_for_node_topic_bench_recheck_20260521.txt` 대비 Node `perf_node_multi_linux_20260521_190730_codex_node_tls_pubsub64_no_null_rid_recheck_20260521.txt`에서 통과했다. 256B는 C `perf_c_multi_linux_20260521_180012_codex_c_multi_pubsub_small_for_node_threshold5_20260521.txt` 대비 Node public publish/subscribe 경로 `perf_node_multi_linux_20260521_184302_codex_node_multi_pubsub_public_publish_borrowed_recheck_20260521.txt`로 보강했다. 나머지는 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. C와 같은 `bench` topic, no-data 예외 제거, metric payload 직접 기록, public publish operation 내부 borrowed Buffer 경로, raw native result의 불필요한 null routingId property 제거를 적용했다. |
| `tls` | `MULTI_SPOT` | `통과(70.1%)` | `통과(92.6%)` | `통과(106.3%)` | `통과(98.2%)` | `통과(110.6%)` | `통과(150.7%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(60.5%)` | `통과(54.0%)` | `통과(49.1%)` | `통과(86.4%)` | `통과(87.9%)` | `통과(89.5%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_171325_codex_node_multi_spot_reqrep_pollcompletion_full_20260521.txt`. auto-HWM 활성과 size별 spotnode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `통과(89.7%)` | `통과(93.2%)` | `통과(96.2%)` | `통과(100.9%)` | `통과(103.5%)` | `통과(153.7%)` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |
| `tls` | `MULTI_STREAM` | `통과(90.8%)` | `통과(86.4%)` | `통과(91.9%)` | `통과(102.4%)` | `해당 없음` | `해당 없음` | 대표 C `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`; Node `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`. 보강 파일은 아래 목록 참조. |

측정 결과 파일:

- C 기준: `perf_c_multi_linux_20260520_004453_codex_c_tcp_multi_for_dotnet_20260520.txt`, `perf_c_multi_linux_20260520_232757_codex_c_ws_multi_current_for_node_20260520.txt`, `perf_c_multi_linux_20260520_234521_codex_c_ws_multi_spot64_256_repro_20260520.txt`, `perf_c_multi_linux_20260521_001957_codex_c_ws_multi_spot1024_262144_recheck_for_node_20260521.txt`, `perf_c_multi_linux_20260520_235328_codex_c_ws_multi_spot65536_repeat3_seq_20260520.txt`, `perf_c_multi_linux_20260520_235741_codex_c_ws_multi_spot131072_repro_debug_20260520.txt`, `perf_c_multi_linux_20260521_012140_codex_c_wss_multi_current_for_node_20260521.txt`, `perf_c_multi_linux_20260521_015925_codex_c_wss_multi_pubsub_all_for_node_20260521.txt`, `perf_c_multi_linux_20260521_024327_codex_c_tls_multi_current_for_node_20260521.txt`, `perf_c_multi_linux_20260520_232413_codex_c_tcp_multi_sendsend_current_all_for_node_20260520.txt`, `perf_c_multi_linux_20260521_011758_codex_c_ws_multi_sendsend_all_for_node_20260521.txt`, `perf_c_multi_linux_20260521_180012_codex_c_multi_pubsub_small_for_node_threshold5_20260521.txt`, `perf_c_multi_linux_20260521_180927_codex_c_multi_rr_threshold5_for_node_20260521.txt`
- Node 측정: `perf_node_multi_linux_20260521_145057_codex_node_tcp_multi_remeasure_20260521.txt`, `perf_node_multi_linux_20260521_145134_codex_node_tcp_spot131072_recheck_20260521.txt`, `perf_node_multi_linux_20260521_001948_codex_node_ws_multi_spot64_256_crash_repro_20260520.txt`, `perf_node_multi_linux_20260521_003015_codex_node_ws_multi_spot_large_recheck_for_node_20260521.txt`, `perf_node_multi_linux_20260521_004553_codex_node_ws_multi_dd_native_sendloop_all_20260521.txt`, `perf_node_multi_linux_20260521_005520_codex_node_ws_multi_routed_pubsub_recheck_20260521.txt`, `perf_node_multi_linux_20260521_011145_codex_node_ws_multi_dr_native_echo_loop_all_20260521.txt`, `perf_node_multi_linux_20260521_011737_codex_node_ws_multi_spotreq_sendsend_stream_20260521.txt`, `perf_node_multi_linux_20260521_020406_codex_node_wss_multi_pubsub_recheck_20260521.txt`, `perf_node_multi_linux_20260521_021710_codex_node_wss_multi_spot256_repro_20260521.txt`, `perf_node_multi_linux_20260521_022729_codex_node_wss_multi_routed_group_20260521.txt`, `perf_node_multi_linux_20260521_023558_codex_node_wss_multi_spot_rest_20260521.txt`, `perf_node_multi_linux_20260521_024207_codex_node_wss_multi_spotreq_sendsend_stream_20260521.txt`, `perf_node_multi_linux_20260521_033444_codex_node_tls_multi_current_for_node_20260521.txt`, `perf_node_multi_linux_20260521_034012_codex_node_tls_multi_pubsub64_256_publish_direct_20260521.txt`, `perf_node_multi_linux_20260521_171325_codex_node_multi_spot_reqrep_pollcompletion_full_20260521.txt`, `perf_node_multi_linux_20260521_180718_codex_node_multi_pubsub_small_no_eagain_exception_20260521.txt`, `perf_node_multi_linux_20260521_181512_codex_node_multi_rr_threshold5_recheck_20260521.txt`, `perf_node_multi_linux_20260521_183956_codex_node_multi_rr_public_borrowed_send_recheck_20260521.txt`, `perf_node_multi_linux_20260521_184302_codex_node_multi_pubsub_public_publish_borrowed_recheck_20260521.txt`, `perf_node_multi_linux_20260521_185753_codex_node_tcp_rr65536_recv_no_throw_recheck_20260521.txt`, `perf_node_multi_linux_20260521_190730_codex_node_tls_pubsub64_no_null_rid_recheck_20260521.txt`
- Node `MULTI_SPOT_REQREP`는 poll completion 반영 뒤 full matrix 결과로 갱신했다. `MULTI_PUBSUB`은 C와 같은 `bench` topic, no-data 예외 제거, metric payload 직접 기록, public publish operation 내부 borrowed Buffer 경로, raw native result의 불필요한 null routingId property 제거로 +5%p 기준까지 올렸다. `MULTI_ROUTER_ROUTER`은 public routed send 내부가 C 단일 part borrowed primitive를 쓰고, public `recvPayloadInto(...DontWait)` 내부 no-data 예외 비용을 제거해 남아 있던 `tcp 65536B`도 통과했다. 실패 은폐용 sleep/backoff나 public API 우회는 추가하지 않았다.

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
| `ws` | `PAIR` | `통과(73.9%)` | `보류(65.0%)` | `통과(88.6%)` | `통과(97.1%)` | `통과(95.5%)` | `통과(91.4%)` | 64B/256B는 C `perf_c_single_linux_20260522_005942_codex_c_ws_single_go_latency_recheck_20260522.txt` 대비 Go `perf_go_single_linux_20260522_010624_codex_go_ws_single_native_stamp_20260522.txt`로 갱신했다. Go active send는 C처럼 native message payload에 직접 metric header를 stamp하고, `DONTWAIT` 미전송 message를 즉시 close한다. 256B는 throughput 비율은 목표권이지만 latency가 C 대비 154.2x라 통과로 확정하지 않는다. `GOGC=off` 진단 `perf_go_single_linux_20260522_012120_codex_go_ws_single_gogc_off_probe_20260522.txt`, `GOMAXPROCS=2` 진단 `perf_go_single_linux_20260522_012257_codex_go_ws_single_gomaxprocs2_probe_20260522.txt`는 256B latency를 해결하지 못했다. 단일 part send를 native move 뒤 실패 시 restore하는 내부 후보는 `go test ./...`의 `TestBlockingSendFailurePreservesMessagePayload`에서 원본 보존 계약을 깨 탈락했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. 나머지는 C `perf_c_single_linux_20260521_090653_codex_c_ws_single_current_after_core_rebuild_for_go_20260521.txt`, Go `perf_go_single_linux_20260521_085525_codex_go_ws_single_full_after_routed_timestamp_20260521.txt`. |
| `ws` | `PUBSUB` | `보류(56.4%)` | `보류(53.8%)` | `통과(89.2%)` | `통과(97.0%)` | `통과(95.6%)` | `통과(90.1%)` | 64B/256B는 위 2026-05-22 C/Go 제한 재측정 파일로 갱신했다. throughput 비율은 Go 단순 one-way 최소 기준을 넘지만 latency가 각각 C 대비 89.4x, 60.0x라 통과로 확정하지 않는다. receive hot path는 C처럼 첫 수신 blocking, 이후 `DONTWAIT` burst drain이다. `GOGC=off`와 `GOMAXPROCS=2` 진단은 latency를 해결하지 못했고, publish topic C 문자열 캐시 후보 `perf_go_single_linux_20260522_012639_codex_go_ws_single_publish_cstring_cache_20260522.txt`도 악화되어 반영하지 않았다. Go public send builder의 원본 보존 계약을 바꾸지 않는 내부 후보가 더 확인되지 않아 보류한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. 나머지는 위 2026-05-21 C/Go full 파일. |
| `ws` | `DEALER_DEALER` | `통과(72.3%)` | `보류(64.7%)` | `통과(95.4%)` | `통과(97.5%)` | `통과(95.7%)` | `통과(91.4%)` | 64B/256B는 위 2026-05-22 C/Go 제한 재측정 파일로 갱신했다. 256B는 throughput 비율은 목표권이지만 latency가 C 대비 133.4x라 통과로 확정하지 않는다. `GOGC=off`와 `GOMAXPROCS=2` 진단은 256B latency를 해결하지 못했다. 단일 part send move/restore 후보는 public 실패 원본 보존 테스트를 깨 반영하지 않았다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. 나머지는 위 2026-05-21 C/Go full 파일. |
| `ws` | `DEALER_ROUTER` | `통과(56.1%)` | `통과(58.1%)` | `통과(74.9%)` | `통과(83.5%)` | `통과(97.8%)` | `통과(81.3%)` | C/Go 파일은 위 PAIR 행과 같다. Go routed active phase를 C `perf_dealer_router.cpp`처럼 sender goroutine의 blocking send와 receiver의 blocking `RecvPart` stop-token 루프로 맞췄다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `ROUTER_ROUTER` | `통과(65.4%)` | `통과(55.9%)` | `통과(67.5%)` | `통과(90.9%)` | `통과(94.6%)` | `통과(81.7%)` | C/Go 파일은 위 PAIR 행과 같다. ROUTER-ROUTER도 C처럼 PING/PONG으로 target route를 확인한 뒤 active와 stop token을 blocking send 의미로 보낸다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `SPOT` | `통과(167.9%)` | `보류(143.9%)` | `통과(110.3%)` | `통과(103.4%)` | `통과(94.1%)` | `통과(83.6%)` | 64B는 C `perf_c_single_linux_20260522_005942_codex_c_ws_single_go_latency_recheck_20260522.txt` 대비 현재 코드 확인 재측정 Go `perf_go_single_linux_20260522_011618_codex_go_ws_single_spot64_native_stamp_confirm_20260522.txt`로 갱신했다. 256B는 Go `perf_go_single_linux_20260522_010624_codex_go_ws_single_native_stamp_20260522.txt` 기준이다. active send가 native message payload에 직접 stamp하도록 바뀐 뒤 64B latency는 C 대비 1.5x로 내려가 통과했다. 256B는 throughput은 C보다 높지만 latency가 C 대비 13.7x라 통과로 확정하지 않는다. `GOGC=off` 진단은 256B latency를 개선했지만 공식 조건 전체를 해결하지 못했고, `GOMAXPROCS=2`와 publish topic C 문자열 캐시는 악화되어 반영하지 않았다. SPOT은 C와 같은 `DONTWAIT` publish/backpressure 대기 의미와 `SubscribePart` 수신 경로를 유지하며 auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. 나머지는 위 2026-05-21 C/Go full 파일. |
| `wss` | `PAIR` | `통과(74.4%)` | `보류(69.0%)` | `통과(116.9%)` | `통과(84.4%)` | `통과(81.5%)` | `통과(87.6%)` | C `perf_c_single_linux_20260522_012906_codex_c_wss_single_for_go_20260522.txt` 대비 Go `perf_go_single_linux_20260522_013230_codex_go_wss_single_current_20260522.txt`. 256B는 throughput 비율은 목표권이지만 latency가 C 대비 117.6x라 통과로 확정하지 않는다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `PUBSUB` | `통과(62.4%)` | `보류(64.8%)` | `통과(110.5%)` | `보류(83.9%)` | `통과(81.2%)` | `통과(86.1%)` | C/Go 파일은 위 wss PAIR 행과 같다. 256B는 latency가 C 대비 77.3x, 65536B는 17.4x라 통과로 확정하지 않는다. `GOGC=off`, `GOMAXPROCS=2`, topic C 문자열 캐시 후보는 ws 제한 재측정에서 같은 latency 병목을 해결하지 못했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `DEALER_DEALER` | `통과(73.9%)` | `보류(68.1%)` | `통과(111.1%)` | `통과(79.5%)` | `통과(80.2%)` | `통과(94.9%)` | C/Go 파일은 위 wss PAIR 행과 같다. 256B는 throughput 비율은 목표권이지만 latency가 C 대비 110.1x라 통과로 확정하지 않는다. 단일 part send move/restore 후보는 public 실패 원본 보존 테스트를 깨 반영하지 않았다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `DEALER_ROUTER` | `통과(53.8%)` | `통과(57.0%)` | `통과(93.3%)` | `통과(84.8%)` | `통과(91.5%)` | `통과(108.5%)` | C/Go 파일은 위 wss PAIR 행과 같다. routed one-way 기준을 통과했고 auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `ROUTER_ROUTER` | `통과(62.8%)` | `통과(55.7%)` | `통과(92.7%)` | `통과(72.8%)` | `통과(89.3%)` | `통과(105.4%)` | C/Go 파일은 위 wss PAIR 행과 같다. routed one-way 기준을 통과했고 auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `SPOT` | `통과(186.5%)` | `보류(112.9%)` | `통과(148.3%)` | `통과(76.0%)` | `통과(79.6%)` | `보류(28.0%)` | C/Go 파일은 위 wss PAIR 행과 같다. 256B는 throughput은 C보다 높지만 latency가 C 대비 135.3x라 통과로 확정하지 않는다. 262144B는 throughput이 Go SPOT 최소 기준보다 낮다. `GOGC=off` 제한 재측정 `perf_go_single_linux_20260522_013700_codex_go_wss_single_spot_gogc_off_probe_20260522.txt`는 262144B를 해결하지 못했다. C와 맞춰 성공 send 뒤 `runtime.Gosched()` 제거를 시도한 `perf_go_single_linux_20260522_013744_codex_go_wss_single_spot_no_sender_yield_20260522.txt`는 receiver 진행이 막혀 악화되어 반영하지 않았다. large frame에서 yield 빈도를 줄이는 후보 `perf_go_single_linux_20260522_014020_codex_go_wss_single_spot_yield_every4_20260522.txt`, `perf_go_single_linux_20260522_014057_codex_go_wss_single_spot_large_yield4_20260522.txt`는 262144B 개선이 재현되지 않거나 256B를 악화시켜 반영하지 않았다. C/Go 262144B 3회 제한 재측정 `perf_c_single_linux_20260522_014311_codex_c_wss_single_spot262144_repeat3_for_go_20260522.txt`, `perf_go_single_linux_20260522_014237_codex_go_wss_single_spot262144_current_repeat3_20260522.txt`에서도 C median 1990.0 msg/s 대비 Go median 약 546.8 msg/s로 gap이 재현됐다. explicit routing id 제거 `perf_go_single_linux_20260522_014422_codex_go_wss_single_spot262144_no_explicit_rid_20260522.txt`, topic C 문자열 캐시 `perf_go_single_linux_20260522_014549_codex_go_wss_single_spot_cached_topic_20260522.txt`, send builder single-part inline 후보 `perf_go_single_linux_20260522_014809_codex_go_wss_single_spot_inline_send_builder_20260522.txt`, OS thread 고정 `perf_go_single_linux_20260522_015347_codex_go_wss_single_spot262144_lock_osthread_probe_20260522.txt`, receiver-main 구조 `perf_go_single_linux_20260522_015451_codex_go_wss_single_spot262144_receiver_main_probe_20260522.txt`, receiver-main no-yield `perf_go_single_linux_20260522_015538_codex_go_wss_single_spot262144_receiver_main_no_sender_yield_probe_20260522.txt`, receiver-main OS thread 고정 `perf_go_single_linux_20260522_015632_codex_go_wss_single_spot262144_receiver_main_lock_osthread_probe_20260522.txt`는 미달을 해결하지 못했거나 다른 size를 악화시켜 반영하지 않았다. `perf_go_single_linux_20260522_015710_codex_go_wss_single_spot_receiver_main_all_sizes_probe_20260522.txt`에서 receiver-main 구조가 작은 size latency를 크게 악화시키는 것도 확인했다. 남은 gap은 Go public `Publish(...).Message(...).Submit`의 실패 시 원본 보존 계약과 C의 native part 소비형 publish 의미 차이에서 온다. public API 변경 없이 가능한 내부 후보를 더 찾지 못했으므로, 별도 설계 항목으로 성공 시 consume을 명시하는 고성능 publish/send path가 필요하다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `PAIR` | `통과(74.0%)` | `통과(71.8%)` | `통과(121.8%)` | `통과(76.2%)` | `통과(80.8%)` | `통과(90.8%)` | C `perf_c_single_linux_20260522_015850_codex_c_tls_single_for_go_20260522.txt` 대비 Go `perf_go_single_linux_20260522_020215_codex_go_tls_single_current_20260522.txt`. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `PUBSUB` | `통과(61.3%)` | `통과(68.1%)` | `통과(120.0%)` | `통과(76.1%)` | `통과(79.5%)` | `통과(90.7%)` | C/Go 파일은 위 tls PAIR 행과 같다. receive hot path는 C처럼 첫 수신 blocking, 이후 `DONTWAIT` burst drain이며 auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `DEALER_DEALER` | `통과(74.1%)` | `통과(73.2%)` | `통과(118.9%)` | `통과(78.6%)` | `통과(81.2%)` | `통과(94.3%)` | C/Go 파일은 위 tls PAIR 행과 같다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `DEALER_ROUTER` | `통과(55.7%)` | `통과(54.5%)` | `통과(88.8%)` | `통과(85.9%)` | `통과(87.4%)` | `통과(88.9%)` | C/Go 파일은 위 tls PAIR 행과 같다. routed one-way 기준을 통과했고 auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `ROUTER_ROUTER` | `통과(64.7%)` | `통과(63.2%)` | `통과(88.9%)` | `통과(85.2%)` | `통과(88.9%)` | `통과(85.9%)` | C/Go 파일은 위 tls PAIR 행과 같다. routed one-way 기준을 통과했고 auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `SPOT` | `통과(156.8%)` | `통과(125.6%)` | `통과(101.3%)` | `통과(68.8%)` | `통과(69.7%)` | `통과(69.2%)` | C/Go 파일은 위 tls PAIR 행과 같다. SPOT은 C와 같은 `DONTWAIT` publish/backpressure 대기 의미와 `SubscribePart` 수신 경로를 유지한다. 64B/256B는 C보다 높지만 같은 조건의 C 기준과 비교했고, 2026-05-22 wss SPOT 64B에서도 같은 의미의 outlier가 확인되어 별도 outlier 목록에서 추적한다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |

#### 6.6.2 Multi suite

| Transport | Pattern | 64 | 256 | 1024 | 65536 | 131072 | 262144 | 결과 파일 / 메모 |
|-----------|---------|----|-----|------|-------|--------|--------|------------------|
| `tcp` | `MULTI_DEALER_DEALER` | `보류(36.3%)` | `보류(45.8%)` | `통과(61.1%)` | `통과(63.5%)` | `보류(46.2%)` | `보류(7.0%)` | C `perf_c_multi_linux_20260522_020803_codex_c_tcp_multi_for_go_20260522.txt` 대비 Go 64B/256B/1024B/65536B `perf_go_multi_linux_20260522_044225_codex_go_tcp_multi_dd_recvpart_selected_20260522.txt`, 131072B/262144B `perf_go_multi_linux_20260522_044514_codex_go_tcp_multi_dd_large_recheck_after_recvpart_selected_20260522.txt`. C처럼 단일 poll loop와 pending socket만 `POLLOUT`으로 두는 poll set 의미를 유지했다. server는 C의 single-part `zlink_recv_part` 의미와 맞춰 64B/65536B에서 public `RecvPart` caller-owned 수신 경로를 사용한다. 1024B까지 `RecvPart`를 적용한 후보 `perf_go_multi_linux_20260522_043806_codex_go_tcp_multi_dd_recvpart_le65536_20260522.txt`는 1024B와 large size를 악화시켜 반영하지 않았다. `RecvPart`를 131072B/262144B까지 넓힌 후보 `perf_go_multi_linux_20260522_053911_codex_go_tcp_multi_dd_recvpart_large_20260522.txt`는 131072B를 2.5%로 낮췄고, 262144B만 선택 적용한 재확인 `perf_go_multi_linux_20260522_054204_codex_go_tcp_multi_dd_recvpart_262_only_20260522.txt`도 262144B를 0.5%로 낮춰 반영하지 않았다. client context를 C처럼 하나로 합치는 후보 `perf_go_multi_linux_20260522_050940_codex_go_tcp_multi_shared_client_context_20260522.txt`는 65536B 이상을 크게 악화시켜 DEALER_DEALER에는 반영하지 않았다. single-part send를 copy 대신 move 후 실패 시 restore하는 내부 후보는 `TestBlockingSendFailurePreservesMessagePayload`에서 실패 원본 보존 계약을 깨서 반영하지 않았다. 64B/65536B/131072B는 개선됐지만 64B/256B/131072B/262144B는 Go 단순 one-way 최소 기준보다 낮다. public 계약을 바꾸지 않는 추가 후보가 없어 보류한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_DEALER_ROUTER` | `통과(59.7%)` | `통과(56.1%)` | `통과(55.9%)` | `보류(23.3%)` | `보류(26.8%)` | `보류(41.9%)` | C 파일은 위 행과 같다. Go `perf_go_multi_linux_20260522_050940_codex_go_tcp_multi_shared_client_context_20260522.txt`. per-socket goroutine/poller 구조에서 발생하던 `Bad address`/`I/O error`를 C와 같은 단일 poll loop로 정렬해 전체 size complete를 확보했다. routed echo client는 C `perf_multi_client_helpers.hpp`처럼 한 client context 안에서 모든 dealer socket을 만들도록 맞춰, client당 별도 context와 IO thread를 만들던 차이를 제거했다. 262144B server는 C의 single-part `zlink_router_recv_part` 의미와 맞춰 public `RecvPart` caller-owned 수신 경로를 사용한다. server `RecvPart`를 전체 size에 적용한 후보 `perf_go_multi_linux_20260522_044906_codex_go_tcp_multi_routed_server_recvpart_20260522.txt`는 작은 size를 악화시켜 반영하지 않았다. C helper처럼 client send scan 시작점을 round-robin으로 회전하는 후보 `perf_go_multi_linux_20260522_053520_codex_go_tcp_multi_routed_client_round_robin_20260522.txt`는 65536B 이상과 일부 small size를 낮춰 반영하지 않았다. 64B/256B/1024B는 통과했고 65536B 이상은 아직 multi routed echo 최소 기준보다 낮다. public 계약을 바꾸지 않는 추가 후보가 없어 보류한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_ROUTER_ROUTER` | `보류(38.4%)` | `보류(38.7%)` | `보류(37.8%)` | `보류(24.5%)` | `보류(29.8%)` | `보류(39.2%)` | C 파일은 위 행과 같다. Go `perf_go_multi_linux_20260522_050940_codex_go_tcp_multi_shared_client_context_20260522.txt`. routed echo server는 수신 `RoutingID` 값을 명시적으로 복사해 `SendTo`하고, client는 C와 같은 단일 poll loop로 정렬했다. router-router client도 C routed echo client처럼 한 client context 안에서 모든 router socket을 만들도록 맞춰 client별 context/IO thread 차이를 제거했다. 262144B server는 C의 single-part `zlink_router_recv_part` 의미와 맞춰 public `RecvPart` caller-owned 수신 경로를 사용한다. server `RecvPart`를 전체 size에 적용한 후보 `perf_go_multi_linux_20260522_044906_codex_go_tcp_multi_routed_server_recvpart_20260522.txt`는 일부 작은 size와 65536B/131072B를 악화시켜 반영하지 않았다. C helper처럼 client send scan 시작점을 round-robin으로 회전하는 후보 `perf_go_multi_linux_20260522_053520_codex_go_tcp_multi_routed_client_round_robin_20260522.txt`는 일부 small size만 소폭 올리고 1024B 이상을 낮춰 반영하지 않았다. 전체 size가 여전히 multi routed echo 최소 기준보다 낮다. public 계약을 바꾸지 않는 추가 후보가 없어 보류한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_PUBSUB` | `보류(49.7%)` | `보류(48.8%)` | `통과(82.5%)` | `통과(53.2%)` | `통과(57.4%)` | `통과(71.8%)` | C 파일은 위 행과 같다. Go 64B/256B/1024B/131072B `perf_go_multi_linux_20260522_040504_codex_go_tcp_multi_subscribepart_20260522.txt`, 65536B `perf_go_multi_linux_20260522_040909_codex_go_tcp_multi_pubsub_subscribepart_failed_sizes_recheck_20260522.txt`, 262144B `perf_go_multi_linux_20260522_032109_codex_go_tcp_multi_pubsub262_timeout_fix_20260522.txt`. multi PUBSUB client도 single처럼 public `SubscribePart` caller-owned 수신 경로를 사용하도록 바꿔 wrapper allocation을 줄였다. server publish를 size 생성 메시지로 바꾸는 후보 `perf_go_multi_linux_20260522_051648_codex_go_tcp_multi_pubsub_window_message_20260522.txt`와 1024B~131072B에만 적용하는 후보 `perf_go_multi_linux_20260522_051932_codex_go_tcp_multi_pubsub_window_message_selected_20260522.txt`는 262144B completion을 깨서 반영하지 않았다. 64B/256B는 Go 단순 one-way 최소 기준보다 낮고 public 계약을 바꾸지 않는 추가 후보가 없어 보류한다. 262144B는 5초 후속 재측정이 result 없이 실패했지만 duration 10초 진단 `perf_go_multi_linux_20260522_041056_codex_go_tcp_multi_pubsub262_subscribepart_duration10_20260522.txt`에서는 37.9 Kmsg/s로 수치가 나와 대형 PUBSUB 5초 active window 안정성 항목으로 남긴다. 모든 subscriber를 단일 poller에 등록하고, C처럼 하나의 stop/cooldown 신호로 phase를 종료한다. Go metric timestamp는 C와 같은 epoch ns다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_SPOT` | `보류(45.4%)` | `보류(45.4%)` | `보류(44.7%)` | `통과(77.5%)` | `통과(69.5%)` | `통과(82.4%)` | C 파일은 위 행과 같다. Go `perf_go_multi_linux_20260522_050133_codex_go_tcp_multi_spot_recv_workers_stride_20260522.txt`. server-stamped SPOT latency도 epoch ns로 정렬했고, client drain 내부에서 active deadline을 확인해 backlog가 한 slot에 몰려도 phase가 끝나도록 했다. multi SPOT client는 C `perf_multi_spot_client.cpp`처럼 slot을 worker 수식 `max(4, min(128, (slots+15)/16))`로 나눠 public `SubscribePart` caller-owned 수신 경로를 DONTWAIT drain하고, latency는 C와 같은 `PERF_MULTI_SPOT_LATENCY_SAMPLE_STRIDE=32` 의미로 샘플링한다. server data publisher는 C server option과 맞춰 public `SetNoDrop(true)`와 `PERF_MULTI_SNDTIMEO_MS`를 적용했다. C의 `DONTWAIT` 실패 후 blocking publish 1회 재시도 후보 `perf_go_multi_linux_20260522_045649_codex_go_tcp_multi_spot_nodrop_retry_20260522.txt`는 Go public clone publish 경로에서 262144B가 0.02%로 무너져 반영하지 않았다. `NODROP`/timeout만 분리한 후보 `perf_go_multi_linux_20260522_045839_codex_go_tcp_multi_spot_nodrop_only_20260522.txt`는 large를 조금 올렸지만 small 평균을 해결하지 못해 worker/stride 변경과 함께만 반영했다. 65536B 이상은 통과했고 64B/256B/1024B는 아직 SPOT 최소 기준보다 낮다. public 계약을 바꾸지 않는 추가 후보가 없어 보류한다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_SPOT_REQREP` | `통과(55.8%)` | `통과(55.4%)` | `통과(57.4%)` | `보류(28.2%)` | `보류(28.3%)` | `보류(45.7%)` | C 파일은 위 행과 같다. Go `perf_go_multi_linux_20260522_023554_codex_go_tcp_multi_current_after_runner_cleanup_20260522.txt`. request completion은 public `POLLCOMPLETION` poller를 사용한다. 65536B 이상은 SPOT 최소 기준보다 낮다. client payload 중복 복사 제거 후보 `perf_go_multi_linux_20260522_052455_codex_go_tcp_multi_spot_reqrep_no_payload_clone_20260522.txt`, 실패 size 재확인 `perf_go_multi_linux_20260522_052551_codex_go_tcp_multi_spot_reqrep_no_payload_clone_failed_sizes_recheck_20260522.txt`, binding 내부 단일 part request fast path 후보 `perf_go_multi_linux_20260522_052815_codex_go_tcp_multi_spot_reqrep_request_fastpath_only_failed_sizes_20260522.txt`는 262144B 또는 large size 실패를 만들어 반영하지 않았다. public 계약을 바꾸지 않는 추가 후보가 없어 보류한다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_SPOT_SENDSEND` | `보류(31.6%)` | `보류(32.9%)` | `보류(33.6%)` | `보류(0.2%)` | `보류(12.1%)` | `보류(41.9%)` | C 파일은 위 행과 같다. Go `perf_go_multi_linux_20260522_043022_codex_go_tcp_multi_spot_sendsend_poller_1ms_20260522.txt`. C와 같은 size별 active slot 제한(65536B 이상 32, 131072B 이상 8)을 유지하고, client active loop를 단일 poller 중심으로 정렬했다. Go Spot poller에서 50ms wait만 사용하면 large reply readiness를 놓쳐 65536B 이상이 result 없이 실패하는 후보 `perf_go_multi_linux_20260522_042415_codex_go_tcp_multi_spot_sendsend_poller_20260522.txt`, `perf_go_multi_linux_20260522_042648_codex_go_tcp_multi_spot_sendsend_poller_drain_20260522.txt`가 나와, poll wait 전에 public `RecvRouted(...DONTWAIT)` drain을 한 번 수행하고 wait cap을 1ms로 낮췄다. active 송신을 매번 native `NewWindowMessage`로 만드는 후보 `perf_go_multi_linux_20260522_042800_codex_go_tcp_multi_spot_sendsend_poller_servercopy_65536_20260522.txt`와 active payload를 native message buffer에 직접 stamp하는 후보 `perf_go_multi_linux_20260522_053208_codex_go_tcp_multi_spot_sendsend_native_stamp_20260522.txt`는 large completion을 깨서 반영하지 않았다. server echo가 public `Received.Parts()[0]`를 직접 `received.Send()`에 넘기는 후보 `perf_go_multi_linux_20260522_050605_codex_go_tcp_multi_spot_sendsend_received_part_echo_20260522.txt`는 small size를 올렸지만 65536B/262144B가 사실상 깨져 반영하지 않았다. 실패 없이 유효 수치를 얻었지만 SPOT 최소 기준보다 낮고 131072B는 이전보다 낮다. public 계약을 바꾸지 않는 추가 후보가 없어 보류한다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `tcp` | `MULTI_STREAM` | `통과(98.1%)` | `통과(89.4%)` | `통과(78.4%)` | `통과(90.0%)` | `통과(94.2%)` | `통과(80.1%)` | C 파일은 위 행과 같다. Go `perf_go_multi_linux_20260522_033642_codex_go_tcp_multi_stream_alias_fix_20260522.txt`. Go runner는 shared C stream client가 출력하는 `STREAM` result를 `MULTI_STREAM` 행으로 집계하도록 수정했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_DEALER_DEALER` | `보류(36.7%)` | `보류(52.8%)` | `통과(67.1%)` | `보류(23.7%)` | `보류(12.1%)` | `보류(4.8%)` | C `perf_c_multi_linux_20260522_055144_codex_c_ws_multi_for_go_20260522.txt` 대비 Go `perf_go_multi_linux_20260522_060003_codex_go_ws_multi_current_20260522.txt`. C처럼 단일 poll loop와 pending socket만 `POLLOUT`으로 두는 poll set 의미를 유지했다. server `RecvPart`를 ws 65536B 이상으로 넓힌 후보 `perf_go_multi_linux_20260522_062510_codex_go_ws_multi_dd_recvpart_large_20260522.txt`는 65536B/131072B/262144B를 더 낮춰 반영하지 않았다. tcp에서 확인한 `RecvPart` 확대, client context 공유, 단일 part send move/restore 후보도 public 계약 보존 또는 large completion을 깨 반영하지 않았다. Go 단순 one-way 최소 기준보다 낮은 칸은 public 계약을 바꾸지 않는 추가 후보가 없어 보류한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_DEALER_ROUTER` | `통과(60.7%)` | `통과(62.2%)` | `통과(60.9%)` | `통과(46.7%)` | `통과(51.6%)` | `통과(66.7%)` | C/Go full 파일은 위 행과 같다. routed echo client는 C처럼 한 client context 안에서 모든 socket을 단일 poll loop로 처리한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_ROUTER_ROUTER` | `통과(41.2%)` | `통과(41.7%)` | `통과(41.5%)` | `통과(41.3%)` | `통과(58.5%)` | `통과(71.9%)` | C full 파일은 위 행과 같다. Go 64B/256B/1024B/131072B/262144B `perf_go_multi_linux_20260522_060003_codex_go_ws_multi_current_20260522.txt`, 65536B 제한 재측정 `perf_go_multi_linux_20260522_062743_codex_go_ws_multi_rr65536_recheck_20260522.txt`. 65536B는 full에서 39.1%였으나 같은 조건 제한 재측정에서 41.3%로 최소 기준을 통과했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_PUBSUB` | `보류(46.3%)` | `통과(53.2%)` | `통과(53.4%)` | `통과(63.8%)` | `통과(62.9%)` | `통과(62.5%)` | C full 파일은 위 행과 같다. Go 64B/256B/1024B/262144B `perf_go_multi_linux_20260522_060003_codex_go_ws_multi_current_20260522.txt`, 65536B `perf_go_multi_linux_20260522_061952_codex_go_ws_multi_pubsub65536_recheck_20260522.txt`, 131072B `perf_go_multi_linux_20260522_061854_codex_go_ws_multi_pubsub_mid_recheck_20260522.txt`. server publish를 `NewWindowMessage`로 바꾸는 후보 `perf_go_multi_linux_20260522_062658_codex_go_ws_multi_pubsub_window_message_probe_20260522.txt`는 262144B는 올렸지만 64B 미달을 해결하지 못했고 기존 tcp large completion 실패 이력이 있어 반영하지 않았다. 64B는 Go 단순 one-way 최소 기준보다 낮지만, public 계약을 바꾸지 않는 추가 후보가 없어 보류한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_SPOT` | `보류(47.0%)` | `보류(45.9%)` | `보류(44.2%)` | `통과(73.3%)` | `통과(59.0%)` | `통과(100.1%)` | C full 파일은 위 행과 같다. Go 64B/256B/1024B/131072B/262144B `perf_go_multi_linux_20260522_061247_codex_go_ws_multi_spot_msgunit_report_fix_20260522.txt`, 65536B `perf_go_multi_linux_20260522_061513_codex_go_ws_multi_spot65536_msgunit_report_fix_20260522.txt`. Go multi report의 SPOT fallback `MsgUnit(B)`가 small size에서 4096으로 표시되던 문제를 `bindings/python/perf/perf_report.py`에서 C와 같은 size별 값으로 고쳤고, Go perf helper는 C helper처럼 `SetAutoHwmMsgUnitBytes` 뒤 public `RecalculateAutoHwm()`을 호출한다. C처럼 active publish에서 `DONTWAIT` 실패 뒤 blocking submit 1회를 시도하는 후보 `perf_go_multi_linux_20260522_063626_codex_go_ws_multi_spot_blocking_fallback_20260522.txt`는 64B/256B를 더 낮춰 반영하지 않았다. 64B/256B/1024B는 Go SPOT 최소 기준보다 낮지만, public `SubscribePart` 수신 경로와 C와 같은 worker 분배 의미 안에서 추가 내부 후보를 찾지 못해 보류한다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_SPOT_REQREP` | `통과(61.1%)` | `통과(60.8%)` | `통과(63.8%)` | `보류(42.3%)` | `보류(36.5%)` | `보류(44.1%)` | C full 파일은 위 행과 같다. Go 64B/256B/1024B/262144B `perf_go_multi_linux_20260522_061530_codex_go_ws_multi_spot_reqrep_msgunit_report_fix_20260522.txt`, 65536B/131072B `perf_go_multi_linux_20260522_061627_codex_go_ws_multi_spot_reqrep_mid_msgunit_report_fix_20260522.txt`. request completion은 public `POLLCOMPLETION` poller를 사용한다. tcp에서 payload clone 제거와 single-part request fast path 후보는 실패 size를 해결하지 못하거나 completion을 깨 반영하지 않았다. 65536B 이상은 Go SPOT 최소 기준보다 낮지만, public 계약을 바꾸지 않는 추가 후보가 없어 보류한다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_SPOT_SENDSEND` | `보류(34.0%)` | `보류(30.6%)` | `보류(32.6%)` | `보류(0.2%)` | `보류(25.8%)` | `보류(0.4%)` | C full 파일은 위 행과 같다. Go 64B/256B/1024B `perf_go_multi_linux_20260522_061750_codex_go_ws_multi_spot_sendsend_small_msgunit_report_fix_20260522.txt`, 65536B/131072B/262144B `perf_go_multi_linux_20260522_061822_codex_go_ws_multi_spot_sendsend_large_msgunit_report_fix_20260522.txt`. C와 같은 50ms poll wait cap 후보 `perf_go_multi_linux_20260522_062931_codex_go_ws_multi_spot_sendsend_50ms_wait_probe_20260522.txt`는 65536B/262144B를 더 낮춰 반영하지 않았다. C와 같은 size별 active slot 제한을 유지하고, client active loop는 단일 poller 중심이다. C server는 받은 multipart를 그대로 echo하지만 Go public `Received.Send()`는 단일 part 실패 시 원본 메시지를 보존해야 한다. 단일 part submit을 copy 대신 move 후 실패 시 restore하는 내부 후보는 `go test ./...`의 `TestBlockingSendFailurePreservesMessagePayload`에서 원본 보존 계약을 깨 반영하지 않았다. 전체 size가 Go SPOT 최소 기준보다 낮지만, public 계약을 바꾸지 않는 추가 후보가 없어 보류한다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `ws` | `MULTI_STREAM` | `통과(92.4%)` | `통과(83.5%)` | `통과(79.3%)` | `통과(92.2%)` | `통과(114.1%)` | `통과(149.3%)` | C 64B/256B `perf_c_multi_linux_20260522_055144_codex_c_ws_multi_for_go_20260522.txt`, 1024B/65536B `perf_c_multi_linux_20260522_062009_codex_c_ws_multi_stream_recheck_for_go_20260522.txt`, 131072B/262144B `perf_c_multi_linux_20260522_062037_codex_c_ws_multi_stream_large_for_go_20260522.txt` 대비 Go `perf_go_multi_linux_20260522_060003_codex_go_ws_multi_current_20260522.txt`. C full-run은 1024B 이상 STREAM이 partial이라 같은 조건 제한 재측정으로 보강했다. Go STREAM은 shared C reference client를 사용하므로 측정 surface는 Go STREAM server다. 262144B는 120%를 넘어 outlier 재검토 대상이다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_DEALER_DEALER` | `미달(31.5%)` | `미달(24.2%)` | `통과(66.8%)` | `미달(1.9%)` | `미달(4.8%)` | `미달(0.9%)` | C `perf_c_multi_linux_20260522_063910_codex_c_wss_multi_for_go_20260522.txt` 대비 Go `perf_go_multi_linux_20260522_064814_codex_go_wss_multi_current_20260522.txt`. C처럼 단일 poll loop와 pending socket `POLLOUT` 의미를 유지했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_DEALER_ROUTER` | `통과(60.6%)` | `통과(59.3%)` | `통과(58.7%)` | `통과(47.6%)` | `통과(51.3%)` | `통과(52.9%)` | C/Go full 파일은 위 행과 같다. routed echo client는 C처럼 한 client context 안에서 모든 socket을 단일 poll loop로 처리한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_ROUTER_ROUTER` | `통과(44.5%)` | `통과(42.2%)` | `통과(43.9%)` | `통과(50.1%)` | `통과(52.9%)` | `통과(52.6%)` | C/Go full 파일은 위 행과 같다. 절대 기준을 통과하므로 상대 기준은 진단 보조로만 본다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_PUBSUB` | `미달(42.7%)` | `미달(42.1%)` | `통과(63.2%)` | `통과(53.2%)` | `통과(60.4%)` | `통과(64.7%)` | C full 파일은 위 행과 같다. Go 64B/256B/1024B/262144B `perf_go_multi_linux_20260522_064814_codex_go_wss_multi_current_20260522.txt`, 65536B/131072B 제한 재측정 `perf_go_multi_linux_20260522_065747_codex_go_wss_multi_pubsub_mid_recheck_20260522.txt`. full-run의 65536B/131072B no-result는 같은 조건 제한 재측정으로 배제했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT` | `미달(46.5%)` | `미달(24.8%)` | `미달(12.7%)` | `통과(50.4%)` | `통과(56.4%)` | `통과(93.1%)` | C/Go full 파일은 위 행과 같다. public `SubscribePart` 수신 경로와 C와 같은 worker 분배 의미를 사용한다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT_REQREP` | `통과(55.9%)` | `통과(59.5%)` | `통과(60.0%)` | `통과(76.4%)` | `통과(76.5%)` | `통과(78.5%)` | C full 파일은 위 행과 같다. Go `perf_go_multi_linux_20260522_070749_codex_go_wss_multi_spot_reqrep_submit_not_connected_all_20260522.txt`. 이전 full-run과 단독 제한 재측정 `perf_go_multi_linux_20260522_065858_codex_go_wss_multi_spot_reqrep262144_recheck_20260522.txt`의 262144B no-result는 Go perf client가 active 송신 중 `SubmitNotConnected`를 fatal로 처리하던 차이 때문이었다. C `MULTI_SPOT_REQREP` client처럼 `ZLINK_SUBMIT_NOT_CONNECTED`는 fatal이 아니라 다음 poll loop에서 재시도하는 의미로 맞췄다. request completion은 public `POLLCOMPLETION` poller를 사용한다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_SPOT_SENDSEND` | `미달(29.2%)` | `미달(30.3%)` | `미달(33.7%)` | `미달(0.0%)` | `미달(7.5%)` | `통과(50.9%)` | C/Go full 파일은 위 행과 같다. Go `perf_go_multi_linux_20260522_070848_codex_go_wss_multi_spot_sendsend_submit_not_connected_all_20260522.txt`. 이전 full-run과 단독 제한 재측정 `perf_go_multi_linux_20260522_065912_codex_go_wss_multi_spot_sendsend65536_recheck_20260522.txt`의 65536B no-result는 Go perf client가 active 송신 중 `SubmitNotConnected`를 fatal로 처리하던 차이 때문이었다. C `MULTI_SPOT_SENDSEND` client처럼 `ZLINK_SUBMIT_NOT_CONNECTED`는 fatal이 아니라 다음 poll loop에서 재시도하는 의미로 맞췄다. C와 같은 size별 active slot 제한을 유지하고, client active loop는 단일 poller 중심이다. 65536B/131072B는 유효 수치를 얻었지만 여전히 SPOT 최소 기준보다 낮다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `wss` | `MULTI_STREAM` | `통과(88.6%)` | `통과(95.3%)` | `통과(89.4%)` | `통과(92.3%)` | `해당 없음` | `해당 없음` | C/Go full 파일은 위 행과 같다. Go STREAM은 shared C reference client를 사용하므로 측정 surface는 Go STREAM server다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_DEALER_DEALER` | `미달(26.1%)` | `미달(16.6%)` | `통과(60.2%)` | `미달(4.2%)` | `미달(0.5%)` | `미달(6.2%)` | C `perf_c_multi_linux_20260522_071155_codex_c_tls_multi_for_go_20260522.txt` 대비 Go `perf_go_multi_linux_20260522_072100_codex_go_tls_multi_current_20260522.txt`. C처럼 단일 poll loop와 pending socket `POLLOUT` 의미를 유지했다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_DEALER_ROUTER` | `통과(61.7%)` | `통과(58.8%)` | `통과(59.1%)` | `통과(46.0%)` | `통과(47.7%)` | `통과(44.8%)` | C/Go full 파일은 위 행과 같다. routed echo client는 C처럼 한 client context 안에서 모든 socket을 단일 poll loop로 처리한다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_ROUTER_ROUTER` | `미달(22.0%)` | `통과(42.8%)` | `통과(42.3%)` | `통과(46.6%)` | `통과(52.1%)` | `통과(51.0%)` | C/Go full 파일은 위 행과 같다. 절대 기준을 통과하는 256B 이상은 상대 기준을 진단 보조로 본다. 64B는 multi routed echo 최소 기준보다 낮다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_PUBSUB` | `미달(41.5%)` | `미달(42.3%)` | `통과(52.4%)` | `통과(51.0%)` | `통과(56.2%)` | `통과(58.7%)` | C/Go full 파일은 위 행과 같다. client는 public `SubscribePart` caller-owned 수신 경로를 사용하고, C처럼 하나의 stop/cooldown 신호로 phase를 종료한다. 64B/256B는 Go 단순 one-way 최소 기준보다 낮다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_SPOT` | `미달(45.9%)` | `통과(57.2%)` | `통과(79.1%)` | `통과(160.8%)` | `통과(139.8%)` | `통과(112.4%)` | C/Go full 파일은 위 행과 같다. public `SubscribePart` 수신 경로와 C와 같은 worker 분배 의미를 사용한다. 65536B/131072B는 120%를 넘어 outlier 재검토 대상이다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_SPOT_REQREP` | `통과(52.3%)` | `통과(54.5%)` | `통과(50.3%)` | `통과(61.6%)` | `통과(62.7%)` | `통과(64.4%)` | C/Go full 파일은 위 행과 같다. request completion은 public `POLLCOMPLETION` poller를 사용한다. `SubmitNotConnected`는 C처럼 fatal이 아니라 다음 poll loop에서 재시도하는 의미로 처리한다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_SPOT_SENDSEND` | `미달(27.3%)` | `미달(29.2%)` | `미달(28.1%)` | `미달(0.0%)` | `미달(33.4%)` | `미달(0.0%)` | C/Go full 파일은 위 행과 같다. `SubmitNotConnected`는 C처럼 fatal이 아니라 다음 poll loop에서 재시도하는 의미로 처리한다. C와 같은 size별 active slot 제한을 유지하고, client active loop는 단일 poller 중심이다. 전체 size가 SPOT 최소 기준보다 낮다. auto-HWM 활성과 size별 SpotNode `MsgUnit(B)` 일치를 확인했다. |
| `tls` | `MULTI_STREAM` | `통과(93.8%)` | `통과(101.2%)` | `통과(98.5%)` | `통과(97.1%)` | `해당 없음` | `해당 없음` | C/Go full 파일은 위 행과 같다. Go STREAM은 shared C reference client를 사용하므로 측정 surface는 Go STREAM server다. auto-HWM 활성과 size별 `MsgUnit(B)` 일치를 확인했다. |

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
