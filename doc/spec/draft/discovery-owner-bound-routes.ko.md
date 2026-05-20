# Discovery owner-bound route 초안

이 문서는 구현 전 초안이며 현재 공개 계약이 아니다.
아래 내용은 Discovery와 Registry의 주소 매핑 record를 owner provider 생존 상태에
묶어 정리하기 위한 설계안이다. 정식 spec 문서와 공개 헤더에 반영되기 전까지
응용은 이 동작에 의존하면 안 된다.

## 목적

Discovery와 Registry에는 주소 매핑에 가까운 두 종류의 record가 필요하다.

- core가 직접 관리하는 `SPOT RID -> owner node` 매핑
- 응용이나 Framework가 등록하는 `kind + key -> owner provider` route binding

이 record들은 일반적인 저장소 값이 아니다. 어떤 논리 주소나 key가 현재 어느
provider에 속하는지 찾기 위한 보조 주소 매핑이다. 따라서 record마다 TTL을 두고
주기적으로 갱신하기보다, 그 record를 소유한 provider registration의 생존 상태를
따르게 하는 것이 더 단순하다.

이 초안의 목적은 다음 두 동작을 하나의 원칙으로 정리하는 것이다.

1. SpotNode provider가 사라지면 그 node가 소유한 SPOT RID owner entry를 함께
   정리한다.
2. route binding owner provider가 사라지면 그 provider가 등록한 route binding을
   함께 정리한다.

## 설계 원칙

1. 주소 매핑 record는 일반 key-value 저장소가 아니다.
2. 주소 매핑 record는 개별 TTL을 갖지 않는다.
3. 주소 매핑 record는 owner provider registration에 귀속된다.
4. owner provider가 heartbeat timeout, unregister, shutdown으로 제거되면
   Registry는 그 provider가 소유한 record를 함께 정리한다.
5. bind 계열 API는 실제 객체를 소유한 socket 또는 SpotNode의 Discovery로만
   호출해야 한다.
6. resolve 계열 API는 owner가 아니어도 가능하지만, 정해진 channel namespace
   안에서만 조회한다.
7. Registry는 죽은 owner provider와 연결된 record를 resolve 결과로 반환하면
   안 된다.
8. Registry cluster를 사용할 때 owner-bound record는 owner provider와 같은
   registry 출처를 따라 동기화되어야 한다.
9. 기존 Registry provider sync도 이 owner 모델에 맞춰 함께 갱신한다.
10. 구현은 하나의 계약 변경으로 반영한다. provider sync만 먼저 공개하거나
    route binding만 별도로 공개하는 중간 계약은 두지 않는다.
11. SPOT owner entry를 Registry에 올리는 동작은 기본값으로 꺼져 있어야 한다.
    SpotNode를 Discovery에 붙였다는 이유만으로 SPOT RID owner 주소가 Registry에
    저장되면 안 된다. 해당 Discovery에서
    `ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC`를 `1`로 설정한 경우에만 SPOT owner
    topology row를 publish한다.

## 구현 적용 범위

구현 순서는 정할 수 있지만, 최종 반영은 하나의 기능 단위로 처리한다. 이 초안의
동작은 서로 의존한다. `registration_id`가 없는 provider sync 위에 route binding을
얹거나, owner identity가 없는 topology cleanup만 먼저 공개하면 cluster 환경에서
주소 매핑의 소유자를 정확히 구분할 수 없다.

따라서 구현 완료 기준은 아래 항목이 모두 같은 변경 묶음에 들어가는 것이다.

1. provider registration에 `registration_id`를 발급하고 Discovery에 전달한다.
2. Registry peer sync가 provider의 원 출처 registry id, `registration_id`,
   `provider_update_seq`를 보존한다.
3. SPOT owner topology row가 내부 owner identity를 가진다.
4. provider 제거 시 SPOT owner entry와 route binding을 같은 상태 전이 안에서 정리한다.
5. route binding 공개 API와 내부 protocol을 추가한다.
6. late join full snapshot이 provider, SPOT owner entry, route binding을 함께 수렴시킨다.
7. 관련 테스트와 draft 이후 정식 spec 반영 항목을 함께 맞춘다.

이 순서는 작업 순서일 수는 있지만 배포 단위나 공개 계약의 단계 구분을 뜻하지 않는다.

## 권장 구현 방향

이 기능은 node/socket 주소 리스트와 owner-bound 객체 주소를 같은 Registry snapshot
안에서 다루되, 적용 모델은 분리한다. 둘 다 주소를 찾기 위한 record처럼 보이지만
의미가 다르기 때문이다.

node/socket 주소 리스트는 "현재 연결 가능한 provider 주소"다. 이 목록은 RID를 논리
key로 사용하고 endpoint를 현재 연결 주소로 사용한다.

```text
provider_key:
  channel_name
  service_role
  routing_id

provider_entry:
  endpoint
  source_registry
  registration_id
  provider_update_seq
  last_heartbeat
  weight
  value
```

같은 RID로 endpoint가 바뀌면 주소 리스트 관점에서는 같은 logical provider의 주소가
이동한 것으로 본다. 실제 provider generation은 endpoint를 불변으로 다루므로, endpoint
변경은 보통 새 `registration_id`를 가진 재등록으로 표현된다. Discovery는 old endpoint를
disconnect하고 new endpoint를 connect한다. endpoint가 같으면 `registration_id`가
바뀌었더라도 주소 연결만 보고 reconnect를 강제하지 않는다.

SPOT, Actor, route binding 같은 owner-bound 객체 주소는 "논리 객체가 어느 owner
generation에 속하는지"를 나타낸다. Actor route record 는 actor node rid 와 actor
generation 을 함께 담아야 stale update 를 구분할 수 있다. 이 record의 owner는
endpoint가 아니라 provider generation이다.

```text
owner_generation:
  source_registry
  registration_id
```

따라서 같은 RID로 provider가 다시 올라와도 새 `registration_id`를 받으면 이전
generation의 SPOT, Actor, route binding은 새 generation에 자동 승계하지 않는다.
새 process가 실제 객체를 다시 만들었다면 다시 report 또는 bind해야 한다.

Registry는 peer sync에서 받은 provider row를 곧바로 Discovery에 노출하지 않는다.
먼저 원 출처와 generation을 보존한 raw view로 저장하고, 그 다음 materialized view를
만든다.

```text
raw view:
  advertising_registry
  source_registry + registration_id + channel_name + service_role + routing_id

materialized view:
  channel_name + service_role + routing_id -> endpoint
```

materialized view는 Discovery auto-connect와 provider 조회에 사용한다. 같은
`channel_name + service_role + routing_id`를 서로 다른 `source_registry`가 동시에
live provider로 광고하면 RID 충돌로 본다. 이 경우 Registry는 해당 RID를
materialized view에서 제외하고 진단 가능하게 기록한다. 충돌 상태를 임의의 endpoint
선택으로 해결하면 잘못된 node로 연결될 수 있다.

peer snapshot을 적용할 때 교체 기준은 `source_registry`가 아니라
`advertising_registry`다. 어떤 peer가 원 출처가 다른 provider를 전달할 수 있기
때문이다. peer `P`의 새 snapshot을 받으면 `advertising_registry == P`인 raw view만
snapshot 기준으로 교체하고, 그 뒤 materialized view를 다시 만든다. `source_registry`는
owner generation을 식별하기 위한 원 출처로 보존한다.

같은 `source_registry + registration_id` provider generation에서 서로 다른 endpoint가
보이면 그 generation의 관측값이 서로 모순된 것이다. provider generation 안에서
endpoint는 불변으로 다룬다. 이런 모순은 stale peer snapshot이나 protocol 오류로
생길 수 있으므로 Registry는 임의의 endpoint를 고르지 않는다. 원 출처 Registry가 직접
광고한 최신 row가 있으면 그 row를 우선하고, 그렇지 않으면 해당 provider generation을
materialized view에서 제외해 잘못된 연결을 막는다.

Discovery auto-connect는 endpoint set이 아니라 `routing_id -> endpoint` diff로
동작해야 한다.

```text
desired_peers:
  routing_id -> endpoint

active_peers:
  routing_id -> endpoint
```

적용 규칙은 아래와 같다.

1. 새 RID가 보이면 해당 endpoint를 connect한다.
2. 기존 RID의 endpoint가 같으면 연결 상태를 유지한다.
3. 기존 RID의 endpoint가 바뀌면 old endpoint를 disconnect하고 new endpoint를 connect한다.
4. RID가 사라지면 마지막으로 연결했던 endpoint를 disconnect한다.
5. local RID와 같은 remote RID는 endpoint가 달라도 connect 후보에서 제외한다.

구현 작업은 아래 순서로 진행하는 것이 좋다. 이 순서는 내부 작업 순서이며, 공개 계약을
나누는 단계가 아니다.

1. provider model을 RID key와 `registration_id` 기반으로 바꾼다.
2. Registry peer sync snapshot을 원 출처를 보존하는 schema로 바꾼다.
3. raw provider view와 materialized provider view를 분리하고 RID 충돌 처리를 추가한다.
4. Discovery auto-connect를 `routing_id -> endpoint` diff로 바꾼다.
5. SPOT owner topology row에 owner identity를 추가하고,
   `ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC`가 켜진 Discovery에서만 Registry에
   publish하도록 만든다.
6. route binding API와 내부 protocol을 추가한다.
7. cascade cleanup과 late join snapshot 테스트를 추가한다.

## 성능 범위와 비목표

Registry는 고속으로 동작해야 하지만, 일반 목적 대용량 저장소가 아니다. 이 기능에서
Registry가 맡는 역할은 provider 주소와 owner-bound 주소 매핑을 빠르게 조회하고,
owner provider가 사라졌을 때 관련 record를 정확히 정리하는 것이다.

단일 Registry가 억 단위 record를 `std::map`, `std::set`, 개별 `std::string` allocation
중심으로 직접 보관하고 full snapshot으로 peer와 동기화하는 것은 이 초안의 목표가
아니다. 억 단위 record는 메모리, reverse index, peer snapshot, late join, cleanup
비용이 모두 커져 Registry의 coordination 역할을 해친다.

구현자는 Redis source code를 실제로 내려받아 저장 구조와 hot path를 분석한 뒤,
검증된 방식을 zlink Registry의 주소 매핑 용도에 맞게 적극적으로 적용한다. Redis 자체의
명령, eviction, persistence, cluster 기능을 가져오자는 뜻은 아니다. 이 초안에서
가져올 대상은 작은 key/value를 촘촘하게 저장하고, lookup/update/cleanup 지연을 줄이는
내부 구조와 실행 방식이다.

구현은 Redis 계열 저장 구조를 설계 참고 대상으로 삼는 데서 멈추지 않는다.
`dict`의 2단 hash table, 2의 거듭제곱 bucket, mask 기반 bucket 선택, separate chaining,
점진적 rehash, rehash 중 lookup/update 처리, empty bucket 이동 budget 같은 구체적인
방식을 분석하고, 필요한 부분을 route materialized table과 raw observation index에
반영한다. compact encoding, arena, lazy free, chunked scan 같은 아이디어도 zlink
Registry의 주소 매핑 용도에 맞게 줄여서 적용한다. materialized table entry는 route
key를 한 번만 소유하고, value 안에 같은 key를 다시 저장하지 않는다. 이는 Redis
`dictEntry`의 key/value 분리 방식과 같은 방향이며, 대량 route set에서 중복 문자열
메모리를 줄이기 위한 규칙이다. owner identity는 반복 저장하지 않고 table 내부에서
intern id로 공유한다. bucket과 node link는 10M 단위 route table에서 충분한 32비트
entry id를 사용해 64비트 포인터형 index 비용을 줄인다. materialized entry node는
fixed scalar만 담고, route key/value byte는 append-only block arena에 붙인다. 같은
channel 이름은 intern하고, node 자체도 chunk 단위로 할당해 대량 insert 때 container
capacity가 live record 수보다 크게 커지는 비용을 피한다.

이 초안의 성능 목표는 아래 범위를 전제로 한다.

1. provider 주소 row는 channel 안의 live node/socket 수에 비례한다.
2. SPOT owner entry와 route binding은 주소 매핑에 필요한 작은 record로 제한한다.
3. route key와 value 크기를 제한해 snapshot과 memory 사용량을 예측 가능하게 한다.
4. resolve는 exact lookup만 제공하고 scan, prefix query, listing은 제공하지 않는다.
5. cleanup은 owner reverse index를 사용해 owner가 가진 record 수에 비례하도록 처리한다.

대량 route binding을 목표로 하는 구현은 아래 자료구조를 기본으로 삼는다.

```text
provider lookup:
  hash map by channel + role + routing_id

route lookup:
  hash map by route hash -> entry id

entry arena:
  entry id -> fixed-size route entry

key arena:
  packed bytes for route keys

owner cleanup:
  owner id -> intrusive route entry list
  owner id -> intrusive topology entry list
```

정렬 순회가 필요한 public snapshot이나 진단 API는 조회 경로와 분리한다. hot path에서
`std::map` 같은 tree 구조만 사용하면 record 수가 늘수록 비용이 커진다. 구현은 exact
lookup과 cleanup path에는 hash 기반 index를 우선 사용해야 한다.

route entry는 가능하면 fixed-size에 가깝게 둔다. owner identity는 반복 저장하지 않고
작은 `owner_id`로 intern한다. key와 value는 entry마다 `std::string`이나 `std::vector`로
들고 있지 않고 key/value arena에 붙여 저장한다. arena도 큰 단일 vector에 의존하지 않고
작은 block을 이어 붙이는 구조를 우선 사용한다.

```text
route_entry:
  key_offset
  key_size
  key_hash
  owner_id
  value_offset
  value_size
  updated_at_ms
  flags
```

이 구조는 Redis가 작은 aggregate type을 compact encoding으로 저장하고, 필요할 때
일반 hash table로 전환하는 아이디어와 같은 방향이다. 우리 경우에는 범용 자료구조가
필요하지 않으므로 처음부터 route binding 전용 dense table을 둘 수 있다.

route binding을 Actor 위치처럼 사용할 때 모든 Actor instance를 Registry에 직접
등록하는 것은 가능하다. 다만 이 경우 반드시 dense storage, snapshot chunking, memory
limit, owner reverse index가 있어야 한다. 단순 map/set 구현으로는 큰 규모를 목표로
하면 안 된다.

Actor 수가 매우 많으면 Framework나 응용이 Actor id를 shard, partition, owner group
같은 더 큰 단위로 묶고, Registry에는 그 단위의 owner만 등록하는 편이 더 안정적이다.

```text
recommended:
  actor shard id -> owner provider

not recommended:
  every actor id -> owner provider
```

수천만 개 이상의 개별 객체 위치를 저장하려면 route binding 저장 구조를 별도
고밀도 directory table로 구현해야 한다. 이 경우에도 Registry peer full snapshot을
그대로 한 덩어리로 보내면 안 되고, chunked snapshot과 backpressure가 필요하다.
억 단위 단일 Registry는 여전히 비목표로 둔다. 그 규모는 shard를 나누거나 별도
directory service를 두는 것이 맞다.

단일 노드에서 기대할 수 있는 대략적인 범위는 저장 구조와 memory budget에 따라
나뉜다. 아래 값은 route key와 value가 작고, exact lookup 위주이며, snapshot이 chunk로
전송된다는 전제의 목표 범위다.

```text
simple map/set storage:
  comfortable: 100K - 1M records
  upper target: 1M - 5M records

dense arena/hash storage:
  comfortable: 10M - 30M records
  high-memory target: 50M - 100M records
  beyond 100M: separate sharding or directory service recommended
```

`50M - 100M` 범위는 Redis식 compact encoding 아이디어를 적용하고, key/value 크기가
작고, 충분한 메모리와 chunked snapshot이 있을 때의 상한 목표다. 이 범위를 기본 보장
값으로 보면 안 된다. 운영 기본값은 더 낮게 잡고, 실제 record 크기와 churn, peer 수,
snapshot 주기를 기준으로 조정한다.

구현은 기본 제한값을 두어야 한다.

1. route binding 총 개수 제한
2. owner provider 하나가 등록할 수 있는 route binding 개수 제한
3. key arena와 value arena memory 제한
4. peer snapshot 한 번에 실을 수 있는 record 수 제한
5. snapshot chunking 또는 backpressure
6. memory budget 초과 시 신규 bind 실패

제한 초과 시 route bind는 `ENOSPC` 또는 `E2BIG` 계열 오류로 실패해야 한다. 실패한
bind가 부분적으로 저장되어서는 안 된다.

### 고성능 구현 상세

대량 route binding을 지원하려면 단순히 `std::unordered_map`으로 바꾸는 정도로는
부족하다. key, value, owner identity, reverse index가 모두 같은 수만큼 늘어나므로
작은 allocation을 줄이고, cleanup 경로가 전체 record 수에 비례하지 않도록 만들어야
한다.

구현은 두 저장 모드를 둘 수 있다.

1. small mode는 현재 코드와 가까운 map/vector 기반 구현이다. 구현이 단순하고
   테스트하기 쉽지만 수백만 record를 넘기면 목표 범위에서 벗어난다.
2. dense mode는 route binding 전용 table이다. 수천만 record 이상을 목표로 할 때는
   이 모드를 사용한다.

dense mode의 기본 구성은 아래와 같다.

```text
dense route store:
  owner table
  key arena
  value arena
  raw observation array
  route entry array
  route hash index
  raw route key index
  raw owner index
  owner intrusive list
  free entry list
```

cluster를 쓰지 않는 단일 Registry에서는 raw observation과 materialized route entry를
같은 row로 표현할 수 있다. cluster를 쓰면 둘을 분리한다. raw observation은 특정
advertising registry가 보낸 사실이고, materialized route entry는 여러 observation 중
현재 resolve에 사용할 winner다.

```text
raw_route_observation:
  advertising_registry_id
  observation_generation
  channel_id
  kind
  key_hash
  key_offset
  key_size
  owner_id
  value_offset
  value_size
  updated_at_ms
```

`owner table`은 owner identity를 한 번만 저장하고 작은 `owner_id`를 발급한다.
route entry는 `owner_id`만 가진다. channel name, service role, routing id,
source registry, registration id를 route entry마다 반복 저장하면 record 수가 늘 때
메모리 사용량이 너무 커진다.

```text
owner_entry:
  channel_id
  role
  routing_id_id
  source_registry_id
  registration_id
  first_route_entry
  first_topology_entry
  route_count
  topology_count
```

channel name, routing id, endpoint, source registry id처럼 반복되는 byte string은
intern table에 넣고 작은 id로 참조한다. intern table도 hash index와 byte arena를
사용한다. 비교할 때는 hash만 믿지 않고 실제 byte sequence를 마지막에 확인한다.

route entry는 fixed-size에 가깝게 유지한다.

```text
dense_route_entry:
  channel_id
  kind
  key_hash
  key_offset
  key_size
  value_offset
  value_size
  owner_id
  updated_at_ms
  next_owner_entry
  prev_owner_entry
  next_free_entry
  flags
```

`next_owner_entry`와 `prev_owner_entry`는 owner reverse index다. owner가 제거될 때
`owner_entry.first_route_entry`에서 시작해 linked list를 따라가면 그 owner가 소유한
route만 방문할 수 있다. 이 방식은 `owner_id -> set<route_key>`보다 큰 규모에서 더
안정적이다. set node allocation이 없고, route 삭제도 entry id 기준으로 처리할 수
있기 때문이다.

route hash index는 open addressing 기반 flat hash table을 기본으로 한다. bucket은
entry id와 key hash만 들고, 실제 key byte 비교는 key arena를 보고 수행한다.
hash 값은 Redis `dict`가 쓰는 방식과 같은 SipHash 계열 함수를 사용한다. 외부 입력에서
온 route key가 한 bucket에 몰려 resolve 성능을 떨어뜨리는 일을 막기 위해서다.

```text
route_hash_bucket:
  key_hash
  entry_id
  entry_generation
  state
```

cluster 모드의 route hash index는 materialized route entry를 가리킨다. 같은 route
identity에 대해 raw observation이 여러 개 있으면 충돌 처리 규칙으로 winner를 고른 뒤
materialized entry 하나만 hash index에 노출한다. raw observation lookup은 별도
observation index나 advertiser list를 사용한다.

raw route key index는 같은 `channel_id + kind + key`에 속한 raw observation handle을
찾기 위한 index다.

```text
observations_by_route:
  route identity -> packed observation handle list
```

raw owner index는 특정 owner가 광고한 raw observation을 찾기 위한 index다.

```text
observations_by_owner:
  owner_id -> packed observation handle list
```

advertiser별 index도 같은 handle list 방식으로 둔다. raw observation row 안에 list
node를 직접 넣지 않는다. 삭제나 snapshot 교체로 stale handle이 생길 수 있으므로,
순회 중 generation을 확인하고 맞지 않으면 건너뛴다.

bind, unbind, peer snapshot 적용은 영향을 받은 route identity만 dirty set에 넣는다.
materialized winner 재계산은 `observations_by_route[route identity]`만 순회해야 한다.
전체 raw observation table을 훑어서 winner를 다시 계산하면 대량 모드 목표를 만족할 수
없다. winner 선택 기준은 충돌 처리 절의 tie-breaker와 동일하다. 즉, 같은 route
identity에 대해 `updated_at_ms`가 더 큰 observation을 우선하고, 같으면 더 작은
`source_registry`, 그래도 같으면 owner routing id byte sequence의 사전식 순서를
적용한다.

충돌 해결 방식은 linear probing, quadratic probing, robin hood hashing 중 하나를
선택할 수 있다. 중요한 계약은 아래와 같다.

1. hash 충돌이 나도 channel, kind, key byte를 비교해서 정확한 route만 반환한다.
2. table load factor는 상한을 둔다. 기본 상한은 0.70 이하를 권장한다.
3. rehash 중에도 memory budget을 넘으면 bind는 실패해야 한다.
4. 삭제는 tombstone을 사용할 수 있지만 tombstone 비율이 높아지면 재구성해야 한다.

bucket `state`는 empty, occupied, tombstone 중 하나다. entry id는 재사용될 수 있으므로
bucket은 entry id와 entry generation을 함께 가진다. stale bucket이나 stale advertiser
list entry가 재사용된 다른 route를 가리키면 안 된다.

incremental rehash는 두 hash table을 동시에 들고 진행한다.

```text
rehash state:
  old_table
  new_table
  migrate_cursor
  migrate_budget_per_tick
```

lookup은 rehash 중 old table과 new table을 모두 확인한다. bind와 unbind는 먼저
`migrate_budget_per_tick`만큼 bucket을 옮긴 뒤 new table에 적용한다. rehash가 끝나면
old table을 lazy free 대상으로 넘긴다. 이 방식은 큰 table resize가 Registry event loop를
오래 막는 일을 피하기 위한 것이다.

key arena와 value arena는 append-only segment를 기본으로 한다. route overwrite나
unbind가 많으면 arena 안에 죽은 byte range가 생긴다. 구현은 아래 중 하나를 선택해야
한다.

1. tombstone byte를 허용하고 dead byte 비율이 임계값을 넘으면 compact한다.
2. size class별 slab을 두고 작은 value 재사용을 우선한다.

dead byte 비율 임계값의 권장값은 40%다. dead byte 비율이 40%를 넘으면 compaction
또는 slab 재사용을 시작한다. 이 값은 구현 상수로 노출해서 운영 환경에 따라 조정할
수 있도록 한다.

초기 구현은 append-only segment와 background-free compaction 없이도 가능하다. 다만
dead byte 비율과 arena 사용량을 진단 counter로 노출해야 한다. memory budget을 넘으면
새 bind를 실패시키고, 기존 live record를 임의로 지우면 안 된다.

memory budget은 live data만 보지 않는다. 아래 항목을 모두 더한 값으로 판단한다.

```text
memory budget accounting:
  owner table bytes
  raw observation array bytes
  route entry array bytes
  hash table bytes
  raw route key index bytes
  raw owner index bytes
  key arena live and dead bytes
  value arena live and dead bytes
  snapshot staging bytes
  pending lazy free bytes
```

bind는 필요한 byte를 예약한 뒤에만 상태를 갱신한다. 예약이 실패하면 raw observation,
hash bucket, owner list, arena 중 어느 것도 부분 갱신하면 안 된다.

Registry 내부 route store는 단일 writer 모델을 기본으로 한다. bind, unbind, provider
cleanup, peer snapshot 적용은 Registry event loop에서 순서대로 실행한다. 다른 thread가
직접 dense table을 읽거나 쓰면 안 된다. 외부 thread에서 들어온 resolve나 query는
Registry event loop로 marshal하거나 immutable snapshot view를 읽어야 한다.

route bind hot path는 아래 순서를 따른다.

```text
bind route:
  validate key and value size
  resolve owner id from live provider
  reserve memory for raw observation, entry, key, and value
  upsert local raw observation
  mark route identity dirty
  recompute winner from observations_by_route
  allocate or reuse materialized route entry
  append key/value bytes if needed
  unlink old owner list node if owner changed
  write materialized route entry
  link entry into owner list
  update hash bucket
  bump snapshot sequence
```

route unbind hot path는 local raw observation을 먼저 제거하고, 같은 route identity의
winner를 다시 계산한다.

```text
unbind route:
  resolve owner id from live provider
  find materialized route entry by route identity
  validate materialized owner matches request owner
  find local raw observation by route identity and owner id
  mark local raw observation deleted
  mark route identity dirty
  recompute winner from observations_by_route
  unlink old materialized owner list node if winner changed
  update or remove materialized route entry
  update hash bucket
  bump snapshot sequence
```

route resolve hot path는 hash lookup과 key byte 비교만 수행해야 한다. owner cleanup,
snapshot 생성, diagnostics용 정렬 순회는 resolve path에 들어오면 안 된다.

```text
resolve route:
  hash channel + kind + key
  probe route hash index
  validate entry generation
  compare channel, kind, and key bytes
  check owner id is live
  return owner routing id and value
```

owner cleanup은 owner list만 방문해야 한다.

```text
cleanup owner:
  find owner id
  mark owner state cleanup_pending
  for each raw observation in observations_by_owner:
    mark raw observation deleted
    mark route identity dirty
  for each dirty route identity:
    recompute winner from observations_by_route
    update or remove materialized route entry
    update hash bucket
  clear owner route list
  clear owner raw observation list
```

이 경로의 비용은 전체 route 수가 아니라 제거되는 owner가 가진 route 수에 비례해야
한다. owner가 매우 많은 route를 가진 경우 cleanup이 길어질 수 있으므로, Registry event
loop를 오래 막지 않도록 batch 삭제를 허용할 수 있다. 단, batch 사이에 resolve가 죽은
owner를 반환하면 안 되므로 cleanup 시작 시 owner를 not-live로 먼저 표시한다.

batch cleanup은 owner state를 먼저 바꾼 뒤 작업 queue에 넣는다.

```text
owner_state:
  live
  cleanup_pending
  deleted

cleanup queue:
  owner_id
  next_observation_index
  budget_remaining
```

resolve는 route entry를 찾은 뒤 owner state가 live인지 확인한다. owner state가
cleanup_pending이면 route entry가 아직 hash table에 남아 있어도 실패해야 한다. 이
규칙이 있어야 batch 삭제가 event loop latency를 낮추면서도 죽은 owner 주소를 반환하지
않는다.

batch cleanup은 `budget_remaining` 소진 시 작업 queue에 남은 작업을 다음 tick으로
미룬다. 권장 tick당 budget은 구현이 정하되, 단일 tick에서 cleanup 처리 시간이
event loop 목표 latency의 10% 이하를 넘지 않도록 제한한다. cleanup은 owner당 route
수에 비례하므로 총 완료 tick 수는 `owner route count / budget_per_tick`을 초과하면
안 된다. cleanup_pending 상태의 owner는 완전히 삭제되기 전까지 hash table에 stale
entry가 남지만 resolve에서 걸러진다. 구현은 cleanup 완료 시 owner state를 deleted로
전환하고, 해당 owner id를 재사용 가능한 pool에 반환한다.

peer snapshot 생성도 한 번에 모든 record를 frame으로 만들면 안 된다. snapshot은
sequence와 cursor를 가진 chunk 단위로 보낸다.
cursor는 Redis `dictScan`처럼 table 내부 위치를 조금씩 전진시키는 값이어야 한다.
snapshot chunk를 복사하는 동안에는 Redis safe iterator와 같은 의미의 rehash pause를
잡아 cursor가 가리키는 table layout이 중간에 이동하지 않게 한다.

```text
snapshot chunk:
  snapshot_seq
  chunk_index
  chunk_count
  is_last
  max_records
  max_bytes
  records
```

받는 Registry는 같은 `advertising_registry + snapshot_seq`에 대해 chunk를 적용한다.
chunk가 누락되거나 순서가 깨지면 해당 snapshot을 버리고 다음 full snapshot을 기다린다.
late join에서 수천만 record를 받는 동안에도 기존 materialized view는 마지막으로 완성된
snapshot을 유지한다. 새 snapshot은 staging raw view에 적용한 뒤, 마지막 chunk를 받은
시점에 materialized view로 전환한다.

staging raw view도 memory budget에 포함한다. snapshot을 받는 동안 기존 live view와
staging view를 동시에 들 수 있으므로, 대량 snapshot은 일시적으로 메모리를 크게 쓸 수
있다. staging budget을 넘으면 chunk 적용을 중단하고 해당 snapshot을 버린다. 이 경우
기존 materialized view는 유지되고, 다음 full snapshot을 기다린다.

대량 모드에서 peer timeout 처리는 `advertising_registry`별 raw observation handle list를
가져야 한다. peer가 timeout될 때 전체 raw table을 scan하면 안 된다.

```text
observations_by_advertiser:
  advertising_registry -> packed observation handle list
```

이 list는 provider, route, topology raw observation을 각각 따로 가진다. peer timeout은
해당 advertiser list만 순회하고, 영향을 받은 provider key, route identity, owner id만
dirty set에 넣는다. materialized view 재계산도 전체 rebuild가 아니라 dirty key와
dirty owner를 우선 처리한다. full rebuild는 startup, late join snapshot 전환, 진단용
복구 경로에만 허용한다.

observation handle은 entry id와 generation을 함께 가져야 한다. route 삭제나 rehash 뒤
entry id가 재사용될 수 있기 때문이다. advertiser list에 stale handle이 남아 있으면
순회 중 건너뛰고, stale 비율이 높아지면 list를 compact한다.

`observations_by_route`와 `observations_by_owner`도 같은 방식으로 stale handle을
건너뛰고 compact한다. route identity별 list가 없으면 last-writer-wins 재계산이 전체
raw table scan으로 커질 수 있다. owner별 list가 없으면 provider cleanup이 전체 raw
table scan으로 커질 수 있다.

대량 모드에서 예상 메모리 사용량은 record 하나가 key/value byte만큼만 차지한다고
계산하면 안 된다. route 하나는 대략 아래 항목을 함께 가진다.

```text
per route memory:
  route entry: 64 - 96 bytes
  hash bucket: 16 - 24 bytes
  owner links: included in route entry
  key bytes: key_size
  value bytes: value_size
  allocator and alignment overhead
```

key와 value가 작아도 1억 record는 수 GB에서 수십 GB까지 갈 수 있다. 그래서 이 초안은
1억 record를 기본 보장으로 두지 않고, dense mode와 충분한 memory budget이 있을 때의
상한 목표로만 둔다.

## 용어

- **provider registration**: Discovery가 socket 또는 SpotNode를 대신해 Registry에
  등록한 provider entry다. Registry는 heartbeat로 이 entry의 생존을 관리한다.
- **owner provider**: 특정 주소 매핑 record를 등록한 provider registration이다.
- **owner-bound record**: owner provider가 제거될 때 함께 정리되어야 하는 record다.
- **SPOT owner entry**: Registry topology에 저장된 `ZLINK_SERVICE_KIND_SPOT_PUB`
  entry다. `routing_id`는 SPOT RID이고, `endpoint`는 owner SpotNode endpoint다.
- **route binding**: 사용자가 정의한 `kind + key`를 현재 owner provider로 연결하는
  Registry record다.
- **live provider**: Registry service provider 목록에 아직 남아 있고 timeout으로
  만료되지 않은 provider다.
- **source registry**: provider가 처음 등록된 원 출처 Registry다. owner generation의
  일부로 사용한다.
- **provider update sequence**: 같은 provider generation 안에서 provider의 value나
  weight 같은 부가 속성이 바뀐 순서를 나타내는 값이다. owner generation을 구분하는
  값은 아니며, stale snapshot이 최신 provider row를 덮어쓰지 못하게 하기 위해 사용한다.
- **advertising registry**: peer sync snapshot을 보낸 Registry다. source registry와
  같을 수도 있고 다를 수도 있다. 여러 Registry를 거쳐 전달된 provider를 정확히
  다루려면 두 값을 구분해야 한다.
- **raw observation**: 특정 advertising registry가 snapshot에서 광고한 provider 또는
  owner-bound record다. Registry는 raw observation을 모아 materialized view를 만든다.
- **owner generation**: 특정 provider registration의 단일 실행 인스턴스를 식별하는
  단위다. `source_registry + registration_id` 쌍으로 구분한다. 같은 routing id로
  서버가 재시작되어도 새 `registration_id`를 발급받으면 새 owner generation이다.
  이전 generation이 소유하던 owner-bound record는 새 generation에 자동 승계하지 않는다.

## 공통 owner 모델

Registry가 실제로 생존을 관리하는 단위는 Discovery handle 자체가 아니라 provider
registration이다. Discovery는 socket 또는 SpotNode를 대신해 provider를 등록하고,
그 provider의 heartbeat를 보낸다. 따라서 owner-bound record의 owner는 Discovery
포인터가 아니라 Registry에 저장된 provider registration이다.

owner identity는 최소한 아래 값을 가진다.

```text
owner identity:
  channel_name
  service_role
  routing_id
  source_registry
  registration_id
```

`channel_name + service_role + routing_id`는 provider map에서 owner를 찾기 위한
논리 key다. `endpoint`는 현재 connect 대상 주소이며 provider key가 아니다. 같은
RID의 endpoint가 바뀌면 주소 리스트 관점에서는 같은 logical provider의 주소가
갱신된 것으로 본다. 그러나 routing id는 사용자가 재사용할 수 있으므로 owner generation을
구분하는 최종 기준으로 쓰면 안 된다. `source_registry`는 Registry cluster에서 이 owner가
어느 registry에서 왔는지 구분하기 위한 값이다. `registration_id`는 Registry가 provider
registration을 받을 때 발급하는 owner generation token이다.

endpoint는 owner identity가 아니라 owner의 현재 주소 속성이다. route binding이나
SPOT owner row가 진단 또는 빠른 resolve를 위해 endpoint를 함께 저장할 수는 있지만,
cleanup과 owner 비교는 `source_registry + registration_id`를 포함한 owner identity로
해야 한다. endpoint가 바뀌었다는 이유로 같은 owner generation의 record를 다른 owner로
보면 안 된다.

`registration_id`는 같은 registry 안에서 provider registration마다 단조 증가하는
`uint64_t` 값이다. cluster 전체 owner identity는 `source_registry + registration_id`로
구분한다. 같은 routing id로 서버가 빠르게 다시 떠도 새 registration은
새 `registration_id`를 받으므로 이전 owner와 새 owner를 구분할 수 있다.

`provider_update_seq`는 owner identity에 포함하지 않는다. 이 값은 같은 owner generation
안에서 provider row의 부가 속성 중 어느 쪽이 최신인지 판단하기 위한 보조 값이다.

구현은 owner-bound record를 반환하기 전에 같은 owner identity를 가진 live provider가
현재 Registry view에 남아 있는지 확인해야 한다. live provider가 없으면 record가
아직 map에 남아 있어도 조회 결과로 반환하지 않는다.

### owner generation

같은 routing id로 서버가 빠르게 재시작되어도 이전 provider와 새 provider는 같은
owner generation이 아니다. 새 process는 새 provider registration이며, 새
`registration_id`를 받으면 새 owner generation으로 본다.

이전 owner generation이 사라지면 그 owner가 등록했던 SPOT RID owner entry와 route
binding은 삭제된 것으로 처리한다. 같은 routing id에 새 owner generation이 이미
등록되어 있더라도 이전 owner의 record를 새 owner에게 자동 승계하지 않는다.

따라서 빠른 재시작 시 동작은 아래와 같다.

1. 이전 provider `R1`이 registration id `G1`으로 등록된다.
2. 서버가 죽고 같은 routing id로 새 provider `R2`가 registration id `G2`로 등록된다.
3. `G1 != G2`이므로 `R1` record는 `R2` record가 아니다.
4. `R1/G1`이 소유한 owner-bound record는 정리 대상이 된다.
5. `R2/G2`가 같은 SPOT RID 또는 route key를 다시 소유하려면 새로 report 또는 bind해야 한다.
6. `R2/G2`가 다시 등록하기 전까지 해당 SPOT RID 또는 route key resolve는 실패한다.

이 규칙은 서버 재시작 후 실제 객체가 다시 생성되었는지 Registry가 알 수 없기 때문에
필요하다. endpoint가 같다는 이유만으로 이전 객체가 새 process에도 존재한다고
가정하면 죽은 객체 주소를 살아 있는 것처럼 반환할 수 있다.

### 빠른 재등록 처리

서버가 내려간 순간을 Registry가 즉시 알 수는 없다. 서버가 내려간 뒤 다시 올라오지
않으면 Registry는 heartbeat timeout으로 이전 provider를 제거하고, 그 provider가
소유한 owner-bound record를 함께 정리한다.

서버가 같은 routing id로 빠르게 다시 올라오면 이전 provider가 아직 timeout되지 않았을
수 있다. 이 경우 새 register 요청은 같은 provider key로 들어온다.

```text
provider key:
  channel_name
  service_role
  routing_id
```

Registry는 같은 provider key로 새 register 요청을 받으면 기존 provider를 새 process로
자동 승계하지 않는다. 대신 아래 순서로 처리한다.

1. 기존 provider registration `G1`을 replaced 상태로 본다.
2. `G1`이 소유한 owner-bound record를 정리한다.
3. 새 provider registration `G2`를 만들고 새 `registration_id`를 발급한다.
4. 이후 같은 SPOT RID 또는 route key가 필요하면 `G2`가 새로 report 또는 bind해야 한다.

주소 리스트 row는 같은 provider key에 대해 새 endpoint 값으로 갱신된다. 즉 같은 RID가
다른 endpoint로 다시 등록되면 주소 공유 관점에서는 endpoint가 이동한 것으로 보인다.
그러나 owner-bound record는 endpoint 이동과 별개로 새 generation에 자동 승계하지
않는다.

이 처리에서 `registration_id`는 서버가 내려간 순간을 감지하기 위한 값이 아니다.
이 값은 같은 provider key를 재사용하는 이전 실행과 새 실행을 구분하기 위한 generation
token이다.

또한 이전 process에서 늦게 도착한 owner-bound 요청을 차단하는 데도 사용한다. 예를
들어 `G1`이 죽기 직전에 보낸 route bind가 `G2` 등록 뒤 도착할 수 있다. 요청에
`owner_registration_id == G1`이 들어 있으면 Registry는 현재 live provider의
`registration_id == G2`와 다르다는 것을 확인하고 그 요청을 거부한다.

### registration token 전달

Registry는 provider register ack에 `registration_id`를 포함해야 한다. Discovery는
등록된 provider별로 이 값을 저장하고, owner-bound record를 report, bind, unbind할 때
해당 `registration_id`를 함께 보낸다.

Registry는 owner-bound 요청을 처리할 때 요청의 `registration_id`가 현재 live provider의
`registration_id`와 같은지 확인한다. 값이 다르면 이전 process에서 지연되어 도착한
요청으로 보고 실패시킨다. 이 검증이 없으면 이전 process의 늦은 topology report나
route bind가 같은 routing id의 새 provider에 잘못 붙을 수 있다.

## SPOT RID owner cleanup

SPOT RID는 독립 프로세스의 주소가 아니라 SpotNode 안에 존재하는 논리 주소다.
따라서 SPOT RID의 생존 여부는 개별 SPOT 객체가 아니라 그 SPOT을 소유한
SpotNode의 생존 여부에 묶인다.

문제가 되는 상황은 다음과 같다.

1. SpotNode가 Discovery에 등록된다.
2. SpotNode 안의 Spot이 SPOT RID를 Registry topology에 보고한다.
3. SpotNode 프로세스가 비정상 종료되어 명시적인 `STOPPED` report를 보내지 못한다.
4. Registry는 node provider heartbeat timeout으로 SpotNode provider를 제거한다.
5. SPOT owner entry가 별도로 남으면 topology snapshot이나 진단 정보에 죽은
   SPOT RID가 계속 보일 수 있다.

이 문제는 SPOT마다 heartbeat를 추가해서 풀 수 있지만, 그렇게 하면 SPOT RID가
독립 생존 단위처럼 보인다. 실제 생존 단위는 SpotNode이므로 node provider 만료를
기준으로 정리한다.

### 정리 대상

Registry는 SpotNode provider를 제거하는 모든 경로에서 SPOT owner entry를 함께
정리해야 한다.

정리 대상은 아래 조건을 모두 만족하는 topology entry다.

- `entry.auto_connect_type == ZLINK_AUTO_CONNECT_SPOT_MESH`
- `entry.service_kind == ZLINK_SERVICE_KIND_SPOT_PUB`
- `entry.service_role == ZLINK_SERVICE_ROLE_SPOT`
- `entry.channel_name == removed_provider.channel_name`
- 내부 `owner_identity == removed_provider.owner_identity`

현재 공개 topology entry에는 owner node routing id와 `registration_id`가 없다.
새 구현에서는 SPOT owner entry 내부 row가 owner node RID와 `registration_id`를 반드시
보관해야 하며, cleanup의 실제 기준은 endpoint가 아니라 owner node RID와 owner
generation이다. endpoint는 진단과 호환성 있는 snapshot 표시를 위한 주소 속성으로만
사용한다.

### Registry owner 선택

`resolve_spot()`에서 특정 SPOT RID의 owner를 고를 때 Registry는 topology entry만
보고 결정하면 안 된다. 후보 topology entry의 내부 owner identity와 일치하는 live
`service_role_spot` provider가 있어야 한다. 후보가 여러 개이면 topology
`last_reported_ms`, owner `registration_id`, owner routing id byte sequence처럼 구현이
정한 안정적인 tie-breaker를 사용한다.

live provider가 없는 topology entry는 stale entry로 본다. stale entry가 아직
`LOST` 또는 삭제 처리되지 않았더라도 `resolve_spot()` 결과가 되어서는 안 된다.

### 상태 처리

Registry는 provider 제거 시 SPOT owner entry를 먼저 `LOST`로 바꾸고, 짧은 grace
구간 뒤 삭제한다. 즉시 삭제는 이 초안의 기본 동작이 아니다.

Registry topology는 이미 `READY`, `LOST`, `STOPPED` 상태를 갖고 있으므로 갑작스러운
node 상실을 `LOST`로 표현하는 것이 자연스럽다. 다만 `resolve_spot()`은 `READY`
상태만 owner로 인정해야 하므로, `LOST`로 바뀐 entry는 즉시 주소 조회 대상에서
제외된다.

삭제 grace는 기존 stopped topology GC와 같은 1000 ms를 기본값으로 한다.

### endpoint 변경과 재사용

같은 routing id나 endpoint가 빠르게 재사용될 수 있으므로 provider 제거 시점에 같은
`channel_name + service_role_spot + routing_id` 조합의 live provider가 이미 다시
등록되어 있으면 새 provider의 SPOT owner entry를 지우면 안 된다.

구현은 node provider의 routing id를 owner identity로 사용한다. SPOT owner entry가
owner node RID와 `registration_id`를 함께 저장해야 한다. 그렇게 하면 endpoint가
변경되거나 재사용되어도 이전 node generation이 소유하던 SPOT RID만 정확히 정리할 수
있다.

새 provider가 같은 routing id로 이미 등록되어 있더라도, 이전 provider generation에
묶인 SPOT owner entry는 새 provider에게 승계하지 않는다. 이전 provider가 소유하던
SPOT은 서버 종료와 함께 사라진 것으로 처리한다. 새 provider가 같은 SPOT RID를 다시
소유하려면 새 topology report를 보내야 한다.

### Registry cluster 동기화

Registry peer sync를 사용할 때 SPOT owner entry는 owner SpotNode provider와 같은
owner generation을 따라야 한다. peer registry가 사라지면 그 peer가 광고하던 raw
observation을 제거하고 materialized view를 다시 만든다. 그 결과 live provider가 더
이상 없는 owner generation의 SPOT owner entry를 함께 정리한다.

다른 registry에서 같은 routing id를 가진 새 provider가 들어온 경우에는
`source_registry`와 `registration_id`를 함께 비교해서 이전 provider의 SPOT owner
entry만 정리한다.

현재 공개 `zlink_registry_topology_entry_t`에는 `source_registry` 필드가 없다.
구현은 public struct를 바꾸지 않더라도 Registry 내부 topology 저장 row에는 owner
`source_registry`와 `registration_id`를 보관해야 한다. 그래야 raw observation 제거 뒤
어떤 SPOT owner entry가 live owner를 잃었는지 정확히 알 수 있다.

## Route binding

기존 Discovery metadata 기능은 route binding API로 대체한다. 호환성은 유지하지
않는다.

Discovery metadata는 일반적인 저장소로 쓰기 위한 기능이 아니다. 이 값은 Actor
위치처럼 어떤 논리 key가 어느 provider에 속하는지를 찾기 위한 보조 주소 매핑에
가깝다. 이름이 metadata이면 호출자가 임의 데이터를 저장해도 된다고 이해하기 쉽고,
TTL이나 정리 기준도 모호해진다.

Route binding은 `channel_name + kind + key`를 owner provider로 연결하는 record다.
route record마다 TTL을 두지 않고, 그 route를 등록한 provider registration의 생존
상태를 따른다.

### kind 값

`kind`는 public C API 에서 `zlink_route_kind_t`로 노출한다. core는 route value의
payload 의미를 해석하지 않지만, core/framework가 함께 쓰는 route identity 충돌을 막기
위해 기본 route kind 값은 공개 상수로 예약한다.

```c
typedef uint32_t zlink_route_kind_t;

#define ZLINK_ROUTE_KIND_INVALID       0u
#define ZLINK_ROUTE_KIND_ACTOR         1u
#define ZLINK_ROUTE_KIND_SPOT_NAME     2u
#define ZLINK_ROUTE_KIND_ACTOR_SESSION 3u
```

계약은 다음과 같다.

- `0`은 invalid 값이다.
- `ZLINK_ROUTE_KIND_ACTOR`는 actor active route sync 가 사용한다.
- `ZLINK_ROUTE_KIND_SPOT_NAME`은 framework Spot name directory 가 사용한다.
- `ZLINK_ROUTE_KIND_ACTOR_SESSION`은 framework actor-session binding 이 사용한다.
- core는 route value payload 의미를 해석하지 않는다.
- 같은 `channel_name` 안에서 `kind + key`가 route identity가 된다.
- 새 public route kind 를 추가할 때는 공개 header, binding, spec 을 함께 갱신한다.

### 크기 제한

route key와 value는 Registry 내부 상태와 peer sync 메시지에 들어가므로 크기 제한이
필요하다. 이 초안의 기본 제한은 아래와 같다.

```c
#define ZLINK_ROUTE_KEY_MAX 256u
#define ZLINK_ROUTE_VALUE_MAX 4096u
```

계약은 다음과 같다.

- `key_size == 0`이면 실패한다.
- `key_size > ZLINK_ROUTE_KEY_MAX`이면 실패한다.
- `value_size > ZLINK_ROUTE_VALUE_MAX`이면 실패한다.
- `value == NULL && value_size == 0`은 빈 value로 허용한다.
- `value == NULL && value_size > 0`이면 실패한다.

구현이 기존 Discovery metadata 최대 크기 option을 제거하지 않고 재사용한다면,
그 option은 route value 최대 크기를 조정하는 이름으로 다시 정의해야 한다. 단,
route key 최대 크기는 protocol map key 안정성을 위해 고정한다.

### 공개 API 변경 요약

제거 대상:

```c
zlink_config_result_t zlink_discovery_set_metadata(
  void *discovery,
  const void *data,
  size_t size);

zlink_config_result_t zlink_discovery_get_metadata(
  void *discovery,
  zlink_msg_t *metadata_out);

zlink_config_result_t zlink_registry_member_peer_metadata(
  void *registry,
  const char *channel_name,
  zlink_service_role_t service_role,
  const char *endpoint,
  zlink_msg_t *metadata_out);

zlink_config_result_t zlink_discovery_member_peer_metadata(
  void *discovery,
  zlink_service_role_t service_role,
  const char *endpoint,
  zlink_msg_t *metadata_out);
```

추가 대상:

```c
typedef uint32_t zlink_route_kind_t;

#define ZLINK_ROUTE_KIND_INVALID       0u
#define ZLINK_ROUTE_KIND_ACTOR         1u
#define ZLINK_ROUTE_KIND_SPOT_NAME     2u
#define ZLINK_ROUTE_KIND_ACTOR_SESSION 3u

zlink_config_result_t zlink_discovery_bind_route(
  void *discovery,
  zlink_route_kind_t kind,
  const void *key,
  size_t key_size,
  const void *value,
  size_t value_size);

zlink_config_result_t zlink_discovery_unbind_route(
  void *discovery,
  zlink_route_kind_t kind,
  const void *key,
  size_t key_size);

zlink_config_result_t zlink_discovery_resolve_route(
  void *discovery,
  zlink_route_kind_t kind,
  const void *key,
  size_t key_size,
  zlink_routing_id_t *owner_rid_out,
  zlink_msg_t *value_out);
```

성공 시 `value_out`은 bind 시 저장한 value frame 으로 초기화된다. 호출자는 사용 후
`zlink_msg_close()`로 닫아야 한다.

이 세 API는 Discovery handle의 `channel_name`을 route namespace로 사용한다. 별도
`channel_name` 인자는 받지 않는다. 다른 channel의 route를 조회하려면 그 channel을
가진 Discovery handle을 사용한다.

### bind 계약

`zlink_discovery_bind_route()`는 route를 새로 만들거나 기존 route를 갱신한다.
호출한 Discovery의 `channel_name`이 route namespace로 쓰인다.

bind는 반드시 route 대상 객체를 실제로 소유한 socket 또는 SpotNode의 Discovery로
호출해야 한다. 이 제약은 route cleanup을 owner provider 생존 상태에 묶기 위해
필요하다.

Registry는 bind 요청을 처리할 때 다음 owner identity를 저장한다.

- owner channel name
- owner service role
- owner routing id
- owner source registry
- owner registration id

Registry는 진단과 resolve 응답 보조 정보로 owner의 현재 endpoint를 함께 보관할 수
있다. 그러나 endpoint는 owner identity가 아니며, cleanup과 owner 비교에는 사용하지
않는다.

Discovery는 bind 요청을 보낼 owner provider를 먼저 골라야 한다. 기본 규칙은
호출한 Discovery가 현재 등록한 provider registration이 정확히 하나일 때만 bind를
허용하는 것이다.

```text
local owner candidates:
  registered_services where channel_name == discovery.channel_name
```

후보가 없으면 bind는 실패한다. 후보가 둘 이상이면 owner를 모호하게 추론할 수
없으므로 bind는 실패한다. 향후 하나의 Discovery가 여러 provider의 route를
등록해야 한다면 owner provider를 명시하는 별도 확장 API를 추가한다.

Discovery는 route bind를 owner provider가 등록된 Registry uplink로 보내야 한다.
임의의 최신 Registry uplink로 보내면 그 Registry가 owner provider를 local provider로
검증하지 못할 수 있다.

Registry는 bind 요청을 받으면 요청에 포함된 owner `service_role + routing_id`가
같은 channel의 live local provider인지 확인한다. provider가 없거나 foreign provider면
bind는 실패한다. provider가 있으면 Registry에 저장된 provider routing id를 route
owner identity로, endpoint를 owner address로 사용한다.

오류 기준은 다음과 같다.

- `discovery == NULL`, `kind == 0`, key/value 인자가 잘못되면 `EINVAL`
- owner provider 후보가 없으면 `ENOENT`
- owner provider 후보가 둘 이상이면 `EBUSY`
- owner provider가 등록된 Registry에 연결되어 있지 않으면 `EAGAIN`
- Registry가 owner provider를 live local provider로 확인하지 못하면 `ENOENT`

### unbind 계약

`zlink_discovery_unbind_route()`는 `channel_name + kind + key`에 해당하는 route를
삭제한다.

unbind는 저장된 owner provider와 호출한 Discovery가 고른 owner provider가 같을 때만
route를 삭제한다. 다른 provider가 이미 같은 route key를 새로 bind한 경우, 이전
owner의 unbind 요청은 새 route를 지우면 안 된다.

이 규칙은 actor 이동이나 route handover 중에 이전 owner가 새 owner의 route를
삭제하는 일을 막기 위한 것이다.

route가 없으면 `ENOENT`로 실패한다. route는 있지만 owner가 다르면 `ENOENT`로
실패한다. 이 경우 호출자는 이미 자신이 소유한 route가 아니라고만 알면 충분하다.

unbind도 bind와 같은 owner provider 선택 규칙을 사용한다.

- owner provider 후보가 없으면 `ENOENT`
- owner provider 후보가 둘 이상이면 `EBUSY`
- owner provider가 등록된 Registry에 연결되어 있지 않으면 `EAGAIN`

### resolve 계약

`zlink_discovery_resolve_route()`는 호출한 Discovery의 `channel_name` 안에서
`kind + key`를 조회한다.

resolve는 route owner와 같은 Discovery로 호출할 필요가 없다. 같은 channel을 보는
Discovery라면 route를 조회할 수 있다. Registry는 저장된 owner provider가 live
상태일 때만 route를 반환한다.

반환값은 다음 의미를 가진다.

- `owner_rid_out`은 owner provider routing id다.
- `value_out`은 bind 시 저장한 선택 값이다.
- route가 없거나 owner provider가 live 상태가 아니면 `ENOENT`로 실패한다.

`owner_rid_out`은 항상 유효한 포인터여야 한다. `owner_rid_out == NULL`이면 실패한다.
owner routing id가 필요 없는 경우에도 지역 변수를 선언해서 전달해야 한다. 향후
`owner_rid_out == NULL`을 허용하는 확장이 필요하면 별도 API로 추가한다.

`value_out != NULL`인 경우 성공 시 새 `zlink_msg_t`로 초기화된 value를 반환한다.
호출자는 사용 후 `zlink_msg_close()`로 닫아야 한다. 저장된 value가 비어 있으면
크기 0의 메시지를 반환한다.

오류 기준은 다음과 같다.

- `discovery == NULL`, `kind == 0`, key 인자가 잘못되면 `EINVAL`
- Registry uplink를 아직 알 수 없으면 `EAGAIN`
- route가 없거나 owner provider가 live 상태가 아니면 `ENOENT`
- Registry reply frame이 깨졌으면 `EPROTO`

### Registry 저장 모델

Registry는 route binding을 service provider와 별도 map으로 저장한다.

```text
route identity:
  channel_name
  kind
  key

route owner:
  owner_channel_name
  owner_service_role
  owner_routing_id
  source_registry
  registration_id

route owner address:
  endpoint

route payload:
  value
  updated_at_ms
```

`updated_at_ms`는 진단과 충돌 해결을 위한 시각이며 TTL이 아니다. route record는
개별 만료 시각을 갖지 않는다.

### 내부 자료구조

Registry는 owner-bound record를 두 방향으로 찾아야 한다.

1. resolve 요청에서는 `channel_name + kind + key`로 route를 찾아야 한다.
2. provider 제거에서는 owner provider가 소유한 모든 record를 한 번에 찾아야 한다.

따라서 primary map과 owner reverse index를 함께 유지한다.

```text
raw provider observations:
  provider_observation_key -> provider_entry

provider_observation_key:
  advertising_registry
  source_registry
  registration_id
  channel_name
  service_role
  routing_id

materialized provider map:
  provider_key -> provider_entry

provider_key:
  channel_name
  service_role
  routing_id

provider_entry:
  endpoint
  routing_id
  source_registry
  registration_id
  provider_update_seq
  registered_at
  last_heartbeat
  weight
  value

owner_identity:
  channel_name
  service_role
  routing_id
  source_registry
  registration_id

owner_address:
  endpoint

raw route observations:
  route_observation_key -> route_observation

route_observation_key:
  advertising_registry
  channel_name
  kind
  key_bytes

route_observation:
  owner_identity
  owner_address
  value
  updated_at_ms

`route_observation_key`에 `owner_identity`가 포함되지 않는 것은 의도된 설계다.
같은 `advertising_registry`가 같은 route key를 다른 owner로 다시 bind하면 이전
observation이 새 observation으로 덮어써진다. 이는 single registry 안의 last writer
wins 정책과 일치한다. advertising_registry당 route key 하나의 observation만 유지한다.

materialized route map:
  route_key -> route_entry

route_key:
  channel_name
  kind
  key_bytes

route_entry:
  owner_identity
  owner_address
  value
  updated_at_ms

routes_by_owner:
  owner_identity -> set<route_key>
```

`key_bytes`와 `value`는 binary data다. 구현은 `std::string`처럼 byte sequence를
key로 쓸 수 있는 타입을 사용해도 되지만 NUL 종료 문자열로 해석하면 안 된다.

위 구조의 `set<route_key>`는 small mode에서 이해하기 쉬운 표현이다. dense mode에서는
owner별 set node를 만들지 않고 route entry 안의 `next_owner_entry`와
`prev_owner_entry`로 intrusive list를 유지한다. 구현은 같은 의미를 더 적은 allocation으로
만족해야 한다.

`raw route observations`는 peer sync와 late join 수렴을 위해 필요하다. peer registry가
timeout되거나 새 full snapshot을 보내면 `advertising_registry` 기준으로 해당 peer가
광고한 route observation만 교체하거나 제거한다. 그 뒤 live provider가 있는 row만
`materialized route map`으로 승격한다.

`routes_by_owner`는 cleanup 성능을 위해 필요하다. owner provider가 제거될 때 전체
materialized route map을 매번 훑으면 route 수가 많아질수록 timeout 처리 비용이 커진다.
owner generation 제거는 아래 순서로 처리한다.

1. raw provider observation 제거 뒤 materialized provider map을 다시 만든다.
2. 더 이상 materialized provider map에 없는 owner generation에서 `owner_identity`를 만든다.
3. `routes_by_owner[owner_identity]`에서 route key 목록을 가져온다.
4. 각 route key가 아직 같은 owner를 가리키는지 확인한 뒤 materialized route map에서 삭제한다.
5. owner index entry를 삭제한다.

4번 확인은 handover race를 막기 위한 방어다. 같은 route key가 이미 새 owner로
덮어써졌다면 이전 owner cleanup이 새 owner route를 지우면 안 된다.

route bind는 아래 순서로 처리한다.

1. materialized provider map에서 요청 owner provider를 찾고 `registration_id`가 같은지 확인한다.
2. local registry가 광고하는 raw route observation을 새 owner와 value로 갱신한다.
3. raw route observations에서 해당 route key의 materialized winner를 다시 계산한다.
4. materialized route map에 기존 route가 있으면 기존 owner의 `routes_by_owner`에서
   route key를 제거한다.
5. materialized route map을 새 winner로 갱신한다.
6. 새 owner의 `routes_by_owner`에 route key를 추가한다.
7. Registry sync sequence를 증가시킨다.

route unbind는 materialized route map에서 route key를 찾은 뒤 저장된 owner가 요청
owner와 같을 때만 삭제한다. 삭제할 때는 local raw route observation, materialized route map,
`routes_by_owner`를 같은 critical section 안에서 함께 갱신한다. 다른 peer가 광고한
같은 route key observation이 남아 있으면 materialized route map은 그 observation으로
다시 수렴할 수 있다.

SPOT owner entry도 같은 원칙을 사용한다. 공개 topology key는 그대로 두고, Registry
내부 topology row에 owner identity를 추가한다.

```text
raw topology observations:
  topology_observation_key -> topology_entry

topology_observation_key:
  advertising_registry
  topology_key
  owner_identity

materialized topology map:
  topology_key -> topology_entry

topology_entry:
  public_topology_entry
  owner_identity
  owner_address

topology_by_owner:
  owner_identity -> set<topology_key>
```

`topology_by_owner`의 `set<topology_key>`도 small mode 설명이다. dense mode에서는
topology entry에 owner intrusive list link를 두거나, owner별 packed topology handle
list를 둔다. SpotNode provider cleanup도 전체 topology map을 scan하면 안 된다.

SpotNode provider가 제거되면 `topology_by_owner[owner_identity]`로 해당 provider가
소유한 SPOT owner entry를 찾는다. 각 topology row가 아직 같은 owner를 가리키는지
확인한 뒤 `LOST`로 바꾸고, grace 시간이 지난 뒤 삭제한다.

`raw topology observations`도 route와 같은 이유로 필요하다. peer timeout이나 full
snapshot 교체는 `advertising_registry` 기준 raw topology observation에만 적용하고,
그 뒤 live owner provider가 있는 row만 materialized topology map에 남긴다.

Registry cluster sync에서 owner provider보다 route row가 먼저 도착할 수 있다. 이
경우 pending index를 별도로 둔다.

```text
pending_routes_by_owner:
  owner_identity -> set<route_key>
```

pending route는 resolve 결과로 반환하지 않는다. 같은 owner provider가 도착하면
pending route를 `routes_by_owner`로 승격한다. pending route를 광고한 peer registry가
timeout되면 그 peer의 raw owner-bound observation은 제거한다. 다만 같은 pending route가
다른 live peer snapshot에도 남아 있으면 그 observation은 유지할 수 있다.

dense mode에서는 pending route도 별도 set node로 만들지 않는다. raw observation의
owner가 아직 live provider로 materialize되지 않았다는 상태만 표시하고, owner가 live가
되는 시점에 materialized route winner 계산 대상으로 올린다.

### 충돌 처리

같은 `channel_name + kind + key`를 여러 owner가 bind할 수 있다. 이때 기본 정책은
**last writer wins**다.

Registry는 새 bind를 받으면 route owner를 새 provider로 바꾼다. 이전 owner가
나중에 unbind를 보내더라도 저장된 owner와 다르면 삭제하지 않는다.

충돌을 오류로 다루는 정책도 가능하지만, actor 이동이나 소유권 handover에서는 새
owner가 기존 route를 덮어쓸 수 있어야 하므로 기본 정책은 last writer wins가 더
단순하다.

last writer wins는 같은 Registry 안의 처리 순서를 기준으로 한다. Registry cluster에서
서로 다른 registry가 같은 route identity를 동시에 bind하면 deterministic tie-breaker가
필요하다. 기본 tie-breaker는 아래 순서다.

1. 더 큰 `updated_at_ms`
2. 같은 시각이면 더 작은 `source_registry`
3. 그래도 같으면 owner routing id byte sequence의 사전식 순서

이 규칙은 cluster sync가 같은 입력을 여러 순서로 받아도 최종 route owner가 같아지게
하기 위한 것이다.

`updated_at_ms`는 wall clock 기반이므로 registry 간 clock skew가 있으면 실제 bind
순서와 tie-breaker 결과가 다를 수 있다. secondary와 tertiary tie-breaker 덕분에
최종 winner는 deterministic하지만, clock skew가 클수록 bind 순서와 winner가 일치하지
않을 가능성이 높아진다. 운영 환경에서는 registry 노드 간 clock 동기화(NTP 등)를
권장한다.

### route value 제한

route value는 주소 매핑에 필요한 작은 보조 정보만 담기 위한 값이다. Registry는
value 내용을 해석하지 않고 exact resolve에서만 반환한다.

아래 기능은 제공하지 않는다.

- route value 조건 검색
- prefix scan
- 전체 route listing
- value 내부 필드별 조회

이 제한은 route binding이 일반 저장소로 확장되는 것을 막기 위한 것이다.

## 공통 cleanup 계약

Registry는 provider registration을 제거하는 모든 경로에서 그 provider가 소유한
owner-bound record를 함께 정리해야 한다.

정리 시점은 다음과 같다.

1. heartbeat timeout으로 provider가 만료될 때
2. unregister 요청으로 provider가 제거될 때
3. discovery destroy 또는 socket shutdown 흐름에서 provider가 제거될 때
4. peer registry timeout으로 그 peer가 광고한 provider observation을 제거할 때
5. channel contract 교체로 provider 집합을 제거할 때

정리 기준은 owner provider identity다. provider가 사라졌는데 owner-bound record가
남으면 resolve 계열 API가 죽은 주소를 반환할 수 있으므로 반드시 함께 제거한다.

cleanup은 provider 제거와 같은 lock 또는 같은 상태 전이 안에서 처리해야 한다.
provider만 먼저 지우고 owner-bound record를 나중에 비동기로 지우면, 그 사이의
resolve가 죽은 owner를 반환할 수 있다.

Registry peer sync는 owner-bound record도 동기화해야 한다. peer registry에서 받은
provider observation을 제거할 때는 materialized view를 다시 만들고, 더 이상 live
provider로 남지 않은 owner generation의 owner-bound record를 함께 제거한다.
`source_registry`는 원 출처이므로 peer timeout의 직접 삭제 기준으로 쓰면 안 된다.

local provider가 제거되는 경우에는 그 local provider가 소유한 raw owner-bound
observation도 함께 제거한다. 예를 들어 local SpotNode provider가 unregister되면 local
Registry가 광고하던 SPOT owner observation과 route binding observation도 같은 상태 전이
안에서 삭제한다.

peer snapshot이나 peer timeout 경로에서는 `advertising_registry`가 보낸 raw observation만
교체하거나 제거한다. 그 뒤 materialized view를 다시 만들고, live owner provider가 없는
route나 SPOT owner row는 resolve 결과에서 제외한다. 같은 owner-bound row가 다른 live peer
snapshot에 아직 남아 있으면 그 raw observation은 유지될 수 있지만, owner provider가
materialize되지 않는 동안에는 pending 또는 stale 상태로만 보관한다.

## 기존 Registry sync 동작과 확장 지점

현재 Registry 사이의 provider 공유는 `msg_service_list`를 주기적으로 보내는 방식이다.
이 메시지는 변경분이 아니라 Registry가 현재 알고 있는 provider 목록의 snapshot이다.
Registry runtime은 다음 경우에 `msg_service_list`를 보낸다.

1. provider 목록이 바뀌어 `_list_seq`가 증가했을 때
2. `_broadcast_interval_ms` 주기가 지났을 때
3. peer가 PUB socket에 새로 subscribe해서 XPUB subscription frame을 받았을 때

받는 Registry는 peer별 `_peer_seq`를 보고 오래된 snapshot을 무시한다. 새 snapshot이면
같은 peer registry id가 광고한 기존 provider observation을 지우고, snapshot 안의
provider observation을 다시 넣는다. peer Registry가 `_broadcast_interval_ms * 3` 동안
보이지 않으면 그 peer registry id가 광고하던 observation을 모두 제거한다.

이 구조 덕분에 새 Registry가 나중에 peer PUB endpoint에 subscribe해도 다음
`msg_service_list`를 받으면 provider 목록은 수렴할 수 있다. 즉, 기존 provider 공유
로직에는 late join을 위한 full snapshot 성격이 이미 있다.

다만 현재 snapshot에는 아래 정보가 없다.

- provider별 원 출처 registry id
- provider별 `registration_id`
- Registry topology row
- SPOT owner entry의 내부 owner identity
- route binding row

현재 구현의 `source_registry`는 메시지 안에서 보존되는 원 출처가 아니다. 받는 Registry는
snapshot 안의 모든 provider를 "이 snapshot을 보낸 peer registry id에서 온 provider"로
저장한다. 따라서 여러 Registry를 거쳐 전달된 provider는 원래 등록된 Registry id를
잃는다.

owner-bound route 설계에서는 `source_registry + registration_id`가 owner generation을
구분하는 값이므로, 기존 `msg_service_list` payload를 그대로 쓰면 충분하지 않다.
기존 provider sync도 같은 수준으로 올려야 한다.

이 초안은 기존 `msg_service_list` schema를 새 snapshot schema로 갱신하는 것을 기본
계약으로 삼는다. provider row마다 원 출처 registry id와 `registration_id`를 싣고,
provider generation 안의 value/weight 변경 순서를 나타내는 `provider_update_seq`도
함께 싣는다. SPOT owner entry와 route binding row도 같은 snapshot sequence 안에
포함한다.

이 schema 변경은 하위 호환을 보장하지 않는다. 구버전 registry는 새 snapshot
schema를 파싱하지 못한다. 따라서 이 기능을 배포할 때는 registry cluster 전체를
동시에 업그레이드해야 한다. rolling upgrade 중 mixed version cluster는 지원하지
않는다.

별도 owner-bound sync 메시지를 추가하는 방식은 기본 계약이 아니다. 구현상 내부적으로
메시지를 나눌 수는 있지만, 받는 Registry는 provider와 owner-bound row를 하나의
snapshot sequence로 적용해야 한다. provider snapshot과 route snapshot이 서로 다른
sequence로 독립 수렴하면 owner identity 검증과 cleanup 순서가 깨질 수 있다.

late join은 full snapshot을 기준으로 처리해야 한다. route나 SPOT owner entry만
변경분으로 보내면 나중에 추가된 Registry가 기존 주소 매핑을 복구할 방법이 없다.

### provider materialization

Registry는 peer에서 받은 provider row를 곧바로 channel 주소 리스트로 쓰지 않는다.
먼저 원 출처와 generation을 보존한 raw view로 저장하고, 그 다음 Discovery에 보낼
materialized view를 만든다.

```text
raw provider identity:
  advertising_registry
  source_registry
  registration_id
  channel_name
  service_role
  routing_id

address row key:
  channel_name
  service_role
  routing_id

address row value:
  endpoint
  source_registry
  registration_id
  provider_update_seq
  weight
  value
```

같은 `source_registry + channel_name + service_role + routing_id`에서 더 큰
`registration_id`가 들어오면 이전 generation은 replaced 상태로 본다. 이전 generation이
소유한 owner-bound record는 정리하고, 주소 row의 endpoint는 새 generation 값으로
갱신한다.

같은 `source_registry + registration_id` 안에서 endpoint가 서로 다른 provider row가
보이면 정상적인 endpoint 이동으로 보지 않는다. provider generation 안의 endpoint는
불변이다. 원 출처 Registry가 직접 광고한 최신 row가 있으면 그 row를 사용하고, 그렇지
않으면 해당 generation을 materialized provider view에서 제외한다. 이 예외는 잘못된
peer snapshot이 auto-connect로 이어지는 것을 막기 위한 방어 규칙이다.

같은 address row key에 대해 오래된 `registration_id`를 가진 provider row가 늦게
도착하면 materialized view를 되돌리면 안 된다. peer snapshot sequence와
`registration_id`, `provider_update_seq`를 함께 확인해서 오래된 row는 무시한다.

peer snapshot 적용은 아래 순서를 따른다.

```text
apply provider snapshot from advertising registry P:
  remove raw observations where advertising_registry == P and not in snapshot
  upsert raw observations from snapshot with advertising_registry == P
  rebuild materialized provider view from live raw observations
  mark owner generations with no materialized provider as removed
  cleanup owner-bound records for removed owner generations
```

서로 다른 `source_registry`가 같은 `channel_name + service_role + routing_id`를
동시에 live provider로 광고하면 RID 충돌이다. RID는 provider의 논리 key이므로 이
상태를 두 개의 연결 후보로 풀면 안 된다. 구현은 충돌 상태를 진단 가능하게 기록하고,
충돌이 해소될 때까지 해당 RID를 auto-connect materialized view에서 제외한다.
이렇게 해야 잘못된 endpoint로 연결되는 일을 막을 수 있다.

### 기존 주소 리스트와 auto-connect 영향

기존 channel provider 목록의 목적은 그대로 유지한다. Discovery는 같은 channel에
등록된 socket provider 주소 목록을 보고, 현재 연결해야 하는 endpoint 집합을 만든다.
그 다음 이미 연결된 endpoint 집합과 비교해서 새 endpoint에는 connect하고, 목록에서
사라진 endpoint에는 disconnect한다.

owner identity를 `source_registry + registration_id`까지 올리는 것은 Registry 내부
소유권과 cleanup을 정확히 하기 위한 것이다. 이것이 곧바로 모든 socket 연결을
generation 단위로 끊고 다시 연결하라는 뜻은 아니다.

주소 리스트 관점의 동작은 다음과 같다.

1. 같은 RID의 provider가 계속 살아 있고 endpoint도 같으면 연결 상태를 유지한다.
2. provider snapshot에서 어떤 RID의 endpoint가 사라지면 Discovery는 그 endpoint를
   disconnect 대상에 넣는다.
3. snapshot에 어떤 RID의 새 endpoint가 나타나면 Discovery는 그 endpoint를 connect
   대상에 넣는다.
4. 같은 RID의 provider가 다른 endpoint로 다시 나타난 경우에는 결과적으로 이전
   endpoint는 disconnect되고 새 endpoint는 connect된다.
5. `registration_id`만 바뀌고 endpoint가 같은 경우, 주소 리스트만으로는 disconnect와
   reconnect를 강제하지 않는다. 다만 이전 generation이 소유하던 SPOT owner entry와
   route binding은 새 generation에 자동 승계하지 않는다.

따라서 이 초안은 기존 auto-connect의 "현재 주소 집합과 active 연결 집합의 차이를
맞춘다"는 동작을 유지한다. 바뀌는 부분은 Registry가 그 주소 row의 원 출처와
generation을 보존하고, owner-bound record cleanup에 그 정보를 사용한다는 점이다.

### 주소 row에서 RID와 endpoint의 의미

socket 주소 리스트에서 `routing_id`와 `endpoint`는 서로 다른 의미를 가진다.
`routing_id`는 provider의 논리 key다. `endpoint`는 그 provider의 현재 connect 대상
주소다. mesh 연결 방향 결정은 RID가 서로 다를 때 RID 순서를 사용한다. RID가 비어
있어 비교할 수 없는 legacy row에만 endpoint를 tie-breaker로 사용할 수 있다.
따라서 주소 리스트의 기본 row는
`channel_name + service_role + routing_id`로 유지한다.

Discovery는 자기 자신의 RID와 같은 provider row를 remote peer 후보로 사용하면 안 된다.
endpoint가 다르더라도 같은 RID는 같은 logical provider를 뜻하므로 자기 자신으로
연결을 시도할 수 있다. mesh 연결 방향을 계산할 때도 local RID와 remote RID가 같으면
endpoint tie-breaker를 적용하지 않고 후보에서 제외한다.

같은 `routing_id`가 같은 `endpoint`로 다시 보이면 주소 리스트 관점에서는 같은
연결 대상이다. `registration_id`가 달라졌더라도 endpoint가 그대로이면 Discovery는
그 이유만으로 disconnect와 reconnect를 강제하지 않는다. transport가 이미 끊겼다면
socket runtime이 해당 endpoint 연결을 다시 시도하면 되고, Registry의 역할은 그
endpoint가 live provider 목록에 남아 있는지 알려 주는 것이다.

같은 `routing_id`가 다른 `endpoint`로 보이는 경우는 두 가지로 해석될 수 있다.

1. 같은 socket이 다른 주소로 이동했다.
2. 같은 RID를 재사용한 새 provider가 다른 주소로 등록됐다.

Registry는 주소 리스트에서 RID를 key로 사용하므로 같은 channel과 같은 role 안에서
같은 RID의 endpoint는 하나만 유지한다. 같은 RID로 다른 endpoint가 들어오면 provider
row의 endpoint를 새 값으로 갱신한다. Discovery는 이전 snapshot의 endpoint와 새
snapshot의 endpoint 차이를 보고 old endpoint를 disconnect하고 new endpoint를 connect한다.

이 정책은 socket 주소 공유와 SPOT owner mapping을 분리하기 위해 필요하다. SPOT
owner entry와 route binding은 특정 owner generation의 객체 소유권을 나타내므로
자동 승계하지 않는다. 반면 socket 주소 row는 "현재 연결 가능한 주소"를 나타내므로
RID별 현재 endpoint를 기준으로 유지한다.

### 중복 connect 처리

Discovery auto-connect는 같은 endpoint에 반복해서 connect를 호출하는 방식에 의존하면
안 된다. 주소 리스트를 RID 기준으로 비교하고, RID별 endpoint가 바뀐 경우에만 이전
endpoint를 disconnect하고 새 endpoint를 connect한다.

Discovery가 내부적으로 유지하는 desired peer 상태도 endpoint set만으로 표현하면
안 된다. 최소한 `routing_id -> endpoint` 형태로 보관해야 한다. endpoint set만
사용하면 같은 endpoint에 다른 RID가 들어온 경우나, 같은 RID의 endpoint가 바뀐 경우를
구분하기 어렵다.

일반 transport endpoint에서는 이미 같은 endpoint가 socket runtime에 등록되어 있으면
connect 호출이 새 연결을 만들지 않고 성공으로 끝날 수 있다. 이 동작은 중복 connect를
완화해 주지만, 주소 갱신 규칙으로 사용하면 안 된다. 이미 등록된 endpoint에 다시
connect를 호출하는 것은 reconnect를 강제한다는 뜻이 아니다.

따라서 주소 리스트 적용 규칙은 아래와 같다.

1. 같은 RID와 같은 endpoint가 계속 보이면 추가 connect를 하지 않는다.
2. 같은 RID의 endpoint가 바뀌면 old endpoint를 disconnect하고 new endpoint를 connect한다.
3. RID가 새로 보이면 그 RID의 endpoint를 connect한다.
4. RID가 사라지면 마지막으로 연결했던 endpoint를 disconnect한다.

이 규칙은 transport별 중복 connect 동작 차이에 의존하지 않고, Discovery가 관리하는
주소 상태를 명확하게 유지하기 위한 것이다.

## Discovery cache 처리

Discovery core는 `resolve_route()` 결과를 TTL 기반으로 저장해서 재사용하는 cache를
제공하지 않는다. `resolve_route()`는 Registry가 가진 현재 materialized view를
확인하는 API다. 따라서 core는 이전 resolve 결과를 TTL이 남았다는 이유로 다음
`resolve_route()` 호출에 반환하면 안 된다.

route value의 의미는 core가 알 수 없다. 예를 들어 value가 actor 위치인지, shard 위치인지,
임시 세션 주소인지에 따라 허용할 수 있는 stale 시간이 다르다. 이 정책을 core가 하나로
정하면 어떤 사용자는 불필요하게 느려지고, 어떤 사용자는 오래된 주소를 받아 위험해질 수
있다. route 조회 결과를 오래 재사용해야 하는 경우에는 framework 또는 application layer가
자기 도메인에 맞는 cache, TTL, 무효화 정책을 별도로 가져야 한다.

core 구현이 허용하는 최적화는 공개 API 의미를 바꾸지 않는 범위로 제한한다.

- 같은 Discovery event-loop turn 안에서 이미 처리 중인 같은 key의 resolve 요청을 하나의
  Registry 요청으로 합칠 수 있다.
- 같은 key의 concurrent in-flight resolve 요청은 하나의 Registry reply를 공유할 수 있다.
- 이 임시 상태는 API 호출 사이에 TTL cache처럼 유지하면 안 된다.
- Registry reconnect, snapshot sequence 불연속, provider view 변경, 같은 process의
  bind/unbind 성공 시 임시 resolve 상태는 폐기해야 한다.

`resolve_spot()`도 같은 원칙을 따른다. Discovery가 이미 유지하는 topology view와
service provider view를 읽어 결과를 만들 수는 있지만, 별도의 TTL 기반 resolve 결과
cache를 두고 다음 호출에 재사용하면 안 된다.

이 규칙은 `resolve_route()`와 `resolve_spot()`의 의미를 단순하게 유지하기 위한 것이다.
조회 API는 core 기준의 현재 view를 확인하고, 장기 cache 정책은 route value의 의미를 아는
상위 계층이 결정한다.

## 내부 protocol 초안

구현은 기존 Discovery control connection을 사용해 Registry에 route 요청을 보낸다.
message id 값은 구현 시 정하되, frame 의미는 아래와 같이 고정한다.

```text
msg_route_bind:
  kind
  channel_name
  owner_service_role
  owner_routing_id
  owner_registration_id
  key
  value

msg_route_bind_reply:
  status_errno

msg_route_unbind:
  kind
  channel_name
  owner_service_role
  owner_routing_id
  owner_registration_id
  key

msg_route_unbind_reply:
  status_errno

msg_route_resolve:
  kind
  channel_name
  key

msg_route_resolve_reply:
  status_errno
  owner_routing_id
  value
```

Registry는 `msg_route_bind`와 `msg_route_unbind`에서 sender routing id만 믿지 않는다.
요청에 들어 있는 owner provider key가 live local provider인지 확인하고, 저장된
provider의 routing id를 owner identity로, endpoint를 owner address로 사용한다. `msg_route_resolve`는 owner가
아닌 Discovery도 보낼 수 있으므로 owner provider key를 요구하지 않는다.

`status_errno == 0`이면 성공이다. 실패하면 `status_errno`는 public API가 설정할
errno 값이다. resolve reply는 항상 세 frame을 보낸다. 실패 응답에서는
`owner_routing_id.size == 0`이고 `value`는 크기 0 frame이다.

Registry peer sync 메시지에는 route binding snapshot도 포함되어야 한다. peer sync의
route row는 `channel_name`, `kind`, `key`, owner identity, `value`, `updated_at_ms`를
포함한다. provider row도 `source_registry`, `registration_id`, `provider_update_seq`를
포함해야 한다. 이 정보가 없으면 받는 Registry가 owner identity를 다시 만들 수 없다.

`key`와 `value`는 문자열이 아니라 binary frame이다. 구현은 NUL 종료 여부에
의존하면 안 된다.

route binding 변경은 Registry cluster sync sequence를 증가시켜야 한다. 이 sequence는
provider, SPOT owner entry, route binding snapshot에 함께 적용된다. 기존 `_list_seq`를
확장해 사용해도 되지만, 의미는 service list sequence가 아니라 Registry snapshot
sequence로 재정의한다. peer registry는 이 sequence로 오래된 route sync가 새 route
owner를 되돌리지 못하게 해야 한다.

peer sync에서 route row를 받았지만 같은 owner identity의 provider가 아직 local view에
없을 수 있다. 이 경우 Registry는 route row를 pending 상태로 저장하되 resolve 결과로는
반환하지 않는다. 이후 같은 `source_registry + registration_id`를 가진 owner provider가
도착하면 pending route를 live 후보로 승격한다. owner provider가 끝까지 도착하지 않고
해당 peer registry가 timeout되면 pending route도 함께 제거한다.

## Registry late join 처리

Registry cluster에는 새 Registry가 나중에 추가될 수 있다. 새 Registry는 합류 시점에
기존 provider, topology, route binding 상태를 아직 모른다. 기존 Registry provider
공유는 `msg_service_list` full snapshot으로 이 문제를 일부 해결하고 있다. owner-bound
record sync도 같은 성질을 가져야 하며, 변경분만 보내는 방식에 의존하면 안 된다.

late join 시 필요한 snapshot 범위는 아래와 같다.

1. channel contract 목록
2. provider 목록과 각 provider의 `source_registry + registration_id + provider_update_seq`
3. SPOT owner topology entry와 내부 owner identity
4. route binding 목록과 owner identity
5. registry sync sequence

새 Registry가 peer PUB endpoint에 subscribe하면 기존 Registry는 XPUB subscription
frame을 보고 `msg_service_list`를 즉시 보낼 수 있고, 늦어도 다음 주기 broadcast에서
snapshot을 보낸다. owner-bound 확장은 이 snapshot 또는 같은 sequence의 별도 snapshot에
현재 live provider와 live provider에 연결된 owner-bound record를 모두 포함해야 한다.

받는 Registry는 full snapshot을 적용할 때 같은 `advertising_registry`에서 온 기존
raw observation을 snapshot 기준으로 교체한다. `source_registry`는 provider와
owner-bound record의 원 출처로 보존하며, snapshot을 보낸 peer id로 덮어쓰지 않는다.

```text
apply full snapshot advertised by registry P:
  remove provider observations where advertising_registry == P and not in snapshot
  remove owner-bound observations where advertising_registry == P and not in snapshot
  upsert provider observations from snapshot with advertising_registry == P
  upsert SPOT owner observations from snapshot with advertising_registry == P
  upsert route binding observations from snapshot with advertising_registry == P
  rebuild materialized provider, SPOT owner, and route views
  cleanup owner generations that no longer have a live provider
  promote pending routes whose owner provider is now present
```

이 순서는 새 Registry가 늦게 합류해도 기존 route와 SPOT owner 정보를 빠짐없이
얻게 하기 위한 것이다. 또한 이미 사라진 provider의 오래된 owner-bound record가
late join 과정에서 다시 살아나지 않게 한다.

full snapshot을 받는 동안 route row가 provider row보다 먼저 처리될 수 있다.
이 경우 앞에서 정의한 `pending_routes_by_owner`를 사용한다. snapshot 적용이 끝난 뒤에도
owner provider가 없는 pending route는 resolve 결과로 반환하지 않는다.

late join 후에도 provider heartbeat timeout과 peer registry timeout 규칙은 동일하게
적용된다. 합류한 Registry가 특정 peer registry를 더 이상 보지 못하면 그 peer가
광고하던 raw observation을 제거한다. 그 결과 어떤 owner generation도 live provider로
materialize되지 않으면 해당 owner-bound record를 함께 제거한다.

## 공개 API 영향

이 초안은 SPOT RID별 TTL 설정 API를 추가하지 않는다.
`zlink_discovery_resolve_spot()` 시그니처도 유지한다.

Discovery metadata API는 route binding API로 대체한다. 정식 반영 시점에는 공개
헤더, 바인딩, errno 문서에서 metadata 이름을 제거하고 route binding 이름으로
정리한다.

기존 `zlink_discovery_set_value()`와 `zlink_discovery_get_value()`는 service provider
attribute로 남긴다. 이 초안은 value API를 route binding으로 대체하지 않는다.

## 테스트 기준

구현 뒤에는 아래 동작을 테스트해야 한다.

1. SpotNode가 정상 등록되고 SPOT RID owner 조회가 성공한다.
2. SpotNode provider heartbeat가 timeout되면 해당 owner identity의 SPOT owner entry가
   `READY` 조회 대상에서 빠진다.
3. timeout 뒤 `zlink_discovery_resolve_spot()`은 해당 SPOT RID에 대해 `ENOENT`로
   실패한다.
4. 같은 channel에 다른 SpotNode가 살아 있으면 그 node의 SPOT RID entry는
   삭제되지 않는다.
5. 같은 node RID가 새 generation으로 재등록된 경우 이전 node의 stale SPOT owner
   entry가 새 node의 live entry를 지우지 않는다.
6. route bind 뒤 같은 channel Discovery에서 resolve가 성공한다.
7. route bind 뒤 다른 `channel_name`을 가진 Discovery에서 같은 `kind + key` resolve가 실패한다.
8. 같은 channel에서 새 owner가 같은 `kind + key`를 bind하면 resolve 결과가 새
   owner로 바뀐다.
9. 이전 owner의 unbind는 새 owner의 route를 삭제하지 않는다.
10. owner provider가 heartbeat timeout 또는 unregister로 제거되면 route도 함께
    제거된다.
11. `kind == ZLINK_ROUTE_KIND_INVALID`는 실패한다.
12. `key == NULL`이거나 `key_size == 0`이면 실패한다.
13. `value == NULL && value_size == 0`인 route도 허용한다.
14. owner를 추론할 수 없는 Discovery에서 bind하면 실패한다.
15. 같은 RID의 provider endpoint가 바뀌면 old endpoint는 disconnect되고 new endpoint는
    connect된다.
16. 같은 RID와 같은 endpoint에서 `registration_id`만 바뀌면 socket 주소 리스트만으로
    reconnect를 강제하지 않는다.
17. local RID와 같은 remote RID는 endpoint가 달라도 auto-connect 후보에서 제외된다.
18. 서로 다른 `source_registry`가 같은 channel, role, RID를 동시에 광고하면 해당 RID는
    materialized provider view에서 제외된다.
19. peer timeout은 `source_registry`가 아니라 `advertising_registry` 기준 raw
    observation만 제거한다.
20. owner provider보다 route row가 먼저 도착하면 pending 상태로 보관하고, 같은
    owner identity의 provider가 도착한 뒤 resolve 가능해진다.
21. 같은 `source_registry + registration_id` provider generation에 서로 다른 endpoint
    observation이 들어오면 원 출처 Registry의 직접 row가 없는 한 materialized provider
    view에서 제외된다.
22. route binding 대량 record 회귀 테스트를 둔다. 기본 CI에서는 작은 key/value로
    100K record 이상을 등록, resolve, owner cleanup까지 검증한다.
23. 확장 회귀 테스트에서는 1M record 이상을 대상으로 등록, random exact resolve,
    owner별 bulk cleanup, snapshot chunking을 검증한다.
24. 성능 또는 수동 회귀 테스트에서는 dense arena/hash storage 기준 10M record 이상을
    대상으로 memory budget, chunked snapshot, cleanup latency를 측정한다.
25. dense mode 테스트는 hash 충돌, tombstone 증가, arena dead byte 증가, rehash 실패,
    memory budget 초과를 각각 검증한다.
26. incremental rehash 테스트는 rehash 중 bind, unbind, resolve가 모두 정확히 동작하고
    event loop가 긴 시간 멈추지 않는지 검증한다.
27. batch cleanup 테스트는 owner가 cleanup_pending이 된 직후 resolve가 실패하고,
    남은 entry가 여러 tick에 걸쳐 제거되는지 검증한다.
28. peer timeout 테스트는 N개 전체 route 중 M개만 영향을 받는 peer가 timeout될 때
    제거된 record 수가 M에 비례하는지 진단 counter로 검증한다. N이 클 때 처리 시간이
    M과 독립적으로 증가하면 안 된다.
29. route winner 재계산 테스트는 단일 route identity에 observation K개가 있을 때
    winner 재계산 비용이 K에 비례하는지 진단 counter로 검증한다. 전체 route 수 N과
    독립적이어야 한다.
30. owner cleanup 테스트는 owner 하나가 R개 route를 소유할 때 cleanup 비용이 R에
    비례하는지 진단 counter로 검증한다. 전체 route 수 N과 독립적이어야 한다.
31. SpotNode cleanup 테스트는 owner 하나가 T개 topology entry를 소유할 때 cleanup
    비용이 T에 비례하는지 진단 counter로 검증한다. 전체 topology map 크기와
    독립적이어야 한다.
32. chunked snapshot 테스트는 chunk 누락, 순서 오류, staging memory budget 초과,
    마지막 chunk 전 staging view 격리를 검증한다.
33. 대량 record 테스트는 scan이나 listing에 의존하지 않고 exact lookup과 owner cleanup
    비용이 기대 범위에 있는지 확인한다.
34. memory budget을 초과하는 bind는 `ENOSPC` 또는 `E2BIG` 계열 오류로 실패하고,
    부분 저장이 남지 않는지 검증한다.

## 정식 spec 반영 조건

이 초안이 구현된 뒤에는 다음 문서와 계약을 함께 정리한다.

- `core/include/zlink.h`의 Discovery와 Registry API
- `doc/spec/core/service/discovery.ko.md`
- `doc/spec/core/service/registry.ko.md`
- `doc/spec/core/service/spot.ko.md`
- 관련 errno 문서와 바인딩 문서
- Framework route resolver draft가 이 API를 기본 route store로 참조하는지 여부

정식 spec에는 구현된 동작만 반영한다. 구현 전에 기존 정식 spec 문서에 이 내용을
섞어 쓰지 않는다.
