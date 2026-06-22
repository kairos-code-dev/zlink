# Framework message flow tracing 계획

> 이 문서는 success-path 메시지 흐름 추적 기능의 계획 겸 런북이다. C++ 레퍼런스
> 구현은 완료되었고(아래 "현재 상태" 참고), 나머지 언어는 C++를 미러링해 정렬한 뒤
> 언어별 정식 spec/guide 문서(`guide/09-monitoring.ko.md` 등)에 반영한다.
>
> 관련: [[framework-message-dispatch-error-observer-plan.ko.md]](framework-message-dispatch-error-observer-plan.ko.md)
> 는 실패 관측을 다루고, 이 문서는 정상 흐름 관측을 다룬다. 둘은 같은 이벤트 어휘
> (`surface`/`kind`/`correlation_id`)와 같은 fan-out 패턴(표준 로거 라우팅 + offload observer)을 공유한다.

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

| 모드 | 의미 (인바운드+아웃바운드 전 phase에 동일 적용) |
|------|------|
| `off` | 아무것도 찍지 않음. 에러 기본 로그까지 침묵. (observer/callback은 명시 구독이므로 계속 발화) |
| `errors_only` | (기본값) 에러 + `dropped` 전이만 |
| `key_transitions` | + `received` / `dispatched` / `replied` / `sent` / `reply_received` (왔냐/처리됐냐/응답됐냐/보냈냐/응답받았냐) |
| `verbose` | + `include_message_sizes`일 때 `size=` 부가 |
| `diagnostic` | + native poll/socket 진단 (후속) |

게이팅 규칙(모든 언어 동일): `dropped`는 `errors_only` 이상, 나머지 성공 전이
(`received`/`dispatched`/`replied`/`sent`/`reply_received`)는 `key_transitions` 이상에서 발화.

`sample_rate`는 정상 트래픽을 핫패스에서 thinning한다. `dropped`/에러는 진단상 중요하므로
샘플링을 우회해 항상 통과한다. (C++ 레퍼런스는 프로세스 전역 atomic 카운터로 1/N 결정론적 샘플링.
언어별로는 자국 표준 방식 가능하나 "dropped/에러는 항상 통과" 규칙은 유지.)

### 이벤트 + 옵저버 (공개 계약)

```cpp
enum class message_flow_phase_t {
    received,        // 인바운드: 잘 형성된 메시지가 dispatch surface에 도착
    dispatched,      // 인바운드: fire-and-forget(send/publish) 핸들러로 전달
    replied,         // 인바운드: request 처리 완료, 응답 생성
    dropped,         // 의도적 폐기(핸들러 없음/디코드 실패 등) — 에러 리포터가 담당
    sent,            // 아웃바운드: 다른 channel/spot/node로 메시지 전송
    reply_received   // 아웃바운드: 보낸 request의 응답 수신
};
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

phase 의미: `received`/`dispatched`/`replied`는 **인바운드**(이 노드가 받는 쪽),
`sent`/`reply_received`는 **아웃바운드**(이 노드가 보내는 쪽). 한 request는 보내는 노드에서
`sent`→`reply_received`, 받는 노드에서 `received`→`replied`로 찍혀, 같은 `correlation_id`로
양 노드 로그가 이어진다.

### 성능 (off일 때 제로코스트) · 런타임 토글

디버깅 기능이므로 **꺼져 있을 때 운영 성능에 영향이 없어야** 하고, **운영 중 임시로 켜고 끌 수**
있어야 한다. 두 가지를 함께 만족시키는 설계:

- **참조 기반 트레이서**: 트레이서는 dispatch options를 *복사하지 않고* 빌려쓴다(포인터/레퍼런스).
  per-dispatch 옵션 복사(shared_ptr atomic·문자열 복사)를 제거.
- **지연 평가(lazy)**: 호출부는 이벤트(문자열 필드)를 **게이트 통과 후에만** 생성한다.
  C++은 `tracer.trace(phase, [&]{ return event{...}; })` 형태로, off면 람다가 호출되지 않아
  문자열 힙 할당이 0. (헬퍼는 인자 빌드도 게이트 뒤로 미룬다.)
- **게이트 = relaxed atomic load 1회 + 비교**: 모드를 공유 atomic으로 읽으므로 off 경로 비용은
  사실상 분기 하나.
- **런타임 토글(공유 atomic 모드)**: `dispatch_diagnostics_options_t::live_mode`
  (`shared_ptr<atomic<message_flow_log_mode_t>>`)가 있으면 정적 `message_flow`를 override하고
  매 dispatch마다 live로 읽는다. 호스트가 apply 때 설치하며, dispatch options를 복사해도 같은
  atomic을 공유하므로 모든 surface가 즉시 반영. 운영 API: **`app_t::set_message_flow_mode(mode)`**
  (스레드 안전, 재시작 불필요). `message_flow`는 설정 기본값(seed).

> 언어별: dispatch options를 surface로 복사하는 구조라면 정적 모드만으로는 런타임 토글이 안 된다.
> 공유 atomic(또는 동등한 라이브 셀)을 옵션에 실어 모든 surface가 같은 값을 읽게 하고, 그걸 바꾸는
> app API를 제공할 것. 호출부는 반드시 게이트 뒤에서 이벤트를 생성(lazy)해 off 비용을 0으로.

### 출력 라우팅 (로거 우선, 폴백은 표준 에러스트림)

트레이서/에러리포터는 **프레임워크 표준 로거를 통해** 출력한다. 로거에 넘길 때는
카테고리(`zlink.framework.dispatch`) + 구조화 필드로, 폴백 텍스트는 `zlink flow: …` /
`zlink framework dispatch error: …` 포맷을 쓴다.

**통합/분리 선택 (사용자가 고른다)** — `dispatch_diagnostics_options_t::log_file`:
- **분리**: `log_file` 지정 → 트레이싱/에러 전용 파일 로거로 보낸다. 애플리케이션 로그
  (`app.logging()`)와 절대 섞이지 않는다.
- **통합**: `log_file` 미지정 + 앱 로거 sink 있음 → 공유 앱 로거가 앱 로그와 트레이싱을
  함께 담는다(같은 파일/콘솔).
- **폴백**: 둘 다 없음 → 표준 에러스트림(C++=`std::clog`). (고볼륨 트래픽이 로거 in-memory
  레코드 버퍼에 무한 적재되는 것 방지)

이렇게 사용자는 "한 곳에 모아 보기"와 "트레이싱만 따로 파일로" 중 선택할 수 있다.
(C++ 예: 분리 `o.configure_dispatch().diagnostics.log_file = "flow.log"`, 통합
`app.logging().use_file("app.log")` 만 설정. .NET/Java/Node도 동일 의미의 옵션 제공.)

> 언어별 주의: 각 언어의 로깅 파사드가 다르다. dispatch options에 "로거 핸들/sink"를 실어
> 트레이서가 닿게 하는 plumbing이 필요하다(C++은 `dispatch_options_t.diagnostics_logger`).
> stream 같은 별도 서브시스템은 dispatch options가 자동 전파되지 않으므로 별도 plumbing 필요
> (C++은 `stream_runtime_state`/`actor_gateway_state`에 dispatch를 명시 주입).

### 관측 백엔드/콜렉터 연동 — 레이어 경계 (중요 원칙)

로그를 외부 콜렉터(Loki/ELK/Fluentd/Datadog 등)나 분산 추적(OpenTelemetry)으로 보내는 것은
**가능해야 하지만, 그 연동은 애플리케이션 레이어의 선택**이다. 프레임워크는 특정 백엔드/SDK
(OTel 등)에 **의존하지 않는다.** 대신 연동에 필요한 최소 접점만 제공한다.

**프레임워크가 제공 (백엔드 무관, 의존성 0):**
1. `correlation_id` — 메시지 식별자(텍스트 grep + 프로그램 매핑 둘 다 가능).
2. **구조화 출력(structured fields)** — 로그를 텍스트 한 줄로만 내지 말고 `log_record`의
   key/value 필드(phase/surface/kind/packet/channel/topic/corr/src/spot/actor/size)로 채운다.
   콜렉터가 정규식 파싱 없이 ingest 가능. (이게 codex가 말한 "structured JSON" = **로그 포맷**
   개선이며, 프레임워크 레벨에서 OTel 없이 가능.)
3. **관측 훅** — `set_message_flow_observer` / `set_message_dispatch_error_observer`.
   구조화 이벤트(`message_flow_event_t`)를 그대로 콜백으로 받는다. 로그 모드(통합/분리)와 무관.

**애플리케이션이 선택 (원할 때만, 프레임워크 밖):**
- **로깅 백엔드(provider) 끼우기 (.NET ILoggerProvider / SLF4J 바인딩 방식)**:
  `app.logging().use_provider("name", record→backend)` (또는 `use_callback_sink`)로 앱의 로깅
  백엔드를 등록. 콘솔/파일을 안 켜면(둘 다 기본 off) 프레임워크는 자체 출력을 강제하지 않고 모든
  레코드가 앱 백엔드로만 간다. **프레임워크는 내장 네트워크/OTLP sink를 제공하지 않는다** — 앱이 브리지.
- **레코드 캡처 제어**: 내장 in-memory 버퍼(`captured_records`)는 테스트/점검용이며 기본 상한
  (4096) ring. 프로덕션에서 provider만 쓸 땐 `app.logging().disable_record_capture()`로 끄거나
  `set_max_captured_records(n)`으로 조정(무한 적재 방지).
- **OpenTelemetry / W3C `traceparent` / span 모델**: 앱이 자기 OTel SDK에서 observer 콜백을
  받아 `correlation_id`를 span/traceparent로 매핑한다. **프레임워크는 OTel을 모른다.**
  (traceparent 전파·부모/자식 span은 포맷이 아니라 추적 체계이므로 앱 레이어 책임.)

> 요약: 프레임워크 = `correlation_id` + 구조화 필드 + observer 훅까지. 그 위의 OTel/span/콜렉터
> 어댑터는 앱이 끼운다. 이 경계를 깨고 프레임워크에 OTel을 하드 의존시키지 말 것.

### 길목 (hook point) — 논리 지점, 전 언어 공통

성공 전이는 **에러 리포터가 이미 호출되는 바로 그 dispatch 길목**에 미러링한다(인바운드).
아웃바운드는 메시지를 인코딩해 전송하는 클라이언트 길목에 추가한다. `dropped`/에러는 기존
에러 리포터가 담당하므로 트레이서는 성공 전이만 찍어 중복을 피한다.

| surface | 방향 | 논리 길목 | phase | C++ 레퍼런스 파일 |
|---------|------|-----------|-------|-------------------|
| channel | 인바운드 | server 패킷 dispatch | 헤더 디코드 후 `received`; request→`replied`, command→`dispatched` | `channels/channel_packet_dispatcher.cpp` |
| channel | 아웃바운드 | client request/send/publish 제출 | request→`sent`+`reply_received`, send/publish→`sent` | `channels/channel_runtime.cpp` (message_bus submit_*) |
| route mesh | 인바운드 | route 패킷 dispatch | `received`; send→`dispatched`, request→`replied` | `channels/route_packet_dispatcher.cpp` |
| route mesh | 아웃바운드 | route client 제출 | `sent`; request-reply는 `reply_received` | `channels/channel_runtime.cpp` (route_client submit_*) |
| spot subscription | 인바운드 | subscription dispatch | `received`; 성공 `dispatched` | `spots/spot_runtime.cpp` `dispatch_subscription` |
| spot subscription | 아웃바운드 | spot publish | `sent` | `spots/spot_runtime.cpp` `publish_erased` |
| spot actor | 인바운드 | actor packet relay | `received`; 성공 `replied` | `spots/spot_runtime.cpp` actor packet relay |
| spot actor | 아웃바운드 | actor join_spot | `sent`+`reply_received` | `actors/actor_gateway_runtime.cpp` `join_spot_erased` |
| stream session | 인바운드 | stream packet dispatch | `received` (client→session) | `streams/stream_runtime.cpp` `dispatch_packet` |

> `source_rid` 필드 의미가 방향에 따라 다르다: 인바운드=송신자 rid, 아웃바운드=목적지 노드 rid.
> 로그엔 둘 다 `src=`로 나오므로 혼동 주의(원하면 언어별로 `dst=` 분리 가능 — 단 4언어 합의 필요).

구현체(C++): `src/runtime/diagnostics/message_flow_tracer.hpp` (header-only),
`enum_name`은 `src/runtime/diagnostics/dispatch_diagnostics_names.hpp`로 분리해 에러 리포터와 공유.
옵저버 fan-out은 에러 리포터와 동일하게 전용 `offload_executor_t`로 핫패스에서 분리.

### 선행 의존성

`dropped`/에러 로그는 [[framework-message-dispatch-error-observer-plan]](framework-message-dispatch-error-observer-plan.ko.md)
의 dispatch 에러 리포터에 의존한다. 그 리포터가 없는 언어는 **먼저(또는 함께)** 구현해야
성공/실패가 한 stream으로 이어진다. 이 문서의 트레이서는 그 위에 성공 전이만 얹는다.

## 현재 상태

2026-06-22 기준.

| 항목 | 상태 |
|------|------|
| C++ 공개 계약(phase/event/observer/setter) | ✅ 완료 |
| C++ 트레이서 + enum_name 공유 헤더 | ✅ 완료 |
| C++ 에러 리포터 `off` 모드 게이팅 | ✅ 완료 (기본 errors_only라 기존 동작 보존) |
| C++ 에러 기본 로그에 `corr`/`topic`/`src`/`actor` 추가(정상·실패 줄 포맷 통일) | ✅ 완료 |
| C++ channel / route / spot subscription / spot actor 배선 | ✅ 완료 |
| C++ 단위 테스트 `test_cpp_framework_message_flow` | ✅ 그린 (모드 게이팅·샘플·size·off 침묵) |
| C++ 트레이서/에러리포터를 프레임워크 로거(`logger_t`)로 라우팅 | ✅ 완료 (sink 설정 시; 미설정 시 std::clog 폴백). `app.logging().use_file(...)`로 파일 캡처 |
| C++ `logging_builder_t::use_file/use_rotating_file` 부모 디렉토리 자동 생성 | ✅ 완료 (이전엔 디렉토리 없으면 조용히 실패) |
| Bingo 샘플(api/play/session)에 파일 로깅 배선 + `.gitignore` | ✅ 완료 (`samples/Bingo/logs/`, run_sample.sh가 `BINGO_LOG_DIR` export) |
| C++ 아웃바운드 트레이싱(핸들러→다른 channel: `sent`/`reply_received`) | ✅ 완료 (message_bus submit_request/send/publish). phase에 `sent`/`reply_received` 추가 |
| C++ stream 인바운드(client→session `received`) | ✅ 완료 (stream_runtime dispatch_packet). dispatch 옵션을 stream_runtime_state로 plumbing |
| C++ spot actor 인바운드 `received` 추가 | ✅ 완료 (기존 `replied`에 더해) |
| C++ 아웃바운드 route client (handler→다른 노드/actor relay: `sent`/`reply_received`) | ✅ 완료 (submit_send/request/request_reply). Bingo로 `route_mesh_channel sent` 검증 |
| C++ handler→spot join_spot (`sent`/`reply_received`) | ✅ 완료 (actor gateway에 dispatch plumbing + join_spot_erased). cross-node join은 route_mesh로 표시 |
| C++ 핸들러→spot publish 트레이싱 (`sent`) | ✅ 완료 (spot_context publish_erased, state->node->dispatch) |
| C++ 트레이싱 로그 통합/분리 선택 (`diagnostics.log_file`) | ✅ 완료 (지정=전용 파일·앱로그와 분리, 미지정=앱 로거 통합). Bingo는 분리 모드로 시연 |
| C++ off 제로코스트 (참조 기반 트레이서 + lazy 이벤트 + atomic 게이트) | ✅ 완료 (옵션 복사·문자열 할당 제거, 전 호출부 lazy 전환) |
| C++ 런타임 토글 (`app_t::set_message_flow_mode`, 공유 atomic `live_mode`) | ✅ 완료 (재시작 없이 on/off, 모든 surface live 반영) |
| C++ stream correlation_id 1급 헤더 필드(와이어) — connector+framework | ✅ 완료 (flag 0x08, 메타 뒤 u8 len+bytes, 클라 생성·서버 echo). Bingo로 stream `received corr=` 검증 |
| stream correlation_id 전체 적용(타 언어 바인딩/엔진) | ⬜ 추후 검토 |
| 구조화 필드 출력(`log_record.fields` key/value) + `node=` 식별자 for 콜렉터/집계 | ✅ 완료 (logger_t::log_with_fields, tracer/error reporter 구조화, `diagnostics.node_id`). Bingo 실측 |
| observer를 통한 콜렉터/OTel 어댑터(앱 레이어) | ⬜ 앱 책임 — 프레임워크는 훅만 제공 |
| `.NET` / Java / Kotlin / Node parity | ⬜ 미착수 (C++ 미러링) |
| 언어별 `guide/09-monitoring` 문서 반영 | ⬜ 미착수 |

## 4언어 parity 런북

C++를 레퍼런스로 미러링한다. 각 언어는 이미 dispatch 에러 관측 인프라가 있으므로
(예: `.NET` `ZLinkMessageFlowLogger`/`ZLinkTelemetry`, Java `ZLinkDispatchErrorReporter`)
그 옆에 success-path 트레이서를 추가하는 형태가 된다.

1. **계약**: `message_flow_phase_t`(6값: received/dispatched/replied/dropped/**sent/reply_received**),
   `message_flow_event_t`(record/struct/interface), `message_flow_observer`/callback setter를
   dispatch options에 추가. (Java는 `ZLinkMessageFlowLogMode`/`ZLinkDiagnosticsOptions`가 이미
   있으니 event+observer+sent/reply_received만 추가)
2. **트레이서**: 모드 게이팅(off<errors_only<key_transitions<verbose<diagnostic) + 샘플링
   (dropped/에러는 항상 통과) + **언어 표준 로거 라우팅 + 통합/분리 선택(`diagnostics.log_file`)**
   + observer offload(핫패스 분리). **성능: 참조 기반(옵션 복사 금지) + lazy(게이트 통과 후에만
   이벤트 생성) + 게이트는 공유 atomic load 1회** → off 제로코스트. `log_file` 지정=전용 파일,
   미지정=앱 로거 통합, 둘 다 없음=표준 에러스트림 폴백. 폴백 로그 라인은 `zlink flow: phase=…
   surface=… kind=… packet=… channel=… topic=… corr=… src=… spot=… actor=… [size=]` 동일 토큰.
2b. **런타임 토글**: 옵션에 공유 atomic 모드(`live_mode`)를 실어 모든 surface가 live로 읽게 하고,
   `app.set_message_flow_mode(mode)` 류의 API로 운영 중 재시작 없이 켜고 끈다.
3. **에러 리포터 게이팅 + 포맷 통일**: `off`일 때 기본 에러 로그 침묵(observer는 유지). 그리고
   **에러 기본 로그에도 `corr`/`topic`/`src`/`actor`를 출력**해 정상 줄과 토큰을 맞춘다 →
   성공/실패를 `grep corr=<id>` 한 번에. 출력은 값이 있는 필드만.
4. **배선(인바운드)**: channel/route/spot subscription/spot actor/**stream** dispatch 길목에
   `received` + 성공(`dispatched`/`replied`). (위 길목 표의 "인바운드" 행 전부)
5. **배선(아웃바운드)**: channel client(request `sent`+`reply_received`, send/publish `sent`),
   route client(`sent`, request-reply `reply_received`), spot publish(`sent`),
   actor join_spot(`sent`+`reply_received`). (길목 표의 "아웃바운드" 행 전부)
   — dispatch options가 자동 전파 안 되는 서브시스템(stream/actor gateway)은 dispatch 주입 plumbing 필요.
6. **stream correlation_id**: 해당 언어의 stream 클라이언트가 C++ 세션과 통신하면 아래 "스트림
   correlation_id 헤더 필드"의 **와이어 레이아웃을 동일하게** 구현해야 상호운용된다(클라 생성·서버 echo).
7. **테스트**: 모드 게이팅·샘플·`off` 침묵·correlation id 출력(정상·실패 양쪽)·인바운드/아웃바운드
   전 surface·stream corr round-trip 회귀(MFLOW-001~011).
8. **codex 반복 리뷰 (언어별 필수, 이슈 0까지 반복)**: 그 언어 구현이 끝날 때마다 codex에 리뷰를
   요청해 **누락·정확성·성능·레이어 경계 위반**을 점검한다. 리뷰가 지적을 내면 → 수정 → **다시 리뷰**를
   돌려 **새 지적이 없을 때까지 반복**한다. 그 뒤에야 해당 언어를 "완료"로 표기한다.
   - 리뷰에 줄 것: 이 문서 + 그 언어 구현 파일 목록 + C++ 레퍼런스 파일(대조용).
   - 리뷰 체크포인트(매 회차): (a) 길목 표의 인바운드/아웃바운드 행이 **전부** 배선됐는가,
     (b) off일 때 **제로코스트**(옵션 복사·이벤트 생성 없음)인가, (c) 런타임 토글이 모든 surface에
     **live**로 반영되는가, (d) 구조화 필드+`node=`로 콜렉터 ingest 가능한가, (e) 빌더 체인 전용
     (직접 필드 대입 불가)인가, (f) **OTel/span을 프레임워크에 하드 의존시키지 않았는가**(경계 위반),
     (g) stream corr 와이어가 C++와 **바이트 동일**한가, (h) 수명/동시성 안전(참조 dangling, atomic 순서).
   - 종료 조건: codex가 high/medium 지적을 더 내지 않음. 남기기로 한 보류 항목(메타데이터 와이어,
     source_rid/target_rid)은 "의도된 보류"로 명시하고 리뷰 루프에서 제외한다.

### 적용 시 주의 (다른 언어에서 자주 막히는 지점)

- **per-process corr 카운터**: correlation_id는 envelope/header를 만드는 쪽이 부여하는 프로세스
  전역 단조값이다. 노드마다 카운터가 독립이라 숫자만 같고 다른 메시지일 수 있다. 노드 간
  연결은 corr이 **전파**될 때만 성립(channel request↔reply, stream request↔reply echo,
  route 전파). 전역 유일 ID(UUID)로 가정하지 말 것.
- **stream corr는 송신측이 생성, 서버는 echo만**. 서버가 ingress에서 생성하지 않는다(결정사항).
  그래서 클라이언트가 안 넣으면 그 stream 메시지엔 corr이 없다.
- **로거 plumbing**: dispatch options를 채널/spot에는 자동 전파해도 stream/actor gateway 등
  별도 서브시스템엔 명시 주입이 필요하다. 누락하면 그 surface만 로그가 안 나온다(조용한 실패).
- **파일 sink 디렉토리**: use_file류가 부모 디렉토리를 자동 생성하는지 확인(C++은 없어서 추가함).
- **message_size**: verbose에서만, 값이 있을 때만. C++ 레퍼런스는 일부 길목에서 미채움(점진 보강).

## 스트림 correlation_id 헤더 필드 (와이어 프로토콜 변경)

채널 envelope는 `correlation_id`를 1급 필드로 갖는데(`envelope_header_t::correlation_id`),
스트림은 `metadata("correlation_id")` 관례에 머물러 두 전송계층이 비대칭이었다. 이를 해소하기
위해 **스트림 헤더에도 correlation_id를 1급 필드로** 올린다(채널과 일관).

**결정**
- correlation_id는 스트림 패킷 헤더의 1급 필드다(metadata 관례 폐기).
- **보내는 클라이언트가 생성**하고, **서버(session)는 echo만** 한다(reply에 요청 corr 복사).
- 자동 부여 시퀀스는 채널과 동일하게 client codec의 프로세스 전역 단조 카운터(hex).
- corr은 **트레이싱 전용이 아니라 프로토콜 필드**라, 클라이언트가 **항상** 생성한다(트레이싱 off여도).
  이유: 서버가 자기/클라 토글 상태와 무관하게 incoming stream을 corr로 추적할 수 있어야 하기 때문.
  비용은 미미(카운터 hex는 보통 SSO=힙 할당 없음 + 와이어 ~1–17B). 진짜로 깎아야 하는 앱을 위해
  connector opt-out(기본 on)을 후속 옵션으로 둘 수 있음. → "off 제로코스트"는 **dispatch 핫패스의
  트레이서**에 대한 보장이지, 이 프로토콜 필드까지 0으로 만든다는 의미가 아니다.

**와이어 레이아웃** (connector `header_codec`와 framework `stream_runtime` 양쪽 동일해야 함)
- `header_flags_t`에 `has_correlation_id = 0x08` 추가.
- 바이트 배치: `kind, codec, flags, [request_seq], name(u8 len+bytes), [metadata], [correlation_id(u8 len+bytes)]`
  — correlation_id는 **메타데이터 블록 뒤 마지막**에, flag가 set일 때만 `u8 길이 + 바이트`.
- control 패킷은 flag 불가(기존 규칙 유지), send/request/response/error는 허용.

**구현 범위 (이번 회차: C++만)**
- connector core(C++): `stream_header_t` 필드 + `header_codec` encode/decode + flag + **C++ client send 경로 자동 생성**.
- framework(C++): `stream_header_t` 1급 필드화 + encode/decode 동기화 + `reply_packet` echo + dispatch_packet `received` 트레이스가 1급 필드 사용.
- **전체 적용(다른 언어 바인딩/엔진 godot·axmol·unreal, JS/Java/dotnet 스트림)은 추후 검토.**
  엔진들은 connector core 코덱을 공유하므로 decode는 자동 호환(flag 미set이면 필드 없음 = 하위호환).

**회귀**: MFLOW-009 (아래) — C++ client→session에서 헤더 correlation_id가 부여·전파되고 인바운드 `received`에 `corr=`가 찍힘.

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
| MFLOW-008 | 실패 줄에도 `corr=`가 출력되어 `grep corr=<id>`로 성공·실패 메시지가 모두 잡힘 |
| MFLOW-009 | (C++) stream 헤더 correlation_id 1급 필드 round-trip(encode/decode) + client 자동부여 + server reply echo + 인바운드 `received`에 `corr=` |
| MFLOW-010 | `diagnostics.log_file` 지정 시 트레이싱/에러가 전용 파일로만 가고 앱 로거에 안 섞임; 미지정 시 앱 로거와 통합 |
| MFLOW-011 | `live_mode`가 정적 `message_flow`를 override하고 런타임 변경이 즉시 반영(off→on→off). off일 때 이벤트 미생성(제로코스트) |

## codex 리뷰 후속 (2026-06-22)

C++ 구현/문서에 대한 codex 리뷰 결과와 처리 상태.

**✅ 반영 완료 (C++)**
- 🟠→✅ **스트림 에러 응답 corr echo**: `write_error_frame`가 요청 corr을 에러 헤더에 복사.
- 🟡→✅ **`set_message_flow_mode` 레이스 제거**: app 생성 시 공유 atomic을 1회 생성(재대입 없음).
- 🟠→✅ **spot 트레이싱 완전 lazy**: `report_spot_dispatch_trace`를 string_view 인자로 바꿔 게이트
  통과 후에만 std::string 생성.
- 🟠→✅ **connector/framework corr 생성 경량화**: `ostringstream` 제거, 정수→hex 직접.
- D2→✅ **구조화 필드 출력 + `node=`**: `logger_t::log_with_fields`, tracer/error reporter가
  key/value 필드로 출력, `diagnostics.node_id` 추가. (콜렉터/노드 집계 대응)

**⬜ 보류/후속**
- 🔴 **스트림 메타데이터 와이어 불일치**(기존 이슈): connector(`u16 길이+blob`) vs
  framework(`count+inline`). 메타데이터 없는 프레임만 호환. 메타데이터 사용/타 언어 미러링 전에
  한쪽으로 통일 필요(parity 회차에서). 이 문서 와이어 레이아웃도 metadata 인코딩 명시할 것.
- 🟠 **일부 surface corr=null**(설계상 한계): spot 구독/actor/publish/join_spot은
  spot_rid/actor_id로 키잉(해당 경로에 channel-style corr 없음). "모든 surface corr 키잉"은
  과장이므로 주장 완화 — corr 가능 경로(channel/route/stream)만 corr, spot/actor는 spot/actor id.
- `source_rid`/`target_rid` 분리: 타 언어 복사 전(parity 회차)에 적용 권고. 현재는 `src=` 한 필드에
  인바운드=송신자 / 아웃바운드=목적지를 담음(문서에 명시됨).
- 통합 테스트: 실제 call site·connector↔framework 인터op 회귀 추가(후속).

**더 나은 아이디어 (레이어 경계 반영)**
- 구조화 필드 출력(D2): **프레임워크 채택 완료** — 위 경계 절 참고.
- traceparent/span/OTel(D1/D3): **앱 레이어** — 프레임워크는 observer 훅만, OTel 의존 금지.
- 샘플링 의미 명세(한 corr 생애주기 일괄 샘플 여부), 에러 시 링버퍼 flight-recorder는 선택적 후속.

### codex 2차 리뷰 (델타 커밋 4989d64e9, 2026-06-22)

1차 수정 3건(스트림 에러 corr echo / corr 생성 경량화 / spot 완전 lazy) **모두 verified**.
1차 핵심설계(ref+lazy+atomic 게이트)도 통과. 신규 지적 2건:
- 🟠→✅ **lazy `trace()`가 `noexcept`인데 람다가 할당** → 예외 시 terminate. **수정 완료**:
  lazy trace의 `build_event()`+emit을 try/catch로 감쌈(실패 카운터 증가, 기존 log_default와 동일 정책).
- 🟠→문서화 **stream connector가 off여도 corr 항상 생성**: 위 "스트림 correlation_id" 결정대로
  **의도된 트레이드오프**(프로토콜 필드, SSO라 사실상 무할당). 필요 시 connector opt-out 후속.
