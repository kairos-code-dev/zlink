# ZLink Framework Node.js 공개 계약

이 디렉토리는 Node.js와 TypeScript framework가 제공해야 하는 정식 public contract를
소유한다. package export, public declaration과 contract test는 이 계약을 따라야 한다.

전체 public interface, decorator, context, option과 client 시그니처의 기준은
[handler-interfaces](handler-interfaces.ko.md)다. 기능별 계약은 같은 디렉토리의
NestJS, actor, stream, registry와 monitoring 문서에서 설명한다.

## 취소 인자

Node.js public interface는 일반 handler에 취소 인자를 자동으로 추가하지 않는다.
호출자가 중단할 수 있어야 하는 request 대기, 연결, 종료와 같은 장기 작업은 Node.js
관례에 따라 optional `AbortSignal`을 사용할 수 있다. 정확한 적용 대상은 언어별
interface 시그니처로 고정한다.
