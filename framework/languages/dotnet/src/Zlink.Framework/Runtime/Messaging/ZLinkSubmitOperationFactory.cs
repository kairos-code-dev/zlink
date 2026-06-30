namespace Zlink.Framework.Runtime.Messaging;

internal sealed class ZLinkSubmitOperationFactory(
    TimeSpan? sendTimeout,
    Action wake)
{
    public DateTimeOffset? ResolveDeadline()
    {
        return sendTimeout is null
            ? null
            : DateTimeOffset.UtcNow.Add(sendTimeout.Value);
    }

    public PendingSubmit CreateCommand(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit)
    {
        return PendingSubmit.CreateCommand(parts, trySubmit, ResolveDeadlineOrThrow(parts), wake);
    }

    public PendingSubmit CreateRequest(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit,
        TaskCompletionSource<object?> completion)
    {
        return PendingSubmit.CreateRequest(parts, trySubmit, ResolveDeadlineOrThrow(parts), wake, completion);
    }

    private DateTimeOffset? ResolveDeadlineOrThrow(IReadOnlyList<Message> parts)
    {
        var deadline = ResolveDeadline();
        if (deadline is DateTimeOffset value && value <= DateTimeOffset.UtcNow)
        {
            ZLinkMessageParts.DisposeAll(parts);
            throw new TimeoutException("ZLink async submit timed out before the socket became writable.");
        }

        return deadline;
    }
}