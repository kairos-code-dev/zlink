# Discovery owner-bound route 구현 실행 계획

> 상태: 구현 전 실행 계획
> 기준 draft: [`draft`](../../spec/draft/discovery-owner-bound-routes.ko.md)
> 대상 범위: `core/`, `bindings/`, `framework/`, `samples/`, `doc/`, `doc/site/`
> 최종 종료 판정: `미적용/미반영 항목이 없습니다. POSD 리팩토링 후보가 없습니다. 전체 테스트와 perf smoke가 모두 통과했습니다.`

## 1. 목적

이 문서는 Discovery owner-bound route 설계를 사용자 개입 없이 끝까지 구현하기 위한
실행 계획이다. 설계 내용은 기준 draft를 단일 기준으로 삼는다. 이 문서는 draft 내용을
반복해서 풀어 쓰지 않고, 구현 순서와 통과 조건만 정의한다.

구현자는 단계 중간에 판단이 필요하면 먼저 기준 draft를 읽고 결정한다. draft와 코드가
충돌하면 draft, 구현, 테스트, 문서를 같은 변경 묶음 안에서 맞춘다. 코드만 맞고 문서가
남아 있거나, 문서만 맞고 테스트가 없으면 완료로 보지 않는다.

## 2. 기준 링크

구현 범위는 아래 draft 섹션을 따른다.

- [목적](../../spec/draft/discovery-owner-bound-routes.ko.md#목적)
- [설계 원칙](../../spec/draft/discovery-owner-bound-routes.ko.md#설계-원칙)
- [구현 적용 범위](../../spec/draft/discovery-owner-bound-routes.ko.md#구현-적용-범위)
- [권장 구현 방향](../../spec/draft/discovery-owner-bound-routes.ko.md#권장-구현-방향)
- [성능 범위와 비목표](../../spec/draft/discovery-owner-bound-routes.ko.md#성능-범위와-비목표)
- [고성능 구현 상세](../../spec/draft/discovery-owner-bound-routes.ko.md#고성능-구현-상세)
- [공통 owner 모델](../../spec/draft/discovery-owner-bound-routes.ko.md#공통-owner-모델)
- SPOT RID owner cleanup:
  [draft](../../spec/draft/discovery-owner-bound-routes.ko.md#spot-rid-owner-cleanup)
- [Route binding](../../spec/draft/discovery-owner-bound-routes.ko.md#route-binding)
- [공통 cleanup 계약](../../spec/draft/discovery-owner-bound-routes.ko.md#공통-cleanup-계약)
- 기존 Registry sync 동작과 확장 지점:
  [draft](../../spec/draft/discovery-owner-bound-routes.ko.md#기존-registry-sync-동작과-확장-지점)
- [Discovery cache 처리](../../spec/draft/discovery-owner-bound-routes.ko.md#discovery-cache-처리)
- [내부 protocol 초안](../../spec/draft/discovery-owner-bound-routes.ko.md#내부-protocol-초안)
- [Registry late join 처리](../../spec/draft/discovery-owner-bound-routes.ko.md#registry-late-join-처리)
- [공개 API 영향](../../spec/draft/discovery-owner-bound-routes.ko.md#공개-api-영향)
- [테스트 기준](../../spec/draft/discovery-owner-bound-routes.ko.md#테스트-기준)
- [정식 spec 반영 조건](../../spec/draft/discovery-owner-bound-routes.ko.md#정식-spec-반영-조건)

설계 판단 기준은 [`software-design-principles.md`](../../principal/software-design-principles.md)를
따른다.

## 3. 실행 원칙

- 사용자에게 추가 판단을 요구하지 않는다.
- 호환성 유지는 목표가 아니다.
- 기존 사용자 변경은 되돌리지 않는다.
- 실패한 테스트, 빌드, perf smoke는 우회하지 않는다.
- 실패 원인을 수정하고 같은 gate를 다시 실행한다.
- runner 경로나 옵션이 바뀌었으면 `--help`, README, 빌드 metadata를 확인해 같은 의미의
  명령으로 갱신하고, 이 계획 문서도 함께 고친다.
- 각 단계는 자체 리뷰를 통과해야 다음 단계로 간다.
- `미적용/미반영 항목 0개`가 되기 전에는 다음 단계로 넘어가지 않는다.
- POSD 리뷰는 core와 bindings 모두에서 반복한다.
- POSD 리팩토링 후보가 남아 있으면 다음 단계로 넘어가지 않는다.
- 정식 문서 반영, native runtime 갱신, binding library 갱신 뒤 전체 테스트와
  perf smoke가 모두 성공해야 완료로 본다.

환경 문제도 가능한 한 저장소 안에서 해결한다. 필수 toolchain이 없고 저장소 안의
대체 runner도 없을 때만 blocker로 기록한다. blocker 기록은 최종 종료가 아니며, 환경이
복구되면 같은 단계부터 계속한다.

## 4. 공통 반복 루프

모든 단계는 아래 루프를 사용한다.

1. 기준 draft 링크를 다시 읽는다.
2. 현재 코드, 테스트, 문서에 반영된 항목을 적는다.
3. 미적용/미반영 항목을 고친다.
4. 해당 단계의 테스트나 검증 명령을 실행한다.
5. 실패하면 원인을 수정하고 1번으로 돌아간다.
6. 미적용/미반영 항목이 없고 검증이 통과하면 다음 단계로 간다.

리뷰 결과는 단계별 로그에 남긴다. 기본 위치는
`doc/plan/discovery/logs/discovery-owner-bound-routes/`다.

## 5. Stage 0. 기준선 수집

목표는 구현 전에 실제 실행 가능한 명령과 영향 범위를 확정하는 것이다.

작업:

- `git status --short`로 기존 변경을 기록한다.
- `core/include/zlink.h`와 관련 C++ 구현 파일의 현재 계약을 읽는다.
- Discovery, Registry, SPOT, auto-connect, topology, peer sync, metadata API 위치를
  찾는다.
- bindings 언어별 공개 surface와 native runtime 위치를 찾는다.
- `bindings/c/perf/README.md`와 single/multi runner 옵션을 확인한다.
- core, bindings, framework, samples 테스트 실행 명령을 현재 저장소 기준으로 기록한다.

통과 조건:

- 구현 대상 파일과 테스트 파일 후보가 정리되어 있다.
- 실행할 build/test/perf 명령 목록이 있다.
- 기준 draft에서 구현할 섹션 링크가 모두 확인되어 있다.

## 6. Stage 1. Core 계약과 프로토콜 구현

기준 링크:

- [Route binding](../../spec/draft/discovery-owner-bound-routes.ko.md#route-binding)
- [kind 값](../../spec/draft/discovery-owner-bound-routes.ko.md#kind-값)
- [공개 API 변경 요약](../../spec/draft/discovery-owner-bound-routes.ko.md#공개-api-변경-요약)
- [bind 계약](../../spec/draft/discovery-owner-bound-routes.ko.md#bind-계약)
- [unbind 계약](../../spec/draft/discovery-owner-bound-routes.ko.md#unbind-계약)
- [resolve 계약](../../spec/draft/discovery-owner-bound-routes.ko.md#resolve-계약)
- [공개 API 영향](../../spec/draft/discovery-owner-bound-routes.ko.md#공개-api-영향)
- [내부 protocol 초안](../../spec/draft/discovery-owner-bound-routes.ko.md#내부-protocol-초안)
- [공통 owner 모델](../../spec/draft/discovery-owner-bound-routes.ko.md#공통-owner-모델)
- [owner generation](../../spec/draft/discovery-owner-bound-routes.ko.md#owner-generation)
- [registration token 전달](../../spec/draft/discovery-owner-bound-routes.ko.md#registration-token-전달)

작업:

- `core/include/zlink.h`에 route binding 공개 API와 타입을 반영한다.
- 제거 대상 metadata API를 공개 surface에서 정리한다.
- errno, 인자 검증, ownership, `zlink_msg_t` 반환 규칙을 맞춘다.
- Discovery control protocol에 bind, unbind, resolve message를 추가한다.
- provider register ack에 `registration_id` 전달 경로를 추가한다.
- owner-bound 요청이 owner registration token을 검증하게 만든다.

통과 조건:

- 공개 헤더와 internal protocol이 기준 draft와 일치한다.
- 지연된 이전 generation 요청이 거부된다.
- 기존 metadata public surface 잔재가 없다.
- core 빌드가 성공한다.

## 7. Stage 2. Registry provider sync와 owner-bound store 구현

기준 링크:

- [권장 구현 방향](../../spec/draft/discovery-owner-bound-routes.ko.md#권장-구현-방향)
- 기존 Registry sync 동작과 확장 지점:
  [draft](../../spec/draft/discovery-owner-bound-routes.ko.md#기존-registry-sync-동작과-확장-지점)
- provider materialization:
  [draft](../../spec/draft/discovery-owner-bound-routes.ko.md#provider-materialization)
- [Registry late join 처리](../../spec/draft/discovery-owner-bound-routes.ko.md#registry-late-join-처리)
- [고성능 구현 상세](../../spec/draft/discovery-owner-bound-routes.ko.md#고성능-구현-상세)
- [Registry 저장 모델](../../spec/draft/discovery-owner-bound-routes.ko.md#registry-저장-모델)
- [내부 자료구조](../../spec/draft/discovery-owner-bound-routes.ko.md#내부-자료구조)
- [충돌 처리](../../spec/draft/discovery-owner-bound-routes.ko.md#충돌-처리)
- [route value 제한](../../spec/draft/discovery-owner-bound-routes.ko.md#route-value-제한)

작업:

- provider raw observation과 materialized provider view를 분리한다.
- `source_registry`, `advertising_registry`, `registration_id`,
  `provider_update_seq`를 보존한다.
- RID 충돌, 같은 generation endpoint 충돌, stale snapshot 처리를 구현한다.
- route raw observation, materialized route entry, route winner 재계산을 구현한다.
- owner별 raw observation index, route identity별 observation index를 구현한다.
- peer snapshot chunking, staging raw view, memory budget 처리를 구현한다.
- peer timeout이 advertiser별 observation handle만 순회하게 만든다.
- late join full snapshot이 provider, SPOT owner, route binding을 함께 수렴하게 만든다.

통과 조건:

- 전체 raw table scan 없이 route winner 재계산이 가능하다.
- 전체 raw table scan 없이 owner cleanup이 가능하다.
- peer timeout은 `advertising_registry` 기준으로만 raw observation을 제거한다.
- staging snapshot 실패가 기존 materialized view를 깨지 않는다.
- 대량 모드 자료구조가 memory budget을 지킨다.

## 8. Stage 3. SPOT owner cleanup과 Discovery auto-connect 반영

기준 링크:

- SPOT RID owner cleanup:
  [draft](../../spec/draft/discovery-owner-bound-routes.ko.md#spot-rid-owner-cleanup)
- [정리 대상](../../spec/draft/discovery-owner-bound-routes.ko.md#정리-대상)
- [Registry owner 선택](../../spec/draft/discovery-owner-bound-routes.ko.md#registry-owner-선택)
- [상태 처리](../../spec/draft/discovery-owner-bound-routes.ko.md#상태-처리)
- [endpoint 변경과 재사용](../../spec/draft/discovery-owner-bound-routes.ko.md#endpoint-변경과-재사용)
- [공통 cleanup 계약](../../spec/draft/discovery-owner-bound-routes.ko.md#공통-cleanup-계약)
- [Discovery cache 처리](../../spec/draft/discovery-owner-bound-routes.ko.md#discovery-cache-처리)
- 기존 주소 리스트와 auto-connect 영향:
  [draft](../../spec/draft/discovery-owner-bound-routes.ko.md#기존-주소-리스트와-auto-connect-영향)
- 주소 row에서 RID와 endpoint의 의미:
  [draft](../../spec/draft/discovery-owner-bound-routes.ko.md#주소-row에서-rid와-endpoint의-의미)
- [중복 connect 처리](../../spec/draft/discovery-owner-bound-routes.ko.md#중복-connect-처리)

작업:

- SPOT owner topology row에 내부 owner identity를 저장한다.
- SpotNode provider 제거 시 해당 owner의 SPOT entry를 `LOST`로 전이시킨다.
- grace 뒤 삭제와 resolve 제외 규칙을 구현한다.
- `resolve_spot()`이 live owner provider를 확인하게 만든다.
- Discovery auto-connect를 `routing_id -> endpoint` diff 기준으로 정리한다.
- local RID와 같은 remote RID를 endpoint와 관계없이 제외한다.
- route와 SPOT resolve cache 무효화 규칙을 구현한다.

통과 조건:

- node provider timeout 시 해당 owner의 SPOT entry가 resolve에서 즉시 제외된다.
- 같은 RID 새 generation이 이전 generation의 SPOT entry를 승계하지 않는다.
- 같은 RID endpoint 변경은 old disconnect, new connect로 수렴한다.
- 같은 RID와 같은 endpoint에서 registration만 바뀌면 reconnect를 강제하지 않는다.

## 9. Stage 4. Core 테스트와 미적용 리뷰

기준 링크:

- [테스트 기준](../../spec/draft/discovery-owner-bound-routes.ko.md#테스트-기준)

작업:

- 기준 draft의 테스트 항목을 체크리스트로 만든다.
- core unit/integration 테스트를 추가한다.
- 대량 route binding 테스트를 추가한다.
- dense mode, rehash, batch cleanup, snapshot chunking 테스트를 추가한다.
- 미적용 항목 리뷰를 반복한다.

통과 조건:

- 기준 draft 테스트 기준이 모두 테스트에 연결되어 있다.
- 미적용/미반영 항목이 0개다.
- core build와 core test가 모두 통과한다.

기본 명령 후보:

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

명령이 실패하면 runner 위치와 README를 확인해 같은 의미의 명령으로 보정한다.

## 10. Stage 5. Core POSD 리팩토링 반복

기준 링크:

- [`software-design-principles.md`](../../principal/software-design-principles.md)

작업:

- core 전체 변경 범위를 대상으로 POSD 위험 신호를 찾는다.
- 얕은 모듈, 정보 누출, 패스스루, 시간적 분해, 중복 지식을 기록한다.
- 각 항목마다 두 가지 이상 해결안을 검토한다.
- 더 깊은 모듈이 되는 방향으로 리팩토링한다.
- 리팩토링 뒤 같은 리뷰를 다시 한다.

통과 조건:

- 의미 있는 POSD 리팩토링 후보가 0개다.
- 공개 API가 불필요하게 복잡해지지 않았다.
- core build와 core test가 다시 통과한다.

## 11. Stage 6. Core perf smoke

목표는 core 변경이 `bindings/c/perf`의 single/multi 패턴을 깨지 않았는지 확인하는
것이다. 이 단계는 문서 갱신이나 바인딩 배포 준비 전에 통과해야 한다.

작업:

- `core/build` runtime을 최신으로 빌드한다.
- `bindings/c/perf` runner가 출력하는 실제 `libzlink.so` 경로를 확인한다.
- single 패턴 smoke를 실행한다.
- multi 패턴 smoke를 실행한다.
- 실패한 패턴은 코드나 perf runner 문제를 수정한 뒤 전체 smoke를 다시 실행한다.

기본 명령 후보:

```bash
cmake --build core/build
./bindings/c/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --reuse-build
PERF_MULTI_CLIENTS=1 \
PERF_MULTI_DURATION_SECONDS=1 \
PERF_MSG_SIZES=64 \
  ./bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --reuse-build
```

통과 조건:

- single의 모든 지원 패턴이 성공한다.
- multi의 모든 지원 패턴이 성공한다.
- runner가 오래된 `core/build` runtime을 사용하지 않는다.

## 12. Stage 7. Bindings API 반영

기준 링크:

- [Route binding](../../spec/draft/discovery-owner-bound-routes.ko.md#route-binding)
- [공개 API 변경 요약](../../spec/draft/discovery-owner-bound-routes.ko.md#공개-api-변경-요약)
- [공개 API 영향](../../spec/draft/discovery-owner-bound-routes.ko.md#공개-api-영향)

작업:

- C binding surface를 core public API와 맞춘다.
- C++, .NET, Go, Java, Node, Python, Rust binding에 route binding API를 반영한다.
- 제거된 metadata API 의존 코드를 정리한다.
- binding별 오류 매핑, ownership, message 반환 규칙을 core 계약과 맞춘다.
- framework와 samples에서 기존 metadata 기반 route 저장소를 route binding으로 바꾼다.

통과 조건:

- 각 binding이 새 API를 노출한다.
- 제거된 metadata API 의존이 남아 있지 않다.
- language-specific 테스트가 추가되어 있다.
- framework와 samples가 새 route binding 경로로 동작한다.

## 13. Stage 8. Bindings POSD 리팩토링 반복

이 단계는 binding 라이브러리 최종 검증 전에 반드시 수행한다.

작업:

- 각 binding별 public wrapper, native bridge, error mapping, message ownership 코드를
  POSD 기준으로 리뷰한다.
- 호출자가 알아야 할 native 세부가 노출된 부분을 줄인다.
- shallow wrapper가 의미 없는 pass-through로만 남아 있으면 정리한다.
- binding별로 두 가지 이상 대안을 검토하고 더 단순한 public surface를 선택한다.
- 같은 리뷰를 반복한다.

통과 조건:

- binding별 POSD 리팩토링 후보가 0개다.
- core 계약과 binding 계약 사이에 중복 지식이 최소화되어 있다.
- binding별 테스트가 다시 통과한다.

## 14. Stage 9. 문서 반영 전 구현 완료 검증

이 단계는 core와 bindings 구현, core POSD 리뷰, bindings POSD 리뷰가 모두 끝난 뒤
진행한다. 문서를 정식으로 반영하기 전에 구현과 테스트가 먼저 닫혀 있는지 확인한다.

기준 링크:

- [테스트 기준](../../spec/draft/discovery-owner-bound-routes.ko.md#테스트-기준)

작업:

- core test를 다시 실행한다.
- 모든 binding test를 실행한다.
- framework test와 sample smoke를 실행한다.
- [Stage 6](#11-stage-6-core-perf-smoke)의 single perf smoke 명령을 다시 실행한다.
- [Stage 6](#11-stage-6-core-perf-smoke)의 multi perf smoke 명령을 다시 실행한다.
- 실패한 항목이 있으면 구현 단계나 POSD 단계로 돌아가 수정한다.

통과 조건:

- 모든 core, binding, framework 테스트가 성공한다.
- sample smoke가 성공한다.
- `bindings/c/perf` single의 모든 지원 패턴이 성공한다.
- `bindings/c/perf` multi의 모든 지원 패턴이 성공한다.
- 미적용/미반영 항목이 0개다.

## 15. Stage 10. 정식 문서 반영

이 단계는 문서 반영 전 구현 완료 검증이 통과한 뒤 진행한다. 문서 반영 중 구현 누락이
발견되면 해당 구현 단계로 돌아간다.

기준 링크:

- [정식 spec 반영 조건](../../spec/draft/discovery-owner-bound-routes.ko.md#정식-spec-반영-조건)

작업:

- `doc/spec/core/service/discovery.ko.md`
- `doc/spec/core/service/discovery.md`
- `doc/spec/core/service/registry.ko.md`
- `doc/spec/core/service/registry.md`
- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- `doc/spec/core/errno-map.ko.md`
- `doc/spec/core/errno-map.md`
- `doc/internals/`
- `doc/guide/`
- `doc/site/docs/`
- `doc/site/site/`
- `doc/spec/bindings/c/README.md`
- `doc/spec/bindings/cpp/README.md`
- `doc/spec/bindings/dotnet/README.md`
- `doc/spec/bindings/go/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`
- `doc/spec/bindings/rust/README.md`

정식 spec에는 구현된 계약만 넣는다. guide에는 사용법과 의도를 넣고 내부 구조를 넣지
않는다. internals에는 유지보수자가 구현을 이해하는 데 필요한 구조와 데이터 흐름을
넣는다.

통과 조건:

- draft의 구현 완료 내용이 정식 spec, guide, internals, binding spec에 빠짐없이
  반영되어 있다.
- draft와 정식 spec이 충돌하지 않는다.
- 공개 헤더와 정식 spec이 일치한다.
- 문서 검토 중 발견된 미구현 항목이 0개다.

## 16. Stage 11. Native runtime 최신화와 binding 라이브러리 갱신

작업:

- `core/build`를 최신으로 빌드한다.
- 각 binding의 `native/` 또는 runtime resource 폴더에 최신 core runtime을 반영한다.
- Linux x86_64 외 플랫폼 파일은 현재 저장소 정책을 따른다.
- binding library build 산출물을 새 native runtime과 맞춘다.
- native sync 뒤 binding tests를 다시 실행한다.

대표 native 위치:

- `bindings/cpp/native/`
- `bindings/dotnet/native/`
- `bindings/dotnet/runtimes/*/native/`
- `bindings/go/native/`
- `bindings/java/native/`
- `bindings/java/src/main/resources/native/`
- `bindings/node/native/`
- `bindings/python/src/zlink/native/`
- `bindings/rust/native/`

통과 조건:

- binding native runtime이 최신 core build와 맞다.
- binding build가 성공한다.
- binding tests가 모두 통과한다.

## 17. Stage 12. 최종 전체 테스트와 binding perf

작업:

- core test를 다시 실행한다.
- 모든 binding test를 실행한다.
- framework test와 sample smoke를 실행한다.
- [Stage 6](#11-stage-6-core-perf-smoke)의 `bindings/c/perf` single/multi smoke를
  다시 실행한다.
- binding별 perf runner가 있으면 smoke 수준으로 실행한다.

통과 조건:

- 모든 테스트가 성공한다.
- sample smoke가 성공한다.
- `bindings/c/perf` single/multi 패턴 smoke가 성공한다.
- binding별 perf smoke가 실패 없이 끝난다.

## 18. Stage 13. 최종 미적용 리뷰

작업:

- 기준 draft의 모든 섹션을 다시 읽는다.
- core 코드, binding 코드, tests, perf, docs, native runtime, binding library에
  반영된 항목을 대조한다.
- 미적용/미반영 항목이 있으면 해당 단계로 돌아가 수정한다.
- POSD 리뷰를 core와 bindings 모두 다시 수행한다.
- 모든 build/test/perf smoke를 다시 실행한다.

통과 조건:

- 기준 draft의 모든 항목이 구현, 테스트, 문서 중 필요한 위치에 반영되어 있다.
- 미적용/미반영 항목이 0개다.
- core POSD 리팩토링 후보가 0개다.
- bindings POSD 리팩토링 후보가 0개다.
- 전체 테스트가 통과한다.
- `bindings/c/perf` single/multi smoke가 통과한다.
- binding별 perf smoke가 통과한다.

## 19. 최종 종료 문구

모든 gate가 통과했을 때만 아래 문구로 종료한다.

```text
미적용/미반영 항목이 없습니다.
POSD 리팩토링 후보가 없습니다.
전체 테스트와 perf smoke가 모두 통과했습니다.
정식 spec, guide, internals, bindings spec 반영이 완료되었습니다.
binding native runtime과 binding library 갱신이 완료되었습니다.
```

위 문구 중 하나라도 사실이 아니면 종료하지 않는다. 해당 단계로 돌아가 수정하고,
다시 리뷰와 검증을 반복한다.
