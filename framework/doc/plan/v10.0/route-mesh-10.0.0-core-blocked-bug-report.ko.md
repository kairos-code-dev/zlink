# RouteMesh 10.0.0 — Core-blocked 버그 리포트

- 최초 발견 버전: Core `10.0.0`
- 수정·로컬 배포 버전: Core/bindings `10.1.0`
- 작성 맥락: `.NET` framework S8-10 E2E 전환(`e2e/run_e2e_all.sh`) 중, framework/샘플/E2E 레인의 모든 원인을 소거한 뒤 **Core mesh wire admission 레이어에 귀속**된 결함군.
- framework 레인은 관련 완화(명시 은퇴 `DisconnectPeerLifetime(rid, generation)`, 재승인 폴링, presence-aware resolve 등)를 이미 관통했고, 아래 항목들은 **Core 수정 없이는 그린 불가**로 판정되어 이 리포트로 분리한다.
- 2026-07-19 현재 BUG-1·2·4·5·8의 Core 수정과 결정적 회귀 테스트를 반영했다.
- 같은 Core 산출물을 포함한 C++·Java·Node.js·.NET `10.1.0` local package를 다시 만들었다.
  이 문서는 package 위치와 무결성만 기록한다. framework E2E 판정은 framework 담당 작업자가
  이 package를 적용해 수행한다.

핵심 파일:
- `core/src/runtime/services/mesh/mesh_wire_admission.cpp`
- `core/src/runtime/services/mesh/mesh_wire_ingress.cpp`
- `core/include/zlink/service/mesh_node.h` (mesh monitor ABI)

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

local·remote address 쌍은 재연결할 때 다시 사용되므로 물리 연결 식별자로 사용할 수 없다.
Core transport endpoint가 프로세스 안에서 단조 증가하는 `connection_id`를 할당하고, raw socket
monitor의 비공개 payload가 이 값을 MeshNode ingress에 전달하도록 수정했다. admitted lifetime은
승인 당시의 `connection_id`를 보관한다. `DISCONNECTED`는 RID와 이 값이 모두 일치하는 lifetime만
상태를 변경한다.

HELLO가 raw monitor의 READY보다 먼저 수신되는 순서도 허용한다. 이때 admission을 먼저 완료한 뒤
READY를 받으면 현재 RID의 lifetime에 transport 식별자를 보충한다. READY에는 실제 물리 연결
변화임을 나타내는 비공개 flag를 넣어, ready 개수 집계 이벤트를 새 연결로 잘못 처리하지 않는다.
READY 중복 제거 key에도 `connection_id`를 포함하므로 같은 endpoint·RID를 사용하는 successor의
실제 READY edge가 predecessor와 합쳐지지 않는다.

같은 RID와 lifecycle generation을 가진 두 번째 HELLO는 공개 계약대로 `EADDRINUSE` conflict로
거부한다. 거부된 물리 pipe도 함께 종료해 connector가 기존 lifetime의 실제 종료 뒤 다시
handshake할 수 있게 한다. 더 높은 generation의 HELLO가 READY보다 먼저 도착하면 predecessor의
`connection_id`를 successor에 복사하지 않고, 뒤따르는 READY가 새 값을 채운다.
공개 raw monitor event 구조와 일반 ROUTER 전달 계약은 바꾸지 않았다.

`test_stale_transport_disconnect_preserves_admitted_successor`는 이전 transport의 종료를 주입한 뒤
successor가 `ADMITTED`로 유지되는지 검증한다. HELLO-before-READY 선택 회귀는 predecessor ID를
복사하지 않는 경우와 새 READY가 먼저 도착한 경우를 구분해 검증한다.
`test_mesh_peer_admission` 16개 test가 모두 통과했다.

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
source와 연결은 유지한다. peer 상태 변경과 connector·route 변경은 `peer_connection_mutex`로
하나의 전이처럼 직렬화한다. 새 연결이 이전 ROUTER pipe의 비동기 정리를 기다리다 거부되지 않도록
기존 ROUTER의 표준 `ZLINK_RID_DUPLICATE_HANDOVER` 정책을 사용한다. 별도의 Spot drop/NODROP 정책은
추가하지 않았다.

`test_outbound_disconnect_then_reconnect_same_endpoint`는 parent와 child 사이에 명시적인 phase
barrier를 두고 첫 admission 관찰, disconnect, 같은 endpoint 재연결, 두 번째 admission을 순서대로
검증한다. 시간 지연에 의존하던 기존 테스트는 child가 너무 빨리 종료되어 실제 admission을 놓칠 수
있었다. barrier 적용 뒤 50회와 후속 30회 반복이 모두 통과했다.

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
mailbox admission을 수행해, 두 동시 submit이 그 경계에서 역전될 수 있었다. service 전체 mutex를
mailbox backpressure 구간까지 유지하면 한 session의 정체가 다른 session과 shutdown까지 막는다.
따라서 session RID별 submit-order mutex를 사용해 같은 session만 직렬화하고, 서로 다른 session은
독립적으로 진행하도록 수정했다.

회귀 테스트는 첫 submit을 두 단계 사이에서 멈추고 두 번째 submit을 시작해 같은 session의
`S1,S2` 순서를 확인한다. 이어서 첫 session을 같은 위치에서 멈춘 상태로 다른 session의 `X1`
submit이 완료되는지도 검증한다.

추가 재리뷰에서 transfer fence가 같은 session gate를 사용하지 않아 binding 재검 직후 기존
mailbox로 우회할 수 있는 경쟁 조건을 확인했다. transfer fence, disconnect와 unbind도 같은
session gate를 사용하도록 맞췄다. fence는 전역 session registry 아래에서 기다리지 않고 service
수명만 고정한 뒤 registry를 해제하므로, 한 session의 backpressure가 다른 service 조회·종료를
막지 않는다. session gate는 연결된 session이 소유하고 disconnect 때 제거하므로 submit마다
mutex를 다시 할당하거나 다른 session map 전체를 순회하지 않는다.

회귀 테스트는 submit을 mailbox admission 직전에 멈추고 transfer fence를 별도 스레드에서 시작한다.
fence가 앞선 submit을 추월하지 않는지, fence 완료 뒤 새 submit이
`ZLINK_SUBMIT_BACKPRESSURED`/`EAGAIN` transfer 경로를 사용하는지 확인한다.
`test_mesh_node_basic` 11개 test가 모두 통과했다.

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
zero-RID `PEER_REJECTED`/`EPROTO`로 전달하도록 수정했다. 이 오류는 conflict가 아니므로
`result_code=ZLINK_CONNECT_INTERNAL_ERROR`로 분류한다.
`test_raw_handshake_failure_emits_peer_rejected`가 result와 errno를 정확히 검증하며 통과한다.

C++·Java·Node.js·.NET binding에는 monitor 열기, 이벤트 수신, 현재 상태 조회, 닫기 표면을
추가했다. 이벤트에 owner 정보가 적용되지 않을 때 Core가 전달하는 `owner_kind=0`도 Java에서
오류로 바꾸지 않고 `null`로 보존한다. `.NET` framework event bridge는 snapshot 폴링 대신
native monitor를 읽으며, zero-RID `PEER_REJECTED`를 peer 식별자 없는 `HandshakeFailed`로
변환한다.

수정 결과는 다음 local package에 포함했다. 이 위치는 framework 담당자가 적용할 입력이며,
framework 검증 완료를 뜻하지 않는다.

- `.artifacts/wsl/nuget/Systems.Zlink.10.1.0.nupkg`
- `.artifacts/wsl/maven/systems/zlink/zlink/10.1.0/zlink-10.1.0.jar`
- `.artifacts/wsl/npm/zlink-systems-zlink-10.1.0.tgz`
- `.artifacts/wsl/install/zlink-cpp/10.1.0`

framework의 각 언어별 참조 버전 변경과 E2E는 framework 담당 작업자가 수행한다. package 생성
작업에서 framework source나 runner를 변경하지 않았다.

---

## 10.1.0 framework E2E 재검에서 확인한 추가 결함

### BUG-6 — Core 버그 아님: Actor generation의 프로세스 재시작 단조 증가 요구

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

기존 S5 Core review 기록을 다시 확인한 결과, Core Actor generation은 한 MeshNode 프로세스 안에서
Actor 수명을 구분하는 값이며 프로세스 재시작을 넘어 영속 단조 증가한다는 공개 계약이 없다. Core는
durable location state를 소유하지 않으므로 외부 store generation을 Core ActorRef에 주입하는 API를
추가해서도 안 된다.

따라서 이 항목은 Core 버그에서 제외한다. framework의 crash-recovery 판단은 Node lifecycle과
location row가 소유하는 영속 수명을 함께 사용해야 하며, E2E가 Core Actor generation의 값 자체가
달라야 한다고 단언해서는 안 된다. 이 항목 때문에 Core 공개 계약이나 API는 변경하지 않았다.

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

### 수정과 회귀 테스트

두 결함을 함께 수정했다.

1. `handle_peer_down`이 `DRAINING` lifetime을 `CLOSED`로 바꿀 때 상태 변경 여부를 표시하지 않아
   이후 readiness 재계산과 `PEER_CLOSED` emit 단계가 생략됐다. 이 전이도 변경으로 처리한다.
2. BUG-1의 `connection_id` 매칭과 READY 후속 보충으로 실제 종료 이벤트가 정확한 lifetime에
   도달하도록 했다.

`test_peer_drain_and_reconnect`는 첫 peer admission을 parent가 확인한 뒤에만 child를 종료하는
phase barrier를 사용한다. 첫 `PEER_CLOSED`, admitted count 0, 같은 RID의 재연결과 요청·응답을
검증하며 30회 반복해서 모두 통과했다. framework의 MON-A4 재실행은 framework 담당자에게 넘긴다.

### BUG-9 (P0) — 10.1.0 local package native payload와 현재 Core build 불일치

Framework package verifier는 8개 package의 API snapshot과 빈 NuGet consumer를 통과했다. 그러나
`Systems.Zlink.10.1.0.nupkg`의 Linux x64 native payload와 현재 Core runtime의 SHA-256이 다르다.

```
NuGet runtimes/linux-x64/native/libzlink.so
7aa845d48977dcf3eba8217db3a3c4c6af55707104f69e3f8c2968e4d62804a5

core/build/lib/libzlink.so
d0365a2c511e751fd45e199e91959d1b6e476ea2b2c80c2702bdc6193111f42e
```

또한 같은 10.1.0 NuGet의 Linux arm64 payload 이름은 `libzlink.so.9`로 남아 있었다. 현재 E2E가
검증한 native runtime과 source/build 결과를 하나의 release candidate로 귀속할 수 없으므로
S8-11 package/native 일치 gate를 완료할 수 없었다.

### 수정과 로컬 배포 결과

Core 전체 86개 test와 별도 backpressure 종료 회귀 10회를 통과한 source에서
`libzlink.so.10.1.0`을 다시 만들었다.

```text
Core runtime SHA-256
8361fe95c7f31432554d1a151b87b66f76764bf07c68c77671c8ffcbb9a7f198
```

네 package 안의 Core runtime SHA-256은 모두 위 값과 같다. 현재 작업 환경에서 만들고 검증한
Linux x86-64 runtime만 포함하며, 검증하지 않은 다른 플랫폼의 이전 native ABI는 package에서
제외했다.

| binding | local package | package SHA-256 |
|---|---|---|
| .NET | `.artifacts/wsl/nuget/Systems.Zlink.10.1.0.nupkg` | `3490a376f66217a39e3b65460c85ed5d3aee9eb493d10e55cab6451cc4c18d49` |
| Java | `.artifacts/wsl/maven/systems/zlink/zlink/10.1.0/zlink-10.1.0.jar` | `69774e5b74724fa4e8f42f73060c4280d1183232a4e77dc237a8f1440b44aefc` |
| Node.js | `.artifacts/wsl/npm/zlink-systems-zlink-10.1.0.tgz` | `5ff5f3227074940d6ec67508c06362087ad13d6b60cf79d5c189250c29cd6491` |
| C++ | `.artifacts/wsl/install/zlink-cpp/10.1.0` | runtime SHA-256이 위 Core 값과 일치 |

runtime SONAME은 `libzlink.so.10`이고, 네 package에서
`zlink_mesh_node_monitor_open/recv/status/close` export를 제공하는 동일 Core binary를 사용한다.
Java resource filter가 허용 디렉터리의 상위 `native/`까지 제외하던 결함도 수정하고 JAR을 다시
만들었다. 이 결과는 package 자체의 배포·무결성 증거이며 framework E2E 완료 증거는 아니다.

## POSD 설계 재검

수정 전에 확인한 위험 신호와 선택은 다음과 같다.

| 위험 신호 | 관련 원칙 | 검토한 방향 | 선택 |
|---|---|---|---|
| RID나 address 쌍이 논리 peer와 물리 transport를 동시에 식별 | 정보 은닉, 깊은 모듈 | address 쌍 비교 / transport가 발급한 비공개 ID | 재사용되지 않는 process-local `connection_id`를 transport와 monitor 내부에 가둠 |
| 상태를 먼저 바꾸고 connector·route를 나중에 제거 | 시간적 분해 | 호출자가 재시도 시점을 조절 / Core가 전이를 직렬화 | `peer_connection_mutex`가 논리·물리 전이 전체를 소유 |
| 같은 session FIFO를 위해 service 전체를 잠금 | 복잡성을 아래로, 특수·범용 코드 분리 | service mutex 유지 / session별 ordering gate | session별 mutex로 FIFO와 backpressure 영향을 한 session에 제한 |
| READY 실제 변화와 집계 이벤트를 인접 순서로 추론 | 정보 은닉 | 앞뒤 event 값을 비교 / 비공개 edge flag | raw monitor 내부 payload가 실제 edge 여부를 명시 |
| 비공개 reliable monitor가 HWM을 무시하고 계속 증가 | 복잡성을 아래로, 오류를 정의로 제거 | event drop / 무제한 queue / bounded producer backpressure | 공개 raw monitor의 lossy 계약은 유지하고 Mesh 내부 queue만 HWM에서 대기 |
| monitor producer와 consumer가 비공개 frame offset을 각각 계산 | 정보 은닉 | 양쪽의 수동 offset 유지 / 비공개 payload 구조 공유 | `socket_monitor_internal_event_t` 하나가 layout을 소유 |
| session gate를 weak 참조로 두어 submit마다 재할당하고 map 전체를 청소 | 깊은 모듈, session 간 격리 | weak gate 유지 / 연결 session이 gate 소유 | 연결 수명에 gate를 고정하고 disconnect 때 제거 |
| transfer fence가 전역 registry 아래에서 session gate를 기다림 | 복잡성을 아래로, session 간 격리 | 전역 lock 아래 대기 / service 수명 snapshot 뒤 대기 | registry에서는 `shared_ptr` snapshot만 만들고 gate 대기는 registry 해제 후 수행 |
| 재연결 때 이전 pipe가 사라질 때까지 임의 대기 | 오류를 정의로 제거 | sleep·retry / 기존 ROUTER handover 정책 | 표준 `ZLINK_RID_DUPLICATE_HANDOVER` 사용 |

공개 MeshNode·Spot API에는 새 option이나 helper를 추가하지 않았다. Spot 전용 NODROP 구현도 남기지
않았으며, 데이터 전달은 기존 ROUTER의 HWM·send timeout·backpressure 결과를 그대로 사용한다.

## 요약표

| ID | 우선순위 | 시나리오 | 파일:라인 | 근본원인 | 상태 |
|----|:---:|---|---|---|---|
| BUG-1 | P0 | RL-A2·TA-B3·SM-G1·MON-A4-tail·MON-D1 | `endpoint.cpp`, `socket_base_monitor.cpp`, `mesh_wire_ingress.cpp`, `mesh_wire_admission.cpp` | 지연 종료를 RID만으로 오귀속 | `connection_id`로 수정, 반복 30/30 |
| BUG-2 | P0 | RL-A2·TA-B3-recovery | `mesh_node_api.cpp`, `mesh_wire.cpp` | 논리 disconnect와 connector 은퇴의 분리 | 직렬 전이와 표준 ROUTER handover 적용, 반복 50/50+30/30 |
| BUG-3 | P1 | SF-B2 | `mesh` 전송 EOF/liveness | store 회복 창의 전송 정지 | 단독 E2E 통과 기록, framework 전체 순서 재검 필요 |
| BUG-4 | P1 | ST-F3 | `mesh_stream_session_api.cpp` | binding 확인과 mailbox admission 사이 순서 역전 | session별 직렬화와 session 간 격리 회귀 통과 |
| BUG-5 | P2 | MON-A5 | `mesh_wire_ingress.cpp`, 각 binding | pre-admission 실패 미승격과 binding 표면 누락 | exact result/errno 회귀 통과, 10.1.0 package 배포 |
| BUG-6 | - | SM-G1 | Core Actor generation 계약 | process restart 단조 증가를 잘못 요구 | Core 버그에서 제외, framework 단언 수정 대상 |
| BUG-7 | P0 | SF-A2→SF-B1 | peer disconnect·channel admission | peer 제거 뒤 기존 channel target set 손상 | BUG-1·2 적용 package로 framework 순서 재검 필요 |
| BUG-8 | P1 | MON-A4 | mesh monitor peer-close 경로 | DRAINING→CLOSED 변경 누락과 transport 오귀속 | 수정, drain/reconnect 반복 30/30 |
| BUG-9 | P0 | S8-11 package gate | 10.1.0 local package payload | package와 Core runtime 불일치 | 네 package 재배포, Core runtime SHA-256 일치 |

## 재검 절차(Core 수렴 후)
1. framework 담당자는 위 네 `10.1.0` local package 중 담당 언어 package를 중앙 참조 지점에 적용한다.
2. BUG-1/2: `e2e/ResilienceLifecycle`(RL-A2), `e2e/ToActorMessaging`(TA-B3),
   `e2e/RuntimeMonitoring`(MON-A4/D1)을 재실행한다.
3. BUG-3/7: `e2e/StoreFailure`를 단독 시나리오가 아닌 기본 순서 전체로 재실행한다.
4. BUG-4: `e2e/SpotActorTransfer` 전체를 재실행해 ST-F3 이후도 확인한다.
5. BUG-5/8: native monitor bridge를 사용하는 `e2e/RuntimeMonitoring` 전체를 재실행한다.
6. BUG-6: `e2e/SpotService`의 재시작 판정을 Core Actor generation 값 자체가 아니라 Node
   lifecycle과 location row 수명으로 검증한다.
7. framework 결과와 로그는 framework 담당 행에 기록한다. 이 문서의 package 해시는 변경하지 않는다.

## Framework 담당자 확인 기록

이 절은 framework 담당자가 작성한다. Core/package 검증 결과를 다시 해석하지 말고, 실제로 적용한
package와 framework 실행 결과를 기록한다. 실패하면 마지막 성공 단계와 최초 실패 단언, 로그 경로,
Core 회귀인지 framework 문제인지에 대한 근거를 함께 남긴다.

### Package 적용 정보

| 항목 | 확인 내용 |
|---|---|
| 담당 언어 | .NET |
| 적용한 binding version | `10.1.0` |
| 적용한 package 경로 | `.artifacts/wsl/nuget/Systems.Zlink.10.1.0.nupkg`을 `/tmp/zlink-localfeed/Systems.Zlink.10.1.0.nupkg`에 적용 |
| package SHA-256 | `4bfd1604124f31f146707f88df640b7dcb3c11fbdd73f10d433caa19d2bc15be` |
| framework 중앙 참조 파일 | `framework/languages/dotnet/Directory.Packages.props`의 `ZLinkBindingsPackageVersion=10.1.0` |
| restore/resolve에서 확인한 실제 version | `ResilienceLifecycle.Provider/obj/project.assets.json`의 `Systems.Zlink/10.1.0`; 전역 cache package SHA-256도 위 package와 일치. Linux x64 native SHA-256은 `40dcfe1db1263d29c3d866e7059482a261fbab6c0897eeb4f048f535546b11ba` |
| 확인 일시·담당자 | 2026-07-19 10:50 KST, Framework .NET 담당 Codex |

### 시나리오별 결과

| 관련 버그 | framework test | 확인할 동작 | 결과 | 실행 명령·로그 경로 | 확인 내용 |
|---|---|---|---|---|---|
| BUG-1·2 | ResilienceLifecycle RL-A2 | 같은 RID 재기동 뒤 successor가 유지되고 재승인됨 | **실패** | `./run_e2e.sh RL-A2`; `e2e/ResilienceLifecycle/logs/20260719-103808-1803271` | 새 endpoint와 owner generation의 location row까지는 갱신됐다. 그 뒤 replacement endpoint의 `ConnectionReady`가 30초 안에 오지 않아 `/connections/wait`가 500으로 실패했다. 최초 실패 단언은 `RlA2ProviderEndpointRemapScenario.cs:57`이다. framework는 새 row를 확인한 뒤 native monitor evidence를 기다렸으므로 Core 재승인 경로를 다시 확인해야 한다. |
| BUG-1·2 | ToActorMessaging TA-B3 | 명시 disconnect 뒤 같은 endpoint 재연결·재승인 | **실패** | `./run_e2e.sh TA-B3`; `e2e/ToActorMessaging/logs/20260719-104021-1806773` | disconnect 중 요청이 handler에 도달하지 않는 단계는 통과했다. reconnect 뒤 10초 동안 요청을 반복했지만 마지막 결과도 `ActorRouteNotFound`였다. 최초 실패 단언은 `ToActorScenarioContext.cs:195`이다. Core same-endpoint 재승인을 다시 확인해야 한다. |
| BUG-1·8 | RuntimeMonitoring MON-A4·D1 | 실제 lifetime 종료만 Disconnected로 전달되고 새 연결은 유지됨 | **부분 통과 후 실패** | `./run_e2e.sh MON-A4 MON-D1 MON-A5`: `e2e/RuntimeMonitoring/logs/20260719-104155-1809080`; `./run_e2e.sh MON-D1`: `e2e/RuntimeMonitoring/logs/20260719-104305-1810401` | MON-A4의 same-RID endpoint 교체에서 첫 endpoint의 `Disconnected`와 replacement의 `ConnectionReady`는 정확한 서로 다른 endpoint로 전달됐다. 이후 weight 0 변경은 endpoint·RID·값이 없는 `zlink.runtime.mesh_node.channel_changed`로만 전달되어 framework의 `PeerAdmissionChanged` 변환이 실패했다. MON-D1 첫 재시작에서는 같은 endpoint에 `ConnectionReady` 직후 `Disconnected`가 이어졌고, client cleanup의 `Process` 예외가 본래 실패를 덮었다. 전자는 framework event 변환 후속, 후자는 BUG-1 Core 재검 대상이다. |
| BUG-5 | RuntimeMonitoring MON-A5 | RID 없는 handshake 실패가 `HandshakeFailed`로 전달됨 | **통과** | `./run_e2e.sh MON-A5`; `e2e/RuntimeMonitoring/logs/20260719-104536-1812735` | zero-RID invalid handshake가 `monitor-mesh|source=monitor.profile|kind=HandshakeFailed`로 전달됐다. E2E의 구 `monitor-socket` 접두사 단언을 RouteMesh event 표면에 맞춘 뒤 `scenario MON-A5 passed`와 exit 0을 확인했다. |
| BUG-3·7 | StoreFailure 전체 기본 순서 | peer 하나 제거한 뒤 다른 channel target이 유지되고 store 회복 뒤 요청 가능 | **실패** | `./run_e2e.sh`; `e2e/StoreFailure/logs/20260719-104555-1813674` | 기본 순서에서 SF-A1과 SF-A2는 통과했다. 이어진 SF-B1 첫 request가 `Mesh submit to channel 'storefailure.profile' failed with result 'NotFound'`로 실패했다. 최초 실패는 `SfB1FailStaticScenario.cs:23`이다. SF-A2의 peer 제거 뒤 기존 target set이 사라지는 BUG-7이 재현됐다. |
| BUG-4 | SpotActorTransfer ST-F3 이후 | 같은 session 순서 유지와 다른 session 진행 격리 | **BUG-4 범위 통과** | `./run_e2e.sh ST-F3 ST-F4 ST-F5 ST-F6`; `e2e/SpotActorTransfer/logs/20260719-104643-1814771` | ST-F3은 `S1,S2,S3,S4` 순서로 통과했고 ST-F4·F5도 통과했다. ST-F6은 별도 in-flight request correlation이 `TimeoutException`으로 실패했다. 따라서 BUG-4의 same-session FIFO 수정은 확인했지만 ST-F3 이후 전체 묶음은 완전 통과가 아니다. |
| BUG-6 | SpotService SM-G1 | Node lifecycle·location row를 기준으로 재시작 수명을 판정 | **선행 실패로 미도달** | `./run_e2e.sh SM-G1`; `e2e/SpotService/logs/20260719-104845-1824565` | scenario 진입 전 `session-a-play-a` control route가 route settle 5초 뒤에도 준비되지 않아 종료됐다. 또한 현재 SM-G1 client에는 Core Actor generation 값이 달라야 한다는 이전 단언이 남아 있다. control route 회복과 Node lifecycle·location row 기반 단언 수정은 framework 후속이다. |

### Framework 추가 확인 항목

Core와 binding 독립 리뷰에서 framework 변환 계층이 확인해야 할 항목도 발견됐다. framework 담당자는
각 항목을 실제 시나리오와 관측값으로 확인하고, 수정 여부와 로그 경로를 아래 표에 기록한다. 이 표는
Core 수정 완료를 뜻하지 않으며 framework 공개 계약과 event 의미를 다시 확인하기 위한 인계 항목이다.

| 확인 항목 | 확인할 내용 | 결과 | 수정 파일·실행 명령·로그 경로 | 담당자 기록 |
|---|---|---|---|---|
| `BACKPRESSURED` event 분류 | Core의 일반 send backpressure와 multicast 전용 event를 구분하고, 일반 event를 multicast 식별자로 고정하지 않는지 확인 | 미확인 |  |  |
| `CLAIM_REVOKED` domain | application과 infrastructure claim revoke를 framework가 구분할 수 있는지 확인. Core event에 필요한 정보가 없으면 임의 추정하지 말고 공개 계약 설계 이슈로 분리 | 미확인 |  |  |
| observer overflow 계수 | observer queue overflow 때 누락된 event 수를 정확히 누적하고 runtime metric에도 같은 수치를 반영하는지 확인 | 미확인 |  |  |

### Framework 최종 판정

| 항목 | 기록 |
|---|---|
| 전체 결과 | **실패 — 10.1.0 package 적용은 확인했지만 Core-blocked 묶음은 수렴하지 않았다.** |
| 통과한 범위 | BUG-5 MON-A5 exit 0. BUG-4 ST-F3의 `S1,S2,S3,S4` 순서와 ST-F4·F5 통과. MON-A4의 실제 endpoint 종료·replacement 연결 event 구분 통과. |
| 잔여 실패 | RL-A2 replacement 재승인, TA-B3 same-endpoint 재승인, SF-A2→SF-B1 target 보존, MON-D1 successor 유지, ST-F6 request correlation. SM-G1은 control route 선행 실패로 미도달. |
| Core 재검이 필요한 항목과 근거 | BUG-1·2: RL-A2와 TA-B3에서 새 row 또는 reconnect 호출 뒤 admission이 회복되지 않음. BUG-1: MON-D1에서 같은 endpoint의 `ConnectionReady` 직후 `Disconnected`. BUG-7: SF-A1·A2 통과 직후 SF-B1이 `NotFound`. |
| framework에서 후속 수정할 항목 | RouteMesh `channel_changed`를 endpoint·RID·weight가 있는 `PeerAdmissionChanged`로 변환, MON-D1 cleanup이 본래 예외를 보존하도록 수정, SM-G1을 Node lifecycle·location row 기준으로 변경, control route와 ST-F6 timeout 조사. |
| execution ledger에 반영한 stage·ID | S8-10 재검 증거. 실패가 남아 있으므로 S8-10은 계속 `진행`이며 S8-13 이후 review gate를 열지 않는다. |

### Core 2차 package 적용 및 재검 기록

위 결과 뒤 Core는 HELLO-before-READY transport 귀속, bounded reliable monitor queue와 transfer
fence ordering을 추가로 수정하고 package를 다시 만들었다. framework 담당자는 아래 package
checksum을 먼저 확인한 뒤, 1차 실패 중 Core 재검 대상으로 분류한 시나리오를 다시 실행한다.

| 항목 | 기록 |
|---|---|
| 적용할 .NET package SHA-256 | `3490a376f66217a39e3b65460c85ed5d3aee9eb493d10e55cab6451cc4c18d49` |
| package 안 Linux x64 Core SHA-256 | `8361fe95c7f31432554d1a151b87b66f76764bf07c68c77671c8ffcbb9a7f198` |
| 실제 restore/resolve checksum | source package, `/tmp/zlink-localfeed`와 전역 NuGet cache package가 모두 `3490a376f66217a39e3b65460c85ed5d3aee9eb493d10e55cab6451cc4c18d49`로 일치했다. 전역 cache의 `runtimes/linux-x64/native/libzlink.so`는 `8361fe95c7f31432554d1a151b87b66f76764bf07c68c77671c8ffcbb9a7f198`로 일치했고 `project.assets.json`은 `Systems.Zlink/10.1.0`을 resolve했다. |
| 확인 일시·담당자 | 2026-07-19 11:50 KST, Framework .NET 담당 Codex |

| 재검 시나리오 | 결과 | 실행 명령·로그 경로 | 최초 실패 또는 통과 근거 | Core/framework 판정 |
|---|---|---|---|---|
| RL-A2 | **통과** | `./run_e2e.sh RL-A2`; `e2e/ResilienceLifecycle/logs/20260719-112007-1926221` | 새 endpoint의 location row와 `ConnectionReady`를 확인한 뒤 `scenario RL-A2 passed`, exit 0 | RL-A2에서 재현되던 BUG-1·2 범위는 수정 확인 |
| TA-B3 | **실패** | `./run_e2e.sh TA-B3`; `e2e/ToActorMessaging/logs/20260719-113936-1943226` | Framework 재연결이 최초 연결과 같이 기대 RID를 전달하도록 수정했다. disconnect 중 handler 미도달은 통과했지만 reconnect 뒤 10초 동안 마지막 결과까지 `ActorRouteNotFound`; 최초 실패 단언 `ToActorScenarioContext.cs:195` | Framework의 RID 누락을 제거한 뒤에도 same-endpoint 재승인이 회복되지 않았다. BUG-2 수정 미확인 |
| MON-A4·MON-D1 | **BUG-8 통과, BUG-1 실패** | `./run_e2e.sh MON-A4`: `e2e/RuntimeMonitoring/logs/20260719-113741-1941393`; `./run_e2e.sh MON-D1`: `e2e/RuntimeMonitoring/logs/20260719-114225-1953359` | MON-A4의 old endpoint `Disconnected`와 replacement `ConnectionReady` 구분은 통과했다. Framework가 weight 변경을 location descriptor와 `PeerAdmissionChanged`로 전파하도록 수정한 뒤 `svc-b value=0`도 관측됐다. 그 다음 local positive-weight member 선택은 Core `NotFound`로 실패했다(`MonA4AvailabilityTransitionScenario.cs:27`). MON-D1은 cleanup masking을 제거하고 준비 제한을 10초로 늘린 뒤 cycle 2에서 같은 endpoint의 `ConnectionReady` sequence 6 직후 `Disconnected` sequence 7이 다시 발생해, 다음 kill 뒤 새 종료 event를 만들 수 없어 line 33에서 timeout됐다. | BUG-8의 실제 종료 event 구분은 수정 확인. BUG-1의 successor 유지 수정은 실패. MON-A4에는 Core spec §5의 local member 선택 계약을 위반하는 별도 `NotFound`도 남음 |
| StoreFailure 전체 기본 순서 | **실패** | `./run_e2e.sh`; `e2e/StoreFailure/logs/20260719-112437-1930737` | SF-A1과 SF-A2 통과 직후 SF-B1 첫 request가 `Mesh submit to channel 'storefailure.profile' failed with result 'NotFound'`; 최초 실패 `SfB1FailStaticScenario.cs:23` | SF-A2의 별도 polling consumer가 제거한 `api-c` target만 Framework reconciler가 disconnect한다. 살아 있는 target을 제거하는 Framework diff는 없으므로 BUG-7 수정 미확인 |
| ST-F3~F6 | **실패** | `./run_e2e.sh ST-F3 ST-F4 ST-F5 ST-F6`: `e2e/SpotActorTransfer/logs/20260719-112520-1931673`; `./run_e2e.sh ST-F3`: `e2e/SpotActorTransfer/logs/20260719-114058-1944749` | 묶음 실행은 ST-F3에서 `S2,S1,S3,S4`, 집중 재실행은 `S2,S1,S4,S3`으로 실패했다. 집중 실행의 최초 단언은 `SpotActorTransferScenarioContext.cs:305`이며 actor-a ingress backlog부터 arrival 0·1 순서로 역전된 값이 전달됐다. | Framework 아래 Core actor-record 표면에서 이미 순서가 역전되므로 BUG-4 수정 실패 |
| SM-G1 | **Core 판정 대상 아님, 미완료** | `./run_e2e.sh sm-g1`; `e2e/SpotService/logs/20260719-112646-1933753`; Framework 보정 후 `e2e/SpotService/logs/20260719-114743-1972350` | 최초 실행은 actor create·location evidence 뒤 remote session bind가 `ActorRouteNotFound`였다. Framework가 startup location 전파 창의 bind를 bounded retry하도록 수정했다. 후속 실행은 MeshNode start가 Core config 705로 실패해 crash-recovery 단계에 도달하지 못했다. 기존 client의 Actor generation 단조 증가 단언도 아직 Node lifecycle 판정으로 바꿔야 한다. | BUG-6은 원래 Core 버그가 아니다. Framework 시나리오 보정과 별개 startup 실패를 해결한 뒤 재실행 필요 |

### Core 2차 package 수정 여부 판정

| Core 버그 | 판정 | Framework 재검 근거 |
|---|---|---|
| BUG-1 | **수정되지 않음** | RL-A2는 통과했지만 MON-D1 동일 endpoint 재기동에서 successor `ConnectionReady` 직후 stale `Disconnected`가 재현됐다. |
| BUG-2 | **수정되지 않음** | 기대 RID를 유지한 TA-B3 same-endpoint reconnect 뒤에도 재승인이 되지 않았다. |
| BUG-3 | **수정 여부 미확정** | StoreFailure 기본 순서가 BUG-7 증상으로 SF-B1에서 먼저 중단되어 SF-B2에 도달하지 못했다. |
| BUG-4 | **수정되지 않음** | ST-F3에서 같은 session의 제출 순서가 두 실행 모두 Core ingress 전에 역전됐다. |
| BUG-5 | **수정됨** | `./run_e2e.sh MON-A5`; `e2e/RuntimeMonitoring/logs/20260719-114413-1961144`; `scenario MON-A5 passed`, exit 0. |
| BUG-6 | **Core 버그 아님** | Node lifecycle·location row로 판단하도록 Framework 시나리오를 고쳐야 한다. |
| BUG-7 | **수정되지 않음** | SF-A2의 한 peer 제거 뒤 SF-B1에서 살아 있는 channel target이 `NotFound`가 됐다. |
| BUG-8 | **수정됨** | MON-A4에서 old endpoint 종료와 replacement endpoint 연결 event가 서로 구분됐다. |
| BUG-9 | **수정됨** | 전달된 package와 restore cache의 package/native checksum이 모두 일치했다. |

따라서 **Core 2차 package는 일부 버그만 수정됐으며 전체 Core 버그 수정 완료로 판정할 수 없다.**
Framework가 소유한 weight descriptor 전파, MON-D1 cleanup과 준비 제한, TA-B3 기대 RID 재연결은
Framework에서 수정했다. 이 보정 뒤에도 남은 BUG-1·2·4·7은 Core 담당자에게 다시 전달해야 한다.
