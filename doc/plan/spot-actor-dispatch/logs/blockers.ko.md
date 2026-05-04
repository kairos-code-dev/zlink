# Blocker Log

- 날짜: 2026-05-04
- 대상: SPOT Actor Dispatch 구현
- 수행한 명령: `git status --short`, draft spec/plan/POSD 기준 문서 확인
- 발견한 문제: 없음
- 수정한 파일: `doc/plan/spot-actor-dispatch/logs/contract-matrix.ko.md`
- 남은 위험: core/bindings/release 단계는 아직 수행 전
- 다음 확인: baseline build/test와 단계별 implementation review log 작성

## 2026-05-04 release/bindings 진행 차단

- 대상: `bindings/update_zlink_libs.sh` native library 최신화
- 확인 결과: core release는 `core/vX.Y.Z` tag push로 GitHub Actions가 생성한다. `gh` CLI 인증도 확인됐다.
- 차단 내용: core draft 계약, 테스트, 문서 리뷰, version bump, commit, branch push, `core/vX.Y.Z` tag push, release workflow 확인이 아직 완료되지 않았다.
- 영향: `bindings/update_zlink_libs.sh`를 통한 native library 동기화는 release workflow 성공 뒤 진행해야 한다. 최신 native library가 필요한 binding runtime 검증 일부는 release 이후 다시 실행해야 한다.
- 임시 조치: core 공개 헤더와 언어별 binding 공개 표면에서 제거 대상 generic route API를 제거했고, Actor route sync option facade를 추가했다.
