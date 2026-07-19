# RouteMesh 10.0.0 — Core-blocked 버그 리포트

- 대상 버전: Core `10.0.0` (`core/include/zlink.h`: MAJOR=10 MINOR=0 PATCH=0)
- 작성 맥락: `.NET` framework S8-10 E2E 전환(`e2e/run_e2e_all.sh`) 중, framework/샘플/E2E 레인의 모든 원인을 소거한 뒤 **Core mesh wire admission 레이어에 귀속**된 결함군.
- framework 레인은 관련 완화(명시 은퇴 `DisconnectPeerLifetime(rid, generation)`, 재승인 폴링, presence-aware resolve 등)를 이미 관통했고, 아래 항목들은 **Core 수정 없이는 그린 불가**로 판정되어 이 리포트로 분리한다.
- 병렬 Core 세션이 `asio` engine·`mesh_wire`를 동시 수정 중이며 미커밋 `.so`가 혼재한다 → **Core 수렴 후 재검** 필요.
- 2026-07-19 현재 BUG-1·2·4·5의 Core 수정과 결정적 회귀 테스트를 반영했다.
  수정된 Core와 C++·Java·Node.js·.NET binding은 `10.1.0` local package로 배포했다.
  BUG-3과 framework E2E는 이 package를 사용해 재검한다.

핵심 파일:
- `core/src/runtime/services/mesh/mesh_wire_admission.cpp`
- `core/src/runtime/services/mesh/mesh_wire_ingress.cpp`
- `core/include/zlink/eventing/api.h` (mesh monitor ABI)

관련 E2E: RL-A2, TA-B3-recovery, SF-B2, SM-G1(crash-recovery), ST-F3, MON-A4-tail, MON-D1, MON-A5.

---

## BUG-1 (P0) — 지연 도착 pipe-term이 admitted successor를 강등하는 자기영속 flap

### 영향 시나리오
RL-A2(remap), TA-B3-recovery, SM-G1(crash-recovery), MON-A4 말미(same-rid 신규 endpoint 교체), MON-D1(kill→동일 endpoint 재기동 재승인). 전부 **같은 근본원인**.

### 증상
`kill -9`로 죽은 노드를 같은 RID로 재기동하면, 재기동 노드의 wire 재승인이 성공한 직후 **곧바로 다시 끊긴 것으로 관측**되고, 이후 `ERROR`(ENOTCONN) 상태로 고착한다. down-window 내내 소비자 쪽 peer가 `ready`로 유지되다가(EOF 감지 지연), replacement 승인 뒤에야 지연된 stale pipe-term이 도착해 살아 있는 successor를 죽이는 flap이 반복된다.

### 근본원인
`ZLINK_EVENT_DISCONNECTED` 모니터 이벤트가 **routing_id(RID)만** 싣고 오며, `handle_peer_down`이 그 RID를 공유하는 **모든 peer 엔트리**를 상태 검사 없이 강등한다. 방금 admit된 successor도 같은 RID이므로 함께 `ERROR`로 떨어진다.

`mesh_wire_ingress.cpp:1083`
```cpp
} else if (event.event == ZLINK_EVENT_DISCONNECTED) {
    if (event.routing_id.size > 0)
        handle_peer_down (node_, rid_bytes (event.routing_id));   // ← RID만 넘긴다
}
```

`mesh_wire_admission.cpp:286`
```cpp
void handle_peer_down (mesh_node_t *node_, const rid_bytes_t &rid_)
{
    ...
    //  Every lifetime sharing this RID rides the same transport: the
    //  admitted successor errors out and a DRAINING predecessor closes
    //  with the pipe.
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        peer_state_t &peer = node_->peers[i];
        if (peer.rid != rid_)                       // ← RID만으로 매칭
            continue;
        if (peer.state == ZLINK_MESH_PEER_ADMITTED) {
            peer.state = ZLINK_MESH_PEER_ERROR;     // ← 살아있는 successor까지 강등
            peer.last_error = ENOTCONN;
            ...
        } else if (peer.state == ZLINK_MESH_PEER_DRAINING) {
            peer.state = ZLINK_MESH_PEER_CLOSED;
            ...
        }
    }
    ...
}
```

주석의 전제("Every lifetime sharing this RID rides the same transport")가 **kill-9 후 재기동에서 깨진다**: predecessor의 pipe와 successor의 pipe는 물리적으로 **다른 전송(transport/pipe)**인데, 코드는 두 lifetime을 하나의 RID 키로 묶어 predecessor의 종료를 successor의 종료로 오귀속한다.

### 재현 절차
1. 노드 A(rid=`svc-b`)를 mesh에 admit.
2. 소비자 노드가 A와 admitted 상태.
3. A를 `kill -9`(정상 drain 아님).
4. A를 **같은 rid `svc-b`, 새 endpoint(또는 동일 endpoint)**로 재기동 → replacement 승인.
5. 소비자의 OS/전송 계층이 뒤늦게 old pipe의 EOF를 올림 → `ZLINK_EVENT_DISCONNECTED{routing_id=svc-b}`.
6. `handle_peer_down`이 방금 admit된 successor(`svc-b`)를 `ERROR/ENOTCONN`으로 강등.
7. teardown이 다음 admission 시도를 다시 죽여 flap → 결국 `ERROR` 고착, 재승인 불가.

### 기대 동작
지연된 pipe-term은 **그 pipe가 실제로 속한 lifetime에만** 적용되어야 한다. 방금 admit된 다른 lifetime(다른 pipe/generation)의 successor를 강등해서는 안 된다.

### 수정 방향(제안)
- `ZLINK_EVENT_DISCONNECTED`가 **pipe/route 식별자**(현재 route pipe와 대조 가능한 handle 또는 lifecycle_generation)를 함께 실어야 한다.
- `handle_peer_down`이 RID뿐 아니라 **그 이벤트를 유발한 실제 pipe가 현재 admitted lifetime의 pipe와 동일한지** 대조한 뒤에만 강등한다. 다른(과거) pipe면 무시.
- 대안: successor admit 시점에 predecessor pipe의 잔여 term 이벤트를 명시적으로 **draining generation에만** 라우팅.

### framework 측 현황
`DisconnectPeerLifetime(rid, generation)` seam(`ZLinkSpotNodeRuntime.cs:317` → `Node.DisconnectPeerLifetime`)으로 명시 은퇴는 이미 관통한다. 다만 은퇴 시점엔 이미 predecessor가 `CLOSED`(=605 NotFound)로 확인되므로, **지연 도착하는 stale pipe-term** 자체는 framework가 선제 차단할 수 없다(전송 계층 타이밍은 Core 소관).

### 수정과 회귀 테스트

Core는 `CONNECTION_READY`에서 얻은 local·remote address 쌍을 admitted lifetime에 기록하고,
`DISCONNECTED`의 RID와 물리 연결 식별이 모두 일치할 때만 상태를 내린다.
`test_stale_transport_disconnect_preserves_admitted_successor`는 predecessor 종료를 주입한 뒤
successor가 `ADMITTED`로 유지되는지 검증하며 통과한다.

---

## BUG-2 (P0) — 동일 프로세스 lifetime의 disconnect→reconnect 재승인이 ESTALE로 영구 거부

### 영향 시나리오
RL-A2, TA-B3-recovery(명시 disconnect → member 제거 → 같은 프로세스에서 재연결).

### 증상
같은 프로세스 lifetime 안에서 peer를 `disconnect`한 뒤 다시 `connect`하면, 재승인이 `ESTALE`로 거부되어 회복되지 않는다.

### 근본원인
재승인 eligibility가 `CONNECTING`/`ERROR`/`ADMITTED` 상태의 intent만 재수용한다. peer가 `CLOSED`로 내려간 뒤에는 endpoint로도 RID로도 다시 집히지 않는다.

`mesh_wire_admission.cpp:24` (RID 조회 — CLOSED 제외)
```cpp
peer_state_t *find_peer_by_rid_locked (mesh_node_t *node_, const rid_bytes_t &rid_)
{
    ...
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        if (node_->peers[i].rid != rid_ || node_->peers[i].state == ZLINK_MESH_PEER_CLOSED)
            continue;                                   // ← CLOSED는 조회 대상에서 제외
        ...
    }
    return fallback;
}
```

`mesh_wire_admission.cpp:40` (endpoint 조회 — CLOSED 재수용 안 함)
```cpp
peer_state_t *find_intent_by_endpoint_locked (mesh_node_t *node_, const std::string &endpoint_)
{
    //  A re-established transport re-runs the handshake, so intents in the
    //  ERROR or ADMITTED state are eligible again alongside CONNECTING.
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        peer_state_t &peer = node_->peers[i];
        if (peer.endpoint != endpoint_ || peer.inbound)
            continue;
        if (peer.state == ZLINK_MESH_PEER_CONNECTING || peer.state == ZLINK_MESH_PEER_ERROR
            || peer.state == ZLINK_MESH_PEER_ADMITTED)  // ← CLOSED 누락
            return &peer;
    }
    return NULL;
}
```

`validate_admission_locked`의 expected-RID/stale-generation 분기(`mesh_wire_admission.cpp:104`, `112`)가 `ESTALE`를 반환한다.
```cpp
if (intent_ && intent_->has_expected_rid) {
    const rid_bytes_t expected = intent_->expected_rid;
    if (expected != source_rid_) {
        errno = ESTALE;                                 // (A) 기대 RID 불일치
        return -1;
    }
}
const peer_state_t *existing = find_peer_by_rid_locked (node_, source_rid_);
if (existing && existing->state == ZLINK_MESH_PEER_ADMITTED
    && existing->lifecycle_generation > descriptor_.lifecycle_generation) {
    errno = ESTALE;                                     // (B) stale generation
    return -1;
}
```

`handle_peer_down`이 predecessor를 `CLOSED`로 내린 뒤(BUG-1 flap 포함), 같은 RID로 오는 재연결은 (a) CLOSED 엔트리를 재수용하지 못하거나, (b) 남아 있는 higher-generation admitted 잔재와 충돌해 `ESTALE`로 영구 거부된다.

### 기대 동작
같은 프로세스가 동일 RID로 명시 재연결하면, 이전 lifetime이 CLOSED여도 **새 lifetime으로 재승인**되어야 한다(현재는 `ERROR`/`CONNECTING`만 same-lifetime 재수용).

### 수정 방향(제안)
- `find_intent_by_endpoint_locked`가 `CLOSED` 상태의 동일 endpoint outbound intent를 **새 handshake 후보로 재수용**하거나, 재연결 시 CLOSED 엔트리를 새 CONNECTING intent로 승격.
- `validate_admission_locked`의 stale-generation 판정이 CLOSED predecessor를 비교 대상에서 제외.

### 재판정과 수정

결정적 회귀 테스트에서 CLOSED row 자체보다 outbound connector가 남아 자동 reconnect를 계속
소유하는 것이 직접 원인이었다. disconnect와 intent 제거가 논리 상태만 닫고 물리 connector를
종료하지 않아 같은 endpoint의 새 intent와 충돌했다. Core는 완전 제거 시 endpoint connector를
종료하고, inbound peer는 RID route를 종료한다. `MIXED`에서 manual source만 제거하면 discovery
source와 연결은 유지한다. `test_outbound_disconnect_then_reconnect_same_endpoint`가 같은 endpoint
재연결과 재승인을 검증하며 통과한다.

---

## BUG-3 (P1) — store 회복 창의 zombie transport (SF-B2)

### 영향 시나리오
SF-B2: location store(paused Redis) 장애→회복 창에서, 기존 연결로 계속 서빙되어야 하는데 전송이 좀비 상태로 남는다.

### 증상
store가 잠깐 멈췄다가 회복되는 동안, admitted 상태였던 전송이 EOF 감지 지연으로 좀비처럼 남아 후속 요청이 성립하지 않는다. framework 측에서 "store 부재만으로 admitted transport를 은퇴하지 않도록" 환원했음에도(SF-A1/A2/B1 통과) B2만 잔존.

### 근본원인(추정)
Core의 전송 EOF/liveness 감지 지연으로, 실제로 죽은 pipe가 admitted로 유지되거나(BUG-1과 동일 계열의 감지 타이밍), 반대로 살아 있는 연결이 store blip에 얽혀 회복 신호를 못 받는 구간. 정확한 원인은 Core `asio` engine 수정과 얽혀 있어 **Core 수렴 후 재계측 필요**.

### 재현 절차
1. 소비자–공급자 admitted.
2. Redis를 `pause`(명령 응답 보류).
3. `StoreFailureGrace`(30s) 안에 기존 연결로 요청 지속.
4. Redis `unpause`.
5. 회복 후 후속 요청/evidence 회복 여부 관측 → B2에서 좀비 전송으로 정지.

### 기대 동작
store 장애는 **기존 연결에 영향을 주지 않아야** 한다(자동 연결의 신규 판단만 유예). 회복 시 정상 재개.

---

## BUG-4 (P1) — bound-session cross-move에서 pump 레코드 순서 역전 (ST-F3)

### 영향 시나리오
ST-F3: bound-session cross-move ordering. 액터가 노드 간 이주(transfer)하는 동안, 게이트 창에서 native 세션으로 보낸 S1/S2 등의 프레임이 소스 pump에 **역순(S1,S2,S4,S3 형태)으로 표면화**되는 flake(~1/3).

### 증상
```
Actor '...' handoff_packet order mismatch: S1,S2,S4,S3.
```
쌍별 스왑(pairwise swap)이 간헐 발생. 무손실은 보장되나 **arrival order(spec 23 §10.2)**가 깨진다.

### 근본원인(판정 완료 — Core 소관)
framework 전 구간에서 순서 보존을 **배제 완료**했다:
- 클라 connector 바운디드 채널: `SingleReader` FIFO.
- 노드 ingress: serial executor.
- 세션별: `ZLinkStreamSessionSerialExecutor`로 per-session 직렬.
- relay await + pump `RaiseActor`는 pump 스레드 동기 호출.
- capture 지점을 detached 파이프라인에서 **pump 이벤트 순서가 보장되는 ingress**로 이동.
- entry pump 배치 dispatch·frame-relay 수신을 per-actor FIFO 체인으로 직렬화.

계측 결과 **실패 런에서 S1/S2가 ingress에서 pump 순서 그대로 캡처됐는데 그 pump 순서 자체가 이미 S2,S1**이었다. 즉 역전 창은 framework 아래, **Core의 `SendBoundActor` → actor record 표면화 구간**이다. ST-F3 경로는 세션 relay가 아니라 `EnableActorDispatch`의 native 바인딩 직행(client→stream node→core actor records→pump)이므로, 순서 보장 책임이 Core actor-record 큐잉에 있다.

### 재현 절차
1. bound-session 액터를 소스 노드에 두고 transfer 게이트 진입.
2. 게이트 창에서 같은 세션으로 S1..S4를 순차 전송.
3. 소스 pump가 capture한 `handoff_backlog` 순서를 target replay 순서와 대조.
4. ~1/3 확률로 인접 쌍 스왑 관측(부하 ~8에서 재현율 상승).

### 기대 동작
같은 세션·같은 액터로 순차 제출된 프레임은 Core actor record 표면화까지 FIFO 순서를 유지해야 한다(spec 23 §10.2 arrival order).

### 수정 방향(제안)
Core `SendBoundActor` 경로에서 액터 record가 큐에 올라가는 구간의 순서 보장을 점검. 동일 (session, actor)에 대한 연속 submit이 record surfacing에서 재정렬되지 않도록 직렬화.

### 수정과 회귀 테스트

`session_to_actor_submit`이 service mutex 아래에서 binding을 확인한 뒤 mutex를 해제하고 Actor
mailbox admission을 수행해, 두 동시 submit이 그 경계에서 역전될 수 있었다. binding 확인과
mailbox admission을 같은 mutex 구간으로 합쳤다. 회귀 테스트는 첫 submit을 두 단계 사이에서
멈추고 두 번째 submit을 시작해 수정 전 `S2,S1`, 수정 후 `S1,S2`를 결정적으로 검증한다.

---

## BUG-5 (P2) — mesh monitor 이벤트의 binding 표면 미노출 (MON-A5)

### 영향 시나리오
MON-A5: `HandshakeFailed`(무자격 TCP handshake 실패) 관측.

### 증상
무자격 TCP가 handshake에서 실패하면 peer 엔트리가 생성되지 않아 **폴링(snapshot) 기반으로는 관측 불가**. framework의 mesh 이벤트 브리지(`AddMeshNodeEvents`)는 snapshot 파생 이벤트라 handshake 실패를 표면화하지 못한다.

### 근본원인
Core에는 mesh monitor 이벤트 종류가 정의돼 있으나(`core/include/zlink/eventing/api.h`: `ZLINK_MESH_MONITOR_PEER_REJECTED` 등, ABI v1), 이를 **binding으로 노출하는 표면**(`zlink_mesh_node_monitor_*` 계열)이 bindings에 없다. framework는 실패한 handshake에 대한 push 이벤트를 받을 경로가 없다.

```c
/* core/include/zlink/eventing/api.h */
#define ZLINK_MESH_MONITOR_ABI_VERSION 1u
ZLINK_MESH_MONITOR_PEER_REJECTED = 6,   /* ← 존재하나 binding 미노출 */
```

### 기대 동작
mesh monitor 이벤트 스트림(REJECTED/HandshakeFailed 포함)을 소비할 수 있는 binding 표면이 노출되어야, framework가 `ZLinkMeshRuntimeEvent`로 승격해 MON-A5를 관측할 수 있다.

### 수정 방향(제안)
Core `zlink_mesh_node_monitor_*` binding surface(이벤트 recv API)를 bindings에 노출 → framework가 snapshot 폴링 대신 push 이벤트로 REJECTED/HandshakeFailed를 브리지.

### Core 수정과 남은 작업

Core API는 이미 공개되어 있었으므로 “Core API 부재”는 잘못된 판정이었다. 실제 Core 결함은
RID 생성 전 socket handshake 실패를 MeshNode monitor로 승격하지 않은 것이다. raw TCP 실패를
zero-RID `PEER_REJECTED`/`EPROTO`로 전달하도록 수정했고
`test_raw_handshake_failure_emits_peer_rejected`가 통과한다.

C++·Java·Node.js·.NET binding에는 monitor 열기, 이벤트 수신, 현재 상태 조회, 닫기 표면을
추가했다. 이벤트에 owner 정보가 적용되지 않을 때 Core가 전달하는 `owner_kind=0`도 Java에서
오류로 바꾸지 않고 `null`로 보존한다. `.NET` framework event bridge는 snapshot 폴링 대신
native monitor를 읽으며, zero-RID `PEER_REJECTED`를 peer 식별자 없는 `HandshakeFailed`로
변환한다.

수정 결과는 다음 local package에 포함했다.

- `.artifacts/wsl/nuget/Systems.Zlink.10.1.0.nupkg`
- `.artifacts/wsl/maven/systems/zlink/zlink/10.1.0/zlink-10.1.0.jar`
- `.artifacts/wsl/npm/zlink-systems-zlink-10.1.0.tgz`
- `.artifacts/wsl/install/zlink-cpp/10.1.0`

`.NET` framework는 `10.1.0`을 참조해 빌드된다. Java·Node.js·C++ framework는 제거된 9.x
`SpotNode` 표면을 아직 사용하므로, 전환 작업이 끝나기 전까지 기본 참조를 `9.0.4`로 유지한다.
이는 binding package 배포 누락이 아니라 framework 전환 작업의 선행 조건이다.

---

## 10.1.0 framework E2E 재검에서 확인한 추가 결함

### BUG-6 (P0) — 같은 RID의 새 프로세스가 Actor generation을 다시 사용

SM-G1에서 owner lease 만료 뒤 old ActorRef가 `ActorRouteNotFound`로 실패하는 단계까지는 통과했다.
같은 RID `play-a`로 새 프로세스를 시작하고 같은 Actor ID를 다시 만들면 새 ActorRef의 generation이
old ActorRef와 같아 다음 gate가 실패한다.

```
SM-G1 same-rid restart reused the previous actor generation.
```

Core의 `mesh_node_t`는 `next_actor_generation`을 항상 1로 초기화하고
`zlink_mesh_node_actor_new`가 이 process-local counter를 사용한다. 생성 API에는 location store가
발급한 단조 증가 generation을 전달하는 인자가 없다. 따라서 Framework가 Core ActorRef와 wire
검증을 유지하면서 안전하게 우회할 수 없다.

재현 로그:
`framework/languages/dotnet/e2e/SpotService/logs/20260719-094640-1707677`

기대 동작은 같은 MeshName·RID·Actor ID라도 새 생성 수명이 이전 ActorRef generation을 재사용하지
않는 것이다. Core가 node lifecycle을 포함해 Actor generation을 할당하거나, 외부의 영속 generation을
안전하게 주입하는 정식 계약이 필요하다.

### BUG-7 (P0) — peer 하나 제거 뒤 다른 channel target까지 사라짐

SF-B1 단독과 SF-B2 단독은 10.1.0에서 각각 통과했다. 하지만 `SF-A2 → SF-B1` 순서에서는
polling-only consumer와 `api-c`를 추가·제거한 뒤 main consumer의 기존 `api-a`·`api-b` channel
target이 사라지고 첫 store-outage request가 `SubmitResult.NotFound`로 실패한다.

재현 로그:
`framework/languages/dotnet/e2e/StoreFailure/logs/20260719-095054-1716875`

이 결과는 store failure grace 자체의 결함이 아니다. 같은 binary에서 SF-B1만 실행하면 통과하고,
SF-A2가 먼저 peer add/remove를 일으킨 경우에만 기존 target set이 손상된다. endpoint별 disconnect가
해당 peer만 은퇴시키는지, peer removal이 channel admission table의 다른 target을 제거하는지 Core
회귀 테스트가 필요하다.

### BUG-8 (P1) — admitted peer 종료가 monitor `PEER_CLOSED`로 전달되지 않음

MON-A4에서 native monitor bridge는 두 endpoint의 `PEER_ADMITTED`를 받아 `ConnectionReady`로
표면화했다. 그러나 첫 endpoint를 drain하고 process를 종료한 뒤 30초 동안 `PEER_CLOSED`가 오지 않아
`Disconnected` evidence gate가 timeout으로 실패했다.

재현 로그:
`framework/languages/dotnet/e2e/RuntimeMonitoring/logs/20260719-095420-1721269`

monitor open/recv와 Framework mapping은 실제 admission event로 검증됐으므로 binding 연결 부재가
원인이 아니다. Core가 admitted transport 종료를 `PEER_CLOSED`로 push하는지, 지연된 pipe 종료가
현재 peer row와 매칭되지 않아 event가 누락되는지 확인해야 한다.

## 요약표

| ID | 우선순위 | 시나리오 | 파일:라인 | 근본원인 | 상태 |
|----|:---:|---|---|---|---|
| BUG-1 | P0 | RL-A2·TA-B3·SM-G1·MON-A4-tail·MON-D1 | `mesh_wire_ingress.cpp`, `mesh_wire_admission.cpp` | 지연 pipe-term을 RID만으로 오귀속 | 10.1.0 Core 회귀 통과, TA-B3·MON-A4 E2E 잔여 |
| BUG-2 | P0 | RL-A2·TA-B3-recovery | `mesh_node_api.cpp`, `mesh_wire.cpp` | 논리 disconnect 뒤 connector가 남아 재연결 충돌 | 10.1.0 Core 회귀 통과, TA-B3 E2E 잔여 |
| BUG-3 | P1 | SF-B2 | `mesh` 전송 EOF/liveness (asio) | store blip 회복 창의 zombie transport | 10.1.0 SF-B2 단독 통과 |
| BUG-4 | P1 | ST-F3 | `mesh_stream_session_api.cpp` | binding 확인과 mailbox admission 사이 mutex 해제 | 10.1.0 ST-F3 포함 ST-A1~F4 통과 |
| BUG-5 | P2 | MON-A5 | `mesh_wire_ingress.cpp`, 각 binding | pre-admission 실패 미승격과 binding 표면 부재 | 10.1.0 bridge 연결, MON-A4에서 선행 중단 |
| BUG-6 | P0 | SM-G1 | `mesh_runtime.cpp`, `mesh_actor_api.cpp` | process restart 때 Actor generation counter 재사용 | 재현, Core 수정 필요 |
| BUG-7 | P0 | SF-A2→SF-B1 | peer disconnect·channel admission | peer 제거 뒤 기존 channel target set 손상 | 재현, Core 수정 필요 |
| BUG-8 | P1 | MON-A4 | mesh monitor peer-close 경로 | admitted transport 종료 event 누락 | 재현, Core 수정 필요 |

## 재검 절차(Core 수렴 후)
1. `10.1.0` local package와 `core/build/lib/libzlink.so`가 같은 Core source에서 만들어졌는지 확인.
2. BUG-1/2: `e2e/ResilienceLifecycle`(RL-A2), `e2e/ToActorMessaging`(TA-B3), `e2e/SpotService`(SM-G1), `e2e/RuntimeMonitoring`(MON-A4/D1) 재실행.
3. BUG-3: `e2e/StoreFailure`의 SF-B2 단독 통과를 유지하고 BUG-7 수정 뒤 전체 순서 재실행.
4. BUG-4: `e2e/SpotActorTransfer` 전체 재실행으로 ST-F5 이후 시나리오까지 확인.
5. BUG-5·8: `.NET` framework의 native monitor bridge로 `e2e/RuntimeMonitoring` 전체 재실행.
6. BUG-6: `e2e/SpotService`의 SM-G1에서 same-RID 재시작 뒤 새 ActorRef generation과 old ref
   `ActorLocationStale`을 함께 확인.
