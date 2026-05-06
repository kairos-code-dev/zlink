# Bindings Update Log

- 날짜: 2026-05-06
- 대상: bindings native library 최신화
- 수행한 명령: N/A
- 확인한 draft spec 절: Public C API 변경 요약, 회귀 테스트
- 발견한 문제: core release 전이라 `bindings/update_zlink_libs.sh`를 실행하지 않음
- 수정한 파일: N/A
- 검증 결과: pending
- 남은 위험: core release asset 확인 뒤에만 native library를 갱신해야 한다
- 다음 확인: core release 완료 뒤 update script 실행
