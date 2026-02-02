# Packaging & Release (vcpkg / Conan / Tarball)

## 목표
- GitHub Release tarball을 **단일 소스**로 사용
- vcpkg + Conan 모두 제공
- `cmake --install` 산출물 + tarball을 릴리즈 자산으로 제공

---

## 버전 규칙
- 버전은 `VERSION` 파일을 기준으로 한다.
- 태그는 `vX.Y.Z` 또는 `X.Y.Z` 형식을 허용하되, **릴리즈 워크플로우는 tag 기준**으로 동작한다.
- 패키지 버전(Conan/vcpkg)도 `VERSION`과 일치해야 한다.

---

## GitHub Actions 릴리즈 자산
릴리즈 태그 푸시 시 아래 자산을 생성/업로드한다.
- 플랫폼별 zip/tar.gz 아카이브
  - Windows x64/ARM64
  - Linux x64/ARM64
  - macOS x64/ARM64
- 소스 tarball: `zlink-<VERSION>-source.tar.gz`
- 체크섬: `checksums.txt`

워크플로우: `.github/workflows/build.yml`

---

## Conan (packaging/conan)
GitHub Release tarball에서 소스를 가져온다.

파일:
- `packaging/conan/conanfile.py`
- `packaging/conan/conandata.yml`
- `packaging/conan/README.md`

업데이트 절차:
1) 릴리즈 태그 생성 및 GitHub Release 확인
2) tarball URL/sha256 확보
3) `conandata.yml` 업데이트
4) `conan create . --version <VERSION>` 실행

---

## vcpkg (overlay port)
오버레이 포트로 제공한다.

파일:
- `vcpkg/ports/zlink/portfile.cmake`
- `vcpkg/ports/zlink/vcpkg.json`
- `vcpkg/README.md`

업데이트 절차:
1) 릴리즈 태그 생성 및 GitHub Release 확인
2) tarball URL/sha512 확보
3) `portfile.cmake`의 `REF`/`SHA512` 업데이트
4) `vcpkg.json`의 `version-string` 업데이트

---

## CMake install / pkg-config
- `cmake --install` 결과물을 릴리즈 자산으로 배포
- 패키지에는 `include/`, `lib/`, `lib/pkgconfig` 포함

---

## 체크리스트
- [ ] `VERSION` 갱신
- [ ] 태그 생성 (예: `v0.6.0`)
- [ ] GitHub Release 확인
- [ ] Conan `conandata.yml` 업데이트
- [ ] vcpkg 포트 업데이트
- [ ] 릴리즈 자산/체크섬 확인
