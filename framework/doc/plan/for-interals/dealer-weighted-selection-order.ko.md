# Core DEALER 가중 선택 순서 수정

[계획 문서 묶음](README.ko.md) · [Core 문서](../../../../core/doc/README.ko.md) · [DEALER spec](../../../../core/doc/spec/core/socket/06-dealer.ko.md)

DEALER의 가중 선택이 **연속 구간(burst) 방식**이고, 결정적 tiebreak가 없으며, weight
해상도가 상위 계층보다 낮다. 세 가지 모두 core에서 고쳐야 한다.

## 1. 성격 — 계약 위반 수정이 아니라 계약 확장 제안

**현재 Core spec은 아래만 보장한다.**

| 보장 | 위치 |
|---|---|
| weight 범위 `0..100` | `core/doc/spec/core/socket/06-dealer.ko.md:64` |
| 같은 weight는 순환, 다른 weight는 **장기 비율** 반영 | `:70-72` |

따라서 지금 구현의 연속 burst와 비결정적 순서는 **현재 계약을 어기지 않는다.** 이 문서가
다루는 것은 계약을 넓히자는 제안이다.

> **이 문서의 초기 판정은 틀렸다.** "C++ ClientServer가 Core에 선택을 위임하므로 Core를
> 고쳐야 framework 계약이 닫힌다"고 적었으나, 확인 결과 **정식 ClientServer 경로는
> framework가 고른다.**
>
> - `framework/languages/cpp/framework/src/runtime/client_server/client_server_location_runtime.cpp:699`가
>   `bind_client_server_transport`로 sender·requester를 등록한다.
> - `framework/languages/cpp/framework/src/runtime/channels/channel_outbound_exchange.cpp:931`이
>   그 requester가 있으면 그쪽을 쓴다.
> - 하나의 DEALER에 endpoint를 모두 연결하는 `channel_native_client_t`(`:1015`)는
>   **requester가 없을 때만 쓰는 fallback**이다.
> - 정식 경로는 `client_server_location_runtime.cpp:1316-1328`에서 framework가 후보를
>   고르고, 선택된 owner의 DEALER에는 endpoint가 하나뿐이다
>   (`raw_client_server_owner.cpp:668`).
>
> **네 언어 모두 정식 경로에서는 framework가 고른다.** Core에 위임하는 것은 C++의
> fallback 경로 하나이며, 이 수정으로 닫히는 framework 갭은 없다.

### 그럼에도 진행하는 이유

| 이유 | 내용 |
|---|---|
| 공개 core 기능이다 | `ZLINK_DEALER_OPT_WEIGHT`는 framework와 무관하게 binding 사용자에게 노출된다. "비율만 반영"이라는 계약은 burst와 비결정성을 그대로 허용한다 |
| fallback 경로가 남아 있다 | ClientServer 등록 없이 수동 endpoint만 붙인 채널은 이 경로를 탄다 |
| 계층 경계를 바로잡는다 | framework 쪽 `connections_from_next()`가 connect 순서로 Core 선택을 유도하려는 **죽은 코드**를 갖고 있다. 이건 Core 수정과 무관하게 삭제 대상이다 |

**framework 계약(`08-channel-messaging` 「선택 순서」)과 같은 절차를 Core에도 적용할지는
별도 판단이다.** 상위 계층이 정한 것을 하위 계층이 따라야 할 이유는 없다 — Core는 더 많은
사용자를 갖고, 순서를 계약으로 올리면 되돌리기 어렵다.

## 2. 넓히려는 항목

### 결함 1 — 선택이 연속 구간으로 몰린다

`core/src/runtime/sockets/internal/lb.cpp:180-217` `rebuild_weighted_schedule()`은 weight를
최대공약수로 나눈 뒤 **각 pipe의 슬롯을 연속으로 펼친** 일정표를 만든다.

```cpp
for (pipes_t::size_type i = 0; i < _active; ++i) {
    const uint32_t slots = weight_gcd > 0 ? pipe_weight / weight_gcd : pipe_weight;
    for (uint32_t n = 0; n < slots; ++n)
        _weighted_schedule.push_back (_pipes[i]);   // A A A B
}
```

weight `300 / 100`이면 일정표가 `[A, A, A, B]`가 되어 A가 세 번 연속 선택된다. 장기 비율은
맞지만 **연속 선택이 한 후보에 몰린다.** Framework 계약은 이 몰림을 금지한다 — 같은
조건에서 계약이 요구하는 순서는 `B, A, B, B`다.

**같은 weight일 때는 다른 경로를 탄다.** `:203`의 `_active > 1 && !all_equal` 조건 때문에
weight가 모두 같으면 `_weighted_enabled`가 `false`가 되고 일반 round-robin으로 넘어간다.
결과는 우연히 계약과 맞지만, 두 경로가 나뉘어 있어 한쪽만 고치면 어긋난다.

### 결함 2 — 결정적 tiebreak가 없다

일정표는 `_pipes[i]` 순서를 따르며, 이 순서는 **연결·활성화 순서**다. 같은 설정을 가진 두
node가 서로 다른 순서를 낸다. Framework 계약은 후보 식별자 오름차순 tiebreak를 요구하며,
application이 이 재현성에 의존할 수 있다고 명시한다.

`_pipes`는 routing ID나 endpoint 같은 안정적 식별자를 갖고 있지 않으므로, tiebreak 키를
어디서 얻을지 정하는 것이 이 수정의 설계 지점이다.

### 결함 3 — weight 해상도가 상위 계층보다 낮다

| 계층 | 허용 범위 | 근거 |
|---|---|---|
| Framework | `0..10000` | `framework/languages/cpp/framework/src/runtime/channels/channel_runtime.cpp:674,814` |
| Core DEALER | `0..100` | spec `core/doc/spec/core/socket/06-dealer.ko.md:64`, 구현 `lb.cpp:93-94` |

**공개 입력은 조용히 clamp되지 않는다. 거부한다.**

| 자리 | 동작 |
|---|---|
| `core/src/runtime/sockets/common/socket_base_control.cpp:8-13` | `weight_ > 100`이면 `EINVAL` |
| `core/src/runtime/core/options_core_socket.cpp:145-149` | `0..100` 밖이면 설정하지 않는다 |
| `core/src/runtime/sockets/internal/lb.cpp:93-94` | 내부 방어 clamp. 공개 입력이 여기 닿기 전에 이미 거부된다 |

> **초기 판정 두 개를 정정한다.**
>
> 1. "framework가 `5000`과 `10000`을 지정해도 Core에서 둘 다 `100`이 되어 비율이
>    무너진다" — **틀렸다.** 정식 ClientServer는 framework selector가 `0..10000`을 직접
>    쓰고 선택된 DEALER에는 pipe가 하나뿐이다. fallback 경로도 Core에 넘기기 전에
>    framework가 `100`으로 제한한다(`framework/languages/cpp/framework/src/runtime/channels/channel_host_service.cpp:306-317`).
> 2. "지금은 조용히 clamp한다" — **틀렸다.** 공개 setter는 거부한다.

**따라서 이것은 버그 수정이 아니라 Core API 확장 여부의 문제다.** `0..100` 해상도가
부족한 실제 사용 사례가 있는지 먼저 승인받는다. 확대한다면 공개 option 검증, peer-weight
wire 범위, `lb_t`, monitoring event를 **한 계약으로** 바꾼다.

## 3. 수정 방향

### 3.1 선택 절차

`rebuild_weighted_schedule()`의 연속 슬롯 방식을 **누적값 방식**으로 바꾼다. 계약이 정한
절차는 다음과 같다.

1. 모든 후보의 누적값에 자기 weight를 더한다.
2. 누적값이 가장 큰 후보를 고른다. 같으면 후보 식별자 오름차순으로 앞선 후보를 고른다.
3. 고른 후보의 누적값에서 후보 전체의 weight 합을 뺀다.

후보 목록이 바뀌면 새 목록에 있는 후보의 누적값만 유지하고 나머지는 버린다.

**동일 weight 분기를 없앤다.** `_active > 1 && !all_equal` 조건을 제거하고 한 경로로
합친다. 같은 weight에서 이 절차는 자동으로 번갈아 선택하므로 별도 round-robin이 필요 없다.

**호출마다 후보 전체를 순회하는 비용**이 문제가 되면, 절차가 결정적이므로 **같은 누적값
상태가 다시 나타나는 지점**까지를 한 바퀴로 미리 계산해 두고 cursor만 옮길 수 있다.
`weight 합 ÷ 최대공약수`를 주기로 단정하면 안 된다 — 그 길이는 누적값이 전부 0인
상태에서만 성립하고, pipe가 바뀌면 남은 누적값이 그 주기 위의 점이라는 보장이 없다.

### 3.2 Tiebreak 키

**식별자는 이미 `pipe_t`에 있다.**

| 후보 | 접근자 | 확인할 것 |
|---|---|---|
| peer routing ID | `core/src/runtime/core/pipe.hpp:111` `get_routing_id()` | 일반 DEALER peer는 **비어 있을 수 있다.** ClientServer Server는 실제 Server RID를 socket routing ID로 설정한다(`framework/languages/cpp/framework/src/runtime/client_server/raw_client_server_owner.cpp:161`) |
| endpoint | `core/src/runtime/core/pipe.hpp:229` `get_endpoint_pair()` | 같은 peer에 여러 endpoint가 있을 때 안정적인가 |
| pipe 생성 순번 | — | node마다 달라 **목적을 만족하지 못한다** |

> **초기 판정 정정.** "`_pipes`는 안정적 식별자를 갖고 있지 않다"고 적었으나 사실이 아니다.
> `lb_t::_pipes`의 원소는 `pipe_t*`이고(`core/src/runtime/sockets/internal/lb.hpp:47`)
> routing ID와 endpoint 접근자를 모두 갖는다.

**실제 설계 지점은 빈 routing ID 처리다.** Core 전체에 결정적 순서를 약속하려면 다음을
먼저 정해야 한다.

- routing ID가 비어 있는 peer의 tiebreak 키
- 같은 routing ID를 가진 peer가 둘 이상일 때
- 재연결로 같은 peer가 새 pipe를 얻었을 때
- 같은 peer에 endpoint가 여럿일 때

### 3.3 Weight 범위

두 방향 중 하나를 고른다.

| 방향 | 영향 |
|---|---|
| Core를 `0..10000`으로 확대 | `ZLINK_DEALER_OPT_WEIGHT` 공개 계약 변경. 기존 값은 그대로 유효하므로 하위 호환 |
| Framework를 `0..100`으로 축소 | Framework 공개 계약 축소. 이미 `0..10000`을 쓰는 application이 깨진다 |

**첫 번째를 권장한다.** 확대는 기존 값의 의미를 바꾸지 않는다.

### 3.4 Framework 쪽 정리 (함께 진행)

`framework/languages/cpp/framework/src/runtime/channels/channel_runtime_bundle.cpp:134-178`의
`connections_from_next()`는 누적값 SWRR로 winner를 계산한 뒤 endpoint 목록을 winner가 맨
앞에 오도록 회전시켜 반환한다. connect 순서로 core의 선택을 유도하려는 것이다.

**이 코드는 지금도 효과가 없다.** 받는 쪽
`framework/languages/cpp/framework/src/runtime/channels/channel_outbound_exchange.cpp:579-608`
`sync_connections()`가 목록을 `std::set<std::string>`에 넣어 순서를 없앤 뒤 연결하고,
제출도 대상 지정 없이 한다(`:354-364`). core는 connect 순서에 대해 아무것도 약속하지 않는다.

core 수정과 함께 **선택·회전 로직을 삭제한다.** 효과가 없는 데다 core의 역할을 중복한다.

> **삭제 범위를 확인할 것.** `channel_runtime_bundle_t::try_add_auto_connection`은
> `channel_runtime_bundle.cpp` 밖에서 호출되지 않는다. 즉 ClientServer client bundle은
> 실제로 **수동 connect만** 채워지고(`app.cpp:2349`), 수동 연결은 weight가 `100` 고정이다
> (`channel_runtime_bundle.cpp:137`). 그렇다면 `connections_from_next()`의 가중 선택은
> **모든 후보가 같은 weight인 상태에서만 동작**해 온 셈이다. `_auto_connections` 자체가
> 죽은 경로인지 아직 배선되지 않은 것인지 확인한 뒤 삭제 범위를 정한다.

## 4. Spec 변경 계획

Spec이 정본이므로 **구현보다 먼저 개정한다.** 순서는 spec → 구현 → 검증이다.

### 4.1 `core/doc/spec/core/socket/06-dealer.ko.md` §2 (선택 규칙, 현재 `:70-72`)

지금 문구는 이렇다.

> 양수 가중치가 같은 outbound peer는 순환 방식으로 선택한다. 양수 가중치가 다르면 그 비율을
> 선택 빈도에 반영하고, 가중치가 `0`인 peer는 후보에서 제외한다.

"비율을 반영한다"만으로는 burst와 비결정성이 모두 허용된다. 다음을 추가한다.

| 추가할 내용 | 이유 |
|---|---|
| 누적값 3단계 절차를 절차로 명시 | 구현마다 다른 순서가 나오는 것을 막는다 |
| 후보 식별자 오름차순 tiebreak와 그 식별자가 무엇인지 | 같은 설정의 두 process가 같은 순서를 낸다 |
| **연속 선택이 한 peer에 몰리지 않는다**는 보장 | 지금 구현의 `[A,A,A,B]`를 금지한다 |
| 같은 가중치도 같은 절차를 따른다는 명시 | 별도 round-robin 분기를 없앤다 |
| pipe가 추가·제거될 때 남은 pipe의 누적값을 유지한다는 규정 | 비율이 유지되는 근거 |
| 순서가 재현 가능하며 application이 의존할 수 있다는 선언 | 지금은 비보장 → 보장으로 넓히는 변경이다 |

마지막 항목이 **계약 확대**다. 기존 application은 순서에 의존할 수 없었으므로 깨지지
않지만, 앞으로는 이 순서가 계약이 되어 되돌리기 어렵다. 확대 여부를 먼저 판정한다.

### 4.2 `core/doc/spec/core/socket/06-dealer.ko.md` §1 option 표 (현재 `:64`)

| 지금 | 바꿀 것 |
|---|---|
| `ZLINK_DEALER_OPT_WEIGHT` = `int`, `0..100` | 3.3의 결정에 맞춘 범위 |

범위를 넓히면 **상한만 바뀌고 기존 값의 의미는 그대로**이므로 하위 호환이다. 검증 동작은
지금과 같이 **범위 밖 입력을 거부**하는 것으로 유지한다.

### 4.3 `core/doc/spec/core/07-monitoring.ko.md`

`ZLINK_EVENT_PEER_WEIGHT_CHANGED`가 알리는 값의 범위가 4.2와 같아야 한다. 범위를 넓히면
이 문서도 함께 고친다.

### 4.4 영문판 동기화

`06-dealer.en.md`, `07-monitoring.en.md`를 같은 내용으로 맞춘다. EN이 정본인 관례를 따르는
문서라면 EN을 먼저 고치고 KO를 맞춘다.

### 4.5 버전

선택 순서 보장 추가와 weight 범위 확대는 **동작이 관찰 가능하게 바뀌는 변경**이다. 어느
릴리스에 넣을지, 기존 순서에 의존하던 사용자가 있는지 확인한 뒤 릴리스 노트에 순서 변경을
명시한다.

## 5. 구현 순서

**이 작업을 framework보다 먼저 진행한다.** 다만 §1에서 정정했듯 **framework 갭을 닫기
때문이 아니다.** 이유는 다음이다.

1. Core 동작이 바뀌면 **bindings를 다시 배포해야** framework가 그 변경을 본다(§6). 배포가
   framework 작업보다 앞서야 소비자 참조가 꼬이지 않는다.
2. Core 변경은 framework 4언어와 독립적이라 병렬 대기 없이 진행할 수 있다.
3. Core spec 개정은 framework spec과 독립적으로 진행할 수 있다.

```
1. spec 개정 (§4)          06-dealer 선택 규칙 · option 범위 · 07-monitoring · 영문판
   │                        ※ §4.1 마지막 항목(순서 보장 확대) 판정이 먼저
   │
2. weight 범위 (결함 3)     lb.cpp:93-94 clamp 제거 + option 검증
   │                        가장 작고 독립적. 먼저 넣어 분산 오류를 닫는다
   │
3. tiebreak 키 확보 (결함 2) _pipes에서 안정적 식별자를 얻는 경로 (§3.2)
   │                        선택 절차 교체의 선행 조건
   │
4. 선택 절차 교체 (결함 1)   rebuild_weighted_schedule() → 누적값 방식
   │                        동일 weight 분기 제거를 함께
   │
5. 성능 확인               호출마다 O(N) 순회가 문제가 되면 주기 미리 계산 도입
   │
6. framework 정리          C++ connections_from_next() 선택·회전 삭제 (§3.4)
                           ※ 삭제 범위는 _auto_connections 확인 후
```

**2번은 승인이 선행이다.** 결함 3에서 정정했듯 이것은 버그 수정이 아니라 API 확장이다.
`0..100`으로 부족한 사용 사례가 확인되지 않으면 이 단계를 건너뛴다.

**3번이 이 작업의 실제 난이도다.** `_pipes`에 안정적 식별자가 없으므로 어디서 얻을지
정하는 것이 설계 결정이다(§3.2). 여기서 막히면 4번을 시작할 수 없다.

**6번은 framework 쪽이지만 이 작업의 일부로 함께 넣는다.** Core가 고쳐지면 framework의
회전 로직은 효과가 없을 뿐 아니라 오해를 부른다.

## 6. Bindings 배포

Core 동작이 바뀌므로 **bindings를 다시 배포해야 framework가 그 변경을 볼 수 있다.**
언어별 framework 작업은 이 배포 뒤에 시작한다.

### 6.1 버전

현재 Core는 `11.1.0`이다(`core/CMakeLists.txt:11`).

| 변경 | 성격 | 버전 |
|---|---|---|
| weight 범위 확대 | 상한만 늘어나고 기존 값의 의미는 그대로 — **하위 호환** | minor |
| 선택 순서 보장 추가 | 이전에 비보장이던 것을 보장으로 — 계약 **확대**. 기존 코드는 깨지지 않지만 관찰되는 순서가 바뀐다 | minor |

둘 다 minor이므로 **`11.2.0`**으로 올린다. 순서 변경은 breaking이 아니지만 릴리스 노트에
명시한다 — 순서에 의존하던 코드가 있으면 분포가 달라진다.

**C API 시그니처는 바뀌지 않는다.** binding 코드 수정 없이 재빌드만으로 반영된다.

### 6.2 버전 표기를 올릴 자리

| 자리 | 내용 |
|---|---|
| `core/CMakeLists.txt:11` | `project(zlink VERSION …)` |
| Core spec의 버전 표기 | `06-dealer` option 범위와 함께 |
| `bindings/native-artifacts.txt` | native 산출물 버전 marker |
| 각 binding의 package metadata | 아래 6.3 참조 |

binding package 버전은 **각 binding의 metadata가 소유한다**(`scripts/local-package/publish-all-wsl.sh`
머리말). Core 버전을 올렸다고 자동으로 따라가지 않으므로 각각 올린다.

### 6.3 로컬 패키지 배포

저장소에 이미 절차가 있다.

```
scripts/local-package/build-wsl.sh          로컬 빌드
scripts/local-package/publish-all-wsl.sh    Core 바이너리 정렬 + 로컬 패키지 생성
```

`publish-all-wsl.sh`는 두 단계를 수행한다.

1. 검증된 Core release 바이너리를 내려받아 Core 버전 marker를 맞춘다.
2. **.NET · Java/Kotlin · Node.js · C++ 로컬 패키지**를 만든다.

환경 변수는 그대로 통과한다 — `ZLINK_LOCAL_PACKAGE_ROOT`, `CONFIGURATION`,
`ZLINK_SKIP_NPM_CI`, `ZLINK_NODE_PACKAGE_MODE`, `ZLINK_CPP_LOCAL_BUILD_DIR`,
`ZLINK_CPP_INSTALL_PREFIX`.

### 6.4 순서

```
1. Core 구현 완료 + 검증 (§5, §7)
   │
2. core/CMakeLists.txt 버전 → 11.2.0, spec 버전 표기 동기화
   │
3. binding package metadata 버전 상향 (dotnet · java/kotlin · node · cpp)
   │
4. scripts/local-package/build-wsl.sh
   │
5. scripts/local-package/publish-all-wsl.sh <release-tag> --expect-version 11.2.0
   │
6. 소비자 상향 — framework 4언어가 새 로컬 패키지를 참조하도록
   │
7. 언어별 framework 작업 시작
```

**6번에서 소비자 격리에 주의한다.** dotnet은 CPM + nupkg, node는 tarball로 격리하고,
cpp는 ODR 혼합을 피해야 한다. 로컬 패키지가 아니라 기존 설치본을 계속 참조하면 Core
변경이 반영되지 않은 채 framework 작업을 시작하게 된다.

### 6.5 배포 후 확인

- 각 binding에서 `zlink_get_dealer_option(ZLINK_DEALER_OPT_WEIGHT)`가 새 상한을 받는다.
- weight `100 / 300`인 두 peer에 네 번 연속 제출하면 §7의 순서로 도착한다 — **binding
  레벨 test**로 확인한다. Core 단위 test만으로는 배포가 반영됐는지 알 수 없다.
- framework 4언어 빌드가 새 패키지로 통과한다.

## 7. 검증

- weight `100 / 300`인 두 peer에 네 번 연속 제출하면 `B, A, B, B` 순서로 도착한다
  (식별자는 A가 앞선다고 가정).
- weight가 같은 두 peer에 연속 제출하면 번갈아 도착한다.
- 같은 후보 집합과 같은 상태에서 항상 같은 순서가 나온다. 연결 순서를 바꿔도 결과가
  같다.
- weight `5000 / 10000`인 두 peer의 장기 선택 비율이 약 `1:2`에 수렴한다(3.3에서 확대를
  택한 경우).
- pipe가 추가·제거되어도 남은 pipe의 누적값이 유지되어 비율이 유지된다.
- 쓰기 실패로 pipe가 비활성화됐다가 복구되면 그 pipe가 다시 후보에 들어온다.
- 주기를 미리 계산하는 최적화를 넣는 경우, 절차를 매번 수행한 결과와 순서가 완전히 같다.

## 8. 영향 범위

| 영역 | 영향 |
|---|---|
| Core | `lb_t`, DEALER option 검증, spec 2개 문서 |
| Bindings | 없음. C API 시그니처는 그대로다. weight 범위를 확대하면 상한만 바뀐다 |
| Framework 4언어 | C++만 영향을 받는다 — ClientServer 채널 선택이 계약을 만족하게 되고 `connections_from_next()`를 삭제한다. .NET·Java·Node는 두 채널 종류 모두 framework에서 고르므로 영향이 없고, 각자의 선택 절차 결함은 별도로 고쳐야 한다 |
| 기존 application | 선택 **순서**가 바뀐다. 장기 비율은 유지된다. 순서에 의존하던 코드가 있으면 영향을 받지만, 기존 순서는 계약이 아니었다 |

## 9. 관련 문서

- [Framework 구현 갭 목록 A1](framework-internals-implementation-gaps.ko.md) —
  이 수정이 닫는 갭
- [Framework 대상 선택과 위치 캐시](../../framework/common/internals/06-routing-and-cache.ko.md) —
  누적값 절차와 주기 미리 계산 지침
