// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     Describes a single spot dispatch event and the actor messages or channel
///     reply it carries.
/// </summary>
public sealed partial class SpotDispatchInfo
{
    /// <summary>
    ///     Gets the event.
    /// </summary>
    public SpotDispatchEvent Event { get; }

    /// <summary>
    ///     Gets the subject kind.
    /// </summary>
    public SpotDispatchSubjectKind SubjectKind { get; }

    /// <summary>
    ///     Gets the timer.
    /// </summary>
    public IZlinkTimer? Timer { get; }

    /// <summary>
    ///     Gets the actor messages.
    /// </summary>
    public IReadOnlyList<ActorReceived> ActorMessages { get; }

    /// <summary>
    ///     Pops the next actor message carried by this event, advancing an internal
    ///     cursor; returns null once all messages have been taken. Thread-safe.
    /// </summary>
    public ActorReceived? RecvActor()
    {
        return RecvActorCore();
    }

    /// <summary>
    ///     Drains pending channel replies.
    /// </summary>
    public void DrainChannelReply()
    {
        DrainChannelReplyCore();
    }
}