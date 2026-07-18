namespace Zlink.Framework.Runtime.Messaging;

// Maps the binding submit result to the framework's typed error surface for
// the mesh submit paths. Ok/Backpressured are control flow (accept / retry on
// send-ready) and never reach this mapper.
internal static class ZLinkSubmitFailureMapper
{
    public static bool AcceptOrThrow(SubmitResult result, string targetDescription)
    {
        return result switch
        {
            SubmitResult.Ok => true,
            SubmitResult.Backpressured => false,
            _ => throw CreateException(result, targetDescription)
        };
    }

    public static ZLinkFrameworkException CreateException(
        SubmitResult result, string targetDescription)
    {
        var kind = result switch
        {
            SubmitResult.NotFound => ZLinkFrameworkErrorKind.RequestTargetNotFound,
            SubmitResult.NotConnected => ZLinkFrameworkErrorKind.RouteNotConnected,
            SubmitResult.NotAdmitted => ZLinkFrameworkErrorKind.RouteNotConnected,
            _ => ZLinkFrameworkErrorKind.RequestFailed
        };
        return new ZLinkFrameworkException(
            kind,
            $"Mesh submit to {targetDescription} failed with result '{result}'.",
            isRetriable: result is SubmitResult.NotConnected or SubmitResult.NotAdmitted);
    }
}
