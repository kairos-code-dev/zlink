"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLINK_FRAMEWORK_ERROR_KIND_VALUES = exports.ZLinkFrameworkErrorKind = exports.ZLinkFrameworkException = void 0;
exports.isZLinkFrameworkErrorRetriableByDefault = isZLinkFrameworkErrorRetriableByDefault;
class ZLinkFrameworkException extends Error {
    kind;
    constructor(kind, message, isRetriable, cause) {
        super(message, { cause });
        this.kind = kind;
        this.name = 'ZLinkFrameworkException';
        this.code = exports.ZLINK_FRAMEWORK_ERROR_KIND_VALUES[kind];
        this.isRetriable = isRetriable ?? isZLinkFrameworkErrorRetriableByDefault(kind);
    }
    code;
    isRetriable;
}
exports.ZLinkFrameworkException = ZLinkFrameworkException;
var ZLinkFrameworkErrorKind;
(function (ZLinkFrameworkErrorKind) {
    ZLinkFrameworkErrorKind["ActorRouteNotFound"] = "actorRouteNotFound";
    ZLinkFrameworkErrorKind["ActorCreateFailed"] = "actorCreateFailed";
    ZLinkFrameworkErrorKind["ActorAlreadyExists"] = "actorAlreadyExists";
    ZLinkFrameworkErrorKind["ActorTypeMismatch"] = "actorTypeMismatch";
    ZLinkFrameworkErrorKind["SpotCreateFailed"] = "spotCreateFailed";
    ZLinkFrameworkErrorKind["SpotRouteNotFound"] = "spotRouteNotFound";
    ZLinkFrameworkErrorKind["SpotTypeMismatch"] = "spotTypeMismatch";
    ZLinkFrameworkErrorKind["ActorSessionNotBound"] = "actorSessionNotBound";
    ZLinkFrameworkErrorKind["HandlerNotFound"] = "handlerNotFound";
    ZLinkFrameworkErrorKind["RouteHandlerNotFound"] = "routeHandlerNotFound";
    ZLinkFrameworkErrorKind["ActorDispatchHandlerNotFound"] = "actorDispatchHandlerNotFound";
    ZLinkFrameworkErrorKind["PayloadDecodeFailed"] = "payloadDecodeFailed";
    ZLinkFrameworkErrorKind["RouteNotConnected"] = "routeNotConnected";
    ZLinkFrameworkErrorKind["RequestTargetNotFound"] = "requestTargetNotFound";
    ZLinkFrameworkErrorKind["RequestRejected"] = "requestRejected";
    ZLinkFrameworkErrorKind["RequestProtocolError"] = "requestProtocolError";
    ZLinkFrameworkErrorKind["RequestFailed"] = "requestFailed";
    ZLinkFrameworkErrorKind["WorkerQueueFull"] = "workerQueueFull";
    ZLinkFrameworkErrorKind["WorkerTimedOut"] = "workerTimedOut";
    ZLinkFrameworkErrorKind["WorkerFailed"] = "workerFailed";
    ZLinkFrameworkErrorKind["ActorLocationStale"] = "actorLocationStale";
    ZLinkFrameworkErrorKind["ActorCreateRejected"] = "actorCreateRejected";
    ZLinkFrameworkErrorKind["ObjectClientNotConfigured"] = "objectClientNotConfigured";
    ZLinkFrameworkErrorKind["MeshSelectionRequired"] = "meshSelectionRequired";
    ZLinkFrameworkErrorKind["MeshNotFound"] = "meshNotFound";
    ZLinkFrameworkErrorKind["InvalidConfiguration"] = "invalidConfiguration";
    ZLinkFrameworkErrorKind["AlreadySubmitted"] = "alreadySubmitted";
    ZLinkFrameworkErrorKind["ActorGenerationStale"] = "actorGenerationStale";
    ZLinkFrameworkErrorKind["ActorMoving"] = "actorMoving";
    ZLinkFrameworkErrorKind["DeadlineExceeded"] = "deadlineExceeded";
    ZLinkFrameworkErrorKind["PlacementCapacityExhausted"] = "placementCapacityExhausted";
    ZLinkFrameworkErrorKind["RoutingIdConflict"] = "routingIdConflict";
    ZLinkFrameworkErrorKind["SpotGenerationStale"] = "spotGenerationStale";
    ZLinkFrameworkErrorKind["SpotMoving"] = "spotMoving";
    ZLinkFrameworkErrorKind["RelocationDataLost"] = "relocationDataLost";
    ZLinkFrameworkErrorKind["SpotIdConflict"] = "spotIdConflict";
    ZLinkFrameworkErrorKind["RuntimeShutdown"] = "runtimeShutdown";
    ZLinkFrameworkErrorKind["RelocationDisabled"] = "relocationDisabled";
    ZLinkFrameworkErrorKind["RelocationTargetUnavailable"] = "relocationTargetUnavailable";
    ZLinkFrameworkErrorKind["RelocationFailed"] = "relocationFailed";
})(ZLinkFrameworkErrorKind || (exports.ZLinkFrameworkErrorKind = ZLinkFrameworkErrorKind = {}));
exports.ZLINK_FRAMEWORK_ERROR_KIND_VALUES = Object.freeze({
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
    [ZLinkFrameworkErrorKind.RelocationFailed]: 39
});
function isZLinkFrameworkErrorRetriableByDefault(kind) {
    return kind === ZLinkFrameworkErrorKind.RouteNotConnected
        || kind === ZLinkFrameworkErrorKind.ActorLocationStale
        || kind === ZLinkFrameworkErrorKind.ActorMoving
        || kind === ZLinkFrameworkErrorKind.DeadlineExceeded
        || kind === ZLinkFrameworkErrorKind.PlacementCapacityExhausted
        || kind === ZLinkFrameworkErrorKind.SpotMoving
        || kind === ZLinkFrameworkErrorKind.RelocationTargetUnavailable
        || kind === ZLinkFrameworkErrorKind.RelocationFailed;
}
