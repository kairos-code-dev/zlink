export class ZLinkFrameworkException extends Error {
  constructor(
    public readonly kind: ZLinkFrameworkErrorKind,
    message: string,
    isRetriable?: boolean,
    cause?: unknown
  ) {
    super(message, { cause });
    this.name = 'ZLinkFrameworkException';
    this.code = ZLINK_FRAMEWORK_ERROR_KIND_VALUES[kind];
    this.isRetriable = isRetriable ?? isZLinkFrameworkErrorRetriableByDefault(kind);
  }

  public readonly code: number;
  public readonly isRetriable: boolean;
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
  RequestFailed = 'requestFailed',
  WorkerQueueFull = 'workerQueueFull',
  WorkerTimedOut = 'workerTimedOut',
  WorkerFailed = 'workerFailed',
  ActorLocationStale = 'actorLocationStale',
  ActorCreateRejected = 'actorCreateRejected',
  ObjectClientNotConfigured = 'objectClientNotConfigured',
  MeshSelectionRequired = 'meshSelectionRequired',
  MeshNotFound = 'meshNotFound',
  InvalidConfiguration = 'invalidConfiguration',
  AlreadySubmitted = 'alreadySubmitted',
  ActorGenerationStale = 'actorGenerationStale',
  ActorMoving = 'actorMoving',
  DeadlineExceeded = 'deadlineExceeded',
  PlacementCapacityExhausted = 'placementCapacityExhausted',
  RoutingIdConflict = 'routingIdConflict',
  SpotGenerationStale = 'spotGenerationStale',
  SpotMoving = 'spotMoving',
  RelocationDataLost = 'relocationDataLost',
  SpotIdConflict = 'spotIdConflict',
  RuntimeShutdown = 'runtimeShutdown',
  RelocationDisabled = 'relocationDisabled',
  RelocationTargetUnavailable = 'relocationTargetUnavailable',
  RelocationFailed = 'relocationFailed',
  InvalidOperation = 'invalidOperation'
}

export const ZLINK_FRAMEWORK_ERROR_KIND_VALUES: Readonly<Record<ZLinkFrameworkErrorKind, number>> = Object.freeze({
  [ZLinkFrameworkErrorKind.ActorRouteNotFound]: 0,
  [ZLinkFrameworkErrorKind.ActorCreateFailed]: 1,
  [ZLinkFrameworkErrorKind.ActorAlreadyExists]: 2,
  [ZLinkFrameworkErrorKind.ActorTypeMismatch]: 3,
  [ZLinkFrameworkErrorKind.SpotCreateFailed]: 4,
  [ZLinkFrameworkErrorKind.SpotRouteNotFound]: 5,
  [ZLinkFrameworkErrorKind.SpotTypeMismatch]: 6,
  [ZLinkFrameworkErrorKind.ActorSessionNotBound]: 7,
  [ZLinkFrameworkErrorKind.HandlerNotFound]: 8,
  [ZLinkFrameworkErrorKind.RouteHandlerNotFound]: 9,
  [ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound]: 10,
  [ZLinkFrameworkErrorKind.PayloadDecodeFailed]: 11,
  [ZLinkFrameworkErrorKind.RouteNotConnected]: 12,
  [ZLinkFrameworkErrorKind.RequestTargetNotFound]: 13,
  [ZLinkFrameworkErrorKind.RequestRejected]: 14,
  [ZLinkFrameworkErrorKind.RequestProtocolError]: 15,
  [ZLinkFrameworkErrorKind.RequestFailed]: 16,
  [ZLinkFrameworkErrorKind.WorkerQueueFull]: 17,
  [ZLinkFrameworkErrorKind.WorkerTimedOut]: 18,
  [ZLinkFrameworkErrorKind.WorkerFailed]: 19,
  [ZLinkFrameworkErrorKind.ActorLocationStale]: 20,
  [ZLinkFrameworkErrorKind.ActorCreateRejected]: 21,
  [ZLinkFrameworkErrorKind.ObjectClientNotConfigured]: 22,
  [ZLinkFrameworkErrorKind.MeshSelectionRequired]: 23,
  [ZLinkFrameworkErrorKind.MeshNotFound]: 24,
  [ZLinkFrameworkErrorKind.InvalidConfiguration]: 25,
  [ZLinkFrameworkErrorKind.AlreadySubmitted]: 26,
  [ZLinkFrameworkErrorKind.ActorGenerationStale]: 27,
  [ZLinkFrameworkErrorKind.ActorMoving]: 28,
  [ZLinkFrameworkErrorKind.DeadlineExceeded]: 29,
  [ZLinkFrameworkErrorKind.PlacementCapacityExhausted]: 30,
  [ZLinkFrameworkErrorKind.RoutingIdConflict]: 31,
  [ZLinkFrameworkErrorKind.SpotGenerationStale]: 32,
  [ZLinkFrameworkErrorKind.SpotMoving]: 33,
  [ZLinkFrameworkErrorKind.RelocationDataLost]: 34,
  [ZLinkFrameworkErrorKind.SpotIdConflict]: 35,
  [ZLinkFrameworkErrorKind.RuntimeShutdown]: 36,
  [ZLinkFrameworkErrorKind.RelocationDisabled]: 37,
  [ZLinkFrameworkErrorKind.RelocationTargetUnavailable]: 38,
  [ZLinkFrameworkErrorKind.RelocationFailed]: 39,
  [ZLinkFrameworkErrorKind.InvalidOperation]: 40
});

export function isZLinkFrameworkErrorRetriableByDefault(kind: ZLinkFrameworkErrorKind): boolean {
  return kind === ZLinkFrameworkErrorKind.RouteNotConnected
    || kind === ZLinkFrameworkErrorKind.ActorLocationStale
    || kind === ZLinkFrameworkErrorKind.ActorMoving
    || kind === ZLinkFrameworkErrorKind.DeadlineExceeded
    || kind === ZLinkFrameworkErrorKind.PlacementCapacityExhausted
    || kind === ZLinkFrameworkErrorKind.SpotMoving
    || kind === ZLinkFrameworkErrorKind.RelocationTargetUnavailable
    || kind === ZLinkFrameworkErrorKind.RelocationFailed;
}
