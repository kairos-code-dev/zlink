namespace Zlink.Framework.Runtime.Messaging;

internal static class ZLinkMeshCallSupport
{
    /// <summary>Maps a failed first admission attempt onto the async submit
    /// status; unmapped kinds stay exceptions.</summary>
    public static bool TryMapSubmitFailure(
        ZLinkFrameworkException failure, out ZLinkSubmitResult result)
    {
        ZLinkSubmitStatus? status = failure.Kind switch
        {
            ZLinkFrameworkErrorKind.RequestTargetNotFound => ZLinkSubmitStatus.TargetNotFound,
            ZLinkFrameworkErrorKind.ActorRouteNotFound => ZLinkSubmitStatus.TargetNotFound,
            ZLinkFrameworkErrorKind.ActorLocationStale => ZLinkSubmitStatus.TargetNotFound,
            ZLinkFrameworkErrorKind.RouteNotConnected => ZLinkSubmitStatus.RouteNotConnected,
            _ => null
        };
        result = status is { } mapped ? new ZLinkSubmitResult(mapped) : default;
        return status is not null;
    }
}
