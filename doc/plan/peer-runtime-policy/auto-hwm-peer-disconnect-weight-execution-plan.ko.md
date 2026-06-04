# Peer Runtime Policy 3단계 개발 실행 계획

> 작성 기준: 2026-04-24
> 입력 초안:
> - `doc/draft/peer-disconnect-rid.ko.md`
> - `doc/draft/peer-weight.ko.md`
> - `doc/draft/auto-hwm.ko.md`
>
> 목표:
> 세 초안의 요구 항목을 사람의 판단 대기 없이 끝까지 구현하도록,
> 기능별 개발, 전체 테스트, POSD 리팩토링, perf smoke, 문서 반영, 커밋과 푸시,
> 최종 재검토 루프를 하나의 실행 절차로 고정한다.

## TODO

- [x] 단계 0. 실행 전 기준선 고정
- [x] 단계 1. Peer Disconnect by Routing ID 구현, 검증, 문서 반영, 커밋과 푸시
- [x] 단계 2. Peer Weight 구현, 검증, 문서 반영, 커밋과 푸시
- [x] 단계 3. 자동 HWM 정책 구현, 검증, 문서 반영, 커밋과 푸시
- [x] 단계 4. 세 기능 통합 리뷰와 누락 항목 소거
- [x] 단계 5. 최종 전체 검증, 최종 커밋과 푸시

## 1. 개발 순서

순서는 아래처럼 고정한다.

1. `peer-disconnect-rid`
2. `peer-weight`
3. `auto-hwm`

이 순서를 쓰는 이유는 아래와 같다.

- `peer-disconnect-rid`는 peer identity, rid 중복 정책, lifecycle 소유권,
  callback 안 종료 요청, SpotNode peer disconnect 기준을 먼저 정한다.
- `peer-weight`는 peer별 상태 전파, admission 제거, DEALER outbound 선택,
  service/Spot snapshot 변경을 다룬다. peer identity와 lifecycle 기준이 먼저
  안정되어 있어야 변경 범위가 작다.
- `auto-hwm`은 context 예산, 소켓 역할 묶음, 생성 시점 계산, monitoring 진단을
  다룬다. 앞 단계에서 peer lifecycle과 service 상태 표면이 정리된 뒤 적용해야
  연결 수와 후보 상태를 잘못 세지 않는다.

다음 기능으로 넘어가는 조건은 엄격하다. 현재 기능의 구현, 전체 테스트, POSD
리팩토링, perf smoke, `doc/` 문서 반영, 커밋과 푸시가 끝나지 않으면 다음 기능을
시작하지 않는다.

## 2. 공통 실행 원칙

### 2.1 사람 개입 없는 진행

실행자는 아래 규칙을 따른다.

- 초안에 확정안이 있는 항목은 질문하지 않고 그대로 구현한다.
- 코드와 초안이 충돌하면 `core/include/zlink.h`에 최종 반영될 공개 계약을 기준으로
  테스트를 먼저 세우고 구현은 그 계약을 만족하게 맞춘다.
- 초안에 열린 위험이 있으면 구현 중 코드로 확인하고, 둘 이상의 방법이 가능할 때는
  변경 파급이 가장 작은 방법을 고른다.
- 일시적 테스트 실패는 같은 단계 안에서 원인을 찾아 수정한다.
- flaky 의심 실패는 같은 build 상태에서 단독 재실행해 확인한다. 재실행이 통과해도
  원인과 재실행 결과를 해당 단계 기록에 남긴다.
- 외부 인증이 필요한 `git push`는 원격 인증이 준비되어 있다는 전제로 진행한다.
  인증이나 네트워크 문제로 push가 실패하면 코드 완료로 보지 않고, 실패 로그와
  재시도 결과를 남긴 뒤 push가 성공할 때까지 해당 단계 종료를 보류한다.
- 원격 branch가 앞서 있어서 push가 거부되면 destructive 명령을 쓰지 않는다.
  `git fetch`, non-destructive rebase 또는 merge로 원격 변경을 반영하고, 충돌이
  없으면 해당 단계의 build, test, perf smoke를 다시 실행한 뒤 push한다.
  충돌이 나면 충돌 파일과 이유를 로그에 남기고 기존 사용자 변경을 되돌리지 않는
  방향으로 해결한다.

### 2.2 기존 변경 보호

실행 전 `git status --short`를 저장한다. 이미 존재하는 변경은 사용자 또는 다른
작업이 만든 변경으로 본다.

- 관련 없는 기존 변경은 되돌리지 않는다.
- 같은 파일을 수정해야 하면 먼저 diff를 읽고 현재 변경 위에 이어서 작업한다.
- 기능별 커밋에는 해당 기능 구현과 필요한 문서만 포함한다.
- staging 전 `git diff --cached --name-only`로 의도하지 않은 파일이 섞이지 않았는지
  확인한다.

### 2.3 단계별 상태 기록

각 기능 단계는 아래 내용을 `doc/plan/peer-runtime-policy/logs/` 아래에 남긴다.

- 시작 시각과 기준 commit
- 읽은 초안 section 목록
- 구현 체크리스트와 완료 여부
- 실행한 test command와 결과
- POSD 리뷰에서 발견한 구조 문제와 처리 결과
- perf smoke command와 report 파일
- 갱신한 `doc/` 문서 목록
- 커밋 hash와 push 결과

로그는 실행 보조 문서다. 정식 spec, guide, internals 문서를 대신하지 않는다.

## 3. 공통 검증 게이트

각 기능은 아래 게이트를 순서대로 통과해야 한다.

### 3.1 빌드와 전체 테스트

기능 구현이 끝나면 먼저 core runtime을 새로 만든다.

```bash
cmake --build core/build -j"$(nproc)"
```

그 다음 현재 저장소가 제공하는 전체 core test lane을 실행한다.

```bash
ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
ctest --test-dir core/build --output-on-failure -L e2e -j1
ctest --test-dir core/build --output-on-failure -L regression -j1
```

label 구성이 바뀌었거나 위 lane으로 모든 테스트가 잡히지 않으면 아래 명령으로
test surface를 확인하고 누락된 lane을 추가한다.

```bash
ctest --test-dir core/build -N
```

public C API, C++ sample, binding contract가 바뀐 단계에서는 해당 binding test도
함께 실행한다. 각 언어별 명령은 해당 binding의 README와 기존 plan 문서의 명령을
우선한다. 테스트 명령을 찾을 수 없으면 `doc/spec/bindings/README.md`의 capability
matrix와 각 binding 디렉토리의 build/test entry를 확인해 실행 가능한 test를
추가한다.

### 3.2 POSD 리팩토링

전체 테스트가 통과한 뒤, 기능 변경 위에서 POSD 관점의 구조 리뷰를 수행한다.

리뷰 질문은 아래와 같다.

- 새 public API 진입점이 transport, socket, service 세부를 너무 많이 알고 있지
  않은가?
- socket hot path가 policy 계산, 문서용 진단, test hook을 직접 떠안지 않는가?
- shared state가 커져서 lifecycle, routing, monitoring, option 책임이 한 구조체에
  몰리지 않았는가?
- send/recv hot path에 매번 할당, 정렬, 전체 peer 순회가 들어가지 않았는가?
- 같은 검증 또는 mapping 코드가 C API, service API, binding API에 복제되지 않았는가?
- 테스트가 제품 구조보다 더 복잡해져 유지보수 비용을 키우지 않는가?

POSD 리팩토링은 기능 의미를 바꾸지 않는다. 구조를 고친 뒤에는 3.1의 빌드와 전체
테스트를 다시 실행한다.

### 3.3 `bindings/c/perf` smoke

이 문서에서 말하는 perf smoke는 `AGENTS.md`의 Benchmark Build Rules에 따른
`bindings/c/perf` 실행을 뜻한다. 성능 수치의 절대값을 승인 기준으로 삼는 full
benchmark가 아니라, 변경 뒤 benchmark runner와 대표 성능 경로가 정상 실행되는지
확인하는 짧은 검증이다.

perf smoke 전에는 반드시 `core/build` runtime을 다시 빌드한다.

```bash
cmake --build core/build -j"$(nproc)"
```

규칙은 아래와 같다.

- `bindings/c/perf/run_benchmarks_multi.sh`의 core runtime 기준 경로는 기본값으로
  `core/build`다.
- `build_cpp_release`나 다른 임시 빌드 디렉토리 결과로 `bindings/c/perf` 수치를
  판단하지 않는다.
- `core/src/` 또는 `core/include/`를 바꾼 뒤에는 `cmake --build core/build`로 실제
  runtime을 먼저 다시 만든다.
- perf 실행 전에 runner가 출력하는 실제 `libzlink.so` 경로가 `core/build` 아래인지
  확인한다.
- perf 실행 전에 `core/build` runtime이 source보다 오래되면 그 실행은 실패로 보고
  rebuild 뒤 다시 실행한다.
- 아래 명령은 `bindings/c/perf` runner가 현재 지원하는 option만 사용한다. runner에
  없는 `warmup` 성격의 option을 임의로 넣지 않는다.

공통 smoke는 아래를 기본으로 한다.

```bash
./bindings/c/perf/run_benchmarks.sh \
  --pattern PAIR \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --duration 1

./bindings/c/perf/run_benchmarks_multi.sh \
  --pattern SPOT_REQREP \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --clients 2 \
  --duration 1

./bindings/c/perf/run_benchmarks_multi.sh \
  --pattern SPOT_SENDSEND \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --clients 2 \
  --duration 1
```

기능별로 영향을 받는 경로는 아래 추가 smoke를 실행한다.

```bash
# peer-disconnect-rid
./bindings/c/perf/run_benchmarks.sh \
  --pattern DEALER_ROUTER,ROUTER_ROUTER \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --duration 1

./bindings/c/perf/run_benchmarks_multi.sh \
  --pattern STREAM,SPOT_REQREP \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --clients 2 \
  --duration 1

# peer-weight
./bindings/c/perf/run_benchmarks.sh \
  --pattern DEALER_DEALER,DEALER_ROUTER \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --duration 1

./bindings/c/perf/run_benchmarks_multi.sh \
  --pattern DEALER_DEALER,DEALER_ROUTER,SPOT_REQREP,SPOT_SENDSEND \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --clients 2 \
  --duration 1

# auto-hwm
./bindings/c/perf/run_benchmarks.sh \
  --pattern PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --duration 1

./bindings/c/perf/run_benchmarks_multi.sh \
  --pattern PUBSUB,STREAM,SPOT_REQREP,SPOT_SENDSEND \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --clients 2 \
  --duration 1
```

perf smoke가 실패하면 다음 기능으로 넘어가지 않는다. 기능 자체가 성능 측정 경로를
바꾸지 않았더라도 smoke 실패 원인을 확인하고 같은 단계에서 수정한다.

### 3.4 문서 반영

각 기능 구현과 검증이 끝나면 해당 초안의 문서 반영 계획을 정식 문서에 반영한다.

문서 위치 원칙은 아래와 같다.

- `doc/spec/`: `core/include/zlink.h`와 테스트로 확정된 공개 API 계약만 쓴다.
- `doc/guide/`: 사용자가 왜, 언제, 어떻게 쓰는지 설명한다. 내부 socket 배선이나
  inproc endpoint 같은 구현 세부는 넣지 않는다.
- `doc/internals/`: 유지보수자가 구조를 이해하도록 data flow, lifecycle,
  socket 역할, 진단 경로를 설명한다.
- `doc/spec/bindings/`: 각 언어의 public surface, 이름, 에러 매핑, 옵션 범위를
  C core 계약과 맞춘다.
- `doc/site/docs/`: 배포용 복사본이 있는 문서는 같은 의미로 동기화한다.

문서 반영 뒤에는 아래를 확인한다.

```bash
rg -n -f /tmp/zlink-doc-forbidden-terms.txt doc
rg -n "admission" doc/spec doc/guide doc/internals doc/site/docs
```

`/tmp/zlink-doc-forbidden-terms.txt`에는 root `AGENTS.md`의 금지 표현 목록을
줄 단위로 넣는다. 금지어 자체를 계획 문서 본문에 반복해서 적지 않는다.

`admission` 검색은 `peer-weight` 단계에서 필수다. 다른 단계에서는 기존 문맥상
남아도 되는 항목인지 확인하고 로그에 남긴다.

### 3.5 커밋과 푸시

각 기능 단계가 모든 게이트를 통과하면 커밋한다.

권장 커밋 단위는 아래와 같다.

- `plan: add peer runtime policy execution plan`
- `core: add peer disconnect by routing id`
- `docs: document peer disconnect by routing id`
- `core: add peer weight routing policy`
- `docs: document peer weight policy`
- `core: add automatic hwm policy`
- `docs: document automatic hwm policy`
- `chore: complete peer runtime policy final review`

기능 구현과 문서 반영이 강하게 묶인 경우에는 하나의 기능 커밋으로 합칠 수 있다.
다만 다음 기능으로 넘어가기 전에는 해당 기능의 커밋이 원격에 push되어 있어야 한다.

```bash
git status --short
git diff --cached --name-only
git commit -m "<message>"
git push
```

현재 branch에 upstream이 없으면 아래처럼 한 번만 설정한다.

```bash
git push -u origin HEAD
```

## 4. 단계 0. 실행 전 기준선 고정

### 할 일

- `git status --short`로 기존 변경을 기록한다.
- 세 draft 문서를 다시 읽고, 구현 확정안과 문서 반영 계획 section을 단계 로그에
  옮긴다.
- `core/include/zlink.h`, `core/include/zlink_enum.h`, `core/include/zlink_errno.h`의
  현재 public surface를 확인한다.
- `core/tests/README.md`의 최신 test lane을 확인한다.
- `bindings/c/perf/README.md`와 root README의 `core/build` runtime 기준을 확인한다.
- 기준선 build와 test를 실행한다.
- 기준선 perf smoke를 실행한다.

### 완료 기준

- 시작 전 실패가 있으면 기능 작업 전에 원인을 분류하고 먼저 복구한다.
- 기존 실패가 외부 환경 문제로 확인되어 즉시 복구할 수 없으면 실패 명령,
  실패 원인, 재시도 결과를 로그에 남긴다. 제품 코드나 테스트 실패가 남아 있으면
  다음 기능 단계로 넘어가지 않는다.
- plan 문서 자체를 별도 커밋으로 올리고 push한다.

## 5. 단계 1. Peer Disconnect by Routing ID

### 구현 항목

`doc/draft/peer-disconnect-rid.ko.md`의 모든 구현 체크리스트를 만족한다.

필수 구현 항목은 아래와 같다.

- `zlink_disconnect_rid()` public C API 추가
- `zlink_spot_node_disconnect_peer_rid()` public C API 추가
- `ZLINK_CONNECT_NOT_FOUND = 605`
- `ZLINK_CONNECT_CONFLICT = 606`
- `ZLINK_CONNECT_BUSY = 607`
- `ENOENT`, `EADDRINUSE`, `EBUSY` result mapping
- `ZLINK_OPT_RID_DUPLICATE_POLICY`
- `ZLINK_RID_DUPLICATE_REJECT`
- `ZLINK_RID_DUPLICATE_HANDOVER`
- 기존 router 전용 handover option 제거 또는 공통 option으로 흡수
- Discovery attached socket의 manual rid disconnect 차단
- socket 공통 helper 또는 안전한 socket별 helper
- `ROUTER`와 `STREAM` 최적화 경로
- `STREAM` 4바이트 rid 검증
- callback 안 rid disconnect nonblocking 경로
- SpotNode `node_rid -> endpoint set` 기준 disconnect
- C++/Python/Node/Go/Rust/Java/.NET binding API와 에러 매핑

### 테스트 항목

초안의 테스트 기준을 빠짐없이 반영한다.

- 빈 rid, null handle, 잘못된 handle, unsupported handle
- 대상 없음 `ENOENT`
- 중복 rid `EADDRINUSE`
- Discovery attached 상태 `EBUSY`
- `PAIR`, `DEALER`, `ROUTER`, `PUB`, `SUB`, `XPUB`, `XSUB`, `STREAM`
- `ROUTER` route map 기반 disconnect
- `STREAM` callback 안 disconnect deadlock 없음
- callback에서 받은 rid 포인터를 저장하지 않는지 검증
- duplicate policy `REJECT`, `HANDOVER`
- 일반 socket에서 peer rid를 알 수 없는 transport 제약
- endpoint disconnect 기존 동작 회귀
- SpotNode rid 기반 peer disconnect
- Spot facade에 별도 rid disconnect 함수가 없는 계약

### POSD 리뷰 초점

- `socket_base` 공통 helper가 source rid 비교 기준만 숨기고, socket별 특수 정책을
  과하게 끌어안지 않는지 본다.
- attached pipe snapshot이 raw pointer 수명 위험을 만들면 별도 lookup 또는 lifetime
  guard로 구조를 바꾼다.
- callback 재진입에서 lock 재획득이나 blocking wait가 생기지 않게 API 진입 경계를
  분리한다.
- SpotNode는 core socket helper를 억지로 재사용하지 말고, node rid 인덱스와 기존
  endpoint disconnect 경로 재사용이 더 깊은 모듈이면 그 구조를 택한다.

### 문서 반영

아래 문서를 구현 결과와 맞춘다.

- `doc/guide/03-0-socket-patterns.ko.md`와 대응 영문 문서
- `doc/guide/03-4-router.ko.md`와 대응 영문 문서
- `doc/guide/03-5-stream.ko.md`와 대응 영문 문서
- `doc/guide/07-3-spot.ko.md`와 대응 영문 문서
- `doc/guide/12-socket-options.ko.md`와 대응 영문 문서
- `doc/internals/stream-socket.ko.md`와 대응 영문 문서
- `doc/internals/spot-internals.ko.md`와 대응 영문 문서
- 새 `doc/internals/peer-disconnect-rid.ko.md`
- `doc/README.ko.md` internals 목록
- `doc/spec/core/socket/README.ko.md`와 대응 영문 문서
- 각 socket spec
- `doc/spec/core/service/spot.ko.md`와 대응 영문 문서
- errno 또는 connect result 문서
- `doc/spec/bindings/` 전체 언어 문서
- 관련 `doc/site/docs/` 문서

### 단계 종료 조건

- 기능 구현 완료
- 전체 테스트 통과
- POSD 리팩토링 뒤 전체 테스트 재통과
- 기능별 perf smoke 통과
- 문서 반영 완료
- 초안 checklist와 정식 문서 diff 리뷰 완료
- 커밋과 push 완료

## 6. 단계 2. Peer Weight

### 구현 항목

`doc/draft/peer-weight.ko.md`의 모든 구현 체크리스트를 만족한다.

필수 구현 항목은 아래와 같다.

- `ZLINK_ROUTER_OPT_WEIGHT = 0x3106`
- `ZLINK_DEALER_OPT_WEIGHT = 0x3203`
- `ZLINK_SPOT_NODE_OPT_WEIGHT = 0x360C`
- `ZLINK_SPOT_OPT_WEIGHT = 0x3702`
- weight 값 형식 `int`, 범위 `0..100`, 기본값 `100`
- router/dealer/spot-node/spot typed option mapping
- admission public API, enum, monitor event 제거
- admission snapshot field를 `weight`로 변경
- ZMP peer command를 admission에서 weight로 교체
- pipe attach와 runtime weight 변경 시 command 전송
- command decode 실패 시 application payload로 넘기지 않고 무시
- `DEALER -> ROUTER`, `DEALER -> DEALER` outbound weighted 선택
- positive weight가 모두 같으면 기존 round-robin fast path 유지
- 서로 다른 positive weight에서만 weighted schedule 사용
- weight `0`은 후보 제외
- weight `0 -> positive`는 writable 상태 확인 뒤 후보 복귀
- multipart atomicity 유지
- service discovery, registry, Spot peer cache에 weight 전파
- C++/Python/Node/Go/Rust/Java/.NET binding option facade 반영
- sender routing policy option은 추가하지 않음

### 테스트 항목

초안의 기능, 회귀, 성능 회귀 테스트를 모두 반영한다.

- `DEALER -> ROUTER` weight `100:50` 비율
- `DEALER -> DEALER` weight `100:50` 비율
- 기본값 `100`에서 기존 round-robin 분배
- 모든 positive weight가 같은 경우 기존 round-robin 분배
- weight `0` 후보 제외
- weight `0 -> 100` 후보 복귀
- runtime weight 변경 뒤 새 비율 적용
- multipart가 한 peer로만 전달
- 잘못된 weight 설정 실패
- SpotNode weight `0`의 service peer 후보 제외
- Spot weight 변경의 peer cache와 routed 후보 반영
- `zlink_spot_node_peer_entry_t.weight`
- `zlink_member_peer_entry_t.weight`
- admission API와 enum이 public surface에 남지 않는 contract test
- 기존 admission 기반 draining 테스트를 weight `0` 기반 테스트로 대체
- sender routing policy option이 공개 enum에 없는지 contract test

### POSD 리뷰 초점

- `lb_t`가 remote weight, active 후보, schedule dirty state를 한 곳에서 관리하되
  send hot path에 전체 peer 순회가 들어가지 않게 한다.
- weighted schedule 재구성은 상태 변화 시점에만 수행한다.
- one-pipe fast path와 equal-weight round-robin path를 유지한다.
- admission 제거로 생긴 migration 코드를 compatibility layer로 남겨 복잡도를
  늘리지 않는다.
- service/Spot snapshot 변경은 이름만 바꾸지 말고, weight 전파 책임이 어디에 있는지
  코드 구조로 드러나게 한다.

### 문서 반영

아래 문서를 구현 결과와 맞춘다.

- `doc/spec/core/socket/dealer.ko.md`와 대응 영문 문서
- `doc/spec/core/socket/router.ko.md`와 대응 영문 문서
- `doc/spec/core/socket/README.ko.md`와 대응 영문 문서
- `doc/spec/core/service/spot.ko.md`와 대응 영문 문서
- `doc/spec/core/service/discovery.ko.md`와 대응 영문 문서
- `doc/spec/core/service/registry.ko.md`와 대응 영문 문서
- `doc/spec/README.ko.md`와 대응 영문 문서
- `doc/guide/03-3-dealer.ko.md`와 대응 영문 문서
- `doc/guide/03-4-router.ko.md`와 대응 영문 문서
- `doc/guide/03-0-socket-patterns.ko.md`와 대응 영문 문서
- `doc/guide/07-0-services.ko.md`와 대응 영문 문서
- `doc/guide/07-3-spot.ko.md`와 대응 영문 문서
- `doc/guide/12-socket-options.ko.md`와 대응 영문 문서
- `doc/internals/socket-option-defaults.ko.md`와 대응 영문 문서
- `doc/internals/architecture.md`와 한국어 문서
- `doc/internals/multipart-atomicity.ko.md`
- `doc/internals/services-internals.md`와 한국어 문서
- `doc/internals/spot-internals.md`와 한국어 문서
- `doc/spec/bindings/` 전체 언어 문서
- 관련 `doc/site/docs/` 문서

### 단계 종료 조건

- 기능 구현 완료
- 전체 테스트 통과
- POSD 리팩토링 뒤 전체 테스트 재통과
- 기능별 perf smoke 통과
- 문서 반영 완료
- `admission` 잔여 검색 리뷰 완료
- 초안 checklist와 정식 문서 diff 리뷰 완료
- 커밋과 push 완료

## 7. 단계 3. 자동 HWM 정책

### 구현 항목

`doc/draft/auto-hwm.ko.md`의 구현 확정안을 만족한다.

필수 구현 항목은 아래와 같다.

- `ZLINK_CTX_OPT_AUTO_HWM_ENABLE`
- `ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB`
- 사용자가 budget을 주지 않으면 `128MB` 고정 기본값
- process memory limit 자동 조회는 첫 구현에서 제외
- `queue_budget 60%`, `transport_budget 30%`, `runtime_reserve 10%`
- `effective_message_bytes = 1280`
- 내부 시작값 `recent_ewma_message_bytes = 1024`,
  `recent_p95_message_bytes = 1024`, `overhead_factor = 1.25`
- 역할 묶음 `control`, `routed`, `fanout`, `recv_ingress`
- 역할 묶음 비율 `control 5% / routed 25% / fanout 50% / recv_ingress 20%`
- 본문 5.4 절 기준 `floor_function()` 시작값
- 생성 시점 1회 계산
- runtime 재계산은 후속 단계로 분리
- 수동 `SNDHWM`, `RCVHWM`, `SNDBUF`, `RCVBUF` 설정 우선
- 일반 소켓용 자동 HWM 계산
- 일반 소켓용 자동 `SNDBUF` / `RCVBUF` 계산
- `spot` / `spotnode` 내부 소켓을 역할 묶음에 매핑
- `spot` idle runtime 판정은 첫 구현에서 제외
- HWM 재계산 결과는 기존 pipe와 새 pipe에 적용
- `SNDBUF` / `RCVBUF`는 새 transport 연결부터 적용
- monitoring snapshot으로 budget 분배와 requested/effective buffer 진단 노출
- `STREAM` transport buffer 자동 계산값은 perf 참고 계산 결과로 검증

### 테스트 항목

자동 HWM 테스트는 계산식과 적용 경계를 모두 검증한다.

- context option 기본값
- auto enable on/off
- budget MB 입력 검증
- 수동 HWM override 우선
- 수동 transport buffer override 우선
- role group budget 분배
- floor 값 적용
- managed connection 수 구간별 HWM 계산
- 평균 메시지 크기 가정으로 예상 HWM 개수 계산
- 일반 socket 생성 시점 적용
- 새 pipe와 기존 pipe HWM 적용 규칙
- `SNDBUF` / `RCVBUF` 새 연결 적용 규칙
- `spot` / `spotnode` 내부 socket 역할 매핑
- monitoring snapshot 진단 값
- `STREAM` 기본값 회귀
- 기존 수동 설정 테스트 회귀

### POSD 리뷰 초점

- 계산식은 socket hot path에서 떨어진 별도 policy 모듈로 둔다.
- context option, role mapping, monitoring projection이 서로 직접 내부 상태를
  들여다보지 않게 한다.
- `spot` / `spotnode` 내부 소켓 매핑은 service runtime 코드 곳곳에 흩뿌리지 않고
  한 지점에서 설명 가능하게 둔다.
- HWM 계산과 transport buffer 계산은 같은 budget 입력을 쓰되, 결과 적용 시점의
  차이를 코드 경계로 드러낸다.
- runtime 재계산 후속 확장을 막는 임시 전역 상태나 test-only hook을 만들지 않는다.

### 문서 반영

아래 문서를 구현 결과와 맞춘다.

- `doc/guide/10-performance.ko.md`와 대응 영문 문서
- `doc/guide/12-socket-options.ko.md`와 대응 영문 문서
- `doc/guide/03-5-stream.ko.md`와 대응 영문 문서
- `doc/internals/socket-option-defaults.ko.md`와 대응 영문 문서
- `doc/internals/services-internals.ko.md`와 대응 영문 문서
- `doc/internals/spot-internals.ko.md`와 대응 영문 문서
- `doc/internals/stream-socket.ko.md`와 대응 영문 문서가 있으면 함께 반영
- `doc/spec/core/context.ko.md`와 대응 영문 문서
- `doc/spec/core/monitoring.ko.md`와 대응 영문 문서
- `doc/spec/core/socket/stream.ko.md`와 대응 영문 문서
- `doc/spec/core/socket/router.ko.md`와 대응 영문 문서
- `doc/spec/core/socket/dealer.ko.md`와 대응 영문 문서
- `doc/spec/core/socket/pub.ko.md`와 대응 영문 문서
- `doc/spec/core/socket/sub.ko.md`와 대응 영문 문서
- `doc/spec/core/service/spot.ko.md`와 대응 영문 문서
- 필요한 errno 또는 설정 오류 문서
- `doc/spec/bindings/` 전체 언어 문서
- 관련 `doc/site/docs/` 문서

### 단계 종료 조건

- 기능 구현 완료
- 전체 테스트 통과
- POSD 리팩토링 뒤 전체 테스트 재통과
- 기능별 perf smoke 통과
- 문서 반영 완료
- 초안 checklist와 정식 문서 diff 리뷰 완료
- 커밋과 push 완료

## 8. 단계 4. 통합 리뷰와 누락 항목 소거

세 기능 구현이 모두 끝나면 초안별로 요구 항목을 다시 대조한다.

### 리뷰 방법

- 각 draft의 구현 checklist, 테스트 기준, 문서 반영 계획 section을 다시 읽는다.
- public header와 정식 spec 문서가 서로 맞는지 확인한다.
- guide에 내부 구현 설명이 들어가지 않았는지 확인한다.
- internals에 사용자용 사용법이 길게 들어가지 않았는지 확인한다.
- binding 문서의 이름, 에러 매핑, option 범위가 C core와 맞는지 확인한다.
- `doc/site/docs/` 복사본이 정식 문서와 같은 뜻인지 확인한다.
- `rg`로 삭제되어야 할 public surface가 남았는지 확인한다.
- `git diff --name-only`로 각 단계에서 의도한 범위를 벗어난 수정이 없는지 확인한다.

### 필수 검색

```bash
rg -n "zlink_set_admission_state|zlink_get_admission_state|ZLINK_ADMISSION_|PEER_ADMISSION" core bindings doc
rg -n "ZLINK_DEALER_OPT_ROUTING_POLICY" core bindings doc
rg -n "ZLINK_ROUTER_OPT_HANDOVER" core bindings doc
rg -n -f /tmp/zlink-doc-forbidden-terms.txt doc
```

남아 있는 항목이 있으면 아래 중 하나로 처리한다.

- 실제 public surface 잔여물이면 제거한다.
- migration note나 historical bug 문서처럼 남아야 하는 항목이면 이유를 로그에 남긴다.
- draft 문서 자체의 과거 표현이면 구현 완료 뒤 draft 보관 정책에 따라 유지 여부를
  따로 판단한다.

## 9. 단계 5. 최종 전체 검증과 종료 루프

최종 종료 전에는 전체 build, 전체 test, perf smoke, 문서 리뷰를 다시 수행한다.

```bash
cmake --build core/build -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
ctest --test-dir core/build --output-on-failure -L e2e -j1
ctest --test-dir core/build --output-on-failure -L regression -j1
./bindings/c/perf/run_benchmarks.sh --pattern PAIR --transports tcp --msg-sizes 64 --runs 1 --duration 1
./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_REQREP --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1
./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_SENDSEND --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1
```

위 공통 smoke 뒤에는 3.3 절의 기능별 추가 smoke 명령도 모두 다시 실행한다. 마지막
검증은 "현재 기능만 통과"가 아니라 세 기능이 함께 들어간 상태에서
`peer-disconnect-rid`, `peer-weight`, `auto-hwm` 관련 `bindings/c/perf` 대표 경로가
모두 정상인지 확인하는 단계다.

binding implementation을 건드린 언어는 각 언어별 전체 test를 다시 실행한다.

perf smoke 결과는 runner exit code와 report 파일을 함께 본다. exit code가 0이어도
report에 실패 case가 남아 있으면 통과로 보지 않는다. report 파일 경로와 실패 수는
단계 로그에 기록한다.

종료 루프는 아래 조건이 모두 참일 때만 끝난다.

- 세 draft의 필수 구현 항목이 모두 코드에 반영됨
- 세 draft의 테스트 요구가 자동 테스트로 반영됨
- 전체 테스트가 통과함
- POSD 리뷰에서 남은 구조 문제가 없음
- perf smoke가 모두 통과함
- `doc/` 정식 문서가 구현된 public header와 맞음
- guide, spec, internals의 역할 구분을 어기지 않음
- binding 문서와 binding surface가 맞음
- site 문서가 필요한 범위에서 동기화됨
- 금지 표현 검색이 깨끗함
- 미적용 항목과 오적용 항목이 없음
- 모든 단계 커밋이 push됨

하나라도 거짓이면 종료하지 않는다. 해당 항목을 수정한 뒤 단계 5 검증을 다시
실행한다.

## 10. 완료 정의

이 계획은 아래 상태에서 완료된다.

- `peer-disconnect-rid`, `peer-weight`, `auto-hwm` 세 기능이 순서대로 구현되었다.
- 각 기능은 다음 기능으로 넘어가기 전에 테스트, POSD 리팩토링, perf smoke, 문서
  반영, 커밋과 푸시를 끝냈다.
- 최종 리뷰에서 초안 대비 미적용 또는 오적용 항목이 없다.
- 최종 검증 결과와 commit hash, push 결과가 `doc/plan/peer-runtime-policy/logs/`
  아래에 남아 있다.
