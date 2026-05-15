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
프로토콜 처리, 유틸리티 구현은 이 계층에서 관리한다. 이 계층의 헤더는 설치되지
않으며, 공개 계약이 아니다.

## Public Contract

```text
+------------------------------------------------------------+
| core/include/                                              |
| |-- zlink.h                                                |
| `-- zlink/                                                 |
|     |-- actor.h                                            |
|     |-- common.h                                           |
|     |-- core.h                                             |
|     |-- discovery.h                                        |
|     |-- message.h                                          |
|     |-- monitoring.h                                       |
|     |-- registry.h                                         |
|     |-- service.h                                          |
|     |-- service_common.h                                   |
|     |-- socket.h                                           |
|     `-- spot.h                                             |
+------------------------------------------------------------+
```

`zlink.h`는 집계 헤더다. 기능별 공개 계약은 `core/include/zlink/*.h`에
나누어 둔다. `service.h`도 집계 헤더로 유지하며, registry, discovery,
spot처럼 독립된 서비스 영역은 개별 헤더에 둔다.

## API Facade

```text
+------------------------------------------------------------+
| core/src/api/                                              |
| |-- actor/                                                 |
| |-- core/                                                  |
| |-- discovery/                                             |
| |-- message/                                               |
| |-- monitoring/                                            |
| |-- registry/                                              |
| |-- service/                                               |
| |-- socket/                                                |
| `-- spot/                                                  |
+------------------------------------------------------------+
```

`src/api`의 카테고리는 공개 헤더의 기능 구분과 맞춘다. 새 공개 C ABI 함수를
추가할 때는 같은 카테고리 아래에 구현 파일을 둔다. 내부 helper가 커지거나
상태를 갖기 시작하면 `src/runtime`으로 옮기고, API 계층에는 얇은 위임만 남긴다.

## Runtime

```text
+------------------------------------------------------------+
| core/src/runtime/                                          |
| |-- core/                                                  |
| |-- engine/                                                |
| |-- protocol/                                              |
| |-- services/                                              |
| |   |-- actor/                                             |
| |   |-- common/                                            |
| |   |-- control/                                           |
| |   |-- discovery/                                         |
| |   |-- registry/                                          |
| |   `-- spot/                                              |
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

서비스 런타임은 서비스 종류별로 나눈다. actor, discovery, registry, spot은
서로 다른 상태와 정책을 가지므로 같은 폴더에 섞지 않는다. 공통 기반 클래스나
공통 변환 유틸리티만 `services/common`에 둔다.

소켓 런타임은 소켓 패턴별로 나눈다. 공통 큐, 라우팅, lifecycle 처리는
`sockets/common`에 두고, DEALER, ROUTER, PUB/SUB, STREAM처럼 패턴별 의미가
있는 구현은 각 폴더에 둔다.

전송 계층은 transport scheme별로 나눈다. TCP, IPC, WS, TLS/WSS, PGM은
각각 주소 파싱, listener/connecter, transport 구현이 다르므로 같은 폴더에
섞지 않는다.

## Include Rule

소스 내부 include는 카테고리 접두사를 사용한다.

```cpp
#include "api/socket/socket_api_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "sockets/common/socket_base.hpp"
```

카테고리 하위 디렉토리를 전부 include path에 넣지 않는다. 이렇게 해야 파일을
옮겼을 때 숨은 include 의존성이 생기지 않고, 코드만 봐도 어느 계층의 파일인지
알 수 있다.

## Current Exception

`core/src/api/actor/service_spot_actor_api.cpp`는 아직 공개 C ABI 함수와 actor
상태 관리 코드가 많이 섞여 있다. multipart payload 처리, 결과 코드 변환,
actor 입력값 검증처럼 독립적인 helper는 `core/src/runtime/services/actor`로
옮겼다. 새 코드를 추가할 때는 이 파일을 더 키우지 말고, 상태와 정책을 가진
구현부터 같은 runtime actor 폴더로 옮긴다. 최종 목표는 API 파일에는 공개
함수의 검증과 위임만 남기는 것이다.
