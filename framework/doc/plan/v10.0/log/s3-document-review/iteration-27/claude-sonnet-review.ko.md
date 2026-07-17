# Claude Sonnet 독립 문서 리뷰 — S3 iteration 27

## 발견 사항

[1차소스][high] `framework/doc/framework/spec/server/languages/java/01-system-structure.ko.md:293-302` — §4 “Spot-to-spot” 예제가 `context.outbound().requestToSpot(targetSpotRid, request)`로 raw RoutingId를 넘기고 해소 주체를 `SpotRemoteRefResolver`로 명시한다 — 같은 문서 643행은 `SpotHandle`을 대상으로 명시적인 send/request를 제출한다고 서술하며, exact-interface 카탈로그 `02-handler-interfaces.ko.md:755-759,848-852`의 `ZLinkSpotOutbound.sendToSpot/requestToSpot`는 `SpotHandle` 단일 인자만 받고 해소자는 `SpotHandleResolver`/`ActorSpotHandleResolver`(1649, 1655행)다. `SpotRemoteRefResolver`라는 타입은 없다 — 예제를 `SpotHandleResolver`로 얻은 `SpotHandle`을 `requestToSpot(handle, request)`에 넘기는 형태로 재작성하고 `SpotRemoteRefResolver` 언급을 제거한다.

[1차소스][high] `framework/doc/framework/cpp/guide/08-spot.ko.md:214` — `send_to(...)`와 `request_to(...)`가 target node RID와 Spot RID를 지정한다고 서술한다 — exact-interface 카탈로그 `spec/server/languages/cpp/02-framework-interfaces.ko.md`에는 접미사 없는 메서드가 없고 `send_to_spot(spot_handle_t, ...)`/`request_to_spot(spot_handle_t, ...)`만 정의된다. 같은 guide 220행도 `spot_handle_resolver_t`로 얻은 `spot_handle_t`를 사용한다 — 메서드 이름과 인자를 exact interface에 맞춘다.

[1차소스][high] `framework/doc/framework/cpp/guide/13-interface-catalog.ko.md:117-118` — outbound 세 표면 중 Spot direct를 `send_to<TMsg>`, `request_to<TReply,TReq>`로 서술한다 — 같은 문서 108행과 exact-interface 카탈로그는 `send_to_spot`/`request_to_spot`만 정의한다 — 이름을 exact interface와 통일한다.

[1차소스][high] `framework/doc/framework/cpp/guide/02-getting-started.ko.md:213` — `options.configure_locations().auto_connect = true;` 예제 코드를 제시한다 — `location_options_t`의 정식 정의인 `spec/server/languages/cpp/03-location-store.ko.md:11-18`에는 여섯 duration 필드만 있고 `auto_connect`가 없다 — 실재하지 않는 대입문을 제거하거나 실제 공개 표면이 확정될 때까지 보류한다.

[1차소스][high] `framework/doc/framework/java/guide/04-channel-messaging.ko.md:63,89` — `routeClient.requestTo("play.route", target, ...)`를 사용하고 첫 문자열을 route channel 이름이라고 설명한다 — exact-interface 카탈로그 `spec/server/languages/java/02-handler-interfaces.ko.md:825-844`의 `ZLinkRouteClient`에는 `requestTo`가 없고 `sendToNode`/`requestToNode`/`sendToChannel`/`requestToChannel`만 있으며 첫 인자는 `meshName`이다 — 의도에 맞는 정확한 메서드를 사용하고 첫 인자를 MeshName으로 설명한다.

[1차소스][high] `framework/doc/framework/kotlin/guide/04-channel-messaging.ko.md:83-84,94,116` — `routeClient.requestTo("play.route", target, ...)`와 산문의 `sendTo(channel, target, message)`를 사용한다 — exact-interface 카탈로그 `spec/server/languages/kotlin/02-handler-interfaces.ko.md:418-427`의 coroutine 확장은 `send(meshName, target, message)`/`request(meshName, target, message)`만 정의하며 첫 인자는 `meshName`이다 — 정확한 Kotlin 메서드로 바꾸고 첫 인자를 MeshName으로 설명한다.

[1차소스][high] `framework/doc/framework/java/guide/06-actor-session.ko.md:301-302` — Spot owner 조회를 `useRegistrySpotRemoteRefs(...)` 또는 custom `addSpotRemoteRefResolver(...)`로 공개한다고 설명한다 — exact-interface 카탈로그가 정의하는 resolver 타입은 `SpotHandleResolver`/`ActorSpotHandleResolver`이며 해당 remote-ref 이름은 없다 — 실제 handle resolver 등록 표면으로 교체한다.

[1차소스][medium] `framework/doc/framework/common/sample/tictactoe/README.ko.md:262-263` — public `spot remote ref resolver` 계약이라고 설명한다 — 같은 문서 344행과 978행 및 공통 spec `server/24-spot-address-messaging.ko.md`는 `spot handle resolver`로 정의한다 — 용어를 `spot handle resolver`로 통일한다.

## 실행 증거

- provider/model/session: Anthropic Claude / `claude-sonnet-5` / `38cc6a6a-79f9-4501-bac4-97a0fd9f639b`
- 시작과 종료 모두 HEAD, `scope-files.txt`, `scope-files.sha256`, 205개 파일별 SHA-256이 동결값과 일치했다.
- `scripts/verify-framework-doc-contracts.sh`는 `FRAMEWORK DOC CONTRACTS CLEAN`으로 통과했다.
- 205개 범위 전체를 읽었고, 실제 `markdown` + `pymdownx` 10.21.2 anchor를 대조했다.
- 첫 CLI 실행이 파일별 순차 읽기로 오래 걸려 중단한 뒤 같은 provider session을 `--resume`으로 이어서 완료했다. 재개 과정에서 reviewer가 범위 파일을 묶어 읽기 위해 `/tmp/iter27_batch1.txt`, `/tmp/iter27_batch2.txt`를 만들었다. 저장소와 동결 범위는 수정하지 않았고 Edit/Write 도구 호출은 없었지만, manifest의 파일 생성 금지 지시를 문자 그대로 지키지 못했으므로 이 iteration은 finding 존재 여부와 별개로 채택할 수 없다.
