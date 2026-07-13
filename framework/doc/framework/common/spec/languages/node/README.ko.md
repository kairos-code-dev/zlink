# ZLink Framework Node.js 공개 계약

이 디렉토리는 Node.js와 TypeScript framework가 제공해야 하는 정식 public contract를
소유한다. package export, public declaration과 contract test는 이 계약을 따라야 한다.

전체 public interface, decorator, context, option과 client 시그니처의 기준은
[handler-interfaces](handler-interfaces.ko.md)다. 패키지 구조·배포와 등록 표면은
[system-structure](system-structure.ko.md)가 소유한다. client Stream Connector의 정확한 표면은
[stream-connector](stream-connector.ko.md)가 소유하며, 공통 동작은 상위 공통 spec을 따른다.

## 취소 인자

Node.js public interface는 일반 handler에 취소 인자를 자동으로 추가하지 않는다.
호출자가 중단할 수 있어야 하는 request 대기, 연결, 종료와 같은 장기 작업은 Node.js
관례에 따라 optional `AbortSignal`을 사용할 수 있다. 정확한 적용 대상은 언어별
interface 시그니처로 고정한다.
