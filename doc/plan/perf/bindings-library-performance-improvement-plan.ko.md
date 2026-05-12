# bindings 라이브러리 성능 개선 실행 계획

> 작성일: 2026-05-08
>
> 목적: `bindings/c/perf` 결과를 기준으로 삼아 언어별 binding 라이브러리의
> 성능을 목표 비율까지 끌어올리는 작업 순서와 반복 절차를 고정한다.
>
> 기준 정책:
> - [`doc/perf/PERF_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_POLICY.md)
> - [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_SINGLE_TEST_POLICY.md)
> - [`doc/perf/PERF_MULTI_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_MULTI_TEST_POLICY.md)

## TODO

- [ ] 대상 조합별 C 기준 측정 및 비교
- [ ] C++ 목표 달성
- [ ] .NET 목표 달성
- [ ] Java 목표 달성
- [ ] Node 목표 달성
- [ ] Python 목표 달성
- [ ] Go 목표 달성
- [ ] Rust 목표 달성
- [ ] 전체 언어 최종 결과 요약

## 현재 상태 요약 (2026-05-12)

최근 점검 기준은 아래 C multi 결과다.

- C 기준: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260512_121228_c_multi_echo_recheck_20260512.txt`
- .NET 최신 결과: `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260512_122828_dotnet_multi_echo_poller_tag_20260512.txt`
- Java 최신 결과:
  - `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260512_125540_java_rr_small_recv_send_context_20260512.txt`
  - `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260512_125237_java_rr1024_recv_send_context_20260512.txt`
  - `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260512_125259_java_dr_recv_send_context_20260512.txt`

multi perf 측정 의미는 정책과 C 기준에 맞춘 상태다. .NET/Java multi runner는
기본 server/client I/O thread를 모두 `4`로 출력하고, 각 size 케이스에서 raw
socket payload size를 auto-HWM message unit으로 설정한 뒤 context auto-HWM을
재계산한다. 이 값은 payload 최대 크기 제한이 아니라 HWM 예산을 메시지 슬롯 수로
환산하기 위한 기준 단위다.

`MULTI_DEALER_ROUTER`와 `MULTI_ROUTER_ROUTER` echo 계열 client는 C 기준과 같은
단일 app thread poller loop에서 client socket들을 구동한다. 따라서 여기의 thread
수 비교에서 맞춰야 하는 값은 context I/O thread 수이며, server/client 모두 `4`다.
latency는 왕복 echo 패턴에서 RTT의 절반으로 기록하고, one-way 패턴은 송신 시각부터
수신 시각까지의 값을 그대로 기록한다.

echo 서버의 "받은 쪽으로 다시 보낸다" 동작은 object binding에서
`Received.Send(...)` 계열 API로 표현한다. 이 API는 request-reply 의미의
`Reply(...)`가 아니라, `Received` 내부 receive context가 알고 있는 원래 송신
경로로 일반 routed message를 보내는 기능이다. ROUTER에서 직접 받은 메시지는
peer routing id로 보내고, SPOT routed 수신은 source node rid와 source spot rid를
함께 보존한 원래 경로로 보낸다. 사용자가 routing id 필드를 조합해 send target을
다시 만들게 하지 않는 것이 이 API의 목적이다.

perf의 echo hot path도 이 의미를 그대로 사용한다. .NET, Java, Node, Python, Go,
Rust, C++ multi `DEALER_ROUTER` / `ROUTER_ROUTER` 서버는 가능한 경우
`received.Send(...)` 또는 언어별 동일 이름 API로 즉시 echo를 시도하고,
backpressure 큐에 들어간 항목만 기존처럼 명시 routing id를 보관해 재전송한다. 이는
측정 의미를 바꾸는 perf 우회가 아니라, 공개 API가 실제 사용자 코드에서 기대하는
"받은 상대에게 send" 의미를 perf에도 적용한 것이다. C binding은 object receive
wrapper가 없으므로 예외로 두고, C perf는 계속 명시 routing id 기반 C API를 기준으로
사용한다.

현재 남은 .NET multi 미달 조합은 아래와 같다.

| Pattern | Size | C Kops/s | .NET Kops/s | Ratio | 목표 |
|---------|------|----------|-------------|-------|------|
| MULTI_ROUTER_ROUTER | 64B | 423.978 | 215.295 | 0.508 | 0.75 |
| MULTI_ROUTER_ROUTER | 256B | 421.436 | 214.040 | 0.508 | 0.80 |
| MULTI_ROUTER_ROUTER | 1024B | 412.499 | 208.208 | 0.505 | 0.82 |
| MULTI_DEALER_ROUTER | 64B | 450.881 | 272.008 | 0.603 | 0.75 |
| MULTI_DEALER_ROUTER | 256B | 443.658 | 274.240 | 0.618 | 0.80 |
| MULTI_DEALER_ROUTER | 1024B | 441.970 | 269.636 | 0.610 | 0.82 |

현재 남은 Java multi 미달 조합은 아래와 같다.

| Pattern | Size | C Kops/s | Java Kops/s | Ratio | 목표 |
|---------|------|----------|-------------|-------|------|
| MULTI_ROUTER_ROUTER | 64B | 423.978 | 249.857 | 0.589 | 0.70 |
| MULTI_ROUTER_ROUTER | 256B | 421.436 | 246.756 | 0.586 | 0.75 |
| MULTI_ROUTER_ROUTER | 1024B | 412.499 | 244.776 | 0.593 | 0.77 |
| MULTI_DEALER_ROUTER | 256B | 443.658 | 316.651 | 0.714 | 0.75 |
| MULTI_DEALER_ROUTER | 1024B | 441.970 | 318.112 | 0.720 | 0.77 |

Java `MULTI_DEALER_ROUTER` 64B는 ready socket만 recv하도록 C 기준과 맞춘 뒤
`0.723`으로 목표 `0.70`을 넘었다. 나머지 echo 조합은 아직 미달이다.

다음 개선은 perf 우회가 아니라 binding 라이브러리 내부 최적화로 진행한다.

1. `.NET`: `Received.Send(...)` delegate 주입을 ref-out routed recv hot path에서
   직접 send context로 바꿨고, echo client는 POLLIN ready가 없는 socket에 대해
   `Recv(DontWait)`를 호출하지 않게 했다. 그래도 RR/DR 모두 목표와 큰 차이가 남는다.
   다음 후보는 public `Poller.WaitAll` 결과 처리 비용과 `Message.WrapBytes` send
   수명 비용을 더 줄이는 것이다.
2. `Java`: profiler에서 `RecvResult.values()` 배열 생성, ROUTER `DONT_WAIT`
   recv의 일반 FFM downcall, `Received.send(Message)`의 `List.of`와 per-recv lambda
   비용을 확인했다. 이를 줄인 뒤 RR 1024B는 `238.164` → `244.776` Kops/s로
   올랐지만 RR 전체와 DR 256B 이상은 아직 남았다.
3. 두 언어 모두 다음 병목은 client poller loop와 public message send/recv 경계다.
   perf 전용 native 우회가 아니라 binding public API 내부 비용을 줄이는 방향으로
   이어 간다.

## 1. 범위와 목표

이번 작업은 perf 자체를 빠르게 만드는 일이 아니라, 각 언어 binding 라이브러리가
public API를 통해 내는 실제 성능을 개선하는 일이다. perf는
`doc/perf` 정책에 맞게 이미 작성되어 있다고 보고, perf 자체의 측정 버그가
확인된 경우를 제외하면 수정하지 않는다.

목표 비율은 같은 suite, pattern, transport, message size, metric 조합에서
`bindings/c/perf` 결과를 기준으로 계산한다.

| 순서 | 언어 | perf 경로 |
|------|------|-----------|
| 1 | C++ | `bindings/cpp/perf` |
| 2 | .NET | `bindings/dotnet/perf` |
| 3 | Java | `bindings/java/perf` |
| 4 | Node | `bindings/node/perf` |
| 5 | Python | `bindings/python/perf` |
| 6 | Go | `bindings/go/perf` |
| 7 | Rust | `bindings/rust/perf` |

목표 비율은 message size별로 다르게 적용한다. 64B는 per-message 고정비용(wrapper
객체 생성, GC, JIT 오버헤드)이 지배하고, 256KB는 bandwidth bound이라 모든 언어가
수렴하기 때문이다. 아래 표는 모든 패턴 중 최솟값 기준이다. 즉 특정 패턴에서 이
수치를 밑돌면 해당 size 목표를 달성하지 못한 것으로 본다.

| Size | C++ | .NET | Java | Rust | Go | Node | Python |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 64B | ≥75% | ≥75% | ≥70% | ≥88% | ≥55% | ≥42% | ≥22% |
| 256B | ≥85% | ≥80% | ≥75% | ≥90% | ≥65% | ≥52% | ≥30% |
| 1024B | ≥88% | ≥82% | ≥77% | ≥92% | ≥68% | ≥57% | ≥35% |
| 64KB | ≥90% | ≥85% | ≥80% | ≥92% | ≥72% | ≥62% | ≥40% |
| 128KB | ≥90% | ≥85% | ≥80% | ≥92% | ≥74% | ≥64% | ≥42% |
| 256KB | ≥90% | ≥85% | ≥80% | ≥92% | ≥75% | ≥65% | ≥43% |

목표 근거:

- C++/Rust는 zero-overhead wrapper 기준. 64B에서 ROUTER_ROUTER(multi)가 구조적으로
  63% 내외가 한계이므로 75%/88%로 설정하고, 256KB에서 전체 목표(90%/92%)에 수렴한다.
- .NET은 최적화 후 single ROUTER_ROUTER tcp 64B에서 82.5%까지 확인됐고, multi는
  더 낮아 75%를 사이즈별 최솟값으로 설정한다.
- Java는 JVM ≈ CLR 기준으로 .NET 목표의 94% 비율(= 계획 전체 목표 80%/85%)을 적용한다.
  현재 측정값(31~48%)은 구현 최적화가 미완료된 상태이며 목표값이 아니다.
- Go는 goroutine I/O 모델이 .NET threadpool과 경쟁력이 있어 .NET보다 약간 낮게 설정.
- Node는 V8 event loop per-message 오버헤드를 감안해 .NET의 약 65% 수준.
- Python은 GIL + ctypes 오버헤드를 감안해 .NET의 약 28% 수준.

비교의 1차 기준은 `throughput`이다. `bandwidth`는 같은 payload 크기에서
throughput과 같은 방향으로 움직여야 하며, `latency`, `latency_p95`,
`latency_p99`가 크게 악화되면 목표 비율을 만족해도 완료로 보지 않는다.
레이턴시는 낮을수록 좋기 때문에, C 기준보다 느린 정도가 해당 size의 언어 목표 비율의
역수 안에 들어오는지 함께 본다. 예를 들어 256KB에서 90% 목표 언어는 레이턴시가 C의
약 `1 / 0.90` 배를 크게 넘지 않아야 한다.

## 2. 고정 전제

- `bindings/c/perf`가 기준이다. 기준은 전체 matrix를 미리 고정하지 않고,
  작업 중인 동일 suite, pattern, transport, message size 조합을 필요할 때
  `bindings/c/perf`에서 직접 측정한 결과로 삼는다.
- `bindings/c/perf`를 실행하기 전에 `core/build` runtime을 최신으로 만든다.
  `core/src/` 또는 `core/include/`를 바꾼 뒤에는 반드시
  `cmake --build core/build`를 먼저 실행한다.
- `bindings/c/perf` 수치는 `core/build`의 `libzlink.so` 기준으로만 해석한다.
  `build_cpp_release`나 임시 빌드 디렉터리의 runtime으로 C 기준을 만들지 않는다.
- 각 언어는 위 표의 순서대로 하나씩 끝낸다. 앞 언어가 목표를 달성하기 전에는
  다음 언어 최적화로 넘어가지 않는다.
- 각 언어 perf는 해당 언어 binding의 public API만 사용해야 한다. native core
  perf 바이너리를 호출해 결과만 중계하는 방식은 측정으로 인정하지 않는다.
  C를 제외한 언어의 perf가 `zlink_*` C API, C FFI 함수, 내부 native helper,
  내부 구현 클래스를 직접 호출해 수치를 만들면 해당 언어 binding 성능으로
  인정하지 않는다. C API 호출은 해당 binding 라이브러리 내부 구현에만 둘 수
  있고, perf hot path는 사용자에게 공개된 binding API를 통과해야 한다.
  성능 수치 달성만을 위해 새 public API, native API, raw handle API, zero-copy
  전용 API를 추가한 뒤 perf만 그 경로를 쓰게 하는 것도 금지한다. 공개 API는
  이미 구현된 계약을 기준으로 보고, 성능 개선은 기존 binding public API의
  내부 구현, 객체 수명, 복사/할당, 예외 변환, callback dispatch 비용을 줄이는
  방향으로 수행한다.
- perf 측정 의미, RESULT line, ready/active phase, fail/skip/unsupported 의미는
  `doc/perf` 정책과 동일하게 유지한다.
- retry, inflight 제한, sleep 기반 보정, 숨은 fallback으로 수치를 만들지 않는다.
- 이 작업은 사람의 추가 판단을 기다리지 않고 진행한다. 모든 언어가 목표를
  달성할 때까지 자동으로 다음 측정, 분석, 수정, 검증 단계로 넘어간다.

## 3. 무중단 자동 운영 원칙

이 계획은 사람이 중간에 방향을 다시 정해 주지 않아도 끝까지 진행할 수 있어야
한다. 작업자는 아래 원칙에 따라 스스로 다음 행동을 결정한다.

작업자는 중간 결과를 설명할 수 있지만, 그 설명은 종료가 아니라 진행 중 상태
공유로만 취급한다. C++ → .NET → Java → Node → Python → Go → Rust 전체가
완료 정의를 만족하기 전에는 최종 보고로 작업을 닫지 않는다. 목표 미달 조합이
하나라도 남아 있으면 반드시 다음 측정, 분석, 수정, 테스트, 재측정 중 하나로
즉시 이어 간다.

### 3.0 최종 응답 금지 조건

아래 조건 중 하나라도 참이면 작업을 끝냈다고 말하지 않는다.

- 현재 언어의 정책 조합 중 목표 미달, partial, fail, 누락 결과가 남아 있다.
- 현재 언어의 목표 달성 여부를 같은 pattern, transport, message size 단위로
  확인하지 않았다.
- 앞 언어가 완료되지 않았는데 다음 언어 측정이나 최종 요약으로 넘어가려 한다.
- 변경한 binding/core 코드에 필요한 회귀 테스트나 smoke가 아직 통과하지 않았다.
- 실행 기록의 "다음 판단"에 다음 작업 항목이 남아 있다.

이 경우 작업자는 짧은 진행 상황을 남긴 뒤 바로 다음 작업 항목을 실행한다.
진행 상황 공유는 final report가 아니며, 작업 중단 사유가 될 수 없다.
대화나 실행 세션이 끊긴 뒤 다시 시작해도 새 계획을 세우지 않는다. 마지막
기록의 "다음 판단"과 현재 언어의 남은 미달 조합을 읽고, 그 지점에서 같은
루프를 재개한다.

### 3.1 중단하지 않는 기본 루프

전체 작업은 아래 루프를 목표 달성까지 반복한다.

1. 현재 대상 언어를 고른다.
2. 현재 볼 동일 pattern, transport, message size에서 C 기준과 대상 언어 결과를
   측정한다.
3. 목표 미달 조합을 찾는다.
4. 가장 큰 병목 하나를 고른다.
5. perf가 아니라 라이브러리 또는 core를 수정한다.
6. 필요한 회귀 테스트를 추가하거나 갱신한다.
7. 테스트와 perf smoke를 실행한다.
8. 목표 조합을 다시 측정한다.
9. 목표 조합이 기준을 만족하면 남은 정책 조합을 같은 방식으로 하나씩 확인해
   언어 완료 여부를 판단한다.
10. 해당 언어를 완료 처리하고 다음 언어로 넘어간다.

이 루프는 실패를 만나도 멈추지 않는다. 실패는 다음 작업 항목으로 변환한다.

| 실패 유형 | 자동 다음 행동 |
|-----------|----------------|
| build 실패 | 실패 로그를 읽고 가장 가까운 컴파일/링크 오류부터 수정한다 |
| 테스트 실패 | 실패 테스트를 기준으로 원인을 좁히고 라이브러리 구현을 고친다. 테스트 기대값은 공개 헤더와 정책이 틀렸다고 확인된 경우에만 수정한다 |
| perf partial | 실패 조합을 bug 항목으로 기록하고 fail 원인을 먼저 고친다 |
| 목표 비율 미달 | 가장 손실이 큰 조합을 다음 병목 분석 대상으로 고른다 |
| 결과 흔들림 | 성공한 같은 조건을 반복 측정하고 median 기준으로 판단한다. 실패한 조합을 통과시키기 위한 재시도는 하지 않는다 |
| 환경 문제 | ulimit, 포트 충돌, stale build, runtime 경로를 자동으로 점검하고 고친다 |

루프를 한 번 실행한 뒤에도 목표가 남아 있으면 같은 언어에서 다시 1번으로
돌아간다. "개선됐지만 목표 미달"은 완료가 아니라 다음 병목 분석 입력이다.
문서 기록을 남긴 직후에도 다음 측정 또는 다음 수정 명령을 계속 실행한다.
응답 작성, 라운드 기록, 로그 요약은 루프의 보조 작업일 뿐이다. 이런 작업을
마친 뒤에는 반드시 현재 언어의 다음 미달 조합으로 돌아가서 명령 실행 상태를
이어 간다.

### 3.2 사람에게 묻지 않는 의사결정 규칙

아래 선택은 사람에게 묻지 않고 결정한다.

- 어떤 언어를 다음에 할지: 이 문서의 순서를 따른다.
- 어떤 조합을 먼저 볼지: 목표 대비 ratio가 가장 낮은 조합을 먼저 본다.
- 어떤 metric을 우선할지: `complete` 여부, 64B throughput, 큰 메시지 bandwidth,
  latency triplet 순서로 본다.
- perf를 고칠지 말지: 정책 위반 또는 측정 버그일 때만 고친다.
- core와 binding 중 어디를 고칠지: 여러 언어에서 같은 증상이면 core를 먼저 보고,
  특정 언어에서만 보이면 해당 binding을 먼저 본다.
- 테스트를 어디에 둘지: core 버그는 core 테스트, binding 버그는 해당 binding
  테스트에 둔다.
- 변경이 실패하면 어떻게 할지: 실패한 변경을 작게 되돌리거나 더 작은 가설로
  나눈 뒤 즉시 다음 라운드를 실행한다.

### 3.3 자동 확장 조건

사람의 판단을 기다리는 대신, 아래 조건을 만나면 작업 범위를 자동으로 확장한다.
이 경우에도 해당 언어를 끝내기 전에는 다음 언어로 넘어가지 않는다.

- 공개 API 계약 변경이 필요하면 `doc/spec/draft/`에 구현 전 초안을 먼저 작성한 뒤,
  `core/include/zlink.h`, 테스트, errno 문서, binding 문서를 맞추는 작업까지
  이어 간다.
- core 병목이면 core 개선 작업으로 전환하고, core runtime을 다시 만든 뒤 현재
  대상 조합의 C 기준을 다시 측정해 대상 언어 측정을 계속한다.
- 특정 언어 runtime 제한이 병목이면 public API 의미를 유지하는 범위에서 buffer
  재사용, native handle 소유권, 호출 횟수 축소 같은 대안을 계속 시도한다.
- 같은 가설이 반복해서 실패하면 실패한 가설 목록에 남기고 다음 가설로 넘어간다.

자동 확장 라운드는 반드시 아래 정보를 남긴다.

- 마지막 동일 조합 C 결과 파일과 대상 언어 결과 파일.
- 목표 미달 조합 목록.
- 이미 시도한 변경과 배제한 가설.
- 병목이 public API, runtime, core, binding 중 어디에 있는지에 대한 근거.
- 다음 자동 작업 항목과 검증 명령.

### 3.4 전체 종료 조건

전체 작업은 아래 조건이 충족될 때만 끝난다.

- 모든 언어가 정책에 정의된 single + multi 조합을 같은 pattern, transport,
  message size 단위로 확인했을 때 목표 비율을 만족하고 complete로 끝났다.

그 외 상황에서는 작업을 멈추지 않는다. 실행 환경 장애도 완료 조건이 아니며,
복구 명령, 빌드 재생성, 포트 정리, ulimit 조정 같은 자동 복구 작업으로 변환한다.
분석만 하고 중단하지 않으며, 항상 다음 측정 또는 다음 수정으로 이어 간다.

최종 응답은 위 전체 종료 조건을 만족한 뒤에만 작성한다. 전체 종료 조건을
만족하지 못했는데 응답을 해야 하는 상황이면, 응답에는 "진행 중"임을 밝히고
바로 이어서 실행할 다음 명령 또는 수정 항목을 함께 남긴 뒤 작업을 계속한다.
특히 C++이 완료되지 않았다면 .NET으로 넘어가지 않고 C++의 남은 목표 미달
조합을 계속 처리한다.
중간 응답 후 실제 명령 실행이 가능한 상태라면 같은 턴에서 바로 다음 명령을
실행한다. 응답은 작업 경계가 아니라 로그 경계이며, 다음 명령을 실행하지 않은
상태로 멈추면 이 계획을 위반한 것으로 본다.

### 3.5 AI 절대 금지 규칙

아래 규칙은 성능 목표 달성 여부, 마감 압박, 기술적 어려움과 관계없이
**어떤 이유로도 어길 수 없다**. 이 규칙을 어기려는 상황이 발생하면
즉시 작업을 멈추고 위반 시도 이유와 현재 상태를 보고한다.
계속 진행 여부는 사람만 결정할 수 있다.

**이 절은 이 문서의 다른 모든 절보다 우선한다.**

1. **binding public API를 성능 목표 달성만을 위해 수정하는 것은 금지한다.**  
   public API는 C API 또는 다른 언어 binding과 동작 불일치가 확인된 경우,
   또는 정책 계약 상 명백한 버그인 경우에만 수정할 수 있다.
   성능을 이유로 API 시그니처, 반환 타입, 소유권 계약, 예외 정책을 바꾸는 것은 금지한다.

2. **binding 또는 core 라이브러리 버그를 perf 코드에서 우회(workaround/bypass)하는 것은 금지한다.**  
   올바르게 구현된 perf가 동작하지 않으면 그 원인은 binding 또는 core 버그로 본다.
   perf 코드를 바꿔 증상을 숨기지 않는다. 해당 라이브러리를 수정한다.

3. **성능 수치 달성만을 위해 public API 계약 외의 경로를 여는 것은 금지한다.**  
   내부 API, native handle, raw FFI, zero-copy 전용 경로를 perf에서만 사용하도록
   추가하는 것은 성능을 위장한 API 우회다. 이 경우 perf 수치는 결과로 인정하지 않는다.

4. **목표 달성이 어렵다는 이유로 위 1–3 중 하나라도 선택하는 것은 금지한다.**  
   목표를 달성하지 못하더라도 위 행동을 선택하지 않는다.
   이 경우 "목표 미달 / 추가 분석 필요" 상태로 보고하고 사람의 판단을 기다린다.

## 4. 대상 조합 기준 수립

각 작업 라운드는 전체 perf matrix를 먼저 측정하지 않는다. 현재 분석할 동일
suite, pattern, transport, message size 조합만 C perf와 대상 언어 perf에서
각각 측정한다. 이렇게 해야 오래 걸리는 선측정 때문에 작업이 지연되지 않고,
수정한 병목이 실제 대상 조합에 어떤 영향을 줬는지 바로 볼 수 있다.

```bash
cmake --build core/build

# single 예시
bindings/c/perf/run_benchmarks.sh --pattern <PATTERN> --msg-sizes <SIZE> --results-tag c_<PATTERN>_<SIZE>_YYYYMMDD
bindings/<lang>/perf/run_benchmarks.sh --pattern <PATTERN> --msg-sizes <SIZE> --results-tag <lang>_<PATTERN>_<SIZE>_YYYYMMDD

# multi 예시
bindings/c/perf/run_benchmarks_multi.sh --pattern <PATTERN> --msg-sizes <SIZE> --results-tag c_<PATTERN>_<SIZE>_YYYYMMDD
bindings/<lang>/perf/run_benchmarks_multi.sh --pattern <PATTERN> --msg-sizes <SIZE> --results-tag <lang>_<PATTERN>_<SIZE>_YYYYMMDD
```

비교는 항상 같은 조합끼리만 한다. 예를 들어 `MULTI_DEALER_ROUTER / tcp / 64B`
를 분석한다면 C도 그 조합만 측정하고, 대상 언어도 같은 조합만 측정한다.
`ALL` 또는 여러 size를 먼저 돌려 만든 오래된 파일을 근거로 현재 라운드의
목표 달성 여부를 판단하지 않는다.

언어 완료 판정도 한 번에 full matrix를 먼저 돌리는 방식이 아니다. 정책에 정의된
조합을 순서대로 확인하되, 각 조합은 동일한 `pattern / transport / size` 단위로
C와 대상 언어를 측정해 비교한다. 이미 같은 core runtime과 같은 작업 날짜에
측정한 동일 조합 결과가 있으면 그 파일을 재사용할 수 있지만, core 또는 해당
binding이 바뀐 뒤에는 그 조합을 다시 측정한다.

## 5. 비교 방법

결과 비교는 `RESULT` line의 동일 조합끼리 한다. 동일 조합이란 아래 값이 모두
같은 행을 뜻한다.

- suite: single 또는 multi
- pattern
- transport
- message size
- metric

throughput과 bandwidth는 높을수록 좋다.

```text
throughput_ratio = target_lang_throughput / c_throughput
bandwidth_ratio  = target_lang_bandwidth  / c_bandwidth
```

latency 계열은 낮을수록 좋다.

```text
latency_ratio     = c_latency     / target_lang_latency
latency_p95_ratio = c_latency_p95 / target_lang_latency_p95
latency_p99_ratio = c_latency_p99 / target_lang_latency_p99
```

각 ratio가 언어별 목표 이상이면 해당 metric은 통과로 본다. 예를 들어 Java의
목표는 80%이므로 `0.80` 이상이어야 한다. 단, latency 계열은 측정 흔들림이
크기 때문에 단일 행만 보고 결론을 내리지 않는다. throughput 목표를 만족한 뒤에도
`latency_p95` 또는 `latency_p99`가 반복 실행에서 계속 나빠지면 별도 병목으로
다룬다.

결과 해석 우선순위는 아래와 같다.

1. `complete` 여부: partial이면 먼저 실패 조합을 고친다.
2. 64B throughput: binding boundary 비용이 가장 잘 드러나는 기준이다.
3. 큰 메시지 bandwidth: 복사와 buffer 재사용 문제가 잘 드러난다.
4. latency triplet: 이벤트 루프, poller, runtime scheduling 문제가 잘 드러난다.
5. pattern별 편차: 특정 패턴만 낮으면 공통 binding보다 해당 socket wrapper를 먼저 본다.

## 6. 반복 작업 루프

각 언어는 아래 루프를 반복한다.

1. 현재 볼 `suite / pattern / transport / size` 조합을 고른다.
2. 그 조합만 C perf와 대상 언어 perf에서 측정한다.
3. 같은 조합끼리 `throughput`, `bandwidth`, `latency`, `latency_p95`,
   `latency_p99`를 비교한다.
4. 목표 미달 여부를 `pattern / transport / size / metric` 단위로 정리한다.
5. 가장 큰 손실이 있는 hot path를 하나 고른다.
6. binding 라이브러리나 core 라이브러리에서 병목을 줄이는 최소 변경을 만든다.
7. 변경 범위에 맞는 회귀 테스트를 먼저 실행한다.
8. 대상 조합 smoke를 실행한다.
9. 같은 대상 조합을 다시 측정해 개선 여부를 확인한다.
10. 결과 파일, 변경 요약, 남은 미달 조합을 `bindings-improvement-logs/<lang>.md`에 남긴다.

한 번에 여러 가설을 섞지 않는다. 같은 라운드에 변경을 많이 넣으면 어떤 변경이
성능을 올렸는지 판단할 수 없고, 회귀가 생겼을 때 되돌릴 경계도 흐려진다.

### 6.1 라운드 기록 양식

각 라운드가 끝나면 아래 형식으로 기록한다. 기록 위치는
`bindings-improvement-logs/<lang>.md`다(언어별 로그 분리). 새 라운드는 항상
해당 언어 로그 파일 하단에 같은 양식으로 추가한다.

```markdown
### YYYY-MM-DD <lang> round N

- 동일 조합 C 결과:
- 대상 언어 결과:
- 목표 미달 조합:
- 선택한 병목 가설:
- 변경한 라이브러리 파일:
- 추가/수정한 회귀 테스트:
- 실행한 검증 명령:
- 결과:
- 다음 판단:
```

목표 미달 조합은 가능한 한 구체적으로 적는다.

```text
suite=<single|multi>, pattern=<name>, transport=<name>, size=<bytes>,
metric=<throughput|bandwidth|latency|latency_p95|latency_p99>,
c=<value>, lang=<value>, ratio=<value>, target=<value>
```

## 7. perf 수정 금지 범위

이번 작업에서 perf 수정은 예외다. 아래 경우만 perf를 수정할 수 있다.

- RESULT line 파싱이나 출력이 정책과 다르게 구현된 경우.
- 같은 payload를 측정한다고 표시하지만 실제 metric header, phase, run_id,
  active 집계 조건이 C 기준과 다른 경우.
- runner가 stale runtime을 쓰거나, 공식 entrypoint 계약과 다르게 동작하는 경우.
- perf 코드가 해당 언어 binding public API가 아니라 내부 API나 native perf를
  직접 호출하는 경우.
- perf가 C를 제외한 언어에서 `zlink_*` C API, C FFI, native handle, 내부 helper,
  내부 구현 클래스를 직접 사용해 binding public API를 우회하는 경우.

아래 수정은 금지한다.

- timeout을 늘려 실패를 통과처럼 보이게 하는 수정.
- retry를 추가해 실패를 숨기는 수정.
- inflight/outstanding 제한으로 throughput을 인위적으로 안정화하는 수정.
- 측정 구간 밖에서 보내야 할 메시지를 active 집계에 포함하는 수정.
- 특정 언어에만 유리하도록 pattern, transport, size 기본값을 바꾸는 수정.
- 성능 수치 달성만을 위해 새 public API, native API, raw handle API, zero-copy
  전용 API를 만들고 perf만 그 API를 사용하게 하는 수정.

perf 버그가 아닌데 perf를 바꾸고 싶어지는 상황은 대부분 라이브러리 문제나
측정 환경 문제다. 이 경우 perf를 고치지 말고 라이브러리 또는 환경을 고친다.

### 7.1 binding public API 수정 제한

binding public API(공개 헤더, 공개 메서드, 공개 인터페이스)는 아래 경우에만 수정할 수 있다.

- **C API와 동작 불일치**: 동일 패턴·트랜스포트에서 C API 계약을 따르지 않는 구현.
- **다른 언어 binding과 동작 불일치**: 같은 API 계약이 다른 언어 binding에서는 올바르게
  구현됐지만 해당 언어에서만 틀리게 구현된 경우.
- **정책 계약과 불일치**: `doc/spec` 또는 `doc/perf` 정책이 명시하는 계약과 다르게
  구현된 경우.

아래 경우는 수정할 수 없다.

- 성능 수치를 올리기 위해 API 시그니처, 반환 타입, 소유권 계약, 예외 정책을 바꾸는 것.
- perf에서만 이점이 있는 새 오버로드, 힌트 파라미터, zero-copy variant를 추가하는 것.
- 내부 구현 최적화를 위해 public API 의미를 암묵적으로 변경하는 것.

public API 수정이 필요하다고 판단되면 즉시 작업을 멈추고 수정 이유, 불일치 근거,
변경 전후 API 시그니처를 보고한 뒤 사람의 승인을 받는다.

## 8. 버그 처리 규칙

perf 실행 중 실패가 나오면 먼저 실패를 정상 신호로 취급한다.

| 상황 | 처리 |
|------|------|
| perf 자체 측정 버그 | perf를 수정할 수 있다. 단, RESULT 의미와 정책 계약을 바꾸지 않고 smoke로 검증한다 |
| binding 라이브러리 버그 | perf에서 우회하지 않는다. 해당 binding 회귀 테스트를 추가하고 라이브러리를 수정한다 |
| core 라이브러리 버그 | perf에서 우회하지 않는다. core 회귀 테스트를 추가하고 core를 수정한 뒤 perf를 다시 실행한다 |
| 환경 문제 | fd limit, 포트 충돌, stale runtime 같은 원인을 수정하고 같은 명령을 다시 실행한다 |
| 정책 미지원 조합 | 정책에 정의된 조합인지 확인한 뒤, 정의되지 않은 조합만 unsupported로 둔다 |

라이브러리 버그를 고칠 때는 아래 순서를 지킨다.

1. 실패 조합과 로그를 `doc/bug/perf/` 또는 관련 bug 문서에 기록한다.
2. 재현 가능한 회귀 테스트를 추가한다.
3. 회귀 테스트가 실패하는 것을 확인한다.
4. core 또는 binding 라이브러리를 수정한다.
5. 회귀 테스트가 통과하는 것을 확인한다.
6. perf smoke와 목표 조합 측정을 다시 실행한다.

## 9. 언어별 작업 기준

### 9.1 C++

- 목표: C 기준 90% 이상.
- 우선 확인 지점:
  - C++ wrapper가 불필요한 heap allocation이나 문자열 변환을 hot path에 넣는지 확인한다.
  - RAII wrapper가 소켓 ownership은 지키면서 send/recv 경로에 과한 동기화를
    추가하지 않는지 확인한다.
  - C API와 동일한 recv drain, poller, metric header 처리 의미를 유지한다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

### 9.2 .NET

- 목표: C 기준 85% 이상.
- 우선 확인 지점:
  - P/Invoke boundary에서 per-message allocation, array pinning, marshal 비용이
    반복되는지 확인한다.
  - buffer 재사용, span 기반 API, handle ownership이 public API 의미를 해치지
    않는 범위에서 적용되는지 확인한다.
  - ReadyToRun, tiered compilation 옵션이 정책 권장값과 맞는지 확인한다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

### 9.3 Java

- 목표: C 기준 80% 이상.
- 우선 확인 지점:
  - JNI boundary에서 byte array copy가 반복되는지 확인한다.
  - direct buffer, native handle lifecycle, exception 변환이 hot path에 들어가는지
    확인한다.
  - `-server`, `-XX:TieredStopAtLevel=4` 등 perf 실행 옵션이 정책 권장값과 맞는지
    확인한다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

### 9.4 Node

- 목표: C 기준 70% 이상.
- 우선 확인 지점:
  - N-API boundary에서 Buffer copy와 JS object 생성이 per-message로 반복되는지
    확인한다.
  - event loop 연동이 recv drain을 충분히 진행하는지 확인한다.
  - TSFN 같은 direct callback 동기화 비용을 perf 기본 surface에 끌어들이지
    않았는지 확인한다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

### 9.5 Python

- 목표: C 기준 70% 이상.
- 우선 확인 지점:
  - C extension boundary에서 bytes 생성과 GIL 잡는 구간이 hot path를 막는지
    확인한다.
  - reusable buffer나 memoryview를 public API 의미 안에서 쓸 수 있는지 확인한다.
  - recv drain을 Python 루프가 불필요하게 잘게 쪼개지 않는지 확인한다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

### 9.6 Go

- 목표: C 기준 80% 이상.
- 우선 확인 지점:
  - cgo 호출 횟수와 per-message allocation을 확인한다.
  - goroutine scheduling과 poller 대기 구조가 active 구간을 깎아 먹지 않는지
    확인한다.
  - native handle finalizer가 측정 경로에 영향을 주지 않도록 ownership을
    명확히 둔다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

### 9.7 Rust

- 목표: C 기준 90% 이상.
- 우선 확인 지점:
  - safe wrapper가 불필요한 copy, allocation, dynamic dispatch를 hot path에
    넣는지 확인한다.
  - ownership과 lifetime을 타입으로 지키되, send/recv 경로는 C API 의미에
    가깝게 유지한다.
  - panic/Result 변환과 error mapping이 active loop 안에서 반복되지 않는지
    확인한다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

## 10. 검증 게이트

변경 라운드마다 최소한 아래 게이트를 지난다.

1. 관련 회귀 테스트 통과.
2. 대상 언어 single smoke 통과.
3. 대상 언어 multi smoke 통과.
4. 수정한 라이브러리 범위가 core이면 `core/build` runtime 재빌드 후 C smoke 통과.
5. 목표 조합 재측정에서 개선 확인.
6. 정책에 정의된 조합을 같은 pattern, transport, message size 단위로 확인했을 때
   목표 비율 충족.

권장 smoke 명령은 아래와 같다.

```bash
bindings/<lang>/perf/run_benchmarks.sh --pattern <PATTERN> --msg-sizes <SIZE> --runs 1
bindings/<lang>/perf/run_benchmarks_multi.sh --pattern <PATTERN> --msg-sizes <SIZE> --runs 1
```

core를 바꾼 경우에는 C 기준도 함께 확인한다.

```bash
cmake --build core/build
bindings/c/perf/run_benchmarks.sh --pattern <PATTERN> --msg-sizes <SIZE> --runs 1
bindings/c/perf/run_benchmarks_multi.sh --pattern <PATTERN> --msg-sizes <SIZE> --runs 1
```

## 11. 완료 정의

언어 하나의 완료 조건은 아래 모두를 만족하는 것이다.

- 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size 단위로
  확인했을 때 모두 `complete`다.
- 정책에 정의된 조합을 `UNSUPPORTED`나 `SKIP`으로 숨기지 않았다.
- C와 같은 `suite / pattern / transport / size` 조합에서 목표 비율을 만족한다.
- throughput을 올리기 위해 latency triplet을 비정상적으로 악화시키지 않았다.
- 발견한 binding/core 버그는 회귀 테스트와 함께 수정했다.
- perf 자체를 수정했다면, 그 수정 이유가 측정 버그였고 정책 의미를 바꾸지 않았음을
  결과 로그에 남겼다.

전체 작업의 완료 조건은 C++ → .NET → Java → Node → Python → Go → Rust 순서로
위 조건을 모두 통과하고, 마지막에 조합별 C 기준과 대상 언어 결과 요약을 남기는
것이다.

## 12. 최종 요약 양식

전체 작업이 끝나면 아래 표를 채운다.

| 언어 | 64B | 256B | 1024B | 64KB | 128KB | 256KB | single 결과 | multi 결과 | 비고 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C++ | ≥75% | ≥85% | ≥88% | ≥90% | ≥90% | ≥90% | 미측정 | 미측정 | 미정 |
| .NET | ≥75% | ≥80% | ≥82% | ≥85% | ≥85% | ≥85% | 미측정 | 미측정 | 미정 |
| Java | ≥70% | ≥75% | ≥77% | ≥80% | ≥80% | ≥80% | 미측정 | 미측정 | 미정 |
| Node | ≥42% | ≥52% | ≥57% | ≥62% | ≥64% | ≥65% | 미측정 | 미측정 | 미정 |
| Python | ≥22% | ≥30% | ≥35% | ≥40% | ≥42% | ≥43% | 미측정 | 미측정 | 미정 |
| Go | ≥55% | ≥65% | ≥68% | ≥72% | ≥74% | ≥75% | 미측정 | 미측정 | 미정 |
| Rust | ≥88% | ≥90% | ≥92% | ≥92% | ≥92% | ≥92% | 미측정 | 미측정 | 미정 |

## 실행 기록

각 라운드 기록은 [bindings-improvement-logs/](bindings-improvement-logs/) 폴더의
언어별 로그 문서로 분리한다. 새 라운드는 해당 언어 로그 파일에 추가한다.

- [bindings-improvement-logs/README.md](bindings-improvement-logs/README.md) — 인덱스
- [bindings-improvement-logs/cpp.md](bindings-improvement-logs/cpp.md)
- [bindings-improvement-logs/dotnet.md](bindings-improvement-logs/dotnet.md)
- [bindings-improvement-logs/java.md](bindings-improvement-logs/java.md)
