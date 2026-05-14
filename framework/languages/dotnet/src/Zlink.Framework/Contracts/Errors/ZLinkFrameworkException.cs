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
    ActorNotAuthenticated,
    ActorRouteNotFound,
    ActorCreateFailed,
    ActorAlreadyExists,
    ActorSessionNotBound,
    SessionRouteNotFound,
    SessionLocationUpdateFailed,
    SessionProxyTimeout,
    ActorDispatchTimeout,
    ActorDispatchHandlerFailed,
    CodecFailed,
}
