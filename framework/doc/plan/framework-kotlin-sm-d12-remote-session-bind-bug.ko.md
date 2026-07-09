# Kotlin SpotService SM-D12 Remote Session Bind Bug

이 문서는 Kotlin SpotService `SM-D12` 구현 중 확인한 Java/Kotlin framework 계층의 미완료 동작을
분리한 버그 리포트다. `core/`는 수정하지 않았고 native core build도 수행하지 않았다.

## 현상

`SM-D12`는 client가 `session-a` stream에 붙어 `play-a` actor로 bind한 뒤 연결을 끊고,
`session-b` stream에서 같은 `play-a` actor로 다시 bind해야 한다. Kotlin 재현에서는
`session-a`가 public `ZLinkRouteClient.requestToNode(...)`로 `play-a`의 `EnsureActorReq`를 호출하고
정상 reply를 받는다. 그러나 그 뒤 public `ZLinkSessionActors.bind(ActorRef)`로 remote actor ref를
bind하면 completion이 끝나지 않아 stream request가 timeout된다.

재현 로그:

```text
framework/languages/java/e2e-kotlin/SpotService/logs/20260707-190948-3221857/session-a-flow.log
message flow outcome=RECEIVED surface=STREAM_SESSION kind=REQUEST label=kotlin-sm-session-a packet=ActorRemoteAuthReq corr=1
message flow outcome=SENT surface=ROUTE_MESH_CHANNEL kind=REQUEST label=kotlin-sm-session-a packet=EnsureActorReq channel=spot.service.route src=play-a
message flow outcome=REPLY_RECEIVED surface=ROUTE_MESH_CHANNEL kind=RESPONSE label=kotlin-sm-session-a packet=EnsureActorReq channel=spot.service.route src=play-a

framework/languages/java/e2e-kotlin/SpotService/logs/20260707-190948-3221857/play-a-flow.log
message flow outcome=RECEIVED surface=ROUTE_MESH_CHANNEL kind=REQUEST label=kotlin-sm-play-a packet=EnsureActorReq channel=spot.service.route corr=1 src=session-a
message flow outcome=REPLIED surface=ROUTE_MESH_CHANNEL kind=REQUEST label=kotlin-sm-play-a packet=EnsureActorReq channel=spot.service.route corr=1 src=session-a

framework/languages/java/e2e-kotlin/SpotService/logs/20260707-190948-3221857/client-session-transfer.stderr.log
java.util.concurrent.TimeoutException: request timed out after PT5S
```

## 비교 범위

- `.NET` SpotService `SM-D12`는 구현되어 있고, `session-a`에서 만든 `play-a` actor state를
  `session-b` 재접속 뒤에도 유지하는 scenario가 있다.
- Node SpotService `SM-D12`는 구현되어 있고, `logs/20260629-220618-1691419` 선택 실행과
  `logs/20260630-074201-3148526` all 실행 proof가 문서화되어 있다.
- C++ SpotService `SM-D12`는 구현되어 있고, `Client/Scenarios/sm_d12_scenario.hpp`와 runner evidence
  검증이 있다.
- Java SpotService 문서에는 `SM-D12`가 아직 gap으로 남아 있다.

따라서 현재 실패는 core 전체 공통 실패로 단정하지 않는다. Kotlin E2E에서 사용하는 Java framework
session actor bind 경로의 remote `ActorRef` completion 문제로 먼저 분리한다.

## 재현 절차

현재 Kotlin SpotService에는 실패를 재현하기 위한 `session-transfer` client mode가 있다. scenario 완료
목록에는 넣지 않았으므로 명시적으로 mode를 지정해 실행한다.

```bash
cd framework/languages/java/e2e-kotlin/SpotService
ZLINK_CORE_BUILD_DIR=<tmp core build copy> \
ZLINK_LIBRARY_PATH=<tmp core libzlink.so copy> \
ZLINK_KOTLIN_E2E_MODES=session-transfer \
nice -n 10 timeout 300s ./run_e2e.sh all
```

기대 결과는 `session-a-flow.log`에서 `EnsureActorReq` reply까지 확인되고,
client가 `ActorRemoteAuthReq` 응답을 받지 못해 timeout되는 것이다.

## 다음 조사 지점

1. `ZLinkSessionActors.bind(ActorRef)`가 remote actor ref에 대해 어떤 completion 조건을 기다리는지 확인한다.
2. stream session dispatch handler 내부에서 remote actor bind completion이 같은 dispatch queue나 actor gateway
   callback에 의존해 교착되는지 확인한다.
3. Java SpotService의 `SM-D12`도 같은 public 경로로 재현되는지 확인하고, Java와 Kotlin이 같은 실패면
   Java framework runtime/session binding 계층에서 수정한다.
