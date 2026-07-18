# S8 NODE bindings 전환 리뷰 — iteration 4 (R2 / claude-sonnet)

## 1. Scope 확인
- 대상 commit: `006d34f97`
- 명령: `git ls-files bindings/node/src bindings/node/native/src bindings/node/samples bindings/node/binding.gyp bindings/node/package.json`(`/build/`·`node_modules`·`prebuilds` 제외)
- 파일 수: **140** (prompt 명시값과 일치)
- aggregate SHA-256(`LC_ALL=C sort <filelist> | xargs sha256sum | sha256sum`): **`967205f1af41bf948f105b576167f21536d3a59094cb06ed675c9f4674fa3963`** — prompt 명시값과 일치
- 부가 확인: HEAD(`7a68f7973`)는 target `006d34f97`의 후손이며 `git diff 006d34f97 HEAD -- <scope>`는 무변화(직후 freeze 커밋이 manifest/prompt 문서만 추가), `git status --porcelain -- <scope>`도 무변화 — scope 내용이 target과 byte 단위로 동일함을 재확인.
- 파일 수정 없음(정적 대조만 수행).

## 2. iter-3 finding 해소 판정
| finding | 내용 | 판정 | 근거 |
|---|---|---|---|
| R2(NF3-1) | `option_mapping.ts` SocketOption 테이블에 Core에 없는 phantom `DISCOVERY_SPOT_OWNER_SYNC=0x3035`/`DISCOVERY_ACTOR_ROUTE_SYNC=0x3036`, Core의 `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES=0x3034` 누락 | **해소** | `option_mapping.ts`(24줄) 현재 `AUTO_HWM_MSG_UNIT_BYTES: 0x3034`만 존재, `0x3035`/`0x3036` 미검출. Core `zlink_enum.h`의 `zlink_option_t`+`zlink_router_option_t`+`zlink_dealer_option_t`+`zlink_pub_option_t`+`zlink_sub_option_t`+`zlink_stream_option_t` 6개 enum 전체(72개 hex)와 node 테이블(72개 hex)을 스크립트로 set-equality 비교 → **완전 일치, 중복 없음**. scope 전체 재검색(`0x3035`/`0x3036`/`SPOT_OWNER_SYNC`/`ACTOR_ROUTE_SYNC`) 0건. |
| R1 | `monitor.ts` `MonitorSourceKind` JSDoc이 제거된 spot pub/sub source를 여전히 서술 | **해소** | `MonitorSourceKind = Object.freeze({ Socket: 1 })` + 주석 "Core 10.0.0 defines only the socket source." Core `zlink_monitor_source_kind_t`(`ZLINK_MONITOR_SOURCE_SOCKET=1`뿐)와 일치. spot pub/sub 서술 없음. |

두 finding 모두 신규 반례 없이 해소 확인. iter-1·iter-2 finding은 이전 회차에 해소·확인된 것으로 재개하지 않음.

## 3. I1/I2/I3 Finding

### I1 — 계약 일치(enum·옵션id 테이블 Core 정확 일치·pull dispatch·native decl==등록)
- **SocketOption 테이블**: 위 §2에서 Core 6개 옵션 enum과 hex-by-hex 완전 일치 확인(72/72, 결측·잉여·중복 0).
- **MonitorSourceKind**: Core `zlink_monitor_source_kind_t`와 일치.
- **pull-dispatch enum**: `contracts/service/dispatch.ts`의 `ReceiveKind`(1~13)·`OperationKind`(1~11)·`MeshDestinationKind`(1~5)·`ReadyOwnerKind`(Node=1/Spot=2/Actor=3)를 Core `core/include/zlink/service/dispatch.h`의 `zlink_mesh_record_kind_t`/`zlink_mesh_operation_kind_t`/`zlink_mesh_destination_kind_t`/`zlink_mesh_owner_kind_t`와 대조 → 전부 정확 일치. `ActorLifecycleKind`/`ActorJoinResult`도 `core/include/zlink/service/actor.h`의 `zlink_actor_lifecycle_kind_t`/`zlink_actor_join_result_t`와 일치.
- **pull-dispatch 파이프라인 일관성**: `runtime/service/dispatch.ts`의 `RuntimeReadyBatch.takeClaim → RuntimeClaim.recvBatch → RuntimeReceiveBatch/RuntimeReceiveRecord.reply/replyActorJoin` 흐름이 native export(`meshReadyBatchNew/Reset/Destroy`, `meshNodeDrainReady`, `meshReadyBatchTakeClaim`, `meshClaimRelease`, `meshReceiveBatchNew/Reset/Destroy`, `meshClaimRecvBatch`, `meshReply`, `actorJoinReply`)와 1:1 대응.
- **native decl==등록**: `addon_exports.cc`의 `ZLINK_METHOD` 175개 JS 함수명을 5개 `binding_*.ts` 인터페이스(`binding_core`/`binding_context`/`binding_eventing`/`binding_service`/`binding_socket`)의 top-level 메서드 선언과 스크립트로 set 비교 → **정확 1:1 일치**(선언만 있고 미등록, 등록만 있고 미선언 모두 0건; 참고: 정규식 오탐으로 `handler`가 잡혔으나 이는 콜백 파라미터 타입의 필드명일 뿐 top-level 메서드 아님).
- **binding.gyp==native 소스**: `sources` 6개 항목이 `native/src/*.cc` 실제 파일 전량과 일치.
- **Verdict: CLEAN** (blocker/high/medium 0)

### I2 — POSD·DDD
- 최대 파일 `runtime/service/mesh_node.ts`(549줄): 도메인별 섹션(Lifecycle/Peers/Node-Channel Messaging/Publisher/Options/Spots/Actors/Transfer Fence/Pull Dispatch/Stream Session)으로 명확히 분리된 얇은 native 위임 facade. 각 메서드는 파라미터 정규화 + 단일 native 호출 + 결과 매핑의 3~10줄 패턴으로 일관, god-class 징후 없음.
- 다른 상위 파일(`binding_service.ts` 349줄, `socket_operations.ts` 276줄, `binding_service_types.ts` 276줄 등)도 동일하게 얇은 타입/위임 레이어이며 책임 혼재 없음.
- contracts/runtime 계층 분리(공개 인터페이스 vs 구현) 유지, native 호출은 runtime 레이어에만 존재.
- **Verdict: CLEAN** (blocker/high/medium 0)

### I3 — 정리(폐기 no-hit·dead code)
- `TODO|FIXME|XXX|HACK|deprecated|@ts-ignore|@ts-nocheck|eslint-disable` scope 전체 검색 → 0건.
- phantom 옵션 잔존 참조(`0x3035`/`0x3036`/`SPOT_OWNER_SYNC`/`ACTOR_ROUTE_SYNC`) → 0건.
- no-hit 재확인: `runtime/**/*.ts`의 top-level export 200개를 추출해 `src/**/*.ts`+`samples/**/*.ts` 전체에서 단어경계 출현 횟수를 집계 → 정의 자신의 1회를 넘는 미사용 없음(orphan 0건). manifest의 "no-hit 0"과 일치.
- **Verdict: CLEAN** (blocker/high/medium 0)

## 4. Low finding 목록
없음.

## 5. 폐기 no-hit 판정
0건(coordinator manifest 수치와 일치, §3 I3에서 독립 재확인).

## 6. 결론
세 축(I1/I2/I3) 모두 blocker/high/medium 0. iter-3 finding 전량 해소, 신규 반례 없음.

BINDINGS REVIEW CLEAN
