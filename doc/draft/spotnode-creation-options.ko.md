[스펙 목차](../README.ko.md)

# Draft -- SpotNode 생성 옵션

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`와 정식 spec 문서에 없는
> API, 상수, 기본 동작을 보장하지 않는다.
> 구현과 공개 헤더, 관련 테스트, 바인딩 문서, 정식 문서가 확정되면 이 내용을
> 정식 spec 문서에 나누어 반영한다.

## 0. 무인 구현 진행 기준

이 문서는 구현자가 별도 확인을 기다리지 않고 끝까지 진행할 수 있게 하는 기준이다.
구현 중 선택지가 나오면 이 절의 고정 결정값과 단계 순서를 우선한다.

### 0.1 진행 규칙

- 중간 확인을 기다리지 않는다. 이 문서에 적힌 API 이름, enum 값, 오류 정책,
  출력 형식, 검증 순서를 고정 결정값으로 사용한다.
- 같은 단계에서 이슈가 나오면 원인 파악, 수정, 재검증까지 끝낸 뒤 다음 단계로
  넘어간다.
- 일회성 우회, 미완료 표시, 빈 구현을 남기지 않는다.
- 기존 untracked 문서, draft 문서, 작업 중 변경 파일은 임의로 삭제하거나
  되돌리지 않는다.
- `core/include/` 또는 `core/src/`를 바꾼 뒤에는 반드시 `cmake --build core/build`
  로 core runtime을 먼저 다시 빌드한다.
- `bindings/c/perf` 실행 전 runner가 출력하는 `libzlink.so` 경로가 `core/build`
  아래인지 확인한다.
- runner가 stale runtime을 감지하면 perf를 계속 돌리지 않는다. `core/build`를
  다시 빌드한 뒤 재실행한다.
- 실패 로그는 같은 단계 안에서 해석한다. 테스트를 줄이거나 기준을 낮춰 통과시키지
  않는다.
- 완료 전에는 14장 완료 기준과 15장 구현 가능성 리뷰 체크리스트를 다시 읽고,
  미반영 항목이 0개인지 확인한다.

### 0.2 고정 결정값

| 항목 | 결정 |
|------|------|
| SpotNode 생성 API | 기존 `zlink_spot_node_new`에 `options` 인자를 추가한다 |
| 호환성 | 기존 함수 시그니처 호환성은 목표가 아니다 |
| 기본 mode | `options == NULL` 또는 `options->mode == 0`이면 `ALL` |
| invalid mode | 생성 실패, `errno=EINVAL` |
| disabled 기능 | 암묵적 소켓 생성 없이 실패, `errno=ENOTSUP` |
| Spot mode | `Spot`은 node mode를 그대로 상속한다 |
| mode 변경 | 생성 뒤 enable/disable API는 만들지 않는다 |
| snapshot API | `zlink_spot_node_internal_sockets_snapshot()` 별도 row API |
| snapshot 성격 | 관찰 전용, 새 내부 소켓을 만들지 않는다 |
| socket type 노출 | 공개 `ZLINK_SOCKET_*` 값만 노출한다 |
| perf 기본 표 | `auto_hwm_visible == 1`인 row만 출력한다 |
| 빈 표 | row가 없으면 표 자체를 생략한다 |
| MsgUnit | perf message size를 socket option으로 설정하고 snapshot 값을 출력한다 |

### 0.3 작업 파일 지도

구현자는 아래 파일군을 기준으로 작업 범위를 잡는다. 실제 코드 구조가 조금 다르면
가장 가까운 같은 책임의 파일에 반영한다.

| 영역 | 파일 |
|------|------|
| 공개 enum/API | `core/include/zlink_enum.h`, `core/include/zlink.h` |
| C API 구현 | `core/src/api/service_spot_node_api.cpp` |
| SpotNode access | `core/src/services/spot/spot_node_access.hpp`, `core/src/services/spot/spot_node_access.cpp` |
| SpotNode 상태 | `core/src/services/spot/spot_node.hpp`, `core/src/services/spot/spot_node_state.hpp` |
| Spot handle | `core/src/services/spot/spot_handle.hpp`, `core/src/api/service_spot_node_api.cpp` |
| runtime 생성 | `core/src/services/spot/spot_runtime.hpp`, `core/src/services/spot/spot_runtime.cpp`, `core/src/services/spot/spot_data_plane_runtime.cpp` |
| pub/sub facade | `core/src/services/spot/spot_node_defaults.cpp`, `core/src/services/spot/spot_subject_publish.cpp`, `core/src/services/spot/spot_subject_query.cpp` |
| routed request/reply | `core/src/api/service_spot_request_reply_*.cpp`, `core/src/api/service_spot_request_reply_internal.hpp` |
| service attachment | `core/src/services/spot/spot_node_service_attachment*.cpp` |
| perf C runner | `bindings/c/perf/multi/common/perf_multi_runtime.hpp`, `bindings/c/perf/run_comparison.py`, `bindings/c/perf/run_benchmarks_multi.sh` |
| perf SPOT patterns | `bindings/c/perf/multi/common/perf_multi_spot_control.hpp`, `bindings/c/perf/multi/src/perf_multi_spot*.cpp` |
| core tests | `core/tests/unittest/`, `core/tests/integration/`, `core/tests/e2e/spot/`, `core/tests/bench/` |
| binding tests | `bindings/cpp/tests/`, `bindings/go/`, `bindings/rust/tests/`, `bindings/python/tests/`, `bindings/node/tests/`, `bindings/dotnet/tests/`, `bindings/java/src/test/` |
| 정식 문서 | `doc/spec/`, `doc/guide/`, `doc/internals/`, `doc/perf/`, `bindings/*/README*` |

### 0.4 필수 검증 명령

아래 명령은 작업 단계에 맞게 실행한다. 실패하면 같은 단계 안에서 수정하고 다시
실행한다.

```sh
test -d core/build || cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release
cmake --build core/build
ctest --test-dir core/build --output-on-failure
test -d bindings/c/build || cmake -S bindings/c -B bindings/c/build \
  -DZLINK_C_CORE_BUILD_DIR="$PWD/core/build"
cmake --build bindings/c/build
```

binding을 수정한 뒤에는 해당 언어의 테스트를 실행한다. 수정한 binding만 먼저
검증하고, 마지막에는 가능한 전체 binding smoke를 실행한다.

```sh
bindings/cpp/tests/run_tests.sh
bindings/go/tests/run_tests.sh
bindings/rust/tests/run_tests.sh
bindings/python/tests/run_tests.sh
bindings/node/tests/run_tests.sh
bindings/dotnet/tests/run_tests.sh
bindings/java/tests/run_tests.sh
```

`bindings/c/perf` smoke는 core runtime 재빌드 뒤 실행한다.

```sh
bindings/c/perf/run_benchmarks_multi.sh \
  --pattern SPOT \
  --pattern SPOT_REQREP \
  --transports tcp \
  --msg-sizes 64,65536 \
  --duration 1
```

perf 출력에서 아래 조건을 확인한다.

- runner가 출력한 `libzlink.so` 경로가 `core/build` 아래다.
- stale runtime 오류가 없다.
- `Auto-HWM spotnode`와 `Auto-HWM spot`은 snapshot row 기준으로 출력된다.
- `MsgUnit(B)`는 테스트 message size와 일치한다.
- row가 없는 `Auto-HWM spot` 표는 출력되지 않는다.

### 0.5 반복 완료 루프

구현자는 아래 루프를 미반영 항목이 없어질 때까지 반복한다.

1. 현재 단계의 코드, 테스트, 문서를 반영한다.
2. 해당 단계의 빌드와 테스트를 실행한다.
3. 실패하면 같은 단계에서 원인을 찾아 수정하고 2번으로 돌아간다.
4. 통과하면 14장 완료 기준과 15장 체크리스트에서 아직 반영되지 않은 항목을
   표시한다.
5. 미반영 항목이 있으면 해당 항목이 속한 단계로 돌아가 수정한다.
6. 미반영 항목이 0개이고 필수 검증 명령이 모두 통과했을 때만 완료한다.

## 1. 목적

이 초안은 `SpotNode`를 만들 때 사용할 기능 범위를 미리 정하는 생성 옵션을
추가한다.

현재 `SpotNode`는 pub/sub와 routed request/reply 기능을 모두 쓸 수 있다는
전제로 내부 runtime을 준비한다. 이 방식은 단순하지만, 실제 사용 기능이 하나뿐인
경우에도 관련 없는 내부 소켓과 HWM 설정이 함께 만들어진다.

특히 `Spot` handle은 수천 개까지 생성될 수 있다. 이때 사용하지 않는 기능의
내부 소켓과 상태까지 준비하면 메모리, monitor, auto-HWM accounting, perf 출력이
모두 불필요하게 커진다.

따라서 `SpotNode` 생성 시점에 mode를 고정해서 아래 목표를 달성한다.

- pub/sub 전용 node는 routed/router 내부 소켓을 만들지 않는다.
- routed 전용 node는 topic pub/sub 내부 소켓을 만들지 않는다.
- `Spot` handle은 별도 mode를 받지 않고 자신이 붙은 `SpotNode` mode를 따른다.
- auto-HWM과 monitor/perf 출력은 실제 생성된 내부 소켓만 보여준다.

## 2. 공개 API 초안

호환성 유지는 이번 초안의 목표가 아니다. 기존 생성 함수에 옵션 인자를 추가한다.

```c
typedef enum zlink_spot_node_mode_t {
    ZLINK_SPOT_NODE_MODE_PUBSUB = 1,
    ZLINK_SPOT_NODE_MODE_ROUTED = 2,
    ZLINK_SPOT_NODE_MODE_ALL = 3
} zlink_spot_node_mode_t;

typedef struct zlink_spot_node_options_t {
    zlink_spot_node_mode_t mode;
} zlink_spot_node_options_t;

ZLINK_EXPORT void *zlink_spot_node_new(
    void *ctx,
    const zlink_spot_node_options_t *options);
```

### 2.1 SpotNode 내부 소켓 snapshot API

`zlink_monitor_snapshot_t`는 하나의 monitor subject에 대한 현재 상태를 담는
구조체다. 이 구조체에는 `PUB`, `SUB`, `ROUTER` 같은 실제 socket type이 없고,
`SpotNode` 내부에 어떤 소켓이 만들어졌는지 나열하지도 않는다.

따라서 `SpotNode` 내부 소켓 목록과 각 소켓의 type, auto-HWM 상태를 확인하는
별도 snapshot API를 둔다.

```c
typedef enum zlink_spot_node_socket_owner_t {
    ZLINK_SPOT_NODE_SOCKET_OWNER_ANY = 0,
    ZLINK_SPOT_NODE_SOCKET_OWNER_NODE = 1,
    ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT = 2
} zlink_spot_node_socket_owner_t;

typedef struct zlink_spot_node_socket_snapshot_filter_t {
    zlink_spot_node_socket_owner_t owner;
    zlink_socket_type_t socket_type;
    char socket_name[64];
} zlink_spot_node_socket_snapshot_filter_t;

typedef struct zlink_spot_node_socket_snapshot_entry_t {
    zlink_spot_node_socket_owner_t owner;
    uint64_t owner_id;
    char owner_name[64];
    char socket_name[64];
    zlink_socket_type_t socket_type;
    uint32_t auto_hwm_visible;
    zlink_monitor_snapshot_t snapshot;
} zlink_spot_node_socket_snapshot_entry_t;

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_internal_sockets_snapshot(
    void *node,
    const zlink_spot_node_socket_snapshot_filter_t *filter,
    zlink_spot_node_socket_snapshot_entry_t *entries,
    size_t *count);
```

이 함수는 `SpotNode`가 실제로 생성한 내부 소켓만 반환한다. snapshot을 얻기 위해
`ensure_default_pub()`처럼 빠진 소켓을 새로 만들면 안 된다. 이 규칙은 mode별
자원 생성 여부를 확인하기 위해 필요하다.

`entries == NULL`이면 필요한 row 개수를 `*count`에 기록하고 성공한다. `entries`가
있지만 `*count`가 부족하면 필요한 row 개수를 `*count`에 기록하고 `ENOBUFS`로
실패한다. 이 동작은 기존 `peers_snapshot`, `subjects_snapshot` 계열 API와 같은
two-pass 조회 방식이다.

`filter == NULL`이면 모든 내부 소켓을 반환한다. `filter`가 있으면 아래 조건을
모두 만족하는 row만 반환한다.

- `owner != ANY`이면 node 소유 소켓과 spot 소유 소켓 중 하나만 반환한다.
- `socket_type != ZLINK_SOCKET_ANY`이면 해당 socket type만 반환한다.
- `socket_name[0] != '\0'`이면 해당 내부 socket 이름만 반환한다.

`owner_id`는 같은 snapshot 결과 안에서 owner를 구분하기 위한 값이다. node 소유
소켓은 `owner_id == 0`을 사용한다. spot 소유 소켓은 공개 포인터 값을 노출하지
않는 안정적인 내부 식별자를 사용한다. 이 값은 프로세스 재시작 뒤에도 유지되는
영구 ID가 아니다.

`owner_name`은 사람이 읽기 위한 표시 이름이다. node 소유 소켓은 `spotnode`를
사용한다. spot 소유 소켓은 `spot` 또는 구현에서 정한 짧은 이름을 사용한다.
`socket_name`은 `route_ingress`, `node_router`, `fanout`, `ingress`처럼 내부
runtime에서 실제로 쓰는 이름을 그대로 사용한다. perf는 이 값을 임의로 줄이거나
다른 label로 바꾸지 않는다.

#### 2.1.1 조회 구조체 상세

`zlink_spot_node_internal_sockets_snapshot()`은 아래 네 가지 값으로 조회한다.

| 인자 | 방향 | 의미 |
|------|------|------|
| `node` | 입력 | 조회할 `SpotNode` handle |
| `filter` | 입력 | 반환할 내부 소켓 row를 제한하는 조건, `NULL`이면 전체 조회 |
| `entries` | 출력 | 내부 소켓 snapshot row 배열, `NULL`이면 필요한 개수만 조회 |
| `count` | 입출력 | 입력 시 배열 용량, 출력 시 실제 필요하거나 기록한 row 개수 |

`zlink_spot_node_socket_snapshot_filter_t`는 row를 고르기 위한 조건이다. 이
구조체는 0으로 초기화하면 전체 조회 조건이 된다.

| 필드 | 기본값 | 의미 |
|------|--------|------|
| `owner` | `ANY` | node 소유 소켓과 spot 소유 소켓 중 어느 쪽을 볼지 정한다 |
| `socket_type` | `ZLINK_SOCKET_ANY` | `ZLINK_SOCKET_PUB`, `ZLINK_SOCKET_SUB`, `ZLINK_SOCKET_ROUTER` 같은 실제 socket type으로 제한한다 |
| `socket_name` | 빈 문자열 | 내부 socket 이름이 정확히 일치하는 row만 반환한다 |

`socket_type == ZLINK_SOCKET_ANY`는 전체 socket type을 뜻한다.
`ZLINK_SOCKET_ANY`는 생성 가능한 socket type이 아니라 filter API에서만 쓰는
wildcard enum 값이다.

`zlink_spot_node_socket_snapshot_entry_t`는 실제 생성된 내부 소켓 하나를
나타낸다.

| 필드 | 의미 |
|------|------|
| `owner` | 소켓이 `SpotNode` 자체 소유인지, 개별 `Spot` 소유인지 나타낸다 |
| `owner_id` | 같은 snapshot 안에서 owner를 구분하는 숫자다 |
| `owner_name` | 사람이 읽을 owner 이름이다. 예: `spotnode`, `spot` |
| `socket_name` | 내부 runtime socket 이름이다. 예: `route_ingress`, `node_router`, `fanout`, `ingress` |
| `socket_type` | 실제 socket type이다. 예: `ZLINK_SOCKET_ROUTER`, `ZLINK_SOCKET_PUB`, `ZLINK_SOCKET_SUB` |
| `auto_hwm_visible` | perf의 기본 Auto-HWM 표에 표시할 row인지 나타낸다 |
| `snapshot` | 해당 내부 소켓에서 얻은 기존 `zlink_monitor_snapshot_t` 값이다 |

`snapshot` 필드는 내부 소켓의 `socket_base_t::monitor_snapshot()` 결과를 담는다.
따라서 `snapshot.source_kind`는 실제 socket snapshot을 뜻하는
`ZLINK_MONITOR_SOURCE_SOCKET`으로 둔다. SpotNode 안에서 어떤 소켓인지에 대한
정보는 `owner`, `owner_name`, `socket_name`, `socket_type` 필드로 판단한다.

`socket_type`은 공개 API의 `zlink_socket_type_t` 값을 사용한다. 구현 내부의
`ZLINK_CORE_SOCKET_*` 값은 그대로 노출하지 않는다. 구현은
`socket_base_t::socket_type()`으로 얻은 내부 값을 공개 `ZLINK_SOCKET_*` 값으로
변환해서 기록한다.

`auto_hwm_visible`은 전체 내부 소켓 snapshot과 perf Auto-HWM 기본 표를 분리하기
위한 값이다. 내부 제어용 PAIR 소켓이나 auto-HWM에서 산정한 HWM/buffer 값을 쓰지
않는 보조 소켓은 snapshot row로는 반환할 수 있지만, perf의 기본
`Auto-HWM spotnode`와 `Auto-HWM spot` 표에는 표시하지 않는다.

기본값은 아래 원칙으로 정한다.

- auto-HWM에서 산정한 HWM 또는 buffer 값이 적용된 내부 소켓이면
  `auto_hwm_visible == 1`이다.
- `snapshot.auto_hwm_enabled != 0`이면 `auto_hwm_visible == 1`이다.
- `snapshot.auto_hwm_role != 0`이면 `auto_hwm_visible == 1`이다.
- 위 조건에 모두 해당하지 않으면 `auto_hwm_visible == 0`이다.

SPOT 내부 runtime은 일부 소켓에서 일반 socket auto-HWM policy flag를 끄고,
별도 계산 결과를 HWM과 buffer에 직접 적용할 수 있다. 이 경우에도 그 값이
auto-HWM 산정 결과라면 `auto_hwm_visible == 1`이어야 한다.

디버그나 내부 검증 도구는 `auto_hwm_visible == 0`인 row도 볼 수 있다. perf의
기본 표는 `auto_hwm_visible == 1`인 row만 사용한다.

문자열 필드인 `owner_name`과 `socket_name`은 항상 NUL 종료해야 한다. 이름이
배열 크기보다 길면 실패하거나, 구현에서 정한 짧은 안정 이름을 사용해야 한다.
조용히 잘린 이름은 서로 다른 소켓을 같은 이름처럼 보이게 만들 수 있으므로
허용하지 않는다.

조회 예시는 아래와 같다.

```c
size_t count = 0;
zlink_config_result_t rc =
    zlink_spot_node_internal_sockets_snapshot(node, NULL, NULL, &count);
if (rc != ZLINK_CONFIG_OK) {
    return -1;
}

zlink_spot_node_socket_snapshot_entry_t *rows =
    calloc(count, sizeof(*rows));
if (!rows) {
    errno = ENOMEM;
    return -1;
}

size_t capacity = count;
rc = zlink_spot_node_internal_sockets_snapshot(
    node, NULL, rows, &capacity);
```

router 계열 내부 소켓만 확인하려면 아래처럼 조회한다.

```c
zlink_spot_node_socket_snapshot_filter_t filter;
memset(&filter, 0, sizeof(filter));
filter.socket_type = ZLINK_SOCKET_ROUTER;

size_t count = 0;
zlink_config_result_t rc =
    zlink_spot_node_internal_sockets_snapshot(
        node, &filter, NULL, &count);
```

#### 2.1.2 반환해야 하는 소켓 범위

이 API는 두 층의 소켓을 함께 다룬다.

첫 번째는 `SpotNode` runtime이 직접 소유한 node 소유 소켓이다. 이 row는
`owner == ZLINK_SPOT_NODE_SOCKET_OWNER_NODE`, `owner_id == 0`,
`owner_name == "spotnode"`를 사용한다.

두 번째는 `zlink_spot_new(node)`로 만든 `Spot` handle이 이미 생성한 pub/sub
facade 소켓이다. 이 row는 `owner == ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT`를 사용한다.
`Spot` handle이 아직 pub 또는 sub facade를 만들지 않았다면 snapshot 호출만으로
새 facade를 만들지 않는다.

아래 이름은 구현에서 안정적으로 유지해야 하는 기본 `socket_name`이다.

| Owner | 내부 대상 | `socket_name` | `socket_type` | 설명 |
|-------|-----------|---------------|---------------|------|
| node | `spot_runtime_t::mesh_pub` | `mesh_pub` | `ZLINK_SOCKET_PUB` | peer mesh로 data/control publish |
| node | `spot_runtime_t::mesh_xsub` | `mesh_xsub` | `ZLINK_SOCKET_XSUB` | peer mesh에서 data/control subscribe |
| node | `spot_runtime_t::peer_ctrl_pub` | `peer_ctrl_pub` | `ZLINK_SOCKET_PUB` | peer control publish |
| node | `spot_runtime_t::peer_ctrl_sub` | `peer_ctrl_sub` | `ZLINK_SOCKET_SUB` | peer control receive |
| node | `spot_runtime_t::route_ingress` | `route_ingress` | `ZLINK_SOCKET_ROUTER` | local spot routed ingress |
| node | `spot_runtime_t::peer_route_ingress` | `peer_route_ingress` | `ZLINK_SOCKET_ROUTER` | peer routed ingress |
| node | `spot_runtime_t::node_router` | `node_router` | `ZLINK_SOCKET_ROUTER` | node-level routed router |
| node | `spot_runtime_t::route_ingress_tx` | `route_ingress_tx` | `ZLINK_SOCKET_DEALER` | lazy sender to route ingress |
| node | `spot_runtime_t::node_router_tx` | `node_router_tx` | `ZLINK_SOCKET_DEALER` | lazy sender to node router |
| node | `spot_runtime_t::peer_route_tx` | `peer_route_tx` | `ZLINK_SOCKET_DEALER` | lazy sender to peer route |
| node | `spot_runtime_t::local_pub_ingress_sub` | `local_pub_ingress` | `ZLINK_SOCKET_SUB` | local publish ingress |
| node | `spot_runtime_t::local_fanout_xpub` | `local_fanout` | `ZLINK_SOCKET_PUB` | local subscriber fanout |
| node | node default pub facade | `default_pub` | `ZLINK_SOCKET_PUB` | node handle publish facade, 이미 생성된 경우만 |
| node | node internal receiver | `internal_receiver` | `ZLINK_SOCKET_SUB` | node handle receive facade, 이미 생성된 경우만 |
| spot | spot pub facade | `pub` | `ZLINK_SOCKET_PUB` | 개별 `Spot` publish facade, 이미 생성된 경우만 |
| spot | spot sub facade | `sub` | `ZLINK_SOCKET_SUB` | 개별 `Spot` receive facade, 이미 생성된 경우만 |

`data_ctrl_front`와 `data_ctrl_back` 같은 runtime 제어용 PAIR 소켓은 기본
Auto-HWM 출력 대상이 아니다. 구현자가 내부 검증 목적으로 반환해야 한다고 판단하면
snapshot row에는 포함할 수 있지만 `auto_hwm_visible == 0`이어야 한다.

lazy sender인 `route_ingress_tx`, `node_router_tx`, `peer_route_tx`는 실제로 생성된
경우에만 반환한다. routed 기능을 사용하지 않아 아직 sender cache가 없다면 snapshot
호출만으로 만들지 않는다.

#### 2.1.3 구현 절차

구현은 아래 순서를 따른다.

1. public API guard를 잡고 `SpotNode` handle 유효성을 확인한다.
2. node lock과 runtime lock을 짧게 잡아 현재 존재하는 내부 소켓 포인터와 이름을
   snapshot 후보 목록에 복사한다.
3. 등록된 `Spot` facade 목록도 같은 방식으로 복사하되, 이미 존재하는 `pub`와
   `sub` 포인터만 포함한다.
4. 포인터 lifetime이 보장되는 범위 안에서 각 소켓의 snapshot을 채운다. 단순히
   raw pointer만 복사한 뒤 owner lock을 풀고 접근하면 안 된다.
5. 필터 조건을 적용하고 two-pass 규칙에 맞게 `entries`와 `count`를 처리한다.

이 절차의 핵심은 조회가 관찰 동작이어야 한다는 점이다. 조회 중에
`ensure_default_pub()`, `ensure_internal_receiver()`, `ensure_spot_pub()`,
`ensure_spot_sub()`, sender cache 생성 함수를 호출하면 안 된다.

구현은 둘 중 하나의 방식을 선택한다.

- owner lock을 유지한 상태에서 필요한 snapshot 값을 채운다.
- lock 밖에서 채우려면 소켓과 facade lifetime을 보장하는 내부 pin 또는 refcount를
  먼저 추가한다.

현재 구조에서 lifetime pin이 없다면 첫 번째 방식이 구현 기준이다. 이때 lock을
잡은 상태에서 새 소켓을 만들거나 blocking wait를 수행하면 안 된다.

### 2.2 기본값

`options == NULL`이면 기존 동작과 같은 `ZLINK_SPOT_NODE_MODE_ALL`로 처리한다.

`options->mode == 0`도 `ZLINK_SPOT_NODE_MODE_ALL`로 처리한다. 구조체를 0으로
초기화한 사용자가 기존 전체 기능을 얻도록 하기 위해서다.

### 2.3 유효하지 않은 값

`mode`가 아래 값 중 하나가 아니면 생성은 실패한다.

- `ZLINK_SPOT_NODE_MODE_PUBSUB`
- `ZLINK_SPOT_NODE_MODE_ROUTED`
- `ZLINK_SPOT_NODE_MODE_ALL`

실패 시 `zlink_spot_node_new()`는 `NULL`을 반환하고 `errno`는 `EINVAL`로 둔다.

## 3. Mode 의미

### 3.1 `ZLINK_SPOT_NODE_MODE_PUBSUB`

pub/sub 기능만 사용하는 node다.

이 mode에서는 topic publish/subscribe에 필요한 내부 runtime만 만든다. routed
request/reply를 위한 router 계열 내부 소켓은 만들지 않는다.

허용되는 대표 동작은 아래와 같다.

- `zlink_publish()`
- `zlink_recv()` 또는 handler 기반 topic 수신
- subscription 설정
- pub/sub용 discovery/peer 연결

금지되는 대표 동작은 아래와 같다.

- `zlink_spot_request_spot()`
- `zlink_spot_request_router()`
- `zlink_spot_reply_spot()`
- `zlink_spot_reply_router()`
- routed recv 전용 경로

금지된 routed 동작을 호출하면 `ENOTSUP`으로 실패한다.

### 3.2 `ZLINK_SPOT_NODE_MODE_ROUTED`

routed request/reply 기능만 사용하는 node다.

이 mode에서는 routed request/reply에 필요한 router 계열 내부 runtime만 만든다.
topic pub/sub fanout/ingress에 필요한 내부 소켓은 만들지 않는다.

허용되는 대표 동작은 아래와 같다.

- `zlink_spot_request_spot()`
- `zlink_spot_request_router()`
- `zlink_spot_reply_spot()`
- `zlink_spot_reply_router()`
- routed recv 전용 경로

금지되는 대표 동작은 아래와 같다.

- topic `zlink_publish()`
- topic subscription 설정
- topic pub/sub handler 설치

금지된 pub/sub 동작을 호출하면 `ENOTSUP`으로 실패한다.

### 3.3 `ZLINK_SPOT_NODE_MODE_ALL`

기존 전체 기능을 모두 켠 node다.

이 mode에서는 pub/sub와 routed request/reply 내부 runtime을 모두 만든다. 기능을
명시하지 않은 사용자는 이 mode를 얻게 된다.

## 4. Spot Handle 상속 규칙

`zlink_spot_new(node)`는 별도 옵션을 받지 않는다.

새 `Spot` handle은 자신이 붙은 `SpotNode`의 mode를 그대로 따른다. 즉 `Spot`
생성 시점에 새 mode를 선택하거나 node mode와 다른 기능을 켤 수 없다.

```c
zlink_spot_node_options_t opts = {
    .mode = ZLINK_SPOT_NODE_MODE_PUBSUB
};

void *node = zlink_spot_node_new(ctx, &opts);
void *spot = zlink_spot_new(node);
```

위 예시에서 `spot`은 pub/sub 전용이다. routed API를 호출하면 `ENOTSUP`으로
실패한다.

이 규칙은 `Spot`을 많이 만들 때 비용을 예측 가능하게 만든다. node mode가
`PUBSUB`이면 각 `Spot`도 routed state를 준비하지 않는다. node mode가 `ROUTED`이면
각 `Spot`도 pub/sub facade와 subscription state를 준비하지 않는다.

## 5. 내부 Runtime 생성 범위

이 절은 구현자가 어떤 내부 소켓을 mode별로 만들지 판단하기 위한 기준이다.
정확한 내부 이름은 구현 과정에서 정리하되, perf와 internals 문서에서는 실제
생성된 소켓 이름만 노출한다.

### 5.1 Pub/Sub 내부 범위

`PUBSUB` mode에서 필요한 내부 소켓은 topic fanout과 topic ingress에 필요한
소켓이다.

대표 내부 범위는 아래와 같다.

- mesh publish 계열
- mesh subscribe/ingress 계열
- peer control pub/sub 중 pub/sub 연결 관리에 필요한 부분
- local topic fanout
- local topic ingress

이 mode에서는 routed request/reply를 위한 router 계열 소켓을 만들지 않는다.

### 5.2 Routed 내부 범위

`ROUTED` mode에서 필요한 내부 소켓은 spot-to-spot, spot-to-router routed
request/reply를 처리하는 소켓이다.

대표 내부 범위는 아래와 같다.

- route ingress
- peer route ingress
- node router
- routed sender cache
- routed request/reply 상태
- routed peer control에 필요한 최소 control 경로

이 mode에서는 topic pub/sub fanout/ingress 소켓을 만들지 않는다.

### 5.3 All 내부 범위

`ALL` mode는 `PUBSUB`과 `ROUTED`의 내부 범위를 모두 포함한다.

## 6. Auto-HWM 영향

Auto-HWM은 실제 생성된 내부 소켓만 계산 대상으로 삼는다.

예를 들어 `PUBSUB` mode에서는 routed/router 내부 소켓이 없으므로 routed role
budget과 router HWM 행을 만들지 않는다. 반대로 `ROUTED` mode에서는 topic
pub/sub fanout/ingress가 없으므로 pub/sub role budget과 관련 HWM 행을 만들지
않는다.

이 원칙은 perf와 monitor 해석을 단순하게 만든다.

- 생성되지 않은 기능의 소켓은 내부 소켓 snapshot과 perf 출력에 나오지 않는다.
- 생성되지 않은 기능의 HWM은 계산하지 않는다.
- 생성되지 않은 기능의 buffer 비용은 auto-HWM accounting에 들어가지 않는다.

## 7. Monitor와 Perf 출력

기존 perf 출력은 `spotnode_data_pub`, `spotend_data_pub`,
`spotnode_control_pub`, `spotnode_control_sub` 같은 perf label을 중심으로
표시했다. 이 방식은 실제 내부 router 소켓이 있는지 확인하기 어렵다.

SpotNode mode가 들어가면 perf 출력은 실제 생성된 내부 소켓을 기준으로 정리해야
한다.

perf는 `zlink_spot_node_internal_sockets_snapshot()` 결과를 사용한다. perf label을
소켓 이름처럼 재해석하지 않고, snapshot row의 `socket_name`, `socket_type`,
`snapshot.auto_hwm_role` 값을 그대로 표시한다.

`Transport`와 `Size(B)`는 snapshot API가 아니라 perf 실행 컨텍스트에서 붙인다.
`MsgUnit(B)`는 `entry.snapshot.auto_hwm_effective_message_bytes`를 사용한다.
`Role`, `Managed`, `Active`, `SNDHWM`, `RCVHWM`, `SNDBUF`, `RCVBUF`는 각각
`entry.snapshot`의 auto-HWM 필드에서 얻는다.

### 7.1 기본 출력 원칙

- `Auto-HWM budget` 표는 기본 출력에서 제외한다.
- `Scope`와 `Source`는 기본 socket 표에서 제외한다.
- `Size(B)`와 `MsgUnit(B)`를 반드시 포함한다.
- 실제 생성된 내부 소켓 중 `auto_hwm_visible == 1`인 row만 기본 출력에 표시한다.
- `Type`은 `socket_type`에서 얻은 실제 socket type을 표시한다.
- `Auto-HWM spotnode` 표는 `owner == NODE` row만 표시한다.
- `Auto-HWM spot` 표는 `owner == SPOT` row만 표시한다.
- 어떤 표든 표시할 row가 없으면 표 자체를 생략한다.
- 행 정렬은 `Size(B) -> OwnerId -> Socket -> Role` 순서로 한다.

출력 컬럼은 아래 필드에서 만든다.

| 출력 컬럼 | source |
|-----------|--------|
| `Transport` | perf 실행 transport |
| `Size(B)` | perf 현재 message size |
| `MsgUnit(B)` | `entry.snapshot.auto_hwm_effective_message_bytes` |
| `Socket` | `entry.socket_name` |
| `Type` | `entry.socket_type`을 사람이 읽는 이름으로 변환 |
| `Role` | `entry.snapshot.auto_hwm_role`을 사람이 읽는 이름으로 변환 |
| `Managed` | `entry.snapshot.auto_hwm_managed_connections` |
| `Active` | `entry.snapshot.auto_hwm_active_hwm_connections` |
| `SNDHWM` | `entry.snapshot.auto_hwm_applied_sndhwm` |
| `RCVHWM` | `entry.snapshot.auto_hwm_applied_rcvhwm` |
| `SNDBUF` | `entry.snapshot.auto_hwm_effective_sndbuf` |
| `RCVBUF` | `entry.snapshot.auto_hwm_effective_rcvbuf` |

### 7.2 예시: Pub/Sub mode

아래 예시는 형식 예시다. 실제 socket 이름은 구현에서 확정한 내부 runtime 이름을
그대로 사용한다.

```text
Auto-HWM spotnode:
  | Transport | Size(B) | MsgUnit(B) | Socket            | Type | Role         | Managed | Active | SNDHWM | RCVHWM | SNDBUF | RCVBUF |
  |-----------|---------|------------|-------------------|------|--------------|---------|--------|--------|--------|--------|--------|
  | tcp       | 64      | 64         | mesh_pub          | pub  | fanout       | 1       | 1      | 469811 | 469811 | 262144 | 262144 |
  | tcp       | 64      | 64         | mesh_xsub         | xsub | recv_ingress | 1       | 1      | 125282 | 125282 | 262144 | 262144 |
  | tcp       | 64      | 64         | peer_ctrl_pub     | pub  | control      | 1       | 1      | 313207 | 313207 | 262144 | 262144 |
  | tcp       | 64      | 64         | peer_ctrl_sub     | sub  | control      | 1       | 1      | 313207 | 313207 | 262144 | 262144 |
  | tcp       | 64      | 64         | local_pub_ingress | sub  | recv_ingress | 1       | 1      | 125282 | 125282 | 262144 | 262144 |
  | tcp       | 64      | 64         | local_fanout      | pub  | fanout       | 1       | 1      | 469811 | 469811 | 262144 | 262144 |

Auto-HWM spot:
  | Transport | Size(B) | MsgUnit(B) | Socket | Type | Role         | Managed | Active | SNDHWM | RCVHWM | SNDBUF | RCVBUF |
  |-----------|---------|------------|--------|------|--------------|---------|--------|--------|--------|--------|--------|
  | tcp       | 64      | 64         | pub    | pub  | fanout       | 1       | 1      | 469811 | 469811 | 262144 | 262144 |
  | tcp       | 64      | 64         | sub    | sub  | recv_ingress | 1       | 1      | 125282 | 125282 | 262144 | 262144 |
```

### 7.3 예시: Routed mode

```text
Auto-HWM spotnode:
  | Transport | Size(B) | MsgUnit(B) | Socket             | Type   | Role   | Managed | Active | SNDHWM | RCVHWM | SNDBUF | RCVBUF |
  |-----------|---------|------------|--------------------|--------|--------|---------|--------|--------|--------|--------|--------|
  | tcp       | 64      | 64         | peer_ctrl_pub      | pub    | control | 1       | 1      | 313207 | 313207 | 262144 | 262144 |
  | tcp       | 64      | 64         | peer_ctrl_sub      | sub    | control | 1       | 1      | 313207 | 313207 | 262144 | 262144 |
  | tcp       | 64      | 64         | route_ingress      | router | routed | 1       | 1      | 469811 | 469811 | 262144 | 262144 |
  | tcp       | 64      | 64         | peer_route_ingress | router | routed | 1       | 1      | 469811 | 469811 | 262144 | 262144 |
  | tcp       | 64      | 64         | node_router        | router | routed | 1       | 1      | 469811 | 469811 | 262144 | 262144 |
```

`ROUTED` mode에서 개별 `Spot`이 별도 socket을 만들지 않으면 `Auto-HWM spot` 표는
나오지 않는다. routed request/reply용 router socket은 SpotNode runtime 소유로
`Auto-HWM spotnode` 표에만 나온다.

### 7.4 예시: All mode

`ALL` mode는 Pub/Sub mode와 Routed mode의 행을 모두 포함한다.

## 8. 오류 정책

mode가 꺼진 기능을 호출하면 아래 원칙을 따른다.

| 상황 | 결과 |
|------|------|
| `PUBSUB` node에서 routed API 호출 | 실패, `errno=ENOTSUP` |
| `ROUTED` node에서 pub/sub API 호출 | 실패, `errno=ENOTSUP` |
| 잘못된 mode로 node 생성 | 실패, `errno=EINVAL` |
| `ctx == NULL` | 실패, 기존 null handle 정책을 따른다 |
| snapshot `node == NULL` | 실패, `errno=EFAULT` |
| snapshot `count == NULL` | 실패, `errno=EINVAL` |
| snapshot `entries == NULL` | 성공, 필요한 row 개수를 `*count`에 기록 |
| snapshot `*count` 부족 | 실패, `errno=ENOBUFS`, 필요한 row 개수를 `*count`에 기록 |
| snapshot invalid `owner` | 실패, `errno=EINVAL` |
| snapshot invalid `socket_type` | 실패, `errno=EINVAL` |
| 내부 `socket_name` 또는 `owner_name`이 64바이트에 들어가지 않음 | 실패, `errno=ENAMETOOLONG` |
| filter `socket_name`이 NUL 종료되지 않음 | 실패, `errno=EINVAL` |

disabled 기능은 내부 소켓을 늦게 만들면서 자동으로 켜지지 않는다. mode는 node 생성
시점에 고정된다.

snapshot의 `socket_type`은 `ZLINK_SOCKET_ANY` 또는 공개 `ZLINK_SOCKET_*` 값만
허용한다. 내부
`ZLINK_CORE_SOCKET_*` 값을 filter로 넣으면 `EINVAL`로 실패한다.

## 9. 무인 구현 순서

1. `core/include/zlink_enum.h`에 `zlink_spot_node_mode_t`와
   `zlink_spot_node_socket_owner_t`를 추가하고, `core/include/zlink.h`에
   `zlink_spot_node_options_t`, snapshot filter/entry 구조체,
   새 `zlink_spot_node_new(ctx, options)` 시그니처와
   `zlink_spot_node_internal_sockets_snapshot()` 시그니처를 추가한다.
2. core의 `spot_node_t`에 mode 저장 필드를 추가한다.
3. `SpotNode` 생성 경로에서 mode를 검증하고 저장한다.
4. pub/sub runtime 초기화와 routed runtime 초기화를 mode별로 분리한다.
5. `Spot` 생성 경로가 node mode를 상속하도록 한다.
6. disabled 기능 호출 지점에서 `ENOTSUP`을 반환한다.
7. auto-HWM accounting이 실제 생성된 내부 소켓만 계산하도록 정리한다.
8. `zlink_spot_node_internal_sockets_snapshot()`을 추가해서 실제 내부 소켓 이름과
   type을 별도 row API로 노출한다.
9. 기존 monitor snapshot은 단일 subject snapshot 역할을 유지하고, SpotNode 내부
   소켓 목록 조회에는 새 row API를 사용한다.
10. perf 출력에서 `Auto-HWM budget` 기본 표를 숨기고, mode별 실제 소켓 표를
   출력한다.
11. C/C++/Go/Rust/Python/Node/.NET/Java 바인딩 시그니처와 문서를 모두 맞춘다.

각 단계의 완료 조건은 아래와 같다.

| 단계 | 완료 조건 |
|------|-----------|
| 1-3 | 공개 헤더와 생성 경로가 빌드되고 invalid mode 테스트가 통과한다 |
| 4-6 | mode별 disabled 기능이 `ENOTSUP`으로 실패하고 켜진 기능은 기존 테스트를 통과한다 |
| 7-9 | 내부 소켓 snapshot row와 auto-HWM accounting 테스트가 통과한다 |
| 10 | `bindings/c/perf` smoke에서 `Auto-HWM spotnode`/`Auto-HWM spot` 출력이 계약과 맞는다 |
| 11 | 모든 binding 문서와 최소 회귀 테스트가 core C API와 같은 의미를 가진다 |

`core/include/` 또는 `core/src/`가 바뀐 단계는 다음 단계로 넘어가기 전에
`cmake --build core/build`를 반드시 실행한다.

## 10. 테스트 계획

### 10.1 Core 생성 테스트

- `options == NULL`이면 `ALL` mode로 생성되는지 확인한다.
- `mode == 0`이면 `ALL` mode로 생성되는지 확인한다.
- 잘못된 mode는 실패하고 `EINVAL`을 반환하는지 확인한다.

### 10.2 Mode별 기능 테스트

- `PUBSUB` mode에서 publish/subscribe가 동작하는지 확인한다.
- `PUBSUB` mode에서 routed request/reply API가 `ENOTSUP`으로 실패하는지 확인한다.
- `ROUTED` mode에서 routed request/reply가 동작하는지 확인한다.
- `ROUTED` mode에서 topic publish/subscribe API가 `ENOTSUP`으로 실패하는지
  확인한다.
- `ALL` mode에서 기존 SPOT pub/sub와 routed 테스트가 모두 통과하는지 확인한다.

### 10.3 자원 생성 테스트

- `PUBSUB` mode에서 router 계열 내부 소켓이 생성되지 않는지 확인한다.
- `ROUTED` mode에서 topic pub/sub 계열 내부 소켓이 생성되지 않는지 확인한다.
- `ALL` mode에서 기존 전체 내부 소켓이 생성되는지 확인한다.

### 10.4 Auto-HWM/Monitor 테스트

- mode별로 생성된 내부 소켓만 내부 소켓 snapshot에 나타나는지 확인한다.
- `zlink_spot_node_internal_sockets_snapshot()`이 내부 소켓을 새로 만들지 않고
  이미 생성된 소켓만 반환하는지 확인한다.
- snapshot row의 `socket_type`이 내부 `ZLINK_CORE_SOCKET_*` 값이 아니라 공개
  `ZLINK_SOCKET_*` 값인지 확인한다.
- `auto_hwm_visible == 0`인 제어용 소켓은 기본 perf Auto-HWM 표에 나오지 않는지
  확인한다.
- 내부 소켓 snapshot row가 `socket_name`, `socket_type`, owner, 포함된
  `zlink_monitor_snapshot_t` 값을 함께 반환하는지 확인한다.
- `PUBSUB` mode snapshot에는 routed/router 소켓이 없는지 확인한다.
- `ROUTED` mode snapshot에는 topic pub/sub fanout/ingress 소켓이 없는지 확인한다.
- `ROUTED` mode에서 개별 `Spot`이 별도 socket을 만들지 않으면
  `Auto-HWM spot` 표가 생략되는지 확인한다.
- `MsgUnit(B)`, `SNDHWM`, `RCVHWM`, buffer 값이 mode별 실제 소켓 기준으로
  출력되는지 확인한다.
- perf 출력에서 `Size(B)`와 `MsgUnit(B)`가 size별 실제 snapshot 기준으로
  정렬되는지 확인한다.

### 10.5 Scale 테스트

- `PUBSUB` mode에서 많은 `Spot`을 만들 때 routed state가 생성되지 않는지
  확인한다.
- `ROUTED` mode에서 많은 `Spot`을 만들 때 pub/sub facade와 subscription state가
  생성되지 않는지 확인한다.
- 기존 `10,000 Spot` bench를 mode별로 나누어 메모리와 생성 시간을 비교한다.

### 10.6 회귀 테스트 추가 계획

core 회귀 테스트는 기능 동작, 자원 생성 여부, snapshot 계약을 분리해서 추가한다.
한 테스트가 여러 실패 원인을 동시에 가리키지 않게 작게 나눈다.

| 영역 | 추가할 테스트 |
|------|---------------|
| 생성 옵션 | `NULL`, zero-init, `PUBSUB`, `ROUTED`, `ALL`, invalid mode |
| mode 제한 | 꺼진 기능 호출 시 `ENOTSUP`, 켜진 기능은 기존처럼 성공 |
| pub/sub | `PUBSUB`와 `ALL`에서 publish/subscribe 기존 회귀 통과 |
| routed | `ROUTED`와 `ALL`에서 spot-to-spot, spot-to-router request/reply 기존 회귀 통과 |
| 내부 소켓 생성 | `PUBSUB`에는 router 계열 row 없음, `ROUTED`에는 topic fanout/ingress row 없음 |
| snapshot two-pass | `entries == NULL`, 부족한 count, 충분한 count, `count == NULL` 오류 |
| snapshot filter | owner, socket type, socket name 필터 |
| snapshot side effect | snapshot 호출 전후 내부 소켓 수가 변하지 않음 |
| perf 출력 | `Auto-HWM spotnode`, `Auto-HWM spot`, 빈 표 생략, size별 정렬 |
| scale | mode별 `10,000 Spot` 생성 시 메모리와 생성 시간 비교 |

bindings 회귀 테스트는 C API가 기준이다. 다른 언어 binding은 C 테스트가 통과한 뒤
각 언어의 얇은 wrapper 계약을 확인한다.

- C binding은 새 `zlink_spot_node_new(ctx, options)` 시그니처와 내부 소켓 snapshot
  API를 직접 테스트한다.
- C++ binding은 RAII wrapper가 options와 snapshot row 배열을 안전하게 감싸는지
  확인한다.
- Go/Rust/Python/Node/.NET/Java binding은 mode enum, options 구조체, snapshot row를
  언어별 idiom에 맞게 노출하는지 확인한다.
- 모든 binding에서 invalid mode, disabled 기능 `ENOTSUP`, two-pass snapshot
  오류 처리를 확인한다.

## 11. 문서 업데이트 계획

구현 전에는 이 draft만 갱신한다. 구현이 끝난 뒤에는 공개 헤더와 테스트 결과를
기준으로 정식 문서에 나누어 반영한다.

| 문서 위치 | 반영 내용 |
|-----------|-----------|
| `doc/spec/` | 공개 API 계약, enum/struct/function, 반환값, errno, two-pass snapshot 규칙 |
| `doc/guide/` | 사용자가 어떤 mode를 골라야 하는지, 많은 `Spot`을 만들 때의 선택 기준 |
| `doc/internals/` | mode별 내부 소켓 생성 범위, snapshot row 생성 방식, auto-HWM accounting |
| `doc/perf/` | `Auto-HWM spotnode`, `Auto-HWM spot` 출력 의미와 해석 방법 |
| `bindings/c/perf/README.md` | perf 실행 시 runtime 경로 확인, 새 출력 표, size별 MsgUnit 기준 |
| 루트 `README` | 공개 사용 예시가 있으면 새 생성 옵션과 기존 예시 갱신 |

정식 spec 문서는 `core/include/zlink.h`를 기준으로 작성한다. 구현에 없는 동작이나
테스트되지 않은 동작은 정식 spec에 넣지 않는다.

## 12. Bindings Spec 업데이트 계획

binding 문서는 core C API가 확정된 뒤 같은 의미로 맞춘다. 각 binding 문서에는
언어별 이름이 달라도 아래 계약이 반드시 들어가야 한다.

- SpotNode 생성 options와 mode enum
- `PUBSUB`, `ROUTED`, `ALL`의 의미
- disabled 기능 호출 시 `ENOTSUP` 또는 언어별 등가 오류
- 내부 소켓 snapshot row 구조
- snapshot two-pass 조회 방식 또는 언어별 배열 반환 방식
- snapshot 호출이 새 내부 소켓을 만들지 않는다는 규칙
- `socket_type`이 공개 socket type이라는 규칙
- `auto_hwm_visible`과 perf 기본 출력의 관계

언어별 반영 순서는 아래와 같다.

1. C binding 문서와 예제를 먼저 갱신한다.
2. C++ binding은 C API와 enum 값이 일치하는지 확인한 뒤 wrapper 문서를 갱신한다.
3. Go/Rust/Python/Node/.NET/Java binding은 생성 options, mode enum, snapshot row를
   각 언어의 타입 시스템에 맞게 추가한다.
4. 각 binding의 README, API reference, 예제를 같은 용어로 맞춘다.
5. binding별 smoke test와 최소 회귀 테스트를 문서에 적은 사용법 그대로 실행한다.

## 13. 배포 후 Bindings 적용 계획

core 배포 직후 모든 binding을 한 번에 바꾸지 않는다. C ABI와 core 동작이 안정된
것을 확인한 뒤 순서대로 적용한다.

1. core release 후보에서 `core/build` 전체 빌드와 core 회귀 테스트를 통과시킨다.
2. C binding과 `bindings/c/perf`를 먼저 갱신하고 smoke를 통과시킨다.
3. perf runner가 출력하는 `libzlink.so` 경로가 `core/build` 아래인지 확인한다.
4. C++ binding을 갱신하고 wrapper compile/runtime test를 통과시킨다.
5. Go/Rust/Python/Node/.NET/Java binding을 순서대로 갱신한다.
6. 각 binding에서 새 mode options와 snapshot API를 feature 문서와 예제에 반영한다.
7. 모든 binding 결과를 모아 정식 spec/guide/internals/perf 문서의 미반영 항목을
   다시 점검한다.
8. binding별 릴리스 노트에는 기존 생성 함수 시그니처 변경, mode 기본값, disabled
   기능 오류, snapshot API 추가를 명시한다.

배포 후 binding 적용 중 core 공개 계약을 바꿔야 하는 문제가 발견되면 binding에서
우회하지 않는다. draft와 core API를 먼저 수정하고, C binding부터 다시 검증한다.

## 14. 완료 기준

- `zlink_spot_node_new(ctx, options)` 시그니처가 공개 헤더와 모든 바인딩에
  반영된다.
- `PUBSUB`, `ROUTED`, `ALL` mode가 모두 동작한다.
- disabled 기능은 암묵적으로 내부 소켓을 만들지 않고 `ENOTSUP`으로 실패한다.
- `Spot` handle은 node mode를 그대로 상속한다.
- `zlink_spot_node_internal_sockets_snapshot()`이 실제 생성된 내부 소켓 이름,
  실제 socket type, owner, auto-HWM snapshot을 반환한다.
- 내부 소켓 snapshot 호출은 생성되지 않은 기능의 소켓을 암묵적으로 만들지
  않는다.
- auto-HWM accounting과 monitor/perf 출력은 실제 생성된 내부 소켓만 보여준다.
- 정식 spec, guide, internals, perf 문서가 mode 의미에 맞게 갱신된다.
- 관련 core 회귀 테스트와 bindings 테스트가 통과한다.

## 15. 구현 가능성 리뷰 체크리스트

구현 전에 아래 항목을 다시 확인한다. 하나라도 어긋나면 문서나 구현을 먼저
수정한다.

- `Auto-HWM spotnode`와 `Auto-HWM spot` 표는 snapshot에 없는 row를 만들어내지
  않는다.
- `Auto-HWM spotnode` 표는 `owner == NODE` row에서만 만든다.
- `Auto-HWM spot` 표는 `owner == SPOT` row에서만 만든다.
- routed request/reply용 router 소켓은 SpotNode runtime 소유다. 개별 `Spot`에
  router socket이 없으면 `Auto-HWM spot`에 router row를 만들지 않는다.
- `Transport`와 `Size(B)`는 perf가 붙이고, `MsgUnit(B)`와 HWM/buffer 값은
  snapshot row에서 읽는다.
- `socket_type`은 공개 `ZLINK_SOCKET_*` 값이다. 내부 `ZLINK_CORE_SOCKET_*` 값을
  ABI로 노출하지 않는다.
- snapshot API는 관찰용이다. 조회 중에 default pub/sub, internal receiver,
  sender cache, request/reply state를 새로 만들지 않는다.
- lifetime pin이 없다면 owner lock을 풀고 복사한 raw socket pointer를 사용하지
  않는다.
- 기본 perf Auto-HWM 표는 `auto_hwm_visible == 1`인 row만 표시한다.
- row가 없는 표는 출력하지 않는다. 빈 `Auto-HWM spot` 표를 만들지 않는다.
