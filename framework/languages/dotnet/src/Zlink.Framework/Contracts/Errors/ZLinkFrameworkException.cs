namespace Zlink.Framework.Contracts.Errors;

public sealed class ZLinkFrameworkException(
    ZLinkFrameworkErrorKind kind,
    string message,
    bool isRetriable = false,
    Exception? innerException = null)
    : Exception(message, innerException)
{
    public ZLinkFrameworkErrorKind Kind { get; } = kind;

    public bool IsRetriable { get; } = isRetriable;
}

public enum ZLinkFrameworkErrorKind
{
    ActorRouteNotFound,
    ActorCreateFailed,
    ActorAlreadyExists,
    ActorTypeMismatch,
    SpotCreateFailed,
    SpotRouteNotFound,
    SpotTypeMismatch,
    ActorSessionNotBound,
    HandlerNotFound,
    RouteHandlerNotFound,
    ActorDispatchHandlerNotFound,
    PayloadDecodeFailed,
    RouteNotConnected,
    RequestTargetNotFound,
    RequestRejected,
    RequestProtocolError,
    RequestFailed,
    WorkerQueueFull,
    WorkerTimedOut,
    WorkerFailed,
}
