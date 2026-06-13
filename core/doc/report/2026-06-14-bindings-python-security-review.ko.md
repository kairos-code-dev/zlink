# Python 바인딩 보안 검토 보고서

- 작성일: 2026-06-14
- 대상 범위: `bindings/python/src/zlink/_native/ffi.py`, `bindings/python/src/zlink/_runtime/handles/native_support.py`
- 검토 방식: ctypes library 로딩, Windows 의존 DLL 검색, native 메시지 버퍼 view 수명을 코드 기준으로 확인했다.
- 상태: 주의 항목 2건

## 요약

Python 바인딩은 `ctypes`로 core native library를 호출한다. 그래서 native library 로딩 경로와 Python 객체가 native buffer를 얼마나 오래 참조하는지가 핵심 위험이다.

검토 결과 일반 사용에서 즉시 깨지는 기능 문제는 확인되지 않았다. 다만 Windows 의존 DLL 검색이 환경 변수와 PATH를 사용하고, receive 결과의 `memoryview`가 native part owner의 수명에 의존한다.

## 확인된 이슈

### PYTHON-BINDING-001: Windows 의존 DLL 검색은 신뢰된 환경을 전제로 한다

- 심각도: 중간
- 근거:
  - `bindings/python/src/zlink/_native/ffi.py:1736-1750`은 library 디렉터리, `ZLINK_OPENSSL_BIN`, `OPENSSL_BIN`, `PATH`를 검색한다.
  - 같은 파일 `1762-1774`는 찾은 DLL 경로를 `os.add_dll_directory`와 `ctypes.CDLL`로 등록·로드한다.
- 영향:
  - 공격자가 환경 변수나 PATH를 제어하는 Windows 프로세스에서는 원하지 않는 OpenSSL DLL을 먼저 로드할 수 있다.
  - 일반 개발 환경에서는 편의 기능이지만, 서비스 배포에서는 환경을 신뢰할 수 있어야 한다.
  - 성능 영향은 초기 로딩 시 검색 비용에 한정된다.
- 권장 조치:
  - 문서에 Windows DLL 검색 경로는 신뢰된 환경에서만 사용한다고 명시한다.
  - 보안이 중요한 배포에서는 library 디렉터리에 필요한 DLL을 함께 배치하고 PATH 검색 의존을 줄인다.

### PYTHON-BINDING-002: native receive buffer view는 owner 수명에 의존한다

- 심각도: 낮음
- 근거:
  - `bindings/python/src/zlink/_runtime/handles/native_support.py:500-521`은 native multipart owner가 열린 동안 native 메시지 data를 `memoryview`로 반환한다.
  - 같은 파일 `526-539`는 part close 또는 owner close 시 native multipart를 닫고 내부 상태를 closed로 바꾼다.
  - `bindings/python/src/zlink/_runtime/handles/native_support.py:542-590`에는 bytes 기반 owner도 있어, 복사본을 소유하는 경로는 안전하게 분리되어 있다.
- 영향:
  - 호출자가 native buffer view를 보관한 뒤 owner가 닫히면 view가 이미 닫힌 native memory를 가리킬 수 있다.
  - 정상 API가 owner 수명을 함께 관리한다면 기능 문제는 드러나지 않는다.
  - 복사를 피하는 경로라 성능에는 유리하지만, 수명 규칙을 문서와 테스트로 고정해야 한다.
- 권장 조치:
  - public API에서 반환되는 view의 수명 규칙을 명확히 문서화한다.
  - 안전성을 우선하는 API에는 `bytes` 복사본을 반환하는 경로를 기본으로 유지한다.

## 기능·성능 검토

`ctypes.string_at`을 사용하는 bytes 변환은 복사본을 만들어 native 수명 문제를 줄인다. 반대로 `memoryview` 경로는 복사를 줄여 성능에는 유리하지만, owner close 이후 사용하면 위험해질 수 있다.

## 결론

Python 바인딩은 동작 편의성과 zero-copy 성격의 view를 함께 제공한다. 보안이 중요한 배포에서는 DLL 검색 경로를 제한하고, native view의 수명 규칙을 테스트와 문서로 고정해야 한다.
