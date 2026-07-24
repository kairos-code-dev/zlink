namespace Zlink.Framework.Contracts.Errors;

public sealed class ZLinkFrameworkException(
    ZLinkFrameworkErrorKind kind,
    string message,
    bool? isRetriable = null,
    Exception? innerException = null)
    : Exception(message, innerException)
{
    public ZLinkFrameworkErrorKind Kind { get; } = kind;

    public bool IsRetriable { get; } = isRetriable ?? IsRetriableByDefault(kind);

    private static bool IsRetriableByDefault(ZLinkFrameworkErrorKind kind)
    {
        // The public failure contract keeps retry policy with the error
        // kind so callers do not need per-surface tables. Only connection
        // readiness and stale location races are useful bounded-retry cases.
        return kind is ZLinkFrameworkErrorKind.RouteNotConnected
            or ZLinkFrameworkErrorKind.ActorLocationStale
            or ZLinkFrameworkErrorKind.ActorMoving
            or ZLinkFrameworkErrorKind.DeadlineExceeded
            or ZLinkFrameworkErrorKind.PlacementCapacityExhausted
            or ZLinkFrameworkErrorKind.SpotMoving;
    }
}

public enum ZLinkFrameworkErrorKind
{
    ActorRouteNotFound = 0,
    ActorCreateFailed = 1,
    ActorAlreadyExists = 2,
    ActorTypeMismatch = 3,
    SpotCreateFailed = 4,
    SpotRouteNotFound = 5,
    SpotTypeMismatch = 6,
    ActorSessionNotBound = 7,
    HandlerNotFound = 8,
    RouteHandlerNotFound = 9,
    ActorDispatchHandlerNotFound = 10,
    PayloadDecodeFailed = 11,
    RouteNotConnected = 12,
    RequestTargetNotFound = 13,
    RequestRejected = 14,
    RequestProtocolError = 15,
    RequestFailed = 16,
    WorkerQueueFull = 17,
    WorkerTimedOut = 18,
    WorkerFailed = 19,
    ActorLocationStale = 20,
    ActorCreateRejected = 21,
    ObjectClientNotConfigured = 22,
    MeshSelectionRequired = 23,
    MeshNotFound = 24,
    InvalidConfiguration = 25,
    AlreadySubmitted = 26,
    ActorGenerationStale = 27,
    ActorMoving = 28,
    DeadlineExceeded = 29,
    PlacementCapacityExhausted = 30,
    RoutingIdConflict = 31,
    SpotGenerationStale = 32,
    SpotMoving = 33,
    RelocationDataLost = 34,
    SpotIdConflict = 35
}
