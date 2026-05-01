namespace Zlink.Framework;

public sealed class ZLinkFrameworkException : Exception
{
    public ZLinkFrameworkException(
        ZLinkFrameworkErrorKind kind,
        string message,
        bool isRetriable = false,
        Exception? innerException = null)
        : base(message, innerException)
    {
        Kind = kind;
        IsRetriable = isRetriable;
    }

    public ZLinkFrameworkErrorKind Kind { get; }

    public bool IsRetriable { get; }
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
    ActorRelayTimeout,
    ActorRelayHandlerFailed,
    CodecFailed,
}
