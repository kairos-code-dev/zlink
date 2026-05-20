using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Messaging;

internal static class ZLinkRawReplyCompletion
{
    public static void Complete(
        RequestResult result,
        IReadOnlyList<Message> reply,
        Action<IReadOnlyList<Message>> complete,
        Action<Exception> fail,
        string failureMessage)
    {
        if (result == RequestResult.Ok)
        {
            complete(reply);
            return;
        }

        ZLinkMessageParts.DisposeAll(reply);
        fail(new TimeoutException(failureMessage));
    }

    public static void Complete(
        RequestResult result,
        IReadOnlyList<Message> reply,
        TaskCompletionSource<IReadOnlyList<Message>> completion,
        string failureMessage)
    {
        if (result == RequestResult.Ok)
        {
            completion.TrySetResult(reply);
            return;
        }

        ZLinkMessageParts.DisposeAll(reply);
        completion.TrySetException(new TimeoutException(failureMessage));
    }
}
