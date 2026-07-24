# MeshNode — 공통 스펙

[스펙 목차](../README.ko.md) · [이전: SPOT 메시징](20-spot-messaging.ko.md) ·
[다음: Actor 모델](22-actor-model.ko.md) · [.NET 인터페이스](languages/dotnet/interfaces/03-configuration-topology.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0에서 RouteMesh에 참여하는 MeshNode의 identity, object role, placement
capability와 startup 계약을 정의한다. MeshNode는 물리 연결과 logical channel membership을 제공하며,
Framework object runtime은 이 transport 위에서 Actor와 Spot의 전역 logical identity를 current owner route로
연결한다.

## 2. Identity와 membership

MeshNode 하나는 다음 identity와 설정을 가진다.

| 항목 | 계약 |
|---|---|
| MeshName | 물리 RouteMesh와 descriptor namespace를 구분하는 immutable 이름 |
| Routing ID | MeshNode lifecycle 동안 유지되는 opaque transport identity |
| Endpoint | peer가 연결할 ROUTER endpoint |
| ChannelName set | 0개 이상의 immutable Server membership |
| Object role | `None`, `Client`, `Server` 가운데 startup 전에 고정한 값 |
| Lifecycle generation | 같은 transport identity의 lifecycle을 구분하는 non-zero fence |
| Descriptor revision | 같은 lifecycle에서 mutable descriptor snapshot 변경을 구분하는 non-zero 값 |

MeshName은 ActorId나 SpotId의 identity key가 아니다. ActorId와 User·Instance SpotId는 Location Store
transaction domain 전체에서 각각 전역 key이며, MeshName은 initial placement와 현재 물리 route의 attribute다.

같은 process에는 같은 MeshName의 MeshNode를 하나만 등록할 수 있다. 서로 다른 MeshName의 MeshNode는 여러 개
등록할 수 있고 mesh 사이의 transport relay는 자동으로 만들지 않는다. `ChannelName`은 별도 socket이나
endpoint를 만들지 않는다. Descriptor를 게시한 뒤 membership, object role, factory와 type capability는 바꿀 수
없다.

## 3. Routing ID

Automatic discovery를 사용하는 MeshNode의 RID는 Framework가 매 lifecycle마다 새로 만든다. Caller는 진단에
사용할 prefix만 지정할 수 있으며 생략하면 Framework가 listener 종류에 맞는 기본 prefix를 사용한다.

- Prefix는 ASCII `[A-Za-z0-9._-]` 1..64자다.
- Full RID는 `prefix-<lowercase-canonical-uuid-v4>` 형식이며 UTF-8 encoded 크기는 255 bytes 이하다.
- Suffix는 RFC 4122 UUID v4 bit layout을 사용하는 16-byte random value를 `8-4-4-4-12` lowercase canonical
  문자열로 표현한다. Prefix와 UUID를 placement, shard, stable application identity로 해석하지 않는다.
- Descriptor owner CAS가 `(MeshName, RID)`의 active owner 충돌을 확인한다. 충돌하면 기존 record를
  변경하지 않고 startup을 즉시 `RoutingIdConflict`로 끝낸다. Framework는 두 번째 UUID를 만들거나
  두 번째 claim을 시도하지 않는다.
- Replacement lifecycle은 이전 RID를 재사용하지 않고 새 RID를 만든다.

Fixed RID는 Location Store descriptor와 automatic discovery를 사용하지 않는 explicit manual topology에서만
허용한다. Object role이 `Client` 또는 `Server`이거나 automatic mode와 fixed RID를 함께 설정하면 startup
configuration error다.

Object Server의 Entry Spot ID는 같은 diagnostic prefix와 독립적인 UUID v4를 사용해
`<prefix>-entry-<lowercase-canonical-uuid-v4>` 형식으로 발급한다. 같은 lifecycle에서는 유지하고 replacement
lifecycle에서는 endpoint가 같아도 새로 발급한다. Global Spot namespace의 active 충돌은 기존 record를
변경하지 않고 startup을 즉시 `SpotIdConflict`로 끝내며 두 번째 UUID나 claim을 만들지 않는다.
Descriptor는 exact Entry Spot ID와 lifecycle
generation을 함께 게시하며 consumer는 이 mapping을 사용하고 RID 문자열을 parse하지 않는다. Framework는
full MeshNode RID를 이어 붙여 Entry Spot ID를 만들지 않는다.

## 4. Object role과 registration

Object role은 MeshNode마다 한 번 선택한다.

| Role | Logical object operation | Local factory와 Entry Spot | Placement target |
|---|---|---|---|
| `None` | 제공하지 않음 | 없음 | 제외 |
| `Client` | create, find와 message 시작 가능 | 없음 | 제외 |
| `Server` | `Client` capability 포함 | 등록한 type을 host함 | eligible type에 포함 |

`Client`와 `Server`는 Location Store가 필수다. `None`은 manager, factory, placement와 hidden local object
runtime을 만들지 않는다. Factory와 Entry Spot 등록은 `Server` builder만 제공한다. Entry Spot ID는 Framework가
발급하며 caller가 생성하거나 fixed RID를 지정하지 않는다.

Actor, User Spot과 Instance Spot factory는 stable type과 relocation policy를 반드시 등록한다. Stable type은 UTF-8
1..255 bytes, case-sensitive exact value이며 normalization하지 않는다. 언어 class FQN은 wire와 Store identity로
사용하지 않는다. 같은 object kind와 stable type의 중복 등록은 startup 오류다. Relocation policy는 `Disabled`,
`Recreate`, `Snapshot` 가운데 하나이며 생략하는 overload나 compatibility default를 제공하지 않는다.

## 5. Placement capability

Object Server descriptor는 node-wide placement weight, node capacity와 등록한 type별 capability를 게시한다.

- Placement weight는 signed integer `0..10000`이고 기본값은 `100`이다. `1..10000`은 eligible node 사이의
  상대적 선택 비중이며 Channel weight와 분리한다. Startup 설정이나 runtime 변경에 범위 밖 값을 지정하면
  configuration error다. 0은 신규 create와 relocation target에서만 제외하며 existing traffic과 이미
  제출했거나 완료된 reservation은 취소하지 않는다.
- Node의 Actor 전체와 Spot 전체 population limit 기본값은 각각 `0`이며 제한을 두지 않는다는 뜻이다.
  양수는 `1..2^31-1` 범위의 상한이고 음수는 startup configuration error다.
- User Spot과 Instance Spot factory는 `(object kind, stable type)`별 Spot limit을 같은 규칙으로 등록한다.
  Entry Spot은 Spot capacity에서 제외하지만 Entry Spot에 존재하는 Actor는 Actor 전체 capacity에 포함한다.
  Actor stable type별 limit은 제공하지 않는다.
- Location Store가 Actor 전체, Spot 전체와 Spot stable type별 active·reserved count의 권한 원본이다.
  Descriptor count는 이 값의 projection이다.
- Factory 실행을 제한하는 activation concurrency는 population capacity와 별도이며 기본값은 128이다.
  양수만 허용하고 실행 중인 factory와 초기화에만 적용한다.
- Typed population capacity filter를 weight보다 먼저 적용한다. Eligible node가 없으면
  `PlacementCapacityExhausted`다.
- Startup builder, runtime option, descriptor와 monitoring snapshot은 같은 weight·capacity 값을 사용한다.

최초 배치는 caller가 target RID, predicate 또는 별도 placement selector를 지정하지 않는다. Deployment
정책은 Framework runtime 내부에서 처리하며 public factory option이나 creation intent에 포함하지 않는다.

Framework는 `Serving` 상태, current owner lease, stable type capability, capacity와 node-wide weight를 사용해
target을 선택한다. 선택 결과는 generic placement reservation으로 확정하며 application에 target RID나 owner
token 선택을 요구하지 않는다. `GetOrCreate`가 Ready object를 찾은 경우 current owner의 capacity와 weight를 다시
적용하지 않는다. Typed capacity와 다른 eligibility filter를 먼저 적용한 뒤 positive weight 합계를 최소
64-bit 정수로 계산한다. Factory option과 descriptor capability에는 stable type, relocation policy, Snapshot
adapter 존재 여부와 type별 capacity만 기록한다.

## 6. 등록과 startup

Framework는 다음 순서로 MeshNode를 시작한다.

1. MeshName, object role, routing mode, endpoint, channel set, factory, stable type, policy, placement option과
   capacity를 검증한다.
2. Location Store가 필요한 role이면 host owner lease를 확보하고 automatic RID의 descriptor owner CAS를
   완료한다.
3. ROUTER를 bind하고 actual advertised endpoint를 확정한다.
4. Complete descriptor를 게시하고 peer connection intent를 계산한다.
5. Peer admission, local handler와 object runtime 준비가 끝난 뒤 `Serving`과 신규 selection을 공개한다.

Object role을 사용하는 host는 Location Store를 명시적으로 등록해야 한다. Manual mode는 endpoint 또는 expected
RID와 endpoint를 application이 모두 제공하며 object runtime을 제공하지 않는다.

## 7. Peer admission과 메시징

Peer는 MeshName, RID, lifecycle generation, descriptor revision, immutable ChannelName set과 security identity를
handshake에서 교환한다. MeshName 또는 trust profile이 다르거나 같은 lifecycle identity의 중복 pipe이면
admission하지 않는다. Lifecycle generation은 non-zero opaque equality token이며 숫자 크기로 비교하지 않는다.
Manual topology의 fixed RID 재연결은 configured intent, authenticated connection handover와 service liveness가
이전 pipe 종료를 확정한 뒤 다른 token을 selection 대상에 포함한다. Automatic RouteMesh는 RID가 더 작은
MeshNode만 connect를 시작한다. Manual 양방향 connect 또는 automatic 경합으로 중복 후보가 생기면
[RouteMesh topology](10-channel-topology.ko.md)의 duplicate-pipe admission을 적용해 하나의 ready connection으로
수렴한다.

Handshake는 channel별 weight도 전달한다. Channel weight를 실행 중 바꾸면 lifecycle generation은 유지하고
descriptor revision만 증가한다. Peer는 더 큰 revision의 complete weight snapshot만 적용한다. Weight 변경은
connection 재생성이나 application message replay를 일으키지 않으며 node-wide placement weight를 바꾸지 않는다.
Node placement weight의 runtime 변경도 같은 descriptor revision으로 순서화하며 이후 create·relocation target
선택에만 적용한다.

| Target | Selection과 delivery |
|---|---|
| Node direct | 같은 MeshName의 exact target RID로 한 번 제출 |
| Channel | process-local ChannelName index가 고른 Mesh에서 ready Server member 가운데 positive channel weight 비율로 한 node를 선택 |
| Logical Multicast | target ChannelName의 ready remote node 전체와 조건부 local Spot subscription에 전달 |
| Actor direct | global ActorId의 current authority와 ObjectGeneration을 확인한 owner route로 제출 |
| Spot direct | global SpotId의 current authority와 ObjectGeneration을 확인한 owner route로 제출 |

Selection과 submit은 하나의 operation이다. 선택한 RID 목록을 application에 반환한 뒤 별도 send를 요구하지 않는다.
Node·Channel·Actor·Spot의 send와 request는 같은 MeshNode ROUTER를 사용한다. Classic fanout은 별도 PUB/SUB socket
계약이며 MeshNode membership과 합치지 않는다.

Node direct는 exact MeshName과 RID가 operation 의미에 포함되는 infrastructure·진단 또는 manual topology에
사용한다. 여러 node가 제공하는 application request는 ChannelName으로 선택한다. Actor와 Spot 메시징은 global
ActorId 또는 SpotId를 target으로 사용하며 NodeRid와 MeshName을 caller target으로 받지 않는다.

기존 Actor·Spot의 current MeshName과 NodeRid는 Location Store authority가 제공한다. Missing Instance Spot만
Spot direct fluent call의 Instance intent에서 optional initial Mesh와 stable type을 받는다. Initial Mesh는 cold
activation placement에만 사용하며 existing owner의 현재 Mesh를 제한하거나 이동시키지 않는다.

Application payload는 owner의 application turn에서 직렬로 처리한다. Completion, send-ready, location reconcile,
reservation과 relocation control은 infrastructure task에서 계속 진행한다. Transport readiness callback에서 application
handler를 직접 실행하지 않는다.

ChannelName handler와 RID direct handler는 서로 다른 namespace를 사용한다. Channel handler context는
ChannelName과 reply source identity를 내부에 보존하며 MeshName이나 물리 route 선택을 업무 코드에 노출하지
않는다. RID direct handler context는 direct route의 MeshName과 source RID를 제공한다.

Spot Logical Multicast는 `(ChannelName, topic filter)` subscription을 node-local로 검사한다. 송신 MeshNode는 target
channel의 remote node마다 routed message를 한 번 제출한다. 수신 MeshNode는 local match마다 같은 immutable message
storage의 reference를 확보해 Spot queue에 넣는다.

## 8. Drain과 종료

`Retiring` node는 새 Channel selection, create·membership과 relocation target에서 제외하지만 아직 permit을 얻지 못한
relocation unit의 existing owner message와 timer는 계속 처리한다. Unit별 seal이 끝나 source application dispatch가
모두 닫히면 `Draining`으로 전환한다. 이미 reservation을 완료한 create, accepted message, completion과 relocation
barrier는 정해진 deadline과 fence에 따라 terminal 상태까지 진행한다.
종료와 handoff 순서는 [54 Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)이 소유한다.

`Shutdown`은 새 relocation을 시작하지 않는다. `Retire`는 등록한 policy에 따라 Actor, User Spot aggregate와 Instance
Spot을 이전한다. Node weight 0 또는 drain 전환은 existing object를 숨겨서 다시 만들거나 application payload를
다른 owner에 새 operation으로 제출하는 근거가 아니다.

## 9. 관측

Snapshot과 event는 MeshName, RID, lifecycle generation, endpoint, object role, node-wide placement weight,
Actor 전체·Spot 전체·Spot stable type별 active·reserved·limit capacity, activation concurrency, capability,
reservation failure와 drain state를 제공한다. RID와 endpoint는 진단
값이며 metric label에는 사용하지 않는다. 세부 계약은 [50 Runtime Monitoring](50-runtime-monitoring.ko.md)이
소유한다.

## 10. 검증 요구

- 같은 process의 중복 MeshName과 잘못된 object role 구성이 startup에서 실패한다.
- `None`, `Client`, `Server`가 manager, factory와 placement capability를 계약대로 제한한다.
- Object role과 Location Store, automatic discovery와 fixed RID의 잘못된 조합이 startup에서 실패한다.
- Automatic RID가 prefix와 lowercase canonical UUID v4 형식을 따르고 active conflict에서 기존 record를
  변경하거나 두 번째 claim을 시도하지 않고 즉시 실패한다.
- Replacement lifecycle이 새 RID를 사용한다.
- Stable type 중복과 policy 생략이 startup에서 실패한다.
- Typed population capacity가 weight보다 먼저 적용되고 weight 0이 existing object와 accepted reservation을
  취소하지 않는다.
- 세 public weight가 `0`, 기본값 `100`과 상한 `10000`을 허용하고 범위 밖 startup·runtime 설정을 거부한다.
- Placement 후보 합계를 최소 64-bit 정수로 overflow 없이 계산한다.
- Entry Spot 자체는 Spot capacity에서 제외되고 그 안의 Actor는 Actor capacity에 포함된다.
- Population capacity와 activation concurrency가 서로의 count와 limit을 대신하지 않는다.
- Channel weight 변경이 placement weight를 바꾸지 않는다.
- Channel select-one이 channel weight와 drain을 반영하고 Node direct에는 영향을 주지 않는다.
- Logical Multicast가 remote node마다 한 번 전송되고 node-local Spot queue가 immutable storage를 공유한다.
- ChannelName handler와 RID direct handler namespace 및 context가 구분된다.
- Draining node가 새 placement target이 되지 않고 accepted operation은 terminal 상태까지 진행한다.
- Actor·Spot application 호출이 NodeRid나 owner token을 target으로 요구하지 않는다.
