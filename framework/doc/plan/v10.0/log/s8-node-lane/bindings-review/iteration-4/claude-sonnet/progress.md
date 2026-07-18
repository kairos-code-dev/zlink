# S8 NODE bindings 리뷰 iteration-4 — R2(claude-sonnet) progress

## 절차 로그
1. `../prompt.md` 원문 로드, byte 단위 동일 prompt 확인. 다른 리뷰어·coordinator 해석 미참조(iteration-2/3의 review.ko.md 등은 열지 않음, phantom option 반례는 prompt.md 본문에서 직접 확인).
2. scope 확인: `git ls-files bindings/node/src bindings/node/native/src bindings/node/samples bindings/node/binding.gyp bindings/node/package.json`(build/node_modules/prebuilds 제외) → 140 파일.
   - aggregate hash 재현 명령: `LC_ALL=C sort <filelist> | xargs sha256sum | sha256sum` → `967205f1af41bf948f105b576167f21536d3a59094cb06ed675c9f4674fa3963` — prompt 명시값과 일치.
   - `git merge-base --is-ancestor 006d34f97 HEAD` → YES(HEAD=`7a68f7973`가 target의 후손).
   - `git diff 006d34f97 HEAD -- <scope>` → 무변화(freeze 커밋은 manifest/prompt 문서만 추가).
   - `git status --porcelain -- <scope>` → 무변화(작업트리 scope 오염 없음). scope 내용이 target commit과 byte 동일함을 재확인.
3. iter-3 finding 해소 판정(소스 대조):
   - R2(NF3-1) `option_mapping.ts` SocketOption 테이블: Core `core/include/zlink_enum.h`의 `zlink_option_t`/`zlink_router_option_t`/`zlink_dealer_option_t`/`zlink_pub_option_t`/`zlink_sub_option_t`/`zlink_stream_option_t` 6개 enum 전체(72개 hex 값)를 파이썬 스크립트로 파싱, node 테이블(72개 hex 값)과 set-equality 비교 → 완전 일치, 중복 없음, phantom `0x3035`/`0x3036` 미검출, `AUTO_HWM_MSG_UNIT_BYTES=0x3034` 존재 확인. **해소**.
   - R1 `monitor.ts` JSDoc: `MonitorSourceKind = { Socket: 1 }` + 주석 "Core 10.0.0 defines only the socket source" — Core `zlink_monitor_source_kind_t`(`ZLINK_MONITOR_SOURCE_SOCKET = 1`뿐)와 일치, spot pub/sub source 서술 없음. **해소**.
   - `grep -rn "0x3035\|0x3036\|SPOT_OWNER_SYNC\|ACTOR_ROUTE_SYNC"` scope 전체 재검색 → 0건(잔존 참조 없음).
4. I1 계약 일치 재검토:
   - pull-dispatch 관련 enum(ReceiveKind/OperationKind/MeshDestinationKind/ReadyOwnerKind, `contracts/service/dispatch.ts`)을 Core `core/include/zlink/service/dispatch.h`의 `zlink_mesh_record_kind_t`/`zlink_mesh_operation_kind_t`/`zlink_mesh_destination_kind_t`/`zlink_mesh_owner_kind_t`와 대조 → 전부 정확 일치.
   - actor lifecycle 관련 enum(ActorLifecycleKind/ActorJoinResult)을 `core/include/zlink/service/actor.h`의 `zlink_actor_lifecycle_kind_t`/`zlink_actor_join_result_t`와 대조 → 일치.
   - native decl==등록 대조: `addon_exports.cc`의 `ZLINK_METHOD` 175개 JS 이름을 `binding_core.ts`/`binding_context.ts`/`binding_eventing.ts`/`binding_service.ts`/`binding_socket.ts` 5개 인터페이스 파일의 top-level 메서드 선언(176개, 1개는 중첩 파라미터 `handler` 오탐)과 set 비교 → 정확 1:1 일치(누락·잉여 0).
   - `binding.gyp`의 6개 native source(`addon.cc`/`addon_core.cc`/`addon_core_options.cc`/`addon_exports.cc`/`addon_mesh_service.cc`/`addon_spot_request_callbacks.cc`)가 `native/src/` 실제 .cc 파일 전량과 일치.
   - `dispatch.ts`(contracts)·`dispatch.ts`(runtime)의 ReadyBatch→Claim→ReceiveBatch→ReceiveRecord.reply 파이프라인이 native export(meshReadyBatchNew/Reset/Destroy, meshNodeDrainReady, meshReadyBatchTakeClaim, meshClaimRelease, meshReceiveBatchNew/Reset/Destroy, meshClaimRecvBatch, meshReply, actorJoinReply)와 1:1 대응 확인.
5. I2 POSD/DDD 재검토: `find bindings/node/src -name '*.ts' | xargs wc -l` 최대 파일 549줄(`runtime/service/mesh_node.ts`) — 도메인별 섹션 주석(Lifecycle/Peers/Messaging/Publisher/Options/Spots/Actors/Transfer/Dispatch/Stream)으로 분리된 얇은 네이티브 위임 facade, god-file 징후 없음. 다른 상위 파일(`binding_service.ts` 349줄 등)도 동일 성격.
6. I3 정리 재검토:
   - TODO/FIXME/XXX/HACK/deprecated/@ts-ignore/@ts-nocheck/eslint-disable 전체 scope grep → 0건.
   - no-hit 재확인: `runtime/**/*.ts`의 top-level export 200개를 추출해 `src/**/*.ts` + `samples/**/*.ts` 전체 텍스트에서 단어경계 출현 횟수를 스크립트로 집계 → 정의 자신의 1회 출현을 넘는 미사용(orphan) 없음(0건).
7. manifest 실행 증거(addon node-gyp green, tsc src+samples green, no-hit 0) 그대로 인용, 재실행하지 않음(리뷰어는 static-only).

## 결론
iter-3 두 finding 모두 신규 반례 없이 해소 확인. I1/I2/I3 전 축 blocker/high/medium 0. low finding 없음. `BINDINGS REVIEW CLEAN`.
