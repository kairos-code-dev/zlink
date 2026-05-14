namespace Zlink.Framework.Runtime.Messaging;

internal static class ZLinkMessageParts
{
    public static IReadOnlyList<Message> Create(
        Message header,
        Message body)
    {
        return new TwoMessageParts(header, body);
    }

    public static void DisposeAll(IReadOnlyList<Message> parts)
    {
        for (var index = 0; index < parts.Count; index++)
        {
            parts[index].Dispose();
        }
    }

    private sealed class TwoMessageParts(
        Message header,
        Message body) : IReadOnlyList<Message>
    {
        public int Count => 2;

        public Message this[int index]
        {
            get
            {
                return index switch
                {
                    0 => header,
                    1 => body,
                    _ => throw new ArgumentOutOfRangeException(nameof(index))
                };
            }
        }

        public IEnumerator<Message> GetEnumerator()
        {
            yield return header;
            yield return body;
        }

        System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator()
        {
            return GetEnumerator();
        }
    }
}
