# .NET public boundary POSD 재설계 초안

## 1. 목적과 결정

이 문서는 RouteMesh 11.0의 .NET 공개 표면을 앱 개발자, provider 개발자, Framework 내부 구현으로
분리하기 위한 설계 초안이다. 기존 정확 인터페이스 명세를 즉시 대체하지 않으며, 승인 뒤 각 명세와
구현·계약 snapshot을 함께 갱신한다.

결정은 다음과 같다.

- 애플리케이션 public API에는 authority, lease, fence, descriptor key, raw payload, relocation phase 같은
  구현 요소를 노출하지 않는다.
- provider SPI는 별도 opt-in package와 namespace에 두고, relocation 도메인 동작이 아니라 저장소의
  최소 일관성 보장만 계약으로 둔다.
- relocation, object creation, capacity allocation, Redis script와 monitoring의 내부 상태 머신은
  Framework가 소유하며 `internal`이다.
- 운영 API는 사람이 읽을 수 있는 상태·요약·안전한 제어 결과만 공개한다. 내부 식별자와 protocol frame은
  diagnostic sink에 직접 전달하지 않는다.

## 2. 공개 대상별 카테고리

| 카테고리 | 소비자 | package/namespace | 공개 범위 | 금지하는 노출 |
|---|---|---|---|---|
| Application API | Actor·Spot 앱 개발자 | 기본 Framework package | 등록, 사용, 제한된 readiness·runtime 상태 | Store, authority, owner lease, capacity, raw payload |
| Operations API | 호스트 운영·관측 개발자 | 기본 Framework package의 Operations 영역 | 상태 요약, health, lifecycle 요청, 안정된 event | descriptor key, routing fence, 저장소 version, protocol frame |
| Provider SPI | 저장소 provider 작성자 | 별도 Provider.Abstractions opt-in package | 최소 저장소 primitive와 명시된 일관성 계약 | Actor·Spot·relocation phase·aggregate·creation 도메인 타입 |
| Provider implementation | Redis 등 공식 구현 | 별도 provider package | options와 등록 extension | provider 내부 script·record·key layout |
| Framework internals | Framework 구현 | internal | relocation coordinator, authority projection, capacity·retry·recovery | public declaration·앱 자동완성 |

## 3. Application API

앱 개발자는 Actor와 Spot을 등록·호출하고, 서비스가 준비되었는지와 런타임이 정상인지 확인할 수 있다.
객체 배치, owner 이동, relocation 복구는 Framework의 책임이며 앱 API는 이를 조작하거나 관찰하기 위한
내부 token을 제공하지 않는다.

유지 가능한 공개 형태는 다음 범주로 제한한다.

- `ZLinkLocationOptions`: 사용자가 선택하는 기능 설정만 제공한다. Store implementation, descriptor,
  capacity나 retry 상태를 public property로 넣지 않는다.
- `IZLinkLocationReadiness`: 서비스 준비 여부와 사용 가능한 사유 코드만 반환한다.
- `IZLinkLocationRuntimeQuery`: paging 가능한 서비스 요약과 고수준 topology 상태만 반환한다.
- Actor·Spot의 기존 public lifecycle API: 성공, 취소, 공개 오류 종류만 노출한다.

`ZLinkAuthorityKey`, `ZLinkLocationOwnerToken`, `ZLinkMeshNodeDescriptorKey`, `ZLinkCapacityVector`,
`ZLinkRelocationCapacityFence`, `ZLinkAuthoritySnapshot`과 relocation·creation 결과 union은 앱 API에서
제거한다.

## 4. Operations API

운영 API는 진단에 유용하되 구현 세부를 재노출하지 않는 read model이어야 한다.

- runtime state, readiness, 연결 수, 서비스별 health, lifecycle 결과와 안정된 오류 분류를 제공한다.
- topology snapshot은 사람이 이해할 역할·상태·수량을 제공한다. Store version, owner lease generation,
  descriptor key, raw routing ID와 protocol envelope는 제공하지 않는다.
- 이벤트에는 correlation 가능한 운영용 ID와 공개된 event kind만 포함한다. 메시지 본문, raw socket metadata,
  내부 fence·payload는 logging/diagnostic implementation 내부에만 남긴다.
- observer와 error sink는 애플리케이션 정책을 위한 고수준 event만 받는다. dispatch internals와 message-flow
  record를 public callback 모델로 고정하지 않는다.

## 5. Provider SPI: 최소 저장소 능력

provider는 relocation을 구현하지 않는다. Framework가 private record를 만들고 상태 전이를 조합한다.
provider는 다음 primitive와 각 보장만 구현한다.

| 능력 | 의미 | 필수 보장 |
|---|---|---|
| Read | 불투명한 key의 현재 immutable value 읽기 | value·version·provider clock을 같은 관측으로 반환 |
| Compare-and-swap | expected version일 때 value 저장 또는 삭제 | 단일 key 원자성, conflict의 현재 관측, 성공의 새 version |
| Atomic batch | 제한된 key 집합의 조건부 변경 | 전부 성공 또는 전부 실패, 어떤 중간 상태도 관측 불가 |
| Lease/TTL | value 또는 별도 lease의 만료·갱신 | provider clock 기준, renew/release의 idempotency와 stale 판정 |
| Immutable blob | 큰 불투명 byte payload의 put/read/renew/delete | content identity, checksum 검증, TTL·orphan cleanup 의미 |
| Scan (선택) | recovery·maintenance용 bounded scan | cursor 안정성 범위와 scan expiry를 명시 |

SPI의 key, value, version, batch condition, lease token, blob reference는 기술적·불투명 타입이다.
`Actor`, `Spot`, `Authority`, `Reservation`, `Aggregate`, `Relocation`, `Creation`은 SPI 이름·메서드·결과
타입에 포함하지 않는다. provider가 더 강한 transaction을 지원해도 Framework의 primitive 구현에만 사용한다.

## 6. Framework 내부 소유

다음은 provider SPI에서 제거하고 Framework internal로 옮긴다.

- authority payload projection, object creation reservation과 terminal publication
- capacity accounting, relocation capacity fence와 aggregate prepare/commit/abort
- relocation root, phase, replay, recovery, idempotency와 retry 판정
- descriptor·owner lease의 의미 해석과 topology admission
- Redis key layout, Lua script, change stamp와 provider-specific serialization
- raw message flow, dispatch diagnostics, socket metadata와 internal event

Framework는 provider primitive로 위 상태를 저장한다. 필요할 때 atomic batch를 사용하지만, batch의 public
계약은 domain transition이 아니라 기술적 precondition과 mutation 집합이다.

## 7. 기존 문서별 이관 계획

| 기존 명세 | 새 역할 | 처리 |
|---|---|---|
| `08-authority-relocation` | provider 도메인 SPI | provider primitive 명세와 internal relocation spec으로 분리; 현재 public authority·reservation·aggregate 타입 제거 |
| `08-location-maintenance` | 앱 API·provider SPI·운영 query 혼재 | options/readiness/query만 앱·운영 문서에 유지; descriptor·lease·capacity·Store는 각각 internal 또는 provider abstraction으로 이동 |
| `08-location-provider-redis` | 공식 provider API | 등록 extension과 최소 options만 공개; Store class, script·stamp·내부 record는 구현 detail로 이동 |
| `10-monitoring-errors` | 오류와 event 혼재 | 공개 오류 분류와 안전한 운영 event만 유지; socket/message-flow·Spot timer 상세는 internal diagnostics로 이동 |
| `10-topology-monitoring` | 운영 snapshot과 런타임 internals 혼재 | 사람 중심 summary·health·lifecycle만 Operations API로 유지; peer/claim/fence·dispatch observer는 internal로 이동 |

## 8. 검증 기준

- 기본 Framework package의 public declaration snapshot에 provider·authority·relocation·raw diagnostics 타입이 없다.
- Provider.Abstractions package만 provider primitive를 노출하며 Actor·Spot·relocation 도메인 타입 참조가 없다.
- Redis package의 소비자는 options와 등록 extension만으로 설치할 수 있고, 내부 Store 구현을 직접 생성할 필요가 없다.
- 애플리케이션·운영 예제의 public API 탐색에서 owner, fence, version, descriptor key, raw payload가 보이지 않는다.
- Framework relocation regression, provider contract test, Redis recovery test가 새 SPI 위에서 동일한 safety 결과를 보인다.

## 9. 실행 순서

1. 이 초안을 검토·승인하고 execution ledger에 수정 진행 항목과 영향 행을 기록한다.
2. package와 namespace 경계, 공개/내부 타입 분류, provider primitive의 exact contract를 확정한다.
3. 기존 다섯 명세를 새 역할별 문서로 교체하고 public declaration snapshot을 갱신한다.
4. Framework 내부 coordinator를 primitive 기반으로 이관하고 Redis provider를 맞춘다.
5. 앱 API·provider SPI·운영 API 계약 테스트, relocation regression과 독립 POSD review를 다시 실행한다.

이 문서가 승인되기 전에는 기존 다섯 명세의 완료 증거나 기존 public surface를 새 설계의 완료 증거로 사용하지 않는다.
