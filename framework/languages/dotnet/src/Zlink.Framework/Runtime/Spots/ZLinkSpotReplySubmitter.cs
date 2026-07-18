namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotReplySubmitter
{
    public static void SubmitAndDispose(
        ZLinkBackendRouteReceived received, IReadOnlyList<Message> replyParts)
    {
        try
        {
            if (received.CanReply)
                received.Reply(replyParts);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }
}
