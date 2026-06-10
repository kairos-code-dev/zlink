export class ZLinkFrameworkException extends Error {
  constructor(
    public readonly kind: ZLinkFrameworkErrorKind,
    message: string,
    public readonly isRetriable = false,
    cause?: unknown
  ) {
    super(message, { cause });
    this.name = 'ZLinkFrameworkException';
  }
}

export enum ZLinkFrameworkErrorKind {
  ActorRouteNotFound = 'actorRouteNotFound',
  ActorCreateFailed = 'actorCreateFailed',
  ActorAlreadyExists = 'actorAlreadyExists',
  ActorTypeMismatch = 'actorTypeMismatch',
  SpotCreateFailed = 'spotCreateFailed',
  SpotRouteNotFound = 'spotRouteNotFound',
  SpotTypeMismatch = 'spotTypeMismatch',
  ActorSessionNotBound = 'actorSessionNotBound',
  HandlerNotFound = 'handlerNotFound',
  RouteHandlerNotFound = 'routeHandlerNotFound',
  ActorDispatchHandlerNotFound = 'actorDispatchHandlerNotFound',
  PayloadDecodeFailed = 'payloadDecodeFailed',
  RouteNotConnected = 'routeNotConnected',
  RequestTargetNotFound = 'requestTargetNotFound',
  RequestRejected = 'requestRejected',
  RequestProtocolError = 'requestProtocolError',
  RequestFailed = 'requestFailed'
}
