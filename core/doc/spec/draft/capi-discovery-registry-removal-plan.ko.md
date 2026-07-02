# C API Discovery/Registry 제거 계획

> 이 문서는 구현 전 계획이다. 현재 공개 계약이 아니며, core C API에서 discovery/registry 공개 표면을
> 먼저 제거한 뒤 framework location runtime/store 전환 작업으로 상위 계층을 복구하기 위한 작업 순서를
> 정의한다.
>
> 이 작업은 배포 전 중간 단계에서 framework, bindings, sample, e2e가 일시적으로 깨질 수 있음을
> 전제로 한다. 배포 가능 상태는 core 제거, bindings 정리, framework location store 전환, 회귀 테스트가
> 모두 끝난 뒤에만 판단한다.

## 1. 목표

core C API에서 discovery와 registry를 공개 기능으로 제공하지 않는다. 자동 연결, peer list, spot 위치
조회, actor 위치 조회, route 위치 조회, topology/status 운영 조회는 framework의 location runtime/store
계약으로 옮긴다.

이번 제거 작업의 목표는 아래와 같다.

- `core/include/` 공개 header에서 discovery/registry API와 type을 제거한다.
- C binding과 C++ binding에 복사된 public header도 같은 상태로 맞춘다.
  않게 한다.
- core 내부 구현에서 public entrypoint로만 쓰던 discovery/registry runtime 코드를 제거하거나 내부 잔존
  코드로 격리한다.
- core 정식 spec/guide/internals 문서에서 discovery/registry를 현재 공개 계약처럼 설명하지 않게 한다.
- framework 쪽 복구 작업이 무엇을 location runtime/store로 대체해야 하는지 명확히 남긴다.

## 2. 비목표

- framework location runtime/store를 이 작업에서 완성하지 않는다.
- Redis location store extension을 이 작업에서 구현하지 않는다.
- 기존 discovery/registry API에 compatibility wrapper를 만들지 않는다.
- core socket, message, actor, spot 자체의 공개 API를 불필요하게 바꾸지 않는다.

## 3. 제거 원칙

이 작업은 호환성을 유지하지 않고 한 번에 제거한다. 따라서 public header에 남아 있는 선언, 언어별
binding wrapper, public spec 문서가 서로 다르게 말하면 안 된다.

원칙:

- 공개 C API에서 제거한 기능은 binding public API에서도 제거한다.
- 제거된 API를 framework가 계속 호출하면 compile failure가 나야 한다. runtime fallback으로 숨기지 않는다.
- sample이나 e2e를 통과시키기 위한 임시 registry/discovery shim은 만들지 않는다.
- core 내부에 남기는 코드는 public C API와 연결되지 않아야 한다. 내부 잔존 이유가 없으면 삭제한다.
- 배포 전에는 core 버전을 올리고, `core/vX.Y.Z` tag로 GitHub Actions release를 실행해야 한다.

## 4. 공개 C API 제거 범위

이 절의 목록은 제거 구현 전에 다시 `core/include`, `bindings/*/include`, 언어별 binding contract를 grep해
검증한다. 목록에 없는 discovery/registry 공개 symbol이 발견되면 같은 제거 범위에 포함한다.

### 4.1 top-level include

`core/include/zlink.h`에서 아래 include를 제거한다.

```c
#include <zlink/service/registry.h>
#include <zlink/service/discovery.h>
```

제거 후 `zlink.h`는 discovery/registry header를 transitively 노출하지 않는다.

### 4.2 discovery header

`core/include/zlink/service/discovery.h`의 public 선언 전체를 제거한다. 파일 자체는 삭제하거나, include
호환을 남기지 않는 방향이면 빈 deprecated header도 두지 않는다.

제거 대상 type, macro:

```c
zlink_route_kind_t
ZLINK_ROUTE_KIND_INVALID
ZLINK_ROUTE_KIND_ACTOR
ZLINK_ROUTE_KIND_SPOT_NAME
ZLINK_ROUTE_KIND_ACTOR_SESSION
zlink_spot_route_t
```

제거 대상 function:

```c
```

확인한다. actor location을 core C API로 제공하지 않는 방향이면 제거한다. actor runtime 자체에서 필요한
internal route 구조라면 public header에서 내리고 internal header로만 둔다.

### 4.3 registry header

`core/include/zlink/service/registry.h`의 public 선언 전체를 제거한다.

제거 대상 type:

```c
```

제거 대상 function:

```c
```

### 4.4 shared service model 제거/검토

`core/include/zlink/service_common.h`와 `core/include/zlink_enum.h`의 아래 type은 discovery/registry가 쓰던
운영 조회 모델이다. framework location runtime으로 옮기는 항목은 core public C API에서 제거한다.

```c
zlink_member_peer_entry_t
zlink_service_role_t
zlink_channel_role_t
ZLINK_CHANNEL_ROLE_*
zlink_service_kind_t
zlink_service_event_detail_mask_t
zlink_service_event_detail_flag_e
zlink_service_event_subject_kind_t
zlink_topology_source_t
zlink_topology_state_t
```

정리 기준:

- `zlink_member_peer_entry_t`는 discovery/registry member peer 조회 전용이면 제거한다.
- `zlink_service_kind_t`, `zlink_topology_source_t`, `zlink_topology_state_t`는 registry topology 조회 전용이면
  제거한다.
- `zlink_service_event_*`가 registry/discovery monitoring 전용이면 제거한다.
- `zlink_service_role_t`와 `zlink_channel_role_t`가 socket/channel option이나 framework binding에서 아직
  public contract로 쓰이면 core에서 직접 제거하지 말고 이름과 문서를 registry 의존 없이 정리한다.
  framework location runtime의 `Role` enum과 의미를 맞춘 뒤 상위 binding에서 새 모델로 대체한다.

### 4.5 enum 정리

`core/include/zlink_enum.h`에서 registry/discovery 전용 enum을 제거한다.

제거 대상:

```c
ZLINK_SERVICE_KIND_DISCOVERY
ZLINK_TOPOLOGY_SOURCE_DISCOVERY
ZLINK_TOPOLOGY_SOURCE_REGISTRY
```

검토 대상:

```c
ZLINK_SERVICE_ROLE_*
ZLINK_CHANNEL_ROLE_*
ZLINK_SERVICE_EVENT_DETAIL_*
ZLINK_SERVICE_EVENT_SUBJECT_*
```

위 값들은 registry/discovery만의 개념이면 제거한다. socket, spot, monitoring, framework location runtime
전환에서 계속 필요한 값이면 이름과 문서가 registry/discovery에 묶이지 않도록 정리한다.

### 4.6 socket/spot attach discovery 제거

discovery handle을 public socket/spot API에 붙이는 표면도 제거한다.

제거 대상:

```c
zlink_socket_attach_discovery
zlink_spot_node_attach_discovery
```

관련 주석도 바꾼다.

- `Discovery-attached raw service participants...`
- `Attach a Discovery instance...`

제거 후 manual connect/disconnect 제한, spot peer 연결, pub/sub 연결 정책은 discovery가 아니라 framework
location runtime 또는 명시적 socket lifecycle 기준으로 다시 설명한다.

### 4.7 C API 제거 체크리스트

구현자는 아래 목록을 public C API 제거 체크리스트로 사용한다.

| 영역 | 제거 대상 |
|------|-----------|
| top-level include | `zlink.h`의 `service/discovery.h`, `service/registry.h` include |
| discovery header | `zlink/service/discovery.h` 전체 |
| registry header | `zlink/service/registry.h` 전체 |
| discovery route kind | `zlink_route_kind_t`, `ZLINK_ROUTE_KIND_*` |
| service/topology model | `zlink_member_peer_entry_t`, `zlink_service_kind_t`, `zlink_topology_source_t`, `zlink_topology_state_t` |
| attach discovery | `zlink_socket_attach_discovery`, `zlink_spot_node_attach_discovery` |
| public comments | `Discovery-attached`, `Attach Discovery`, `registry topology`, `registry member peer` 설명 |

## 5. core 구현 제거 범위

### 5.1 public entrypoint 제거

아래 구현 파일의 public API entrypoint를 제거한다.

- `core/src/api/registry/service_registry_api.cpp`
- discovery public API가 들어 있는 `core/src/api/**` 파일
- socket/spot discovery attach public API 구현

public header에서 제거된 symbol이 shared library export table에 남지 않아야 한다.

### 5.2 runtime discovery/registry 코드 정리

아래 디렉터리와 관련 코드를 감사한다.

- `core/src/runtime/services/discovery/`
- `core/src/runtime/services/registry/`
- discovery/registry protocol type
- registry route rpc, topology query, service summary, member peer query
- socket/spot node의 discovery attachment state

정리 기준:

- public API에서만 호출되던 코드는 삭제한다.
- core 내부 기능에 실제로 필요한 코드가 있으면 internal 이름과 internal header로 옮기고 public 문서에서
  제외한다.
- framework location store 전환 후 필요 없어지는 자동 연결 정책 코드는 framework 문서에 이관되어야 하며
  core에 duplicate policy로 남기지 않는다.

### 5.3 build system 정리

소스 삭제 뒤 CMake/build 파일에서 discovery/registry source를 제거한다.

검토 대상:

- `core/CMakeLists.txt`
- binding native build가 참조하는 generated include/source 목록
- symbol export/version script가 있으면 discovery/registry symbol 제거

### 5.4 제거 뒤 사용하지 않는 core 코드 정리

public C API와 registry/discovery runtime을 제거한 뒤, 남은 파일과 helper가 실제로 쓰이는지 확인한다.
빌드에서 빠졌지만 소스 트리에 남은 파일은 후속 구현자가 잘못 다시 연결할 수 있으므로, 내부 기능에
필요하지 않으면 삭제한다.

정리 대상:

- discovery/registry 전용 protocol message, frame codec, route RPC helper
- registry topology, status, service summary, member peer query 전용 모델과 serializer
- discovery attach 상태를 보관하던 socket/spot node field와 lifecycle hook
- 제거된 테스트나 sample만 쓰던 fixture, fake runtime, test helper
- 더 이상 include되지 않는 internal header와 source file
- CMake/source list에서 빠졌지만 디렉터리에 남아 있는 discovery/registry 전용 파일

확인 방법:

- `rg "discovery|registry|attach_discovery|topology_source|member_peer" core/src core/include core/tests`로
  남은 참조를 확인한다.
- 남은 참조가 core의 현재 public 기능에 필요한지, framework location runtime으로 옮겨야 하는 정책인지
  구분한다.
- framework로 옮겨야 하는 정책이면 core에 임시 helper로 남기지 말고 후속 framework 작업 항목에 기록한다.
- 파일을 삭제했으면 `core/CMakeLists.txt`, test CMake, install/export 목록에서도 같이 제거한다.

### 5.5 POSD 기반 core 리팩토링 점검

삭제가 끝난 뒤 core 코드를 한 번 더 읽고, 제거 과정에서 생긴 얕은 모듈과 패스스루 코드를 정리한다.
목표는 “컴파일만 되는 상태”가 아니라 registry/discovery가 빠진 뒤에도 core 모듈 경계가 단순하게 남는
상태다.

점검 기준:

- 제거된 discovery/registry 개념을 감추기 위한 빈 adapter나 no-op shim을 남기지 않는다.
- 인자를 그대로 넘기기만 하는 패스스루 함수가 생기면 호출자를 직접 연결하거나 모듈 경계를 다시 정한다.
- `service`, `topology`, `route` 같은 이름이 더 이상 registry/discovery 의미를 갖지 않으면 더 구체적인
  이름으로 바꾸거나 삭제한다.
- socket, spot, actor의 공개 계약을 유지하기 위해 내부 조건문만 늘어난 경우, 조건을 하위 모듈로 숨길 수
  있는지 검토한다.
- 제거 뒤 남은 코드 주석이 과거 discovery/registry 동작을 설명하면 현재 동작 기준으로 다시 쓴다.
- 리팩토링은 새 public API를 만들기 위한 단계가 아니다. 필요한 새 계약은 framework location runtime/store
  draft에서 먼저 다룬다.

## 6. binding 정리 계획

### 6.1 C/C++ public header 복사본

아래 복사본은 `core/include`와 같은 제거 상태로 맞춘다.

- `bindings/c/include/zlink.h`
- `bindings/c/include/zlink/service/discovery.h`
- `bindings/c/include/zlink/service/registry.h`
- `bindings/c/include/zlink/service/spot.h`
- `bindings/c/include/zlink/socket/api.h`
- `bindings/c/include/zlink_enum.h`
- `bindings/c/include/zlink_errno.h`
- `bindings/cpp/include/zlink.h`
- `bindings/cpp/include/zlink/service/discovery.h`
- `bindings/cpp/include/zlink/service/registry.h`
- `bindings/cpp/include/zlink/service/spot.h`
- `bindings/cpp/include/zlink/socket/api.h`
- `bindings/cpp/include/zlink_enum.h`
- `bindings/cpp/include/zlink_errno.h`

C++ 전용 공개 헤더도 별도로 정리한다. C header 복사본만 정리하면 C++ 최상위 include와 contract model이
제거 전 공개 API를 계속 노출할 수 있다.

- `bindings/cpp/include/zlink.hpp`
- `bindings/cpp/include/zlink/Contracts/Service/discovery.hpp`
- `bindings/cpp/include/zlink/Contracts/Service/registry.hpp`
- `bindings/cpp/include/zlink/Contracts/Service/discovery_models.hpp`
- `bindings/cpp/include/zlink/Contracts/Service/registry_models.hpp`
- `bindings/cpp/include/zlink/Contracts/Service/spot_node.hpp`
- `bindings/cpp/include/zlink/Contracts/Service/spot_node_models.hpp`
- `bindings/cpp/include/zlink/Contracts/Eventing/events.hpp`
- `bindings/cpp/include/zlink/Contracts/Sockets/socket_contracts.hpp`
- `bindings/cpp/include/zlink/Contracts/Sockets/message_socket_contracts.hpp`
- `bindings/cpp/include/zlink/Contracts/Sockets/routed_socket_contracts.hpp`
- `bindings/cpp/include/zlink/Contracts/Sockets/pubsub_socket_contracts.hpp`

header sync는 수동 편집으로 끝내지 않는다. core build가 정리된 뒤 기존 header sync 절차가 있으면 그
절차로 복사본을 다시 맞춘다.

### 6.2 C++ wrapper

제거 대상:

- `service::discovery_t`
- `service::registry_t`
- `service::registry_query_client_t`
- socket `attach_discovery(...)`
- spot node `attach_discovery(...)`
- registry/discovery 관련 contract header와 model type
- `auto_connect_type`, `service_role`, `service_kind::discovery`, `route_kind`
- `topology_source::discovery`, `topology_source::registry`

C++ wrapper가 public C API 제거 뒤에도 제거된 symbol을 링크하려 하면 안 된다.

### 6.3 .NET binding

제거 대상:

- `IContext.CreateDiscovery(...)`
- `IContext.CreateRegistry()`
- `IDiscovery`
- `IRegistry`
- socket/spot `AttachDiscovery(...)`
- registry/discovery native P/Invoke symbol
- registry/discovery contract tests와 sample tests

framework가 아직 이 API를 호출하면 compile failure를 허용한다. framework location store 전환 작업에서
새 public API로 복구한다.

### 6.4 Java/Kotlin, Node, Python, Go, Rust

언어별 binding에서 아래를 제거한다.

- public context factory의 discovery/registry 생성 API
- public discovery/registry contract/interface/model
- attach discovery wrapper
- registry state/option enum
- discovery/registry tests와 docs

각 언어는 제거 뒤 “해당 symbol이 public API에 노출되지 않는다”는 contract test를 갱신한다.

### 6.5 core 버전 갱신과 GitHub Actions release

core 제거 구현이 끝난 뒤에는 로컬 파일 복사로 배포하지 않는다. 배포는 core 버전을 올린 커밋을 만든 뒤
`core/vX.Y.Z` tag를 push해서 GitHub Actions가 release asset을 만들게 한다.

버전 갱신 대상:

- `VERSION`
  - `LIBZLINK_VERSION_MAJOR`
  - `LIBZLINK_VERSION_MINOR`
  - `LIBZLINK_VERSION_PATCH`
  - `LIBZLINK_VERSION`
- `core/include/zlink.h`
  - `ZLINK_VERSION_MAJOR`
  - `ZLINK_VERSION_MINOR`
  - `ZLINK_VERSION_PATCH`
- `core/packaging/conan/conandata.yml`
  - 새 버전 `X.Y.Z` 항목을 추가한다.
  - URL은 `https://github.com/kairos-code-dev/zlink/archive/refs/tags/core/vX.Y.Z.tar.gz` 형식으로 둔다.

배포 전 로컬 확인:

```bash
./core/version.sh
awk -F= '/^LIBZLINK_VERSION=/{print $2}' VERSION
cmake --build core/build
```

마지막 `nm` 명령은 no hit여야 한다. 로컬 binding 개발 환경만 빠르게 맞춰야 할 때는 아래 스크립트를
사용할 수 있지만, 이것은 배포 절차가 아니다.

```bash
/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh
```

release 실행:

```bash
git status --short
git add VERSION core/include/zlink.h core/packaging/conan/conandata.yml
git commit -m "chore: bump core version to X.Y.Z"
git tag core/vX.Y.Z
git push origin HEAD
git push origin core/vX.Y.Z
```

`core/vX.Y.Z` tag push는 아래 workflow를 실행한다.

- `.github/workflows/build.yml`
  - `Build libzlink Core Libraries`
  - multi-platform native library를 빌드하고 GitHub Release asset을 만든다.
- `.github/workflows/core-conan-release.yml`
  - `Release Core Conan Package`
  - Conan package를 만들고, secret이 설정되어 있으면 remote에 업로드한다.

Git CLI로 tag가 원격에 올라갔는지 먼저 확인한다.

```bash
git ls-remote --tags origin core/vX.Y.Z
git fetch --tags origin
git tag --list "core/vX.Y.Z"
```

GitHub CLI로 workflow 진행 상태를 모니터링한다.

```bash
gh run list --workflow "Build libzlink Core Libraries" --event push --limit 10 \
  --json databaseId,headBranch,headSha,status,conclusion,displayTitle
gh run list --workflow "Release Core Conan Package" --event push --limit 10 \
  --json databaseId,headBranch,headSha,status,conclusion,displayTitle
gh run watch <run-id> --exit-status
gh release view core/vX.Y.Z --json tagName,name,assets,isDraft,isPrerelease
```

실패하면 `gh run view <run-id> --log-failed`로 실패 job 로그를 확인하고, 수정 커밋을 만든 뒤 새 버전 또는
명시적으로 합의한 재시도 tag로 다시 release한다. 이미 외부에 공개된 tag를 조용히 덮어쓰지 않는다.

binding 배포는 core release가 성공한 뒤 별도 단계에서 진행한다. binding release가 필요하면
`.github/workflows/bindings-release.yml`을 사용하고, `core_version` 입력에 방금 배포한 core 버전 `X.Y.Z`를
넣어 core release tag가 존재하는지 검증한다.

### 6.6 제거 뒤 사용하지 않는 binding 코드 정리

각 binding은 public wrapper만 지우고 끝내지 않는다. native symbol loader, generated binding glue,
sample support, test support까지 같이 확인해서 더 이상 쓰이지 않는 파일을 삭제한다.

공통 정리 기준:

- 제거된 native symbol을 선언만 남겨 둔 P/Invoke, FFI, N-API, JNI, cgo, Rust extern 선언 삭제
- discovery/registry wrapper만 쓰던 error mapping, enum mapping, handle wrapper 삭제
- sample runner와 test project에서 빠졌지만 소스 트리에 남아 있는 discovery/registry sample 삭제
- generated/dist 산출물이 source와 같이 version control에 들어 있는 언어는 generated/dist 파일도 함께 정리
- README alignment test나 public surface test가 참조하던 제거 전 API 이름 삭제
- binding 내부 helper가 다른 현재 기능에서도 쓰이는지 확인하고, discovery/registry 전용이면 삭제

언어별 확인 예:

- C/C++: header copy, C++ contract header, sample CMake, contract test, `Runtime/Service/*` 구현 파일
- .NET: P/Invoke symbol, `IContext` factory, `IDiscovery`/`IRegistry` interface, sample solution entry
- Java/Kotlin: JNI symbol binding, service contract class, Gradle sample task
- Node/JavaScript: native addon export, TypeScript declaration, dist-tools generated sample/test
- Go: cgo symbol wrapper, samplecommon의 registry topology wait helper
- Python: ctypes/cffi native symbol table, sample runner, README alignment test
- Rust: extern symbol, public service wrapper, `Cargo.toml` sample binary entry

### 6.7 POSD 기반 binding 리팩토링 점검

binding 정리 뒤에는 언어별 public API가 core 제거 상태를 단순하게 드러내는지 확인한다. 제거된 기능을
감추기 위해 compatibility layer를 만들면 호출자는 더 혼란스러운 API를 보게 되므로, 그런 계층은 남기지
않는다.

점검 기준:

- 제거된 C API를 흉내 내는 wrapper, extension method, deprecated alias를 추가하지 않는다.
- binding별 public contract가 “없는 기능은 없다”고 명확히 말하게 하고, 런타임 오류로 늦게 실패시키지
  않는다.
- native handle을 감싸기만 하던 얕은 class/interface가 discovery/registry 제거 뒤 남아 있으면 삭제한다.
- generated binding과 handwritten wrapper 사이에 같은 enum/type 변환 지식이 중복되면 한쪽을 제거한다.
- sample을 통과시키기 위한 test-only adapter를 만들지 않는다.
- 언어별 차이 때문에 바로 삭제할 수 없는 경우에는 public contract gap으로 남기고, 새 API를 임의로 만들지
  않는다.

## 7. framework 영향과 후속 복구

framework는 기존 registry/discovery API 호출이 compile failure로 드러날 수 있다. 이 실패는 제거 작업의
정상 결과이며, 후속 location runtime/store 작업에서 복구한다.

후속 대체 기준은 `framework/doc/framework/common/draft/framework-location-resolver-store.ko.md`다.

대체 방향:

- registry topology query client 제거
- registry/discovery event source 제거
- embedded registry host sample/e2e 제거
- 자동 연결은 peer location store와 location runtime reconcile로 대체
- spot/actor/route 위치 조회는 framework resolver/store로 대체
- 운영 조회는 `IZLinkLocationRuntimeQuery`로 대체

## 8. 문서 정리 계획

### 8.1 core 정식 문서

아래 문서에서 discovery/registry를 현재 public API처럼 설명하는 내용을 제거하거나 “제거된 기능”으로
옮긴다.

- `core/doc/spec/README.ko.md`
- `core/doc/spec/README.md`
- `core/doc/spec/core/README.ko.md`
- `core/doc/spec/core/README.md`
- `core/doc/spec/core/service/README.ko.md`
- `core/doc/spec/core/service/README.md`
- `core/doc/spec/core/service/discovery.ko.md`
- `core/doc/spec/core/service/discovery.md`
- `core/doc/spec/core/service/registry.ko.md`
- `core/doc/spec/core/service/registry.md`
- `core/doc/guide/07-1-discovery.ko.md`
- `core/doc/guide/07-1-discovery.md`
- `core/doc/guide/07-4-registry.ko.md`
- `core/doc/guide/07-4-registry.md`
- `core/doc/internals/discovery-internals.ko.md`
- `core/doc/internals/discovery-internals.md`
- `core/doc/internals/registry-internals.ko.md`
- `core/doc/internals/registry-internals.md`

정식 spec에는 제거된 API의 사용법을 남기지 않는다. 필요하면 별도 removed/legacy 문서로 이동하고,
현재 공개 계약이 아님을 첫머리에 명시한다.

### 8.2 framework 문서

framework 쪽 문서는 core API 제거와 동시에 완성하지 않는다. 대신 draft와 plan에서 후속 대체 기준을
명확히 유지한다.

- `framework/doc/framework/common/draft/framework-location-resolver-store.ko.md`
- `framework/doc/framework/common/spec/` 반영 계획
- 언어별 framework spec/guide
- sample/e2e 문서

## 9. 회귀 테스트와 검증

### 9.1 제거 직후 최소 검증

제거 커밋에서 확인할 항목:

  header에서 no hit
- `rg "service/discovery.h|service/registry.h" core/include bindings/c/include bindings/cpp/include`가
  top-level include에서 no hit
- `rg "zlink_socket_attach_discovery|zlink_spot_node_attach_discovery" core/include bindings`가 public
  header에서 no hit
- `rg "discovery_models|registry_models|service::discovery_t|service::registry_t|attach_discovery" bindings/cpp/include`가 no hit
- core build가 성공하거나, 실패하면 실패가 제거 중인 상위 wrapper/framework 의존 때문인지 분리되어 있음
- `core/tests/CMakeLists.txt`와 `core/tests/unittest/CMakeLists.txt`에 삭제된 discovery/registry 테스트 target이
  남아 있지 않음

### 9.2 bindings 검증

각 binding에서 갱신할 테스트:

- public API surface에 discovery/registry type이 없는지 확인
- native symbol loader가 제거된 symbol을 찾지 않는지 확인
- socket/spot attach discovery wrapper가 없는지 확인
- registry/discovery sample test 삭제 또는 location store 후속 테스트로 이동

### 9.3 framework 후속 검증

location runtime/store 전환 뒤 확인할 항목:

- 기존 registry/discovery E2E를 location store 기반 E2E로 대체
- embedded registry process 없이 sample smoke 실행
- Redis extension 기반 multi-process 자동 연결 검증
- actor/spot/route resolve의 `Normal`/`Refresh` cache 정책 검증
- `IZLinkLocationRuntimeQuery` 기반 topology/status/summary 검증

## 10. 제거 또는 대체할 테스트 목록

이 절의 테스트는 discovery/registry 공개 C API 제거와 함께 삭제하거나 location runtime/store 기준
테스트로 새로 작성한다. 제거 커밋에서는 삭제가 우선이며, location store 전환 검증은 후속 작업에서
추가한다.

### 10.1 core tests

삭제 대상:

- `core/tests/unittest/unittest_discovery_route_store.cpp`
- `core/tests/unittest/unittest_discovery_service_state.cpp`
- `core/tests/unittest/unittest_registry_access.cpp`
- `core/tests/integration/test_discovery_resolve_spot.cpp`
- `core/tests/integration/test_discovery_socket_auto_connect_policy.cpp`
- `core/tests/integration/test_spot_multi_service_discovery.cpp`
- `core/tests/integration/test_spot_multi_service_discovery_scale.cpp`
- `core/tests/e2e/spot/spot_pubsub_scenario_discovery_cases.cpp`

수정 대상:

- `core/tests/unittest/unittest_public_contract_headers.cpp`
  - discovery/registry header include 기대를 제거한다.
  - 제거된 symbol이 public header에 없는지 확인한다.
- `core/tests/unittest/unittest_service_mode_policy.cpp`
  - discovery service kind/role 기대가 있으면 제거한다.
- `core/tests/integration/test_peer_admission.cpp`
  - registry/discovery topology source를 직접 기대하면 framework location store 후속 테스트로 옮긴다.
- `core/tests/integration/test_spot_actor_dispatch.cpp`
  - actor route lookup이 discovery를 경유하면 새 framework route resolver 테스트로 옮긴다.
- `core/tests/e2e/spot/spot_pubsub_scenario_shared.cpp`
  - discovery case helper와 marker를 제거한다.
- `core/tests/e2e/spot/test_spot_service_introspection.cpp`
  - registry/discovery service summary 또는 topology 검증을 제거한다.
- `core/tests/CMakeLists.txt`
- `core/tests/unittest/CMakeLists.txt`
  - 삭제된 discovery/registry test target, label, scenario registration을 제거한다.
  - 기존 테스트 안에 discovery case만 섞여 있으면 해당 case 이름과 runner marker도 함께 제거한다.

### 10.2 C/C++ binding tests and samples

삭제 대상:

- `bindings/c/samples/discovery_registry_sample.c`
- `bindings/c/samples/registry_query_sample.c`
- `bindings/cpp/samples/discovery_registry_sample.cpp`
- `bindings/cpp/samples/registry_query_sample.cpp`
- `bindings/cpp/samples/spot_recv_sample.cpp`
- `bindings/cpp/tests/contract/test_cpp_contract_callback_mode.cpp`
- `bindings/cpp/tests/contract/test_cpp_contract_layout_headers.cpp`
- `bindings/cpp/tests/contract/test_cpp_contract_service.cpp`
- `bindings/cpp/tests/contract/test_cpp_contract_socket.cpp`
- C++ framework/e2e의 `DiscoveryRegistryHa`와 `RegistryMessaging` 중 core registry/discovery API 성공을
  직접 검증하는 항목은 제거하고 framework location store E2E로 재작성한다.

수정 대상:

- `bindings/c/samples/CMakeLists.txt`
- `bindings/c/samples/run_samples.sh`
- `bindings/c/samples/sample_common.h`
  - registry/discovery sample 등록과 help text를 제거한다.
- `bindings/cpp/CMakeLists.txt`
- `bindings/cpp/samples/run_samples.sh`
- `bindings/cpp/samples/sample_common.hpp`
  - 삭제한 C++ sample과 contract test의 build/run 등록을 제거한다.
- `bindings/cpp/src/Runtime/Service/discovery.cpp`
- `bindings/cpp/src/Runtime/Service/registry.cpp`
- `bindings/cpp/src/Runtime/Service/query.cpp`
- `bindings/cpp/src/Runtime/Service/service_model_access.hpp`
  - public wrapper 제거에 맞춰 삭제하거나 빌드 목록에서 제외한다.

### 10.3 .NET binding tests and samples

삭제 대상:

- `bindings/dotnet/tests/Zlink.Tests/test_discovery_route.cs`
- `bindings/dotnet/tests/Zlink.Tests/test_attach_discovery_contract.cs`
- `bindings/dotnet/tests/Zlink.Tests/test_discovery_router_socket.cs`
- `bindings/dotnet/tests/Zlink.Tests/test_discovery_resolve_spot.cs`
- registry/discovery 전용 sample:
  - `bindings/dotnet/samples/DiscoveryRegistry/Program.cs`
  - `bindings/dotnet/samples/RegistryQuery/Program.cs`

수정 대상:

- `bindings/dotnet/tests/Zlink.Tests/test_socket_surface.cs`
  - `IContext.CreateRegistry`
  - `IContext.CreateDiscovery`
  - `IDiscovery`
  - `IRegistry`
  - socket `AttachDiscovery`
  - spot node `AttachDiscovery`
  - discovery route API 기대를 제거한다.
- `bindings/dotnet/tests/Zlink.Tests/test_spot_pubsub_basic.cs`
  - registry/discovery 기반 spot pub/sub case를 삭제하거나 manual/core-only case만 남긴다.
- `bindings/dotnet/tests/Zlink.Tests/test_request_reply.cs`
  - discovery auto-connect case가 있으면 삭제한다.
- `bindings/dotnet/tests/Zlink.Tests/test_topology_contract.cs`
  - `TopologySource.Registry`/`Discovery` 기대를 제거한다.
- `bindings/dotnet/tests/Zlink.Tests/test_validation_contract.cs`
  - discovery attachment validation 기대를 제거한다.
- `bindings/dotnet/samples/Zlink.Samples.sln`
- `bindings/dotnet/samples/run_samples.ps1`
  - discovery/registry sample entry를 제거한다.

### 10.4 Java/Kotlin binding tests and samples

삭제 대상:

- `bindings/java/samples/Zlink.Samples/src/main/java/systems/zlink/samples/DiscoveryRegistrySample.java`
- `bindings/java/samples/Zlink.Samples/src/main/java/systems/zlink/samples/RegistryQuerySample.java`
- `bindings/kotlin/samples/src/main/kotlin/systems/zlink/samples/DiscoveryRegistrySample.kt`
- `bindings/kotlin/samples/src/main/kotlin/systems/zlink/samples/RegistryQuerySample.kt`

수정 대상:

- `bindings/java/src/test/java/systems/zlink/integration/contract/ServiceContractsIntegrationTest.java`
- `bindings/java/samples/run_samples.sh`
- `bindings/java/samples/Zlink.Samples/build.gradle`
- `bindings/kotlin/samples/build.gradle`
  - discovery/registry sample task를 제거한다.
- Java native symbol tests가 있으면 `NativeDiscoverySymbols`, `NativeRegistrySymbols` 기대를 제거한다.

### 10.5 Node binding tests and samples

삭제 대상:

- `bindings/javascript/samples/discovery_registry_sample.js`
- `bindings/javascript/samples/registry_query_sample.js`
- `bindings/javascript/samples/spot_recv_sample.js`
- `bindings/node/samples/discovery_registry_sample.ts`
- `bindings/node/samples/registry_query_sample.ts`
- `bindings/node/samples/spot_recv_sample.ts`
- `bindings/node/dist-tools/samples/discovery_registry_sample.js`
- `bindings/node/dist-tools/samples/registry_query_sample.js`
- `bindings/node/dist-tools/samples/spot_recv_sample.js`

수정 대상:

- `bindings/javascript/samples/run_samples.sh`
- `bindings/javascript/samples/package.json`
- `bindings/node/samples/run_samples.sh`
  - 삭제한 sample entry와 dist sample entry를 제거한다.
- `bindings/node/tests/enums.test.ts`
  - `RegistryState`
  - `TopologySource.Registry`
  - discovery/registry enum export 기대를 제거한다.
- `bindings/node/tests/api.test.ts`
  - registry query factory와 service export 기대를 제거한다.
- `bindings/node/tests/boundary.test.ts`
  - discovery/registry public boundary 기대를 제거한다.
- `bindings/node/dist-tools/tests/enums.test.js`
- `bindings/node/dist-tools/tests/api.test.js`
- `bindings/node/dist-tools/tests/boundary.test.js`
  - generated/dist test도 같은 기준으로 갱신한다.

### 10.6 Go, Python, Rust binding tests and samples

삭제 대상:

- `bindings/go/samples/discovery_registry_sample/main.go`
- `bindings/go/samples/registry_query_sample/main.go`
- `bindings/go/samples/spot_recv_sample/main.go`
- `bindings/python/samples/discovery_registry_sample.py`
- `bindings/python/samples/registry_query_sample.py`
- `bindings/python/samples/spot_recv_sample.py`
- `bindings/rust/samples/discovery_registry_sample.rs`
- `bindings/rust/samples/registry_query_sample.rs`
- `bindings/rust/samples/spot_recv_sample.rs`

수정 대상:

- `bindings/go/samples/run_samples.sh`
- `bindings/go/samples/internal/samplecommon/samplecommon.go`
  - registry topology wait helper처럼 discovery/registry sample만 쓰던 helper를 제거한다.
- `bindings/go/surface_test.go`
  - discovery/registry public surface 기대 제거
- `bindings/python/samples/run_samples.py`
- `bindings/python/samples/run_samples.sh`
  - 삭제한 Python sample entry를 제거한다.
- `bindings/python/tests/test_native_contract.py`
- `bindings/python/tests/test_readme_alignment.py`
  - README에서 제거된 discovery/registry sample/API 기대 제거
- `bindings/rust/Cargo.toml`
- `bindings/rust/samples/run_samples.sh`
  - 삭제한 Rust sample binary와 runner entry를 제거한다.
- `bindings/rust/tests/service_surface_tests.rs`
  - discovery/registry service surface 기대 제거

### 10.7 framework tests and e2e

제거 단계에서는 아래 테스트와 E2E가 깨질 수 있다. 배포 전에는 location runtime/store 기준으로 삭제 또는
재작성해야 한다.

.NET:

- `framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Registry/RegistryContracts.cs`
- `framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Configuration/BuilderContracts.cs`
- `framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Eventing/EventingContracts.cs`
  - `AddRegistryEvents`
- `.NET` e2e의 `RegistryMessaging`, `DiscoveryRegistryHa`, registry server project가 있는 config

Java/Kotlin:

- `framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/RegistryContractTest.java`
- `framework/languages/java/zlink-framework-core/src/integrationTest/java/systems/zlink/framework/runtime/DiscoveryScaleoutTest.java`
- `framework/languages/java/zlink-framework-core/src/integrationTest/java/systems/zlink/framework/runtime/EmbeddedRegistryTest.java`
- `framework/languages/java/zlink-framework-testkit/src/fakeBackendTest/java/systems/zlink/framework/testkit/RegistryRuntimeFakeBackendTest.java`
- Java/Kotlin e2e의 `RegistryMessaging`, `DiscoveryRegistryHa`

Node:

- `framework/languages/node/test/contract/registry-runtime.test.js`
- `framework/languages/node/test/contract/backend-contract.test.js`
- `framework/languages/node/test/contract/node-binding-parity.test.js`
- Node e2e의 `RegistryMessaging`, `DiscoveryRegistryHa`

C++:

- C++ framework registry topology unit/contract tests
- C++ e2e의 `RegistryMessaging`, `DiscoveryRegistryHa`

공통 원칙:

- registry/discovery 자체를 검증하는 E2E는 삭제한다.
- 자동 연결, spot owner 조회, actor 재연결, topology/status 검증은 framework location runtime/store E2E로
  새로 작성한다.
- 제거된 C API를 mock 또는 private helper로 되살려 기존 E2E를 통과시키지 않는다.

### 10.8 framework samples

framework sample은 제거 커밋 직후 깨질 수 있다. 배포 전에는 registry process와 discovery endpoint 의존을
location runtime/store 기반 흐름으로 바꿔야 한다.

공통 수정 대상:

- sample solution/project/build 파일에서 `Server/Registry` project 또는 target 제거
- sample runner에서 registry process 실행, registry endpoint wait, registry log marker 제거
- sample README에서 discovery registry host, registry readiness, registry query 설명 제거
  `UseRegistrySpotRemoteAddresses(...)` 호출 제거
- registry query client를 readiness probe나 topology 확인에 쓰는 코드를 location runtime query 또는
  sample 전용 health check로 대체

.NET sample:

- `framework/languages/dotnet/samples/GameQuest/GameQuest.csproj`
- `framework/languages/dotnet/samples/GameQuest/Server/Registry/`
- `framework/languages/dotnet/samples/GameQuest/run_sample.ps1`
- `framework/languages/dotnet/samples/GameQuest/README.ko.md`
- `framework/languages/dotnet/samples/GameQuest/Server/GameApi/Program.cs`
- `framework/languages/dotnet/samples/DeliveryDispatch/Server/Registry/`
- `framework/languages/dotnet/samples/DeliveryDispatch/run_sample.sh`
- `framework/languages/dotnet/samples/DeliveryDispatch/run_sample.ps1`
- `framework/languages/dotnet/samples/DeliveryDispatch/README.ko.md`
- DeliveryDispatch server module 중 discovery 또는 registry spot resolver를 호출하는 파일

C++ sample:

- `framework/languages/cpp/samples/DeliveryDispatch/Server/Registry/`
- `framework/languages/cpp/samples/DeliveryDispatch/run_sample.sh`
- `framework/languages/cpp/samples/DeliveryDispatch/README.ko.md`
- `framework/languages/cpp/samples/DeliveryDispatch/Probe/`
- `framework/languages/cpp/samples/DeliveryDispatch/Server/*/main.cpp`
- `framework/languages/cpp/samples/Bingo/Server/Registry/`
- `framework/languages/cpp/samples/Bingo/run_sample.sh`
- `framework/languages/cpp/samples/TicTacToe/run_sample.sh`

Node sample:

- `framework/languages/node/samples/DeliveryDispatch.Ts/Server/Registry/`
- `framework/languages/node/samples/DeliveryDispatch.Ts/Server/Probe/`
- `framework/languages/node/samples/DeliveryDispatch.Ts/run_sample.sh`
- `framework/languages/node/samples/DeliveryDispatch.Ts/run_sample.ps1`
- `framework/languages/node/samples/DeliveryDispatch.Ts/README.ko.md`
- `framework/languages/node/samples/Bingo.Ts/Server/Registry/`
- `framework/languages/node/samples/Bingo.Ts/run_sample.sh`
- `framework/languages/node/samples/Bingo.Ts/run_sample.ps1`
- `framework/languages/node/samples/TicTacToe.Ts/run_sample.sh`
- `framework/languages/node/samples/TicTacToe.Ts/run_sample.ps1`

Java/Kotlin sample:

- Java/Kotlin sample의 registry application, Gradle project, run script entry
- `UseDiscovery` 또는 registry spot resolver와 같은 public framework API 호출부
- sample 문서의 registry/discovery 실행 순서와 readiness 설명

## 11. 작업 순서

1. public surface inventory를 확정한다.
   `core/include`, `bindings/*/include`, 언어별 binding contract에서 discovery/registry symbol 목록을
   만든다.
2. core public header를 제거한다.
   `zlink.h`, discovery/registry header, enum, socket/spot attach discovery 선언을 정리한다.
3. core public API 구현과 build 등록을 제거한다.
   export symbol이 남지 않도록 entrypoint와 CMake/source 목록을 정리한다.
4. core에서 사용하지 않는 discovery/registry 전용 source, header, helper, test fixture를 삭제한다.
5. core 제거 결과를 POSD 기준으로 다시 점검하고, no-op shim, 얕은 adapter, 패스스루 함수를 정리한다.
6. C/C++ header 복사본과 C++ wrapper를 제거한다.
7. .NET binding public wrapper와 tests를 제거한다.
8. Java/Kotlin, Node, Python, Go, Rust binding wrapper와 tests를 제거한다.
9. 각 binding에서 사용하지 않는 generated glue, native symbol wrapper, sample support, test helper를 삭제한다.
10. binding 제거 결과를 POSD 기준으로 다시 점검하고 compatibility layer나 test-only adapter를 남기지 않는다.
11. core 정식 spec/guide/internals에서 discovery/registry 현재 계약 설명을 제거한다.
12. core를 빌드한다.
13. core 버전을 올리고 `VERSION`, `core/include/zlink.h`, `core/packaging/conan/conandata.yml`을 함께
    커밋한다.
14. `core/vX.Y.Z` tag를 push해서 GitHub Actions release를 실행한다.
15. `gh run list`, `gh run watch`, `gh release view`로 release workflow와 release asset을 확인한다.
16. binding/framework compile failure 목록을 기록한다.
17. framework location runtime/store 작업에서 호출부를 복구한다.
18. location store 기반 e2e와 sample smoke가 통과하면 배포 가능 상태로 판단한다.

## 12. 중간 상태 허용 기준

이 제거 작업은 배포 전 중간 상태이므로 아래 실패를 허용한다.

- framework가 `UseDiscovery`, `UseRegistry...`, registry query API를 호출해서 compile failure
- 기존 registry/discovery E2E failure
- binding tests가 제거 전 API를 기대해서 failure
- sample runner가 Registry process를 기대해서 failure

아래 실패는 허용하지 않는다.

- binding public API에 제거된 type이 남음
- 제거된 기능만 쓰던 source/header/helper가 빌드에서 빠진 채 소스 트리에 방치됨
- 문서가 제거된 API를 현재 공개 계약처럼 설명함
- sample을 통과시키기 위해 private/raw/shim 경로를 추가함
- POSD 점검 없이 no-op shim, compatibility wrapper, 패스스루 adapter를 남김

## 13. 완료 조건

제거 단계 완료 조건:

- core C public header에서 discovery/registry 공개 표면 제거
- binding public header 복사본에서 동일 제거
- native export에서 discovery/registry symbol 제거
- core와 binding에서 사용하지 않는 discovery/registry 전용 파일과 helper 제거
- POSD 기반 리팩토링 점검 결과, 제거된 기능을 감추는 shim이나 얕은 wrapper가 남지 않음
- 언어별 binding public wrapper 제거 또는 제거 계획에 따른 compile failure 목록화
- core 정식 문서에서 discovery/registry 현재 계약 설명 제거
- 후속 framework location store 전환 항목이 명확히 기록됨

배포 가능 완료 조건:

- core build 성공
- `VERSION`과 `core/include/zlink.h`의 core version이 일치
- `core/vX.Y.Z` tag 기준 `Build libzlink Core Libraries` workflow 성공
- `core/vX.Y.Z` tag 기준 `Release Core Conan Package` workflow 성공 또는 Conan remote secret 미설정으로 인한
  upload skip이 의도된 상태임을 확인
- `gh release view core/vX.Y.Z`에서 native library asset과 source tarball 확인
- binding build/test가 새 public surface 기준으로 성공
- framework가 location runtime/store API로 자동 연결, spot resolve, actor resolve를 복구
- Redis extension 또는 외부 공유 store 기반 multi-process e2e 성공
- sample smoke가 registry process 없이 성공
