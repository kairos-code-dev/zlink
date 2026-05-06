# Bindings Update Log

## 5.3.9 native library 갱신

- 날짜: 2026-05-06
- 대상: bindings native library 최신화
- 수행한 명령:
  - `bindings/update_zlink_libs.sh core/v5.3.9 --expect-version 5.3.9`
- 확인한 draft spec 절: Public C API 변경 요약, 회귀 테스트
- 발견한 문제:
  - release에 `libzlink_c-*` optional asset이 없어 script가 경고를 출력했지만 core asset은 모두 존재해 진행했다.
- 수정한 파일:
  - bindings native library 파일들
  - `bindings/java/build.gradle`
  - `bindings/node/package.json`
  - `bindings/node/package-lock.json`
  - `bindings/python/pyproject.toml`
  - `bindings/python/src/zlink.egg-info/PKG-INFO`
- 검증 결과:
  - update script 완료.
  - linux-x64 runtime version 확인: python/node/dotnet/java/cpp/go/rust 모두 `5.3.9`.
  - `c_binding` native library presence 확인.
- 남은 위험: release 갱신 뒤 언어별 bindings gate 재검증 필요
- 다음 확인: c, cpp, dotnet까지 기존 완료 항목을 5.3.9 runtime으로 재확인한 뒤 go gate를 닫고 java부터 순차 진행

---

## 5.3.8 native library 갱신

- 날짜: 2026-05-06
- 대상: bindings native library 최신화
- 수행한 명령:
  - `bindings/update_zlink_libs.sh core/v5.3.8 --expect-version 5.3.8`
  - `git status --short`
  - `rg -n "5\\.3\\.8|version" bindings/dotnet/src/Zlink/Zlink.csproj bindings/java/build.gradle bindings/node/package.json bindings/python/pyproject.toml bindings/python/src/zlink.egg-info/PKG-INFO bindings/go/include/zlink.h bindings/cpp/include/zlink.h`
- 확인한 draft spec 절: Public C API 변경 요약, 회귀 테스트
- 발견한 문제: release에 `libzlink_c-*` optional asset이 없어 script가 경고를 출력했지만 core asset은 모두 존재해 진행했다.
- 수정한 파일:
  - bindings native library 파일들
  - `bindings/dotnet/src/Zlink/Zlink.csproj`
  - `bindings/java/build.gradle`
  - `bindings/node/package.json`
  - `bindings/node/package-lock.json`
  - `bindings/python/pyproject.toml`
  - `bindings/python/src/zlink.egg-info/PKG-INFO`
  - generated binding headers: `bindings/cpp/include/zlink_errno.h`, `bindings/go/include/zlink_errno.h`, `bindings/rust/include/zlink.h`, `bindings/rust/include/zlink_errno.h`
- 검증 결과:
  - update script 완료.
  - linux-x64 runtime version 확인: python/node/dotnet/java/cpp/go/rust 모두 `5.3.8`.
  - `c_binding` native library presence 확인.
- 남은 위험: 언어별 bindings spec/code/sample/perf/POSD gate 필요
- 다음 확인: c binding gate부터 순차 진행
