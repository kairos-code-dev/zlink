# Java 바인딩 보안 검토 보고서

- 작성일: 2026-06-14
- 대상 범위: `bindings/java/src/main/java/module-info.java`, `bindings/java/src/main/java/systems/zlink/runtime/nativeapi/LibraryLoader.java`
- 검토 방식: JPMS 공개 범위, native library 로딩 순서, 임시 디렉터리 추출 경로를 코드 기준으로 확인했다.
- 상태: 2026-06-14 주의 항목 2건 문서화 완료. Codex 에이전트 리뷰 통과.

## 요약

Java 바인딩은 내부 native API를 감추고 공개 계약 패키지만 export한다. 이 구조는 호출자가 내부 FFI 경계를 직접 만지는 위험을 줄인다.

보안 관점의 주요 경계는 native library 로더다. `ZLINK_LIBRARY_PATH`, 리소스 추출, `System.loadLibrary("zlink")` fallback, Windows 의존 DLL 검색이 모두 같은 로더에 있다.

## 확인된 이슈

### JAVA-BINDING-001: native library 로딩은 신뢰된 환경을 전제로 한다

- 심각도: 중간
- 상태: 2026-06-14 문서화 완료
- 근거:
  - `bindings/java/src/main/java/systems/zlink/runtime/nativeapi/LibraryLoader.java:35-43`은 사용자가 지정한 경로를 `System.load`로 로드한다.
  - 같은 파일 `68-87`은 jar resource의 native library를 임시 디렉터리에 복사한 뒤 로드한다.
  - 같은 파일 `51-63`은 resource 로딩 실패 시 `System.loadLibrary("zlink")`로 fallback한다.
  - 같은 파일 `123-134`는 같은 디렉터리나 생성된 bridge 디렉터리에서 optional bridge library를 로드한다.
- 영향:
  - 공격자가 JVM 환경 변수, library 검색 경로, 작업 디렉터리, 임시 디렉터리를 제어할 수 있으면 원하지 않는 native library 로딩 위험이 있다.
  - 일반적인 서버 배포에서 실행 환경을 운영자가 관리한다면 기능 문제는 아니다.
  - 성능 영향은 시작 시 로딩 비용에 한정된다.
- 권장 조치:
  - 문서에 native library 경로 override와 fallback이 신뢰된 실행 환경을 전제로 한다고 명시한다.
  - resource 추출 파일의 권한과 디렉터리 소유권을 보수적으로 확인하는 방어를 추가할 수 있다.
- 처리 결과:
  - `bindings/java/README.javadoc.md`에 native library 로딩 순서와 `ZLINK_LIBRARY_PATH`의 신뢰 전제를 명시했다.
  - 권한이 높은 서비스에서는 신뢰할 수 없는 사용자가 환경 변수, JVM library 검색 경로, 작업 디렉터리, 임시 디렉터리를 제어하지 못하게 하라고 적었다.

### JAVA-BINDING-002: Windows 의존 DLL 검색은 환경 변수와 PATH에 의존한다

- 심각도: 중간
- 상태: 2026-06-14 문서화 완료
- 근거:
  - `bindings/java/src/main/java/systems/zlink/runtime/nativeapi/LibraryLoader.java:176-190`은 Windows 의존 DLL을 찾으면 `System.load`로 로드한다.
  - 같은 파일 `196-215`는 `ZLINK_OPENSSL_BIN`, `ZLINK_WINDOWS_RUNTIME_BIN`, 작업 디렉터리 기준 상대 경로, 잘 알려진 설치 경로, `PATH` 항목을 검색한다.
- 영향:
  - 공격자가 환경 변수나 `PATH`를 제어할 수 있는 프로세스에서는 원하지 않는 OpenSSL DLL 또는 runtime DLL을 먼저 로드할 수 있다.
  - 운영자가 실행 환경을 통제하는 일반 배포에서는 기능 문제가 아니다.
  - 성능 영향은 시작 시 검색 비용에 한정된다.
- 권장 조치:
  - 보안이 중요한 배포에서는 필요한 DLL을 native library와 같은 신뢰된 디렉터리에 배치한다.
  - 문서에 `PATH` 기반 검색은 개발 편의를 위한 fallback이며 신뢰된 실행 환경을 전제로 한다고 명시한다.
- 처리 결과:
  - `bindings/java/README.javadoc.md`에 Windows dependency lookup이 `ZLINK_OPENSSL_BIN`, `ZLINK_WINDOWS_RUNTIME_BIN`, 알려진 개발 설치 경로, `PATH`를 참고할 수 있음을 적었다.
  - 보안이 중요한 배포에서는 필요한 DLL을 zlink native library와 같은 신뢰된 디렉터리에 두고 소유권과 쓰기 권한을 관리하라고 적었다.

## 공개 API 경계 확인

- `bindings/java/src/main/java/module-info.java:1-13`은 contracts 패키지만 export한다.
- runtime, nativeapi, internal 패키지는 module export 대상이 아니다.
- 이 구조는 애플리케이션 코드가 내부 native handle을 직접 다루는 것을 줄인다.

## 기능·성능 검토

리소스에서 native library를 추출하는 경로는 패키지 사용성을 높인다. 시작 시 파일 복사, DLL 검색, 로딩 비용은 있지만, 일반 실행 중 메시지 송수신 성능에는 직접 영향을 주지 않는다.

검증:

- `cd bindings/java && ./gradlew test` 통과.
- Codex 에이전트 리뷰에서 "추가 이슈 없음" 판정을 받았다.

## 결론

Java 바인딩의 공개 API 경계는 적절하게 좁혀져 있다. 2026-06-14에 native library 로딩이 신뢰된 배포 환경을 전제로 한다는 점과, 보안이 중요한 배포에서 경로 override와 fallback 사용을 제한해야 한다는 점을 문서화했다.
