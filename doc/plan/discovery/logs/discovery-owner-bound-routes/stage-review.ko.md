# Discovery Owner-Bound Routes Stage Review

기준 draft: [`discovery-owner-bound-routes.ko.md`](../../../spec/draft/discovery-owner-bound-routes.ko.md)

## Stage 0. 기준선 수집

- 반영: 변경 전 `git status --short`, core Discovery/Registry/SPOT/auto-connect 경로,
  바인딩별 공개 surface와 native runtime 위치, `bindings/c/perf` runner 옵션을 확인했다.
- 미반영: 0개.

## Stage 1. Core 계약과 프로토콜 구현

- 반영: `zlink_route_kind_t`, route key/value 제한, bind/unbind/resolve API,
  route control message, register ack의 `source_registry`와 `registration_id` 전달을
  공개 header와 내부 protocol에 반영했다.
- 반영: Discovery metadata public API와 member peer metadata public API를 core surface에서
  제거했다.
- 미반영: 0개.

## Stage 2. Registry provider sync와 owner-bound store 구현

- 반영: provider row는 `source_registry`, `registration_id`,
  `provider_update_seq`를 보존한다.
- 반영: provider materialized projection은 같은 `channel + role + routing_id`에 서로
  다른 source generation 또는 서로 다른 endpoint observation이 있으면 해당 RID를
  제외한다.
- 반영: route binding store는 raw observation store와 materialized winner table을
  분리한다. owner, route identity, advertising registry reverse index를 두어 winner
  재계산과 cleanup이 영향을 받는 record 수에 비례하게 했다.
- 반영: materialized winner table은 Redis `dict` source에서 확인한 검증된 구조를 Zlink
  내부 구현으로 적용했다. 2의 거듭제곱 bucket, mask 기반 bucket 선택, separate chaining,
  점진적 rehash를 사용해 큰 route set의 resize 비용을 조회와 갱신 경로에 나누어 처리한다.
- 반영: materialized table entry는 fixed scalar node와 packed byte arena로 나누어 저장한다.
  route key/value를 entry마다 `std::string`/`std::vector`로 들지 않고 byte block에 붙이며,
  channel 이름은 intern해서 같은 channel의 10M route가 같은 문자열을 반복 저장하지 않는다.
- 반영: materialized table node는 65,536개 단위 chunk로 할당한다. 단일 `std::vector`
  growth가 10M insert에서 16M capacity까지 커지는 낭비를 제거해 Redis entry allocation과
  비슷하게 필요한 entry 수에 비례하도록 바꿨다.
- 반영: materialized table owner identity는 intern id로 공유하고, bucket/node link는
  32비트 entry id를 사용한다. Redis가 pointer와 entry 책임을 작게 유지하는 방향을 Zlink
  route table 규모에 맞게 적용했다.
- 반영: route key와 owner identity hash는 SipHash 계열 방식으로 계산한다.
- 반영: route snapshot 전송은 Redis `dictScan` 방식처럼 cursor로 materialized table을
  chunk 단위로 읽는다. chunk 복사 중에는 rehash pause guard를 잡아 snapshot 순회가 rehash
  cursor와 섞이지 않게 한다.
- 반영: Registry peer route snapshot은 별도 `REGISTRY_SYNC` 메시지로 전파하고,
  `advertising_registry` 기준으로 기존 route observation을 교체한다.
- 반영: route snapshot은 `snapshot_seq`, `chunk_index`, `chunk_count`를 가진 chunk
  단위로 전송한다. 수신 측은 staging view에 순서대로 적용하고 마지막 chunk에서 commit한다.
- 반영: late join peer Registry는 provider snapshot과 route snapshot을 받아 route resolve가
  수렴한다.
- 반영: route snapshot을 한 번도 광고하지 않은 Registry는 빈 `REGISTRY_SYNC`를 보내지
  않으며, route가 생긴 뒤 삭제된 경우에는 빈 snapshot을 한 번 보내 peer state를 정리한다.
- 반영: 기본 route store memory budget, owner별 observation 제한, materialized route 수
  제한, snapshot staging budget을 두고 초과 시 신규 저장을 실패시킨다.
- 미반영: 0개.

## Stage 3. SPOT owner cleanup과 Discovery auto-connect 반영

- 반영: SPOT owner topology row에 내부 owner identity를 저장하고 owner provider 제거 시
  `LOST`로 전이한다.
- 반영: SPOT topology도 owner reverse index를 가진다. owner cleanup은 해당 owner가 가진
  topology row만 방문한다.
- 반영: `resolve_spot()`과 route resolve는 live owner provider를 확인한다.
- 반영: socket auto-connect는 `routing_id -> endpoint` 기준으로 diff를 계산하고 local RID를
  제외한다.
- 미반영: 0개.

## Stage 4. Core 테스트와 미적용 리뷰

- 반영: route binding lifecycle, peer Registry route snapshot propagation, route value
  제한, option rename, metadata surface 제거 테스트를 추가하거나 갱신했다.
- 반영: pending route 승격, route winner fallback, memory budget 실패, provider RID
  충돌 제외를 검증하는 route store 단위 테스트를 추가했다.
- 반영: 65,536개 route insert/materialize/lookup/owner cleanup 성능 회귀 테스트를 추가했다.
  현재 환경 측정값은 insert+materialize 353ms, lookup 21ms, cleanup 247ms이며 cleanup 방문
  수가 record 수와 같은지도 함께 검증한다.
- 반영: rehash pause guard와 cursor 기반 snapshot chunk 단위 테스트를 추가했다.
- 반영: 10M route materialized table 수동 성능 테스트를 추가했다. 현재 환경 측정값은
  insert 7,586ms, 100,000 sample lookup 90ms, snapshot 279ms, erase 3,695ms,
  max RSS 977,228KB, max chain 9다. record당 RSS는 약 100.1B로 1GiB 이하 목표
  범위 안에 들어왔다.
- 검증: `cmake --build core/build`, targeted ctest, full core ctest 101개를 실행했다.
- 미반영: 0개.

## Stage 5. Core POSD 리뷰

- 위험 신호 1: route key type이 public helper처럼 노출될 수 있었다.
  - 대안 A: static helper와 외부 key type 유지.
  - 대안 B: Registry private type과 member helper로 제한.
  - 선택: B. route key 지식이 Registry 내부에 갇혀 정보 은닉이 더 강하다.
- 위험 신호 2: bind/unbind가 endpoint만 보고 모든 service를 scan할 수 있었다.
  - 대안 A: endpoint scan 유지.
  - 대안 B: Discovery가 owner channel과 registration token을 함께 보낸다.
  - 선택: B. 호출자가 아닌 Registry가 owner generation을 검증하므로 모듈이 깊어진다.
- 재검토 결과: 의미 있는 POSD 리팩토링 후보 0개.

## Stage 6. Core perf smoke

- 검증: `bindings/c/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports inproc --reuse-build`
- 검증: `bindings/c/perf/run_benchmarks.sh --pattern SPOT --runs 1 --duration 1 --msg-sizes 64 --transports tcp --reuse-build`
- 검증: `bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --clients 1 --transport-transition-ms 0 --pattern-transition-ms 0 --reuse-build`
- 결과: 모두 통과했다. runner는 `core/build/lib/libzlink.so.5.3.4`를 출력했다.
- 최근 측정: single inproc 64B는 PAIR 2,419.340Kmsg/s, PUBSUB 1,862.992Kmsg/s,
  DEALER_DEALER 2,413.075Kmsg/s, DEALER_ROUTER 1,983.494Kmsg/s,
  ROUTER_ROUTER 2,111.276Kmsg/s다. single SPOT tcp 64B는 26.511Kmsg/s다.
- 최근 측정: multi tcp 64B는 MULTI_DEALER_DEALER 3,407.837Kmsg/s,
  MULTI_PUBSUB 201.600Kmsg/s, MULTI_SPOT 26.736Kmsg/s,
  MULTI_STREAM 17.837Kops/s다.
- 미반영: 0개.

## Stage 7. Bindings API 반영

- 반영: C++/.NET/Go/Java/Node/Python/Rust에 route binding API를 추가하고 제거된 metadata
  API를 정리했다.
- 반영: 각 바인딩 header/native runtime Linux x86_64 위치를 최신 core header/runtime과 맞췄다.
- 검증: C++, Go, Rust, .NET, Java, Node, Python 테스트를 실행했다.
- 검증: framework .NET 전체 suite도 실행했다.
- 미반영: 0개.

## Stage 8. Bindings POSD 리뷰

- 위험 신호 1: native bridge ownership이 언어별 wrapper 밖으로 새어 나갈 수 있었다.
  - 대안 A: raw `zlink_msg_t` 또는 native pointer를 그대로 노출.
  - 대안 B: 각 언어의 기존 Message/RoutingId wrapper로 소유권을 흡수.
  - 선택: B. 호출자는 native message 초기화와 close 규칙을 직접 알 필요가 없다.
- 위험 신호 2: 제거된 metadata API를 deprecated wrapper로 남길 수 있었다.
  - 대안 A: 호환 wrapper 유지.
  - 대안 B: public surface에서 제거.
  - 선택: B. 호환성 유지가 목표가 아니며 중복 계약을 없앤다.
- 재검토 결과: 의미 있는 POSD 리팩토링 후보 0개.

## Stage 9-13. 문서, native runtime, 최종 리뷰

- 반영: core spec, guide, internals, site docs, binding spec에서 route binding 계약을
  정식 문서로 반영하고 제거된 Discovery metadata/member metadata 설명을 정리했다.
- 반영: native runtime Linux x86_64 파일을 최신 `core/build` runtime으로 갱신했다.
- 검증: `cmake --build core/build`, core ctest 101개, C++ ctest 19개, Go/Rust/Python/Java/Node/.NET
  binding tests, framework .NET tests, framework .NET samples build를 최종 재실행했다.
- 검증: `bindings/c/perf` single non-SPOT, single SPOT, multi smoke를 최종 재실행했고 모두
  `core/build/lib/libzlink.so.5.3.4`를 사용해 통과했다.
- 검증: `python -m mkdocs build -f doc/site/mkdocs.yml`는 성공했다. 기존 site link/nav warning은 남아 있지만 이번
  route 계약 반영으로 새 blocker는 만들지 않았다.
- 검증: 주요 binding native runtime 파일의 checksum은 `core/build/lib/libzlink.so.5.3.4`와
  일치한다. 최신 checksum은
  `29d65c8bba05ad918e7ef4f8e79ab0984d3afd1f6ddb75fe3c319812e8724055`다.
- 최종 미적용 리뷰 기준: draft 항목은 구현, 테스트, 문서, native runtime, binding surface에
  필요한 만큼 반영됐다.
- 미반영: 0개.
