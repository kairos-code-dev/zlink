<!-- framework-adapter-nav:start -->
[스펙 목차](../README.ko.md) | [이전: 런타임 메트릭 계기 (Runtime Metrics Instruments)](51-runtime-metrics.ko.md) | [다음: 메시지 흐름 상관관계 (Flow Correlation)](53-flow-correlation.ko.md)
<!-- framework-adapter-nav:end -->


# 메시지 흐름 추적과 dispatch 관측

이 문서는 framework가 표준 기능으로 제공하는 **success-path 메시지 흐름 추적**의 언어 중립
공통 스펙이다. 공통 의미(로그 모드, 이벤트, 옵저버 계약, 성능 계약, 출력 라우팅, 길목,
스트림 correlation_id 와이어 포맷)는 이 문서가 소유하며, 언어별 문서는 여기서 정한 의미를
자기 언어의 관용구와 표면으로만 구체화한다. 네이밍은 [framework API](../05-framework-api.ko.md)와
[공개 계약 관리 §4](../00-public-contract-governance.ko.md#4-언어별-표현-원칙)의 언어별 표현 원칙을 따른다.

> 실패 관측(dispatch error reporter)은 같은 어휘(`surface`/`kind`/`correlation_id`)와 같은
> fan-out 패턴(표준 로거 라우팅 + offload observer)을 공유한다. 이 문서는 **정상 흐름**을,
> error reporter는 **실패 흐름**을 담당하며 둘은 하나의 `correlation_id` 키 stream으로 읽힌다.

## 1. 목적과 성격

framework 개발/운영 중 가장 잦은 디버깅은 "메시지가 도착했는가 / 핸들러로 갔는가 / 응답이
나갔는가"의 확인이다. 이 기능은 그 흐름을 ad-hoc `printf`나 env-gated 코어 디버그 로그가
아니라 **framework 표준 기능으로** 찍어 준다.

핵심 가치는 **correlation id로 한 메시지의 생애주기를 한 줄씩 추적**하는 것이다. 폴백 로그는
`zlink flow:` 접두사 + `corr=<id>`를 쓰므로, 한 요청의 received→dispatched/replied가 시간순으로
grep된다. 실패는 같은 stream에 error reporter가 같은 토큰(`corr=`)으로 찍으므로 정상/실패가
하나의 키 흐름으로 읽힌다.

이 기능은 dispatch **제어**가 아니라 **관측**이다. 모드가 off가 아니어도 framework 기본 동작은
변하지 않고, observer 실패가 메시지 처리나 응답 전송을 깨면 안 된다(관측 callback의 의미는
[비동기 실행 정책 §2](../04-async-execution-policy.ko.md)를 따른다).

## 2. 로그 모드

모드는 dispatch 진단 옵션의 한 필드(`message_flow`)다. verbosity 오름차순이며, 인바운드와
아웃바운드 전 phase에 동일하게 적용된다.

| 모드 | 의미 |
|------|------|
| `off` | **로그만 침묵한다.** 에러 기본 로그까지 찍지 않는다. **metric/counter와 observer 통지는 mode와 무관하게 계속 발생한다** — `off`는 로그 게이트일 뿐 관측 자체를 끄지 않는다 |
| `errors_only` | (기본값) 에러 + `dropped` 전이만 |
| `key_transitions` | + `received` / `dispatched` / `replied` / `sent` / `reply_received` |
| `verbose` | + `include_message_sizes`일 때 `size=` 부가 |
| `diagnostic` | + native poll/socket 진단 (후속 확장 여지) |

게이팅 규칙(전 언어 동일):

- `dropped`와 에러는 `errors_only` 이상에서 발화한다.
- 나머지 성공 전이(`received`/`dispatched`/`replied`/`sent`/`reply_received`)는
  `key_transitions` 이상에서 발화한다.

`sample_rate`는 정상 트래픽을 핫패스에서 thinning한다. **`dropped`와 에러는 진단상 중요하므로
샘플링을 우회해 항상 통과한다.** 샘플링 방식은 언어별 표준을 쓸 수 있으나(C++ 레퍼런스는 프로세스
전역 atomic 카운터로 1/N 결정론적 샘플링), "dropped/에러는 항상 통과" 규칙은 모든 언어가 지킨다.

## 3. 이벤트와 옵저버 계약

phase는 6값이다. `received`/`dispatched`/`replied`는 **인바운드**(이 노드가 받는 쪽),
`sent`/`reply_received`는 **아웃바운드**(이 노드가 보내는 쪽)다.

| phase | 방향 | 의미 |
|-------|------|------|
| `received` | 인바운드 | 잘 형성된 메시지가 dispatch surface에 도착 |
| `dispatched` | 인바운드 | fire-and-forget(send/publish) 핸들러로 전달 |
| `replied` | 인바운드 | request 처리 완료, 응답 생성 |
| `dropped` | 인바운드 | 의도적 폐기(핸들러 없음/디코드 실패 등) — error reporter가 담당 |
| `sent` | 아웃바운드 | 다른 channel/spot/node로 메시지 전송 |
| `reply_received` | 아웃바운드 | 보낸 request의 응답 수신 |

한 request는 보내는 노드에서 `sent`→`reply_received`, 받는 노드에서 `received`→`replied`로
찍혀, 같은 `correlation_id`로 양 노드 로그가 이어진다.

이벤트는 dispatch error 이벤트와 같은 `surface`/`kind` enum을 재사용하고, 아래 필드를 담는다
(값이 있는 필드만 채운다). C++ 레퍼런스 형상:

```cpp
enum class message_flow_phase_t {
    received, dispatched, replied, dropped, sent, reply_received
};
struct message_flow_event_t {                 // surface/kind는 error 이벤트와 동일 enum
    message_flow_phase_t phase;
    dispatch_error_surface_t surface;
    dispatch_message_kind_t message_kind;
    std::optional<std::string> packet_name, channel_name, topic, correlation_id,
                               source_rid, spot_rid, actor_id;
    std::optional<std::size_t> message_size;   // verbose에서만, 값이 있을 때만
    // 아래 두 필드는 flow-correlation 확장이 소유한다(additive, 값 있을 때만):
    std::optional<std::string>   flow_id;      // §flow-correlation §2·§3
    std::optional<flow_origin_t> flow_origin;  // inbound|timer|application|lifecycle, §flow-correlation §4.2
};
class message_flow_observer_t { virtual void on_message_flow(const message_flow_event_t&)=0; };
// dispatch options: set_message_flow_observer(observer | callback)
```

> `flow_id`·`flow_origin`은 [메시지 흐름 상관관계](53-flow-correlation.ko.md) 확장이 소유하는 additive
> 필드다. correlation_id가 channel/route/stream 경계에서만 1급인 한계(§7·§8)를, flow_id가 spot/actor
> 경계와 fleet 전역까지 관통해 보완한다. 두 필드는 하나의 optional pair라서 항상 함께 채우거나 함께
> 비운다. 이 확장을 쓰지 않으면 두 필드는 비어 있고 나머지 계약은 그대로다.

**옵저버 계약**(모든 언어 동일):

- 옵저버는 dispatch 결정을 바꾸지 못하는 관측 callback이다. 모드와 무관하게(즉 `off`여도)
  명시 구독했으면 발화한다.
- 이벤트는 snapshot이어야 한다(native frame / raw message / caller buffer를 들고 있지 않음).
- 옵저버가 예외를 던지거나 rejected future/promise를 반환해도 원래 dispatch 결과는 바뀌지 않는다.
- 옵저버 user code는 receive path에서 직접 실행하지 않고 전용 executor로 offload한다(핫패스 분리).
  bounded queue를 쓰면 overflow 시 새 event를 drop하고 overflow counter를 올린다.

**언어별 투영**: outcome enum, event record/struct/interface, observer/callback setter를 각 언어의
정식 이름으로 내려 적는다(예: `.NET` `IZLinkMessageFlowControl`, Java
`ZLinkMessageFlowOutcome`/`Event`/`Observer`). 단어 교체·생략 없이 케이싱만 변환한다.

## 4. 성능 계약 — off 제로코스트 · 런타임 토글

디버깅 기능이므로 **꺼져 있을 때 운영 성능에 영향이 없어야** 하고, **운영 중 임시로 켜고 끌 수**
있어야 한다. 두 가지를 함께 만족시키는 설계는 다음과 같다.

- **참조 기반 트레이서**: 트레이서는 dispatch 옵션을 *복사하지 않고* 빌려쓴다(포인터/레퍼런스).
  per-dispatch 옵션 복사(shared_ptr atomic·문자열 복사)를 제거한다.
- **지연 평가(lazy)**: 호출부는 이벤트(문자열 필드)를 **게이트 통과 후에만** 생성한다. C++은
  `tracer.trace(phase, [&]{ return event{...}; })` 형태로, off면 람다가 호출되지 않아 문자열 힙
  할당이 0이다. 헬퍼는 인자 빌드도 게이트 뒤로 미룬다.
- **게이트 = relaxed atomic load 1회 + 비교**: off 경로 비용은 사실상 분기 하나다.
- **런타임 토글(공유 atomic 모드)**: 옵션에 공유 atomic 라이브 셀(`live_mode`)을 실어 정적
  `message_flow`를 override하고 매 dispatch마다 live로 읽는다. 옵션을 복사해도 같은 atomic을
  공유하므로 모든 surface가 즉시 반영된다. 운영 API는 **`app.set_message_flow_mode(mode)`**
  류로 스레드 안전·재시작 불필요하며, `message_flow`는 설정 기본값(seed)이다.

> "off 제로코스트"는 **dispatch 핫패스의 트레이서**에 대한 보장이다. 옵션을 surface로 복사하는
> 구조라면 정적 모드만으로는 런타임 토글이 안 되므로, 공유 atomic(또는 동등한 라이브 셀)을 옵션에
> 실어 모든 surface가 같은 값을 읽게 하고 그걸 바꾸는 app API를 제공한다. 호출부는 반드시 게이트
> 뒤에서 이벤트를 생성(lazy)한다. (§9의 스트림 correlation_id는 프로토콜 필드라 이 보장 밖이다.)

## 5. 출력 라우팅 — 로거 우선, 폴백은 표준 에러스트림

트레이서/error reporter는 **framework 표준 로거를 통해** 출력한다. 로거에는 카테고리
(`zlink.framework.dispatch`) + 구조화 필드로 넘기고, 폴백 텍스트는 `zlink flow: …` /
`zlink framework dispatch error: …` 포맷을 쓴다.

**통합/분리 선택**(사용자가 고른다) — dispatch 진단 옵션의 `log_file`:

- **분리**: `log_file` 지정 → 트레이싱/에러 전용 파일 로거로 보낸다. 애플리케이션 로그와 절대
  섞이지 않는다.
- **통합**: `log_file` 미지정 + 앱 로거 sink 있음 → 공유 앱 로거가 앱 로그와 트레이싱을 함께
  담는다(같은 파일/콘솔).
- **폴백**: 둘 다 없음 → 표준 에러스트림(C++=`std::clog`). 고볼륨 트래픽이 로거 in-memory 버퍼에
  무한 적재되는 것을 막는다.

폴백 로그 라인은 전 언어 동일 토큰을 쓴다(값이 있는 필드만 출력).

```
zlink flow: phase=… surface=… kind=… packet=… channel=… topic=… corr=… flow=… origin=… src=… spot=… actor=… [size=]
```

`flow=`와 `origin=`은 **쌍이 모두 있을 때만** 출력한다(한쪽만 있는 불완전한 쌍은 둘 다 생략).
두 토큰의 바이트 표현은 언어 간 동일해야 한다([53 §9](53-flow-correlation.ko.md)).

> 언어별 주의: 각 언어 로깅 파사드가 다르므로 dispatch 옵션에 "로거 핸들/sink"를 실어 트레이서가
> 닿게 하는 plumbing이 필요하다. channel/spot에는 dispatch 옵션이 자동 전파되지만 stream/actor
> gateway 같은 별도 서브시스템엔 **명시 주입**이 필요하다(누락하면 그 surface만 조용히 로그가
> 안 나온다). 파일 sink는 부모 디렉토리를 자동 생성해야 한다.

### 5.1 구조화 필드와 label 식별자

로그는 텍스트 한 줄로만 내지 않고 `log_record`의 key/value 필드
(phase/surface/kind/packet/channel/topic/corr/src/spot/actor/size)로도 채운다. 콜렉터가 정규식
파싱 없이 ingest할 수 있어야 한다. 진단 옵션의 `label`(`label=`)로 여러 프로세스 로그를 집계 키로
구분한다.

## 6. 관측 백엔드 경계 (중요 원칙)

로그를 외부 콜렉터(Loki/ELK/Fluentd/Datadog 등)나 분산 추적(OpenTelemetry)으로 보내는 것은
**가능해야 하지만, 그 연동은 애플리케이션 레이어의 선택**이다. framework는 특정 백엔드/SDK에
**의존하지 않는다.** 대신 연동에 필요한 최소 접점만 제공한다.

**framework가 제공 (백엔드 무관, 의존성 0):**

1. `correlation_id` — 메시지 식별자(텍스트 grep + 프로그램 매핑 둘 다 가능).
2. **구조화 출력** — §5.1의 key/value 필드. 콜렉터가 정규식 파싱 없이 ingest 가능.
3. **관측 훅** — `set_message_flow_observer` / `set_message_dispatch_error_observer`. 구조화
   이벤트를 그대로 callback으로 받는다(로그 모드 통합/분리와 무관).

**애플리케이션이 선택 (원할 때만, framework 밖):**

- **로깅 백엔드(provider) 끼우기** (`.NET` ILoggerProvider / SLF4J 바인딩 방식): 앱 로깅 백엔드를
  등록하고 콘솔/파일을 안 켜면 framework는 자체 출력을 강제하지 않고 모든 레코드가 앱 백엔드로만
  간다. **framework는 내장 네트워크/OTLP sink를 제공하지 않는다** — 앱이 브리지한다.
- **레코드 캡처 제어**: 내장 in-memory 버퍼는 테스트/점검용이며 기본 상한(예: 4096) ring이다.
  provider만 쓸 땐 record capture를 끄거나 상한을 조정해 무한 적재를 막는다.
- **OpenTelemetry / W3C `traceparent` / span 모델**: 앱이 자기 OTel SDK에서 observer callback을
  받아 `correlation_id`를 span/traceparent로 매핑한다. **framework는 OTel을 모른다.**

> 요약: framework = `correlation_id` + 구조화 필드 + observer 훅까지. 그 위의 OTel/span/콜렉터
> 어댑터는 앱이 끼운다. 이 경계를 깨고 framework에 OTel을 하드 의존시키지 않는다.

## 7. 길목 (hook point) — 논리 지점, 전 언어 공통

성공 전이는 **error reporter가 이미 호출되는 바로 그 dispatch 길목**에 미러링한다(인바운드).
아웃바운드는 메시지를 인코딩해 전송하는 클라이언트 길목에 추가한다. `dropped`/에러는 error
reporter가 담당하므로 트레이서는 성공 전이만 찍어 중복을 피한다.

| surface | 방향 | 논리 길목 | phase | C++ 레퍼런스 |
|---------|------|-----------|-------|--------------|
| channel | 인바운드 | server 패킷 dispatch | 헤더 디코드 후 `received`; request→`replied`, command→`dispatched` | `channels/channel_packet_dispatcher.cpp` |
| channel | 아웃바운드 | client request/send/publish 제출 | request→`sent`+`reply_received`, send/publish→`sent` | `channels/channel_runtime.cpp` |
| route mesh | 인바운드 | route 패킷 dispatch | `received`; send→`dispatched`, request→`replied` | `channels/route_packet_dispatcher.cpp` |
| route mesh | 아웃바운드 | route client 제출 | `sent`; request-reply는 `reply_received` | `channels/channel_runtime.cpp` |
| spot subscription | 인바운드 | subscription dispatch | `received`; 성공 `dispatched` | `spots/spot_runtime.cpp` |
| spot subscription | 아웃바운드 | spot publish | `sent` | `spots/spot_runtime.cpp` |
| spot actor | 인바운드 | actor packet relay | `received`; 성공 `replied` | `spots/spot_runtime.cpp` |
| spot actor | 아웃바운드 | actor join_spot | `sent`+`reply_received` | `actors/actor_gateway_runtime.cpp` |
| stream session | 인바운드 | stream packet dispatch | `received` (client→session) | `streams/stream_runtime.cpp` |

> `source_rid`(`src=`) 의미가 방향에 따라 다르다: 인바운드=송신자 rid, 아웃바운드=목적지 노드
> rid. 둘 다 `src=`로 나오므로 혼동 주의(원하면 언어별로 `dst=` 분리 가능 — 단 전 언어 합의 필요).
>
> corr 키잉 한계: `correlation_id`는 channel/route/stream 경로에서만 1급으로 흐른다. spot
> 구독/actor/publish/join_spot은 channel-style corr이 없어 `spot_rid`/`actor_id`로 키잉한다.
> "모든 surface가 corr로 키잉된다"고 과장하지 않는다.

## 8. correlation_id 의미 (전파 규칙)

`correlation_id`는 envelope/header를 만드는 쪽이 부여하는 **프로세스 전역 단조값**이다. 노드마다
카운터가 독립이라 숫자만 같고 다른 메시지일 수 있다. 전역 유일 ID(UUID)로 가정하지 않는다.

노드 간 연결은 corr이 **전파**될 때만 성립한다: channel request↔reply, stream request↔reply
echo, route 전파. 두 노드 로그를 한 corr로 잇고 싶으면 이 전파 경로를 거쳐야 한다.

## 9. 스트림 correlation_id 헤더 필드 (와이어 프로토콜)

채널 envelope는 `correlation_id`를 1급 필드로 갖는데(`envelope_header_t::correlation_id`),
스트림은 `metadata("correlation_id")` 관례에 머물러 두 전송계층이 비대칭이었다. 이를 해소하기
위해 **스트림 헤더에도 correlation_id를 1급 필드로** 올린다(채널과 일관).

**결정**

- correlation_id는 스트림 패킷 헤더의 1급 필드다(metadata 관례 폐기).
- **보내는 클라이언트가 생성**하고, **서버(session)는 echo만** 한다(reply에 요청 corr 복사).
  서버는 ingress에서 생성하지 않는다 → 클라이언트가 안 넣으면 그 stream 메시지엔 corr이 없다.
- 자동 부여 시퀀스는 채널과 동일하게 client codec의 프로세스 전역 단조 카운터(hex)다.
- corr은 **트레이싱 전용이 아니라 프로토콜 필드**라, 클라이언트가 **항상** 생성한다(트레이싱
  off여도). 서버가 토글 상태와 무관하게 incoming stream을 corr로 추적할 수 있어야 하기
  때문이다. 비용은 미미(SSO라 보통 무할당 + 와이어 ~1–17B). 진짜로 깎아야 하는 앱을 위해
  connector opt-out(기본 on)을 후속 옵션으로 둘 수 있다.

**와이어 레이아웃** (connector `header_codec`와 framework `stream_runtime` 양쪽 바이트 동일)

- `header_flags_t`에 `has_correlation_id = 0x08` 추가.
- 바이트 배치: `format_marker(0xF2), kind, codec, flags, [request_seq], name(u8 len+bytes), [metadata], [correlation_id(u8 len+bytes)], [flow_id(36B) + flow_origin(u8)]`
  — correlation_id는 **메타데이터 블록 뒤**에, flag가 set일 때만 `u8 길이 + 바이트`.
- **header의 첫 바이트는 `format_marker = 0xF2`다**(값이 다르면 decode error). marker와 뒤따르는
  flow 필드의 계약은 [32 §4](../stream-connector/32-stream-connector.ko.md)와
  [53 §3](53-flow-correlation.ko.md)이 소유한다 — 이 문서는 correlation_id 위치만 고정한다.
- control 패킷은 flag 불가(기존 규칙 유지), send/request/response/error는 허용.
- flag 미set이면 필드 없음 = 하위호환(코덱을 공유하는 엔진은 decode 자동 호환).

스트림 메타데이터 인코딩 통일과 `source_rid`/`target_rid` 분리는 이 계약의 범위에
포함하지 않는다. 이 계약을 구현하는 동안에는 메타데이터 없는 프레임의 호환성만
검증하며, `src=` 필드의 기존 방향별 의미를 유지한다. 두 기능을 공개 계약으로
추가하려면 별도 스펙 변경 절차를 거친다.

## 10. 회귀 테스트 매트릭스 (MFLOW)

언어별 구현은 아래 회귀를 모두 통과해야 한다.

| ID | 검증 |
|----|------|
| MFLOW-001 | `off` → 모든 성공 전이 + 에러 기본 로그 침묵 |
| MFLOW-002 | `errors_only` → `dropped`/에러만, `received` 없음 |
| MFLOW-003 | `key_transitions` → received/dispatched/replied 출력, `corr=` 포함 |
| MFLOW-004 | `verbose` + `include_message_sizes` → `size=` 출력, opt-out 시 미출력 |
| MFLOW-005 | `sample_rate<1` → 정상 전이 thinning, `dropped`/에러는 항상 통과 |
| MFLOW-006 | observer 등록 시 모드 무관 발화, observer 예외가 dispatch를 깨지 않음 |
| MFLOW-007 | channel/route/spot 각 surface에서 한 요청이 received→replied로 이어짐 |
| MFLOW-008 | 실패 줄에도 `corr=` 출력 → `grep corr=<id>`로 성공·실패 메시지가 모두 잡힘 |
| MFLOW-009 | stream 헤더 correlation_id 1급 필드 round-trip + client 자동부여 + server reply echo + 인바운드 `received`에 `corr=` |
| MFLOW-010 | `log_file` 지정 시 트레이싱/에러가 전용 파일로만 가고 앱 로거에 안 섞임; 미지정 시 앱 로거와 통합 |
| MFLOW-011 | `live_mode`가 정적 `message_flow`를 override하고 런타임 변경 즉시 반영(off→on→off). off일 때 이벤트 미생성(제로코스트) |

## 11. 언어별 투영과 구현 상태

각 언어는 이 문서의 관찰 결과를 해당 언어의 관례에 맞는 public 표면으로 제공한다.
언어별 문서는 의미를 다시 정의하지 않고 실제 시그니처, 등록 코드와 sample을 보충한다.
특정 구현의 내부 tracer 구조와 파일 배치는 공통 공개 계약에 포함되지 않는다.

이 문서는 언어별 구현 진행률을 기록하지 않는다. 각 언어의 현재 차이는
[구현 차이 문서](../90-implementation-gap.ko.md)에 기록하며, observer를 외부 수집기나
OpenTelemetry adapter에 연결하는 일은 application 또는 별도 extension이 담당한다.

---
<!-- framework-adapter-nav:bottom:start -->
[스펙 목차](../README.ko.md) | [이전: 런타임 메트릭 계기 (Runtime Metrics Instruments)](51-runtime-metrics.ko.md) | [다음: 메시지 흐름 상관관계 (Flow Correlation)](53-flow-correlation.ko.md)
<!-- framework-adapter-nav:bottom:end -->
