namespace Zlink.Framework.Messaging;

internal static class ZLinkMessageParts
{
    public static void DisposeAll(IReadOnlyList<Message> parts)
    {
        foreach (var part in parts)
        {
            part.Dispose();
        }
    }
}
