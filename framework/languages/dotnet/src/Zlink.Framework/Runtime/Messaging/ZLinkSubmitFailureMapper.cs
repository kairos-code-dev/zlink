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
            SubmitResult.NotFound => ZLinkFrameworkErrorKind.NotFound,
            SubmitResult.NotConnected => ZLinkFrameworkErrorKind.Unavailable,
            SubmitResult.NotAdmitted => ZLinkFrameworkErrorKind.Unavailable,
            //  Terminated는 대상 object의 context가 사라졌다는 뜻이다. 이 mapper는
            //  target object를 지정한 mesh submit에만 쓰이므로, 호출자 입장에서는
            //  대상이 없어진 것이다. 분류를 InternalFailure로 두면 없어진 global
            //  Spot ID로 보낸 request가 framework 내부 오류처럼 보인다.
            SubmitResult.Terminated => ZLinkFrameworkErrorKind.NotFound,
            _ => ZLinkFrameworkErrorKind.InternalFailure
        };
        return new ZLinkFrameworkException(
            kind,
            $"Mesh submit to {targetDescription} failed with result '{result}'.",
            retryAdvice: result is SubmitResult.NotConnected or SubmitResult.NotAdmitted
                ? ZLinkRetryAdvice.RetryAfterBackoff
                : ZLinkRetryAdvice.DoNotRetry);
    }
}
