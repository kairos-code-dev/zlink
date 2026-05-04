# Core Release Log

- 날짜: 2026-05-04
- 대상: core release
- 수행한 명령: 없음
- 발견한 문제: release 전
- 수정한 파일: 없음
- 남은 위험: local 구현/검증 완료 뒤 version, commit, push, tag, GitHub Actions 확인 필요
- 다음 확인: 최종 문서 리뷰와 smoke 완료 뒤 release 준비

## 2026-05-05 release workflow 확인

- 대상: core release 자동화 경로
- 수행한 명령: `git remote -v`, `gh auth status`, `.github/workflows/build.yml` 확인, `.github/workflows/core-conan-release.yml` 확인
- 확인 결과:
  - `origin`은 `git@github.com-kairos:kairos-code-dev/zlink.git`이다.
  - `gh` CLI는 `kairos-code-dev` 계정으로 인증되어 있다.
  - `.github/workflows/build.yml`은 `core/v*` tag push에서 `Build libzlink Core Libraries`를 실행한다.
  - `.github/workflows/core-conan-release.yml`은 `core/v*` tag push에서 `Release Core Conan Package`를 실행한다.
  - core release tag 형식은 plan대로 `core/vX.Y.Z`이다.
- 다음 release 절차:
  1. core 구현, 테스트, 문서 3회 리뷰를 먼저 완료한다.
  2. `VERSION`, `core/include/zlink.h`, `core/CMakeLists.txt`의 version을 같은 `X.Y.Z`로 올린다.
  3. commit 후 branch push를 하고 `gh run list --branch <branch> --limit 10`, `gh run watch <run-id> --exit-status`로 확인한다.
  4. `git tag core/vX.Y.Z`, `git push origin core/vX.Y.Z`를 실행한다.
  5. `gh run list --workflow "Build libzlink Core Libraries" --limit 10`, `gh run list --workflow "Release Core Conan Package" --limit 10`, `gh run watch <run-id> --exit-status`로 tag workflow를 확인한다.
  6. `gh release view core/vX.Y.Z --json tagName,url,assets`로 release와 asset을 확인한다.
- 남은 위험: core draft 계약이 아직 닫히지 않았으므로 version bump, commit, tag push는 아직 실행하지 않는다.

## 2026-05-05 core/v5.3.5 준비

- release version: `5.3.5`
- 기준 tag: 기존 최신 core tag는 `core/v5.3.4`
- version bump:
  - `core/include/zlink.h`: `ZLINK_VERSION_PATCH 5`
  - `core/CMakeLists.txt`: `project(zlink VERSION 5.3.5 ...)`
  - `core/CMakeLists.txt`: shared library `VERSION "5.3.5"`
  - binding vendored headers `bindings/cpp/include/zlink.h`,
    `bindings/go/include/zlink.h`, `bindings/rust/include/zlink.h`의
    compile-time patch version도 `5`로 맞췄다.
- 수행한 명령:
  - `cmake --build core/build -j"$(nproc)"`
  - `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`
  - `ctest --test-dir core/build --output-on-failure -L integration -j1`
  - `git diff --check`
  - `gh auth status`
  - `gh workflow list --limit 20`
- 결과:
  - core configure/build는 `Detected ZLINK Version - 5.3.5`로 재생성되고 성공했다.
  - unit test 21/21 통과.
  - integration test 63/63 통과.
  - `git diff --check` 통과.
  - `gh` CLI는 `kairos-code-dev` 계정으로 인증되어 있다.
  - release workflow `Build libzlink Core Libraries`,
    `Release Core Conan Package`가 active 상태다.
- 다음 확인: commit, branch push, `core/v5.3.5` tag push, GitHub Actions run watch,
  release asset 확인
