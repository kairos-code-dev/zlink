# Context Auto-HWM Message Unit Rollout Plan

이 문서는 rollout 실행 계획과 완료 기록이다. `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES`
구현, 회귀테스트, binding API 전환, perf smoke, 정식 spec 반영이 완료되었다.
현재 공개 계약은 `doc/spec/core/context.md`, `doc/spec/core/context.ko.md`,
`doc/spec/bindings/README.md`를 기준으로 한다.

## 1. 결정

Auto-HWM message unit은 SpotNode나 Spot handle마다 별도 public API를 추가하지 않는다.
core/C API에 context-level message unit option을 추가하고 모든 binding은 context 옵션 하나만
public API로 노출한다.

핵심 목표는 아래와 같다.

- 사용자는 perf 실행이나 애플리케이션 초기화에서 context에 한 번만 message unit을 설정한다.
- 일반 socket, SpotNode 내부 pub/sub socket, Spot handle이 만든 pub/sub socket은 같은
  context option 설정값을 따른다.
- 기존 per-handle `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`는 남겨 둔다. 이 옵션은 특정 socket만
  다른 message unit을 써야 할 때의 명시적 override다.
- binding public API에서는 socket별, SpotNode별, Spot별 message unit 설정을 제거한다.
  실제 사용 사례는 context 단위 정책으로 충분하므로 언어 binding에서 socket마다 값을
  따로 조절하는 인터페이스를 유지하지 않는다.
- binding perf는 per-socket, SpotNode, Spot별 message unit 설정을 제거하고 context 옵션만
  사용한다.

## 2. Core/C API 계약

추가할 C API 표면은 새 함수가 아니라 기존 context option API의 새 enum 값이다.
정확한 변경 대상은 아래 네 가지다.

### 2.1 변경되는 enum

`core/include/zlink_enum.h`의 `zlink_ctx_option_t`에 아래 값을 추가한다.
`bindings/c/include/zlink_enum.h`는 core 검증 뒤 sync 단계에서 같은 값을 갖게 한다.

```c
typedef enum zlink_ctx_option_t
{
    ZLINK_IO_THREADS = 1,
    ZLINK_MAX_SOCKETS = 2,
    ZLINK_SOCKET_LIMIT = 3,
    ZLINK_THREAD_PRIORITY = 3,
    ZLINK_THREAD_SCHED_POLICY = 4,
    ZLINK_MAX_MSGSZ = 5,
    ZLINK_MSG_T_SIZE = 6,
    ZLINK_THREAD_AFFINITY_CPU_ADD = 7,
    ZLINK_THREAD_AFFINITY_CPU_REMOVE = 8,
    ZLINK_THREAD_NAME_PREFIX = 9,
    ZLINK_CTX_OPT_BLOCKY = 10,
    ZLINK_CTX_OPT_AUTO_HWM_ENABLE = 12,
    ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS = 14,
    ZLINK_CTX_OPT_AUTO_HWM_PROFILE = 17,
    ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES = 18
} zlink_ctx_option_t;
```

`zlink_option_t`는 변경하지 않는다. 기존 handle-level 값은 그대로 유지한다.

```c
ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES = 0x3034
```

### 2.2 변경되는 C macro

`core/include/zlink/core.h`에 아래 기본값 macro를 추가한다. `bindings/c/include/zlink.h`는
core 검증 뒤 sync 단계에서 같은 macro를 갖게 한다.

```c
#define ZLINK_CTX_AUTO_HWM_MSG_UNIT_BYTES_DFLT 0
```

### 2.3 사용하는 C API

새 함수는 추가하지 않는다. 아래 기존 public C API가 새 enum 값을 받게 한다.

```c
ZLINK_EXPORT zlink_config_result_t zlink_ctx_set (
  void *context_,
  zlink_ctx_option_t option_,
  int optval_);

ZLINK_EXPORT int zlink_ctx_get (
  void *context_,
  zlink_ctx_option_t option_,
  zlink_config_result_t *error_out_);

ZLINK_EXPORT zlink_config_result_t zlink_ctx_auto_hwm_recalculate (
  void *context_);
```

사용 예시는 아래와 같다.

```c
zlink_config_result_t rc =
  zlink_ctx_set (ctx, ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES, msg_size);

zlink_config_result_t err = ZLINK_CONFIG_OK;
int msg_unit =
  zlink_ctx_get (ctx, ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES, &err);

zlink_ctx_auto_hwm_recalculate (ctx);
```

### 2.4 public allow-list 변경

`core/src/api/core/context_api.cpp`의 public context option 목록에 아래 항목을 추가한다.

```c
{ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES, true, true},
```

계약은 아래처럼 고정한다.

- 값은 `0` 또는 양수 int다. 음수 값은 `ZLINK_CONFIG_INVALID_ARGUMENT`로 실패한다.
- `0`은 context-level override를 쓰지 않는다는 뜻이다. 이때 socket-type 기본 message
  unit을 유지한다. 현재 non-STREAM socket의 built-in 기본값은 `4096`이고 STREAM 기본값은
  `1024`다.
- `zlink_ctx_set()` 성공 시 `ZLINK_CONFIG_OK`를 반환한다.
- `zlink_ctx_get()` 성공 시 설정된 int 값을 반환하고 `error_out_`이 NULL이 아니면
  `*error_out_ = ZLINK_CONFIG_OK`를 쓴다.
- NULL 또는 invalid context는 기존 context API 규칙대로 `ZLINK_CONFIG_INVALID_HANDLE`을
  반환하거나 `error_out_`에 쓴다.
- 기본값은 `0`이다. 기존 동작을 깨지 않기 위해 context option을 설정하지 않은 context는
  socket-type 기본 message unit을 그대로 사용한다.
- context option은 같은 context에서 만들어진 auto-HWM 대상 socket의 기본 message unit
  override다.
- per-handle `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`를 직접 설정한 socket은 context option
  값보다 명시적 override를 우선한다.
- context option 값을 바꾼 뒤 `zlink_ctx_auto_hwm_recalculate()`를 호출하면, 명시적
  override가 없는 일반 socket과 service-internal socket은 새 context option 값으로 다시
  계산되어야 한다.
- SpotNode/Spot 전용 message unit public API는 추가하지 않는다.

## 3. Core 구현 계획

1. 회귀테스트를 먼저 작성한다.
   - `zlink_ctx_get(... AUTO_HWM_MSG_UNIT_BYTES ...)` 기본값이 `0`인지 확인한다.
   - `zlink_ctx_set(..., 64)` 뒤 get이 `64`를 돌려주는지 확인한다.
   - `zlink_ctx_set(..., 0)` 뒤 일반 non-STREAM socket은 built-in 기본 message unit
     `4096`을, STREAM socket은 built-in 기본 message unit `1024`를 계속 쓰는지 확인한다.
   - context option을 `64`로 설정한 뒤 다시 `0`으로 설정하고 recalc를 호출하면, 일반
     non-STREAM socket은 `4096`, STREAM socket은 `1024`로 돌아가는지 확인한다.
   - 음수 값이 실패하는지 확인한다.
   - 음수 값을 설정하려는 시도가 실패한 뒤 기존 context option 값이 바뀌지 않는지 확인한다.
   - `zlink_ctx_set_data(... AUTO_HWM_MSG_UNIT_BYTES ...)`에 `sizeof(int)`가 아닌 길이의
     buffer를 넘긴 호출은 실패하고 기존 값이 유지되는지 확인한다.
   - context option을 `64`로 설정한 뒤 일반 socket을 만들면 auto-HWM snapshot의
     `auto_hwm_effective_message_bytes`가 `64`인지 확인한다.
   - 일반 socket을 만든 뒤 context option 값을 `64`에서 `256`으로 바꾸고
     `zlink_ctx_auto_hwm_recalculate()`를 호출하면, explicit override가 없는 socket의
     `auto_hwm_effective_message_bytes`가 `256`으로 바뀌는지 확인한다.
   - context option을 `64`로 설정한 뒤 SpotNode를 만들면 `InternalSocketsSnapshot()`의
     visible socket들이 `64`를 message unit으로 쓰는지 확인한다.
   - SpotNode 생성 뒤 context option 값을 `256`으로 바꾸고
     `zlink_ctx_auto_hwm_recalculate()`를 호출하면 SpotNode internal socket snapshot의
     visible socket들이 `256`을 message unit으로 쓰는지 확인한다.
   - socket에 per-handle `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`를 설정한 경우 context option
     값을 바꿔도 해당 socket override가 유지되는지 확인한다.
   - per-handle override가 있는 socket 하나와 override가 없는 socket 하나를 같은 context에
     두고 recalc를 호출해, override socket은 자기 값을 유지하고 나머지는 context option
     값을 따르는지 확인한다.
   - per-handle `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES = 0`의 의미를 테스트로 고정한다. 이 값은
     explicit override 해제로 정의하고 이후 recalc에서 context option 값을 다시 따르는지
     확인한다.
   - per-handle override 해제 뒤 context option 값을 다시 `0`으로 바꾸면 해당 socket도
     socket-type 기본 message unit으로 돌아가는지 확인한다.
   - `zlink_ctx_set(... AUTO_HWM_MSG_UNIT_BYTES ...)`가 기존 auto-HWM context option과 같은
     방식으로 debounced recalc를 예약하는지 확인한다. 즉시 반영이 필요한 테스트에서는
     `zlink_ctx_auto_hwm_recalculate()`를 호출해 결정적으로 검증한다.
   - public allow-list에 빠졌을 때 실패하도록 `zlink_ctx_set`과 `zlink_ctx_get` 양쪽에서
     `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES`가 `ZLINK_CONFIG_OK`로 통과하는지 확인한다.

2. core enum과 기본값을 추가한다.
   - `core/include/zlink_enum.h`
   - `core/include/zlink/core.h`
   - `bindings/c/include/zlink_enum.h`와 `bindings/c/include/zlink.h`는 직접 수정하지 않고
     core 검증 뒤 sync 단계에서 갱신한다.

3. context 저장소에 option을 추가한다.
   - `core/src/runtime/core/ctx.cpp`의 get/set switch에 추가한다.
   - `core/src/api/core/context_api.cpp`의 public option allow-list에 추가한다.
   - 기본값은 context 생성 시 `0`으로 초기화한다.

4. auto-HWM 계산 경로에 context option 값을 연결한다.
   - `ctx_t`에 context message unit 값을 읽는 getter를 추가한다.
   - socket option owner가 explicit per-handle message unit override 여부를 구분하게 한다.
   - per-handle `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES = 0`은 explicit override 해제로 처리한다.
   - explicit per-handle override가 있으면 기존 per-handle option 값을 사용한다.
   - explicit per-handle override가 없고 context option 값이 `0`보다 크면 context option
     값을 사용한다.
   - explicit per-handle override도 없고 context option 값도 `0`이면 기존 socket-type
     기본값을 사용한다.

5. SPOT 내부 socket 경로를 정렬한다.
   - SpotNode default pub/sub, Spot per-handle pub/sub, control/router 등 auto-HWM visible
     internal socket은 context option 설정값을 기본 message unit으로 사용한다.
   - SPOT internal auto-HWM policy는 explicit override가 없으면 context message unit 값을
     읽는다. context option 값이 `0`이면 기존 socket-type 기본값을 유지한다.
   - context recalc 경로에서 SPOT internal socket plan도 다시 만들 수 있게 연결한다.
   - 기존 `refresh_runtime_auto_hwm_msg_unit` 같은 per-handle refresh 경로는 per-handle
     override 설정 때만 사용한다.

6. core 검증을 실행한다.
   - `cmake --build core/build`
   - `ctest --test-dir core/build --output-on-failure -R "ctx_options|auto_hwm|spot|monitor"`

7. binding 작업 전에 core library를 동기화한다.
   - core 수정과 테스트가 완료된 뒤에만 아래 명령을 실행한다.
   - `/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh`
   - 이 단계가 끝나기 전에는 C++, .NET, Java, Node, Rust, Go, Python binding 코드를
     수정하지 않는다.
   - 각 binding은 동기화된 local core library와 vendored C header를 기준으로 context
     option public API를 추가하거나 socket별 message unit public API를 제거한다.
   - sync 실행 뒤 `bindings/c/include/zlink_enum.h`와 `bindings/c/include/zlink.h`에 새 enum과
     default macro가 반영되었는지 확인한다.

## 4. Binding API 적용 계획

binding public API는 context option에만 추가한다. SpotNode/Spot별 message unit API는
추가하지 않고 기존 socket별 message unit public API도 제거한다.
이 절은 core 구현, core 테스트,
`/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh` 실행이 모두 끝난 뒤에만
진행한다.
socket별 message unit API 제거는 breaking change로 처리한다. 삭제된 API의 호환 별칭은
추가하지 않고 migration 문서와 contract test로 context option 대체 경로를 고정한다.

| 언어 | 추가/수정할 public API | 적용 위치 |
|------|-------------------------|-----------|
| C | 새 enum과 default macro만 노출한다. 별도 wrapper 함수는 추가하지 않는다. | `bindings/c/include/zlink_enum.h`, `bindings/c/include/zlink.h` |
| C++ | `zlink::context_options_t::auto_hwm_msg_unit_bytes(zlink::byte_size_t value)`, `zlink::context_options_t::auto_hwm_msg_unit_bytes() const` | `bindings/cpp/include/zlink/Contracts/Core/context.hpp`, `types_impl.hpp`, contract tests |
| .NET | `IContextOptions.AutoHwmMessageUnitBytes { get; set; }` | `bindings/dotnet/src/Zlink/Contracts/Core/ContextOptions.cs`, runtime option facade, surface tests |
| Java | `ContextOptions.autoHwmMessageUnitBytes()`, `ContextOptions.autoHwmMessageUnitBytes(int value)` | `bindings/java/src/main/java/systems/zlink/contracts/ContextOptions.java`, `ContextOption`, contract tests |
| Node | `ctx.options.autoHwmMsgUnitBytes` getter/setter | `bindings/node/src/zlink/contracts/service/models.ts`, `runtime/core/canonical.ts`, typecheck tests |
| Rust | `ContextOptions::set_auto_hwm_msg_unit_bytes(i32)`, `ContextOptions::auto_hwm_msg_unit_bytes() -> Result<i32, ConfigError>` | `bindings/rust/src/runtime/core/ctx.rs`, FFI enum, option tests |
| Go | `func (o *ContextOptions) SetAutoHwmMsgUnitBytes(value int) error`, `func (o *ContextOptions) AutoHwmMsgUnitBytes() (int, error)` | `bindings/go/context.go`, contract/option tests |
| Python | `ContextOptions.auto_hwm_msg_unit_bytes` property | `bindings/python/src/zlink/contracts/core/context.py`, enum, tests |

### 4.1 Socket별 message unit API 삭제 계획

삭제 대상은 언어 binding의 socket/SpotNode/Spot public facade다. C API의
`ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`와 `zlink_set_option`/`zlink_get_option` 경로는 core
저수준 계약이므로 유지한다.

| 언어 | 삭제할 public surface | 대체 경로 |
|------|-----------------------|-----------|
| C | 삭제 없음. handle-level option은 유지한다. | `zlink_ctx_set(ctx, ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES, value)` |
| C++ | socket/service option의 `auto_hwm_msg_unit_bytes(...)` facade 중 socket별 설정 경로를 제거한다. SpotNode/Spot에는 새 facade를 만들지 않는다. | `context.options().auto_hwm_msg_unit_bytes(value)` |
| .NET | socket option의 `AutoHwmMessageUnitBytes`, `ISpotNode.AutoHwmMessageUnitBytes`, `ISpot.AutoHwmMessageUnitBytes`를 제거한다. | `Context.Options.AutoHwmMessageUnitBytes` |
| Java | socket option의 `autoHwmMessageUnitBytes(...)` facade와 perf 전용 SpotNode native option helper를 제거한다. | `Context.options().autoHwmMessageUnitBytes(value)` |
| Node | `socket.options.autoHwmMsgUnitBytes`를 제거한다. SpotNode/Spot native option wrapper는 추가하지 않는다. | `ctx.options.autoHwmMsgUnitBytes = value` |
| Rust | socket/common option의 `set_auto_hwm_msg_unit_bytes`와 `auto_hwm_msg_unit_bytes` facade를 제거한다. | `ctx.options().set_auto_hwm_msg_unit_bytes(value)` |
| Go | socket/common option의 `SetAutoHwmMsgUnitBytes`와 `AutoHwmMsgUnitBytes` facade를 제거한다. | `ctx.Options().SetAutoHwmMsgUnitBytes(value)` |
| Python | `SocketOptions.auto_hwm_msg_unit_bytes` property를 제거한다. | `ctx.options.auto_hwm_msg_unit_bytes = value` |

삭제 검증은 아래처럼 한다.

- 각 binding의 public surface 테스트에서 삭제된 socket별 symbol을 더 이상 노출하지 않는지
  확인한다.
- perf/sample/test가 삭제된 socket별 API를 참조하지 않도록 context option으로 바꾼다.
- 마이그레이션 문서에는 "같은 context의 socket은 같은 message unit 정책을 공유한다"는
  규칙과 context option 대체 코드를 적는다.
- C API 문서에는 handle-level override가 남아 있다는 점을 명시하되, guide에서는 일반 사용
  경로로 안내하지 않는다.

각 binding 검증은 해당 언어의 public surface 테스트에서 아래를 확인한다.

- context option set/get이 동작한다.
- context option을 socket 생성 전에 설정하면 새 socket의 monitor snapshot이 context
  message unit을 따른다.
- context option을 socket 생성 뒤 바꾸고 recalc를 호출하면 override가 없는 socket의 monitor
  snapshot이 새 context message unit을 따른다.
- socket별 message unit API를 쓰지 않고도 새로 만든 socket의 monitor snapshot이 context
  message unit을 따른다.
- socket별, SpotNode별, Spot별 message unit public API가 남아 있지 않다.
- 각 binding의 perf, sample, contract test가 삭제된 socket별 message unit API를 import,
  reflect, compile, typecheck, 호출하지 않는지 확인한다.
- 각 binding은 sync된 C header의 `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES = 18` 값을 사용하고
  binding 내부에 다른 숫자를 중복 정의하지 않는다. 불가피하게 enum을 복제하는 언어는 C
  header와 같은 값을 검증하는 테스트를 둔다.

## 5. Perf 적용 계획

perf runner는 message size별 run을 시작할 때 context에 한 번만 설정한다.

```c
zlink_ctx_set(ctx, ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES, msg_size);
zlink_ctx_auto_hwm_recalculate(ctx);
```

언어별 적용 순서는 기존 binding perf 순서와 같다.

1. C perf
   - `bindings/c/perf` 공통 runtime helper에서 context 생성 직후 message unit을 설정한다.
   - 일반 socket별 `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` 설정은 perf runtime에서 제거한다.
     explicit override 검증은 core 계약 테스트에만 남긴다.
   - SPOT, SPOT_REQREP, SPOT_SENDSEND smoke에서 모든 size의 `MsgUnit(B)`가 message size와
     같은지 먼저 확인한다.

2. C++
   - C++ perf helper가 `context.options().auto_hwm_msg_unit_bytes(...)`를 사용하게 한다.
   - SpotNode/Spot에 별도 API를 추가하지 않는다.

3. .NET
   - `Context.Options.AutoHwmMessageUnitBytes`를 사용하게 한다.
   - 기존 socket option, `ISpotNode.AutoHwmMessageUnitBytes`,
     `ISpot.AutoHwmMessageUnitBytes`는 breaking change로 제거한다.

4. Java
   - `Context.options().autoHwmMessageUnitBytes(size)`를 사용한다.
   - SpotNode 내부 helper로 message unit을 밀어 넣는 임시 경로는 제거한다.

5. Node
   - `ctx.options.autoHwmMsgUnitBytes = msgSize`를 사용한다.
   - SpotNode/Spot native option wrapper 추가 계획은 폐기한다.

6. Rust
   - `ctx.options().set_auto_hwm_msg_unit_bytes(msg_size)`를 사용한다.

7. Go
   - `ctx.Options().SetAutoHwmMsgUnitBytes(msgSize)`를 사용한다.

8. Python
   - `ctx.options.auto_hwm_msg_unit_bytes = msg_size`를 사용한다.

각 언어는 full 전에 모든 size smoke를 먼저 실행한다. SPOT 계열 smoke에서
`MsgUnit(B)=4096`이 한 번이라도 나오면 full 실행을 중단하고 context option 전파 경로를
수정한다.

## 6. 문서 반영 계획

구현 전에는 정식 spec에 섞어 쓰지 않는다.

1. 구현 전 draft
   - `doc/spec/draft/context-auto-hwm-msg-unit.ko.md`
   - 이 문서에는 "현재 공개 계약이 아님"을 첫머리에 적고 context option 값과 per-handle
     override의 우선순위를 명시한다.

2. core 구현 완료 뒤 정식 spec
   - `doc/spec/core/context.ko.md`
   - `doc/spec/core/context.md`
   - context option 표에 `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES`를 추가한다.

3. guide
   - `doc/guide/10-performance.ko.md`
   - `doc/guide/10-performance.md`
   - 사용자는 perf나 애플리케이션 초기화에서 context에 한 번 설정하면 된다고 설명한다.
   - 내부 socket 구조 설명은 넣지 않고 필요하면 internals 문서로 링크한다.

4. internals
   - `doc/internals/socket-option-defaults.ko.md`
   - `doc/internals/socket-option-defaults.md`
   - context option 값, per-handle override, SpotNode internal socket 전파 구조를 설명한다.

5. binding 문서
   - `doc/spec/bindings/README.md`
   - 각 binding README 또는 public API 문서가 있으면 context option만 추가한다.
   - socket별, SpotNode별, Spot별 message unit facade는 삭제된 API로 정리하고 안내하지
     않는다.

6. perf 계획 문서
   - `doc/plan/perf/bindings-library-performance-improvement-plan.ko.md`
   - SPOT `MsgUnit(B)=4096` 해소 방식을 SpotNode/Spot facade가 아니라 context option
     rollout로 바꾼다.

## 7. 완료 기준

- core context option 회귀테스트가 통과한다.
- C perf SPOT 계열 smoke에서 모든 size의 `MsgUnit(B)`가 message size와 일치한다.
- 각 binding public API surface 테스트가 context option을 검증한다.
- 각 binding public API에서 socket별, SpotNode별, Spot별 message unit 설정이 제거된다.
- 각 binding perf는 context 설정만 사용하고 socket별 message unit API에 의존하지 않는다.
- 정식 spec, guide, internals, binding 문서가 구현된 계약과 일치한다.
