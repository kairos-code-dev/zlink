namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotReplySubmitter
{
    public static void SubmitAndDispose(Received received, IReadOnlyList<Message> replyParts)
    {
        try
        {
            received.Reply()
                .Message(replyParts[0])
                .Message(replyParts[1])
                .Submit();
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }
}
