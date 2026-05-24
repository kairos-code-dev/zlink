namespace Zlink.Framework.Runtime.Messaging;

internal static class ZLinkRequestFailureMapper
{
    public static Exception CreateCompletionException(
        RequestResult result,
        string operationName)
    {
        return result switch
        {
            RequestResult.TimedOut => new TimeoutException($"{operationName} timed out."),
            RequestResult.NotConnected => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RouteNotConnected,
                $"{operationName} failed because the target route is not connected.",
                isRetriable: true,
                CreateRequestException(result)),
            RequestResult.NotFound => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestTargetNotFound,
                $"{operationName} failed because the target was not found.",
                innerException: CreateRequestException(result)),
            RequestResult.Rejected or RequestResult.Conflict or RequestResult.Busy => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                $"{operationName} was rejected with result '{result}'.",
                isRetriable: result is RequestResult.Busy or RequestResult.Conflict,
                CreateRequestException(result)),
            RequestResult.ProtocolError => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestProtocolError,
                $"{operationName} failed with a protocol error.",
                innerException: CreateRequestException(result)),
            RequestResult.InvalidArgument or RequestResult.InvalidState or RequestResult.NotSupported
                or RequestResult.Terminated or RequestResult.InternalError => new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RequestFailed,
                    $"{operationName} failed with result '{result}'.",
                    innerException: CreateRequestException(result)),
            _ => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestFailed,
                $"{operationName} failed with result '{result}'.")
        };
    }

    public static Exception CreateSubmitException(
        ZlinkSubmitException error,
        string operationName)
    {
        return error.Result switch
        {
            ZlinkSubmitException.ErrorCode.NotConnected => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RouteNotConnected,
                $"{operationName} failed because the target route is not connected.",
                isRetriable: true,
                error),
            ZlinkSubmitException.ErrorCode.NotFound => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestTargetNotFound,
                $"{operationName} failed because the target was not found.",
                innerException: error),
            ZlinkSubmitException.ErrorCode.Backpressured => new TimeoutException(
                $"{operationName} timed out while the socket was backpressured.",
                error),
            ZlinkSubmitException.ErrorCode.NotAdmitted
                or ZlinkSubmitException.ErrorCode.InvalidState => new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RequestRejected,
                    $"{operationName} submit was rejected with result '{error.Result}'.",
                    innerException: error),
            ZlinkSubmitException.ErrorCode.InvalidArgument
                or ZlinkSubmitException.ErrorCode.InvalidHandle
                or ZlinkSubmitException.ErrorCode.NotSupported
                or ZlinkSubmitException.ErrorCode.ThreadViolation
                or ZlinkSubmitException.ErrorCode.OutOfMemory
                or ZlinkSubmitException.ErrorCode.SeqExhausted
                or ZlinkSubmitException.ErrorCode.Terminated
                or ZlinkSubmitException.ErrorCode.InternalError => new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RequestFailed,
                    $"{operationName} submit failed with result '{error.Result}'.",
                    innerException: error),
            _ => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestFailed,
                $"{operationName} submit failed with result '{error.Result}'.",
                innerException: error)
        };
    }

    public static Exception CreateSubmitTimeoutException(
        Exception? lastSubmitFailure,
        string operationName)
    {
        if (lastSubmitFailure is ZlinkSubmitException submitError)
        {
            return CreateSubmitException(submitError, operationName);
        }

        return lastSubmitFailure is null
            ? new TimeoutException($"{operationName} timed out before the socket became writable.")
            : new TimeoutException(
                $"{operationName} timed out before the socket became writable.",
                lastSubmitFailure);
    }

    private static ZlinkRequestException CreateRequestException(RequestResult result)
        => new((ZlinkRequestException.ErrorCode)(int)result);
}
