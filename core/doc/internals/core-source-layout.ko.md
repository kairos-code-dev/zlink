# Core Source Layout

이 문서는 core 소스의 공개 계약, 공개 API 구현, 내부 런타임 구현을
어디에 두어야 하는지 설명한다. 대상 독자는 core 유지보수자다.

## Layer Model

```text
+--------------------------------------------------------------+
| Installed public contract                                    |
| core/include/zlink.h, core/include/zlink/*.h                 |
+--------------------------------------------------------------+
| C ABI implementation facade                                  |
| core/src/api/<category>/                                     |
+--------------------------------------------------------------+
| Runtime implementation                                       |
| core/src/runtime/<category>/                                 |
+--------------------------------------------------------------+
```

`core/include` 아래 파일은 설치되는 공개 계약이다. 바인딩과 C 사용자는
이 헤더에 선언된 타입과 함수만 안정적인 API로 볼 수 있다.

`core/src/api` 아래 파일은 공개 C ABI 함수의 구현 위치다. 이 계층은
인자 검증, 공개 결과 코드 변환, 공개 핸들 변환, 런타임 호출 위임을 맡는다.
런타임 상태 머신이나 큐, 프로토콜 세부 구현은 이 계층에 새로 추가하지 않는다.

`core/src/runtime` 아래 파일은 내부 구현 위치다. 소켓, 서비스, 전송 계층,
프로토콜 처리, 유틸리티 구현은 이 계층에서 관리한다. 이 계층의 헤더는 설치하지
않으며, 공개 계약이 아니다.

## Public Contract

```text
+------------------------------------------------------------+
| core/include/                                              |
| |-- zlink.h                                                |
| |-- zlink_enum.h                                           |
| |-- zlink_errno.h                                          |
| `-- zlink/                                                 |
|     |-- common.h                                           |
|     |   |-- common.h                                           |
|     |-- core/api.h                                         |
|     |-- eventing/api.h                                     |
|     |-- message/api.h                                      |
|     |-- socket/api.h                                       |
|     `-- service/                                           |
|         |-- actor.h                                        |
|         `-- spot.h                                         |
+------------------------------------------------------------+
```

`zlink.h`는 집계 헤더다. 기능별 공개 계약은 도메인별
`core/include/zlink/<domain>/api.h`(core, eventing, message, socket)와
`core/include/zlink/service/*.h`(actor, spot)에 나누어 둔다. 서비스 영역은
`service/` 아래 개별 헤더에 둔다. `zlink_enum.h`와 `zlink_errno.h`는 enum 상수와
errno 정의를 담은 top-level 공개 헤더이며 `common.h`가 포함한다.

## API Facade

```text
+------------------------------------------------------------+
| core/src/api/                                              |
| |-- actor/                                                 |
| |   `-- spot/                                              |
| |-- core/                                                  |
| |-- message/                                               |
| |-- monitoring/                                            |
| |-- service/                                               |
| |-- socket/                                                |
| `-- spot/                                                  |
|     |-- core/                                              |
|     |-- dispatch/                                          |
|     |-- request_reply/                                     |
|     `-- subject/                                           |
+------------------------------------------------------------+
```

`src/api`의 카테고리는 공개 헤더의 기능 구분과 맞춘다. 새 공개 C ABI 함수를
추가할 때는 같은 카테고리 아래에 구현 파일을 둔다. 내부 helper가 커지거나
상태를 갖기 시작하면 `src/runtime`으로 옮기고 API 계층에는 얇은 위임만 남긴다.
SPOT API 구현은 함수 의미에 따라 `spot/core`, `spot/dispatch`,
`spot/request_reply`, `spot/subject`로 나눈다. 이 하위 폴더는 공개 헤더가
아니라 C ABI 구현 facade의 소유 위치를 구분하기 위한 것이다.

## Runtime

```text
+------------------------------------------------------------+
| core/src/runtime/                                          |
| |-- core/                                                  |
| |-- engine/                                                |
| |-- protocol/                                              |
| |-- services/                                              |
| |   |-- actor/                                             |
| |   |   |-- async/                                         |
| |   |   |-- gateway/                                       |
| |   |   |-- lifecycle/                                     |
| |   |   |-- multipart/                                     |
| |   |   |-- packet/                                        |
| |   |   |-- result/                                        |
| |   |   `-- validation/                                    |
| |   |-- common/                                            |
| |   |-- control/                                           |
| |   `-- spot/                                              |
| |       |-- common/                                         |
| |       |-- data_plane/                                     |
| |       |-- dispatch/                                       |
| |       |-- node/                                           |
| |       |-- pubsub/                                         |
| |       |-- request_reply/                                  |
| |       `-- runtime/                                       |
| |-- sockets/                                               |
| |   |-- common/                                            |
| |   |-- dealer/                                            |
| |   |-- internal/                                          |
| |   |-- pair/                                              |
| |   |-- proxy/                                             |
| |   |-- pubsub/                                            |
| |   |-- router/                                            |
| |   `-- stream/                                            |
| |-- transports/                                            |
| |   |-- ipc/                                               |
| |   |-- pgm/                                               |
| |   |-- tcp/                                               |
| |   |-- tls/                                               |
| |   `-- ws/                                                |
| `-- utils/                                                 |
+------------------------------------------------------------+
```

서비스 런타임은 서비스 종류별로 나눈다. actor와 spot은
서로 다른 상태와 정책을 가지므로 같은 폴더에 섞지 않는다. 공통 기반 클래스나
공통 변환 유틸리티만 `services/common`에 둔다.

SPOT 런타임은 다시 역할별로 나눈다. 제어 프로토콜과 기본값처럼 여러 SPOT
구성요소가 함께 쓰는 작은 정의는 `spot/common`에 둔다. 데이터 플레인 전송
루프와 메시지 입출력은 `spot/data_plane`, dispatch worker와 내부 수신자는
`spot/dispatch`, Spot node 수명주기와 control task는 `spot/node`, pub/sub와
subject 관리는 `spot/pubsub`, 전체 runtime 소유권과 shutdown 흐름은
`spot/runtime`에 둔다. 이 구분은 파일 이름보다 역할을 앞세운다.

소켓 런타임은 소켓 패턴별로 나눈다. 공통 큐, 라우팅, lifecycle 처리는
`sockets/common`에 두고 DEALER, ROUTER, PUB/SUB, STREAM처럼 패턴별 의미가
있는 구현은 각 폴더에 둔다.

전송 계층은 transport scheme별로 나눈다. TCP, IPC, WS, TLS/WSS, PGM은
각각 주소 파싱, listener/connecter, transport 구현이 다르므로 같은 폴더에
섞지 않는다.

## Include Rule

소스 내부 include는 카테고리 접두사를 사용한다.

```cpp
#include "api/socket/socket_api_internal.hpp"
#include "services/mesh/mesh_runtime.hpp"
#include "sockets/common/socket_base.hpp"
```

카테고리 하위 디렉토리를 전부 include path에 넣지 않는다. 이렇게 해야 파일을
옮겼을 때 숨은 include 의존성이 생기지 않고 코드만 봐도 어느 계층의 파일인지
알 수 있다.

## Current Exception

`core/src/api/actor/spot/service_spot_actor_api.cpp`는 아직 공개 C ABI 함수와
actor 상태 관리 코드가 많이 섞여 있다. multipart payload 처리, packet frame
조립, 결과 코드 변환, actor 입력값 검증처럼 독립적인 helper는
`core/src/runtime/services/actor`의 하위 카테고리로 옮겼다. 새 코드를 추가할
때는 이 파일을 더 키우지 말고 상태와 정책을 가진 구현부터 같은 runtime actor
폴더로 옮긴다. 최종 목표는 API 파일에 공개 함수의 검증과 위임만 남기는 것이다.
