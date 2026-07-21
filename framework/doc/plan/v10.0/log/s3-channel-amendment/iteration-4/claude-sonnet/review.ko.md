# S3-CH·S3-FO iteration 4 독립 문서 리뷰

## Snapshot 확인

- 시작 시 `scope-files.sha256`의 71개 파일을 모두 검증했고 모두 `OK`였다.
- `scope-files.sha256` SHA-256은 `c6c88d4db2ee18b87f99eec7c5cd80d6e1f6715c3324b2249c1996bb20c846da`였다.
- `scope-files.txt` SHA-256은 `00bba02f27b40d650b02d659bcd613d4e69097220565dd42419fa2a2937f7818`였다.
- 기준 HEAD `86258cb9a3ecbc7db2dfd86a8c18de45a562734a`를 확인했다.
- 루트 `AGENTS.md`, 문서·POSD 원칙, iteration 4 manifest와 scope 71개 파일 전체를 읽었다.
- `scripts/verify-framework-doc-contracts.sh`는
  `FRAMEWORK DOC CONTRACTS CLEAN languages=5 exact_documents=24 connector_exact=4 formal_documents=55 code_fixtures=19 declarations=1271 transition_owners=13 transition_members=190 feature_maps=55 scenario_rows=995`로
  통과했다.
- iteration 3의 finding을 코드 라인 대조로 재검증했다. 전부 해소되어 있고 각 항목이 verifier의 구조적
  semantic gate로도 고정되어 있다.

## Findings

[계약][medium] framework/doc/framework/spec/server/languages/dotnet/06-location-store.ko.md:313 —
`ListFanoutPublishersAsync`의 draining 제외 책임이 store list 계층에 놓여 있어, draining 제외를 automatic
subscriber의 connection-intent 계산 책임으로 규정한 공통 계약과 같은 문서 안의 자매 API 설명 패턴에서
어긋난다 — 공통 계약은 resolver가 유효한 owner lease를 가진 descriptor snapshot만 제공한다고 규정하고,
draining·낮은 generation/revision 배제는 automatic subscriber의 connection intent 책임으로 명시한다.
Java·Node·C++의 같은 list API에도 store 계층의 draining 제외 문구가 없다 —
`ListFanoutPublishersAsync`는 같은 ChannelName의 유효한 owner lease를 가진 publisher를 반환하며,
draining과 낮은 generation/revision 제외는 automatic subscriber의 connection-intent 계산이 담당한다고
고친다.

## 종료 확인

- 종료 hash 검사에서 scope 71개가 모두 `OK`이고 두 목록 hash가 시작 값과 같음을 다시 확인했다.

DOC REVIEW NOT CLEAN
