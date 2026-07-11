# ZLink Framework C++ 공개 계약

이 디렉토리는 C++ framework가 제공해야 하는 정식 public contract를 소유한다.
public header와 contract test는 이 계약을 따라야 한다.

전체 public surface의 기준은
[cpp-framework-interfaces](cpp-framework-interfaces.ko.md)이며, handler 정렬 규칙은
[handler-interfaces](handler-interfaces.ko.md)에서 설명한다. 기능별 계약은 같은
디렉토리의 channel, Spot, stream, registry와 monitoring 문서에서 설명한다.

## 취소 인자

C++ public interface에는 `.NET` 모양을 옮긴 custom cancellation token을 기본 callback
인자로 두지 않는다. 중단 가능한 장기 작업에 명시적 중단 전달이 필요하면 C++ 표준
수명과 중단 관례를 사용한다. timeout, host shutdown, RAII cleanup과 coroutine 수명은
각 기능 계약을 따른다.
