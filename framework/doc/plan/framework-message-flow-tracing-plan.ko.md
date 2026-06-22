# Framework message flow tracing 계획

> 이 문서는 success-path 메시지 흐름 추적 기능의 계획 겸 런북이다. C++ 레퍼런스
> 구현은 완료되었고(아래 "현재 상태" 참고), 나머지 언어는 C++를 미러링해 정렬한 뒤
> 언어별 정식 spec/guide 문서(`guide/09-monitoring.ko.md` 등)에 반영한다.
>
> 관련: [[framework-message-dispatch-error-observer-plan.ko.md]](framework-message-dispatch-error-observer-plan.ko.md)
> 는 실패 관측을 다루고, 이 문서는 정상 흐름 관측을 다룬다. 둘은 같은 이벤트 어휘
> (`surface`/`kind`/`correlation_id`)와 같은 fan-out 패턴(clog + offload observer)을 공유한다.

## 목적

프레임워크 개발/운영 중 가장 잦은 디버깅이 "메시지가 도착했는가 / 핸들러로 갔는가 /
응답이 나갔는가"의 확인이다. 지금까지는 ad-hoc `printf`나 env-gated 코어 디버그 로그로
때웠다. 이 기능은 그 흐름을 프레임워크가 **표준 기능으로** 찍어준다.

핵심 가치는 **correlation id로 한 메시지의 생애주기를 한 줄씩 추적**하는 것이다.
`zlink flow:` 접두사 + `corr=<id>`로 grep하면 한 요청의 received→dispatched/replied가
시간순으로 보인다. 실패는 기존 에러 리포터가 같은 stream에 찍으므로, 정상/실패가 하나의
correlation-id 키 흐름으로 읽힌다.

이 기능은 dispatch **제어**가 아니라 **관측**이다. 모드가 off가 아니어도 프레임워크 기본
동작은 변하지 않고, observer 실패가 메시지 처리나 응답 전송을 깨면 안 된다.

## 설계

### 모드 (이미 공개 계약에 존재하던 dead config를 실제 소비)

`dispatch_diagnostics_options_t::message_flow` (`contracts/dispatch/execution.hpp`).
모드는 verbosity 오름차순이다.

| 모드 | 의미 |
|------|------|
| `off` | 아무것도 찍지 않음. 에러 기본 로그까지 침묵. (observer/callback은 명시 구독이므로 계속 발화) |
| `errors_only` | (기본값) 에러 + `dropped` 전이만 |
| `key_transitions` | + `received` / `dispatched` / `replied` (왔냐/처리됐냐/응답됐냐) |
| `verbose` | + `include_message_sizes`일 때 `size=` 부가 |
| `diagnostic` | + native poll/socket 진단 (후속) |

`sample_rate`는 정상 트래픽을 핫패스에서 thinning한다. `dropped`/에러는 진단상 중요하므로
샘플링을 우회해 항상 통과한다.

### 이벤트 + 옵저버 (공개 계약)

```cpp
enum class message_flow_phase_t { received, dispatched, replied, dropped };
struct message_flow_event_t {                 // surface/kind는 에러 이벤트와 동일 enum 재사용
    message_flow_phase_t phase;
    dispatch_error_surface_t surface;
    dispatch_message_kind_t message_kind;
    std::optional<std::string> packet_name, channel_name, topic, correlation_id,
                               source_rid, spot_rid, actor_id;
    std::optional<std::size_t> message_size;
};
class message_flow_observer_t { virtual void on_message_flow(const message_flow_event_t&)=0; };
// dispatch_options_t::set_message_flow_observer(observer | callback)
```

### 길목 (hook point)

성공 전이는 **에러 리포터가 이미 호출되는 바로 그 dispatch 길목**에 미러링한다.
에러는 기존 `dispatch_error_reporter_t`가 담당하므로, 트레이서는 received/dispatched/
replied(성공)만 추가한다. `dropped`는 에러 리포터가 이미 찍는 경로와 중복하지 않는다.

| surface | 파일 | received | 성공 |
|---------|------|----------|------|
| channel | `channels/channel_packet_dispatcher.cpp` | 헤더 디코드 직후 | request→`replied`, command→`dispatched` |
| route mesh | `channels/route_packet_dispatcher.cpp` | `dispatch()` switch 직전 | send→`dispatched`, request→`replied` (내부 패킷/핸들러 양쪽) |
| spot subscription | `spots/spot_runtime.cpp` `dispatch_subscription` | context 검증 직후 | 성공 시 `dispatched` |
| spot actor | `spots/spot_runtime.cpp` actor packet relay | — | 성공 시 `replied` |

구현체: `src/runtime/diagnostics/message_flow_tracer.hpp` (header-only),
`enum_name`은 `src/runtime/diagnostics/dispatch_diagnostics_names.hpp`로 분리해
에러 리포터와 공유. 옵저버 fan-out은 에러 리포터와 동일하게 전용 `offload_executor_t`로
핫패스에서 분리.

## 현재 상태

2026-06-22 기준.

| 항목 | 상태 |
|------|------|
| C++ 공개 계약(phase/event/observer/setter) | ✅ 완료 |
| C++ 트레이서 + enum_name 공유 헤더 | ✅ 완료 |
| C++ 에러 리포터 `off` 모드 게이팅 | ✅ 완료 (기본 errors_only라 기존 동작 보존) |
| C++ channel / route / spot subscription / spot actor 배선 | ✅ 완료 |
| C++ 단위 테스트 `test_cpp_framework_message_flow` | ✅ 그린 (모드 게이팅·샘플·size·off 침묵) |
| `.NET` / Java / Kotlin / Node parity | ⬜ 미착수 (C++ 미러링) |
| 언어별 `guide/09-monitoring` 문서 반영 | ⬜ 미착수 |

## 4언어 parity 런북

C++를 레퍼런스로 미러링한다. 각 언어는 이미 dispatch 에러 관측 인프라가 있으므로
(예: `.NET` `ZLinkMessageFlowLogger`/`ZLinkTelemetry`, Java `ZLinkDispatchErrorReporter`)
그 옆에 success-path 트레이서를 추가하는 형태가 된다.

1. **계약**: `message_flow_phase_t`(enum), `message_flow_event_t`(record/struct/interface),
   `message_flow_observer`/callback setter를 dispatch options에 추가. (Java는
   `ZLinkMessageFlowLogMode`/`ZLinkDiagnosticsOptions`가 이미 있으니 event+observer만 추가)
2. **트레이서**: 모드 게이팅(off<errors_only<key_transitions<verbose<diagnostic) + 샘플링 +
   기본 로그(언어 표준 로거) + observer offload. 로그 라인은 `zlink flow: phase=… surface=…
   kind=… packet=… channel=… topic=… corr=… [size=]` 포맷을 4언어 동일 토큰으로.
3. **에러 리포터 게이팅**: `off`일 때 기본 에러 로그 침묵, observer는 유지.
4. **배선**: 각 언어의 channel/route/spot dispatch 길목에 received + dispatched/replied.
5. **테스트**: 모드 게이팅·샘플·`off` 침묵·correlation id 출력 회귀.

## 회귀 테스트 매트릭스 (MFLOW)

| ID | 검증 |
|----|------|
| MFLOW-001 | `off` → 모든 성공 전이 + 에러 기본 로그 침묵 |
| MFLOW-002 | `errors_only` → `dropped`/에러만, `received` 없음 |
| MFLOW-003 | `key_transitions` → received/dispatched/replied 출력, `corr=` 포함 |
| MFLOW-004 | `verbose` + `include_message_sizes` → `size=` 출력, opt-out 시 미출력 |
| MFLOW-005 | `sample_rate<1` → 정상 전이 thinning, `dropped`/에러는 항상 통과 |
| MFLOW-006 | observer 등록 시 모드 무관 발화, observer 예외가 dispatch를 깨지 않음 |
| MFLOW-007 | channel/route/spot 각 surface에서 한 요청이 received→replied로 이어짐 |
