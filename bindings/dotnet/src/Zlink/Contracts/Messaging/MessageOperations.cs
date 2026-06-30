// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     Extension methods that add several message parts to a send, request, reply,
///     or actor-join builder in one call.
/// </summary>
public static class MessageOperations
{
    /// <summary>
    ///     Adds <paramref name="messages" /> as parts, in order. The parts are
    ///     consumed on a successful submit; see <see cref="SendOperation" /> for the
    ///     ownership contract.
    /// </summary>
    /// <returns>The same builder, for chaining further parts, flags, or submit.</returns>
    public static SendSubmitOperation Messages(
        this SendOperation operation,
        IReadOnlyList<Message> messages)
    {
        EnsureNotEmpty(messages);
        return operation.Message(messages[0]).Messages(messages, 1);
    }

    /// <summary>
    ///     Adds <paramref name="messages" /> as parts, in order. The parts are
    ///     consumed on a successful submit; see <see cref="SendOperation" /> for the
    ///     ownership contract.
    /// </summary>
    /// <returns>The same builder, for chaining further parts, flags, or submit.</returns>
    public static SendSubmitOperation Messages(
        this SendSubmitOperation operation,
        IReadOnlyList<Message> messages)
    {
        EnsureNotEmpty(messages);
        return operation.Messages(messages, 0);
    }

    /// <summary>
    ///     Adds <paramref name="messages" /> as parts, in order. The parts are
    ///     consumed on a successful submit; see <see cref="SendOperation" /> for the
    ///     ownership contract.
    /// </summary>
    /// <returns>The same builder, for chaining further parts, flags, or submit.</returns>
    public static RequestSubmitOperation Messages(
        this RequestOperation operation,
        IReadOnlyList<Message> messages)
    {
        EnsureNotEmpty(messages);
        return operation.Message(messages[0]).Messages(messages, 1);
    }

    /// <summary>
    ///     Adds <paramref name="messages" /> as parts, in order. The parts are
    ///     consumed on a successful submit; see <see cref="SendOperation" /> for the
    ///     ownership contract.
    /// </summary>
    /// <returns>The same builder, for chaining further parts, flags, or submit.</returns>
    public static RequestSubmitOperation Messages(
        this RequestSubmitOperation operation,
        IReadOnlyList<Message> messages)
    {
        EnsureNotEmpty(messages);
        return operation.Messages(messages, 0);
    }

    /// <summary>
    ///     Adds <paramref name="messages" /> as parts, in order. The parts are
    ///     consumed on a successful submit; see <see cref="SendOperation" /> for the
    ///     ownership contract.
    /// </summary>
    /// <returns>The same builder, for chaining further parts, flags, or submit.</returns>
    public static RequestCallbackSubmitOperation Messages(
        this RequestCallbackSubmitOperation operation,
        IReadOnlyList<Message> messages)
    {
        EnsureNotEmpty(messages);
        return operation.Messages(messages, 0);
    }

    /// <summary>
    ///     Adds <paramref name="messages" /> as parts, in order. The parts are
    ///     consumed on a successful submit; see <see cref="SendOperation" /> for the
    ///     ownership contract.
    /// </summary>
    /// <returns>The same builder, for chaining further parts, flags, or submit.</returns>
    public static ReplySubmitOperation Messages(
        this ReplyOperation operation,
        IReadOnlyList<Message> messages)
    {
        EnsureNotEmpty(messages);
        return operation.Message(messages[0]).Messages(messages, 1);
    }

    /// <summary>
    ///     Adds <paramref name="messages" /> as parts, in order. The parts are
    ///     consumed on a successful submit; see <see cref="SendOperation" /> for the
    ///     ownership contract.
    /// </summary>
    /// <returns>The same builder, for chaining further parts, flags, or submit.</returns>
    public static ReplySubmitOperation Messages(
        this ReplySubmitOperation operation,
        IReadOnlyList<Message> messages)
    {
        EnsureNotEmpty(messages);
        return operation.Messages(messages, 0);
    }

    /// <summary>
    ///     Adds <paramref name="messages" /> as parts, in order. The parts are
    ///     consumed on a successful submit; see <see cref="SendOperation" /> for the
    ///     ownership contract.
    /// </summary>
    /// <returns>The same builder, for chaining further parts, flags, or submit.</returns>
    public static ActorJoinSubmitOperation Messages(
        this ActorJoinOperation operation,
        IReadOnlyList<Message> messages)
    {
        EnsureNotEmpty(messages);
        return operation.Message(messages[0]).Messages(messages, 1);
    }

    /// <summary>
    ///     Adds <paramref name="messages" /> as parts, in order. The parts are
    ///     consumed on a successful submit; see <see cref="SendOperation" /> for the
    ///     ownership contract.
    /// </summary>
    /// <returns>The same builder, for chaining further parts, flags, or submit.</returns>
    public static ActorJoinSubmitOperation Messages(
        this ActorJoinSubmitOperation operation,
        IReadOnlyList<Message> messages)
    {
        EnsureNotEmpty(messages);
        return operation.Messages(messages, 0);
    }

    /// <summary>
    ///     Adds <paramref name="messages" /> as parts, in order. The parts are
    ///     consumed on a successful submit; see <see cref="SendOperation" /> for the
    ///     ownership contract.
    /// </summary>
    /// <returns>The same builder, for chaining further parts, flags, or submit.</returns>
    public static ActorJoinCallbackSubmitOperation Messages(
        this ActorJoinCallbackSubmitOperation operation,
        IReadOnlyList<Message> messages)
    {
        EnsureNotEmpty(messages);
        return operation.Messages(messages, 0);
    }

    /// <summary>
    ///     Adds <paramref name="messages" /> as parts, in order. The parts are
    ///     consumed on a successful submit; see <see cref="SendOperation" /> for the
    ///     ownership contract.
    /// </summary>
    /// <returns>The same builder, for chaining further parts, flags, or submit.</returns>
    public static ActorJoinReplyOperation Messages(
        this ActorJoinReplyOperation operation,
        IReadOnlyList<Message> messages)
    {
        EnsureNotEmpty(messages);
        foreach (var message in messages) operation = operation.Message(message);

        return operation;
    }

    private static SendSubmitOperation Messages(
        this SendSubmitOperation operation,
        IReadOnlyList<Message> messages,
        int startIndex)
    {
        for (var index = startIndex; index < messages.Count; index++) operation = operation.Message(messages[index]);

        return operation;
    }

    private static RequestSubmitOperation Messages(
        this RequestSubmitOperation operation,
        IReadOnlyList<Message> messages,
        int startIndex)
    {
        for (var index = startIndex; index < messages.Count; index++) operation = operation.Message(messages[index]);

        return operation;
    }

    private static RequestCallbackSubmitOperation Messages(
        this RequestCallbackSubmitOperation operation,
        IReadOnlyList<Message> messages,
        int startIndex)
    {
        for (var index = startIndex; index < messages.Count; index++) operation = operation.Message(messages[index]);

        return operation;
    }

    private static ReplySubmitOperation Messages(
        this ReplySubmitOperation operation,
        IReadOnlyList<Message> messages,
        int startIndex)
    {
        for (var index = startIndex; index < messages.Count; index++) operation = operation.Message(messages[index]);

        return operation;
    }

    private static ActorJoinSubmitOperation Messages(
        this ActorJoinSubmitOperation operation,
        IReadOnlyList<Message> messages,
        int startIndex)
    {
        for (var index = startIndex; index < messages.Count; index++) operation = operation.Message(messages[index]);

        return operation;
    }

    private static ActorJoinCallbackSubmitOperation Messages(
        this ActorJoinCallbackSubmitOperation operation,
        IReadOnlyList<Message> messages,
        int startIndex)
    {
        for (var index = startIndex; index < messages.Count; index++) operation = operation.Message(messages[index]);

        return operation;
    }

    private static void EnsureNotEmpty(IReadOnlyList<Message> messages)
    {
        ArgumentNullException.ThrowIfNull(messages);
        if (messages.Count == 0) throw new ArgumentException("At least one message is required.", nameof(messages));
    }
}