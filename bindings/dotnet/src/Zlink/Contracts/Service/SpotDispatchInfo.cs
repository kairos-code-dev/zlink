// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;

namespace Systems.Zlink;

/// <summary>
/// Represents spot dispatch info.
/// </summary>
public sealed class SpotDispatchInfo
{
    internal static readonly SpotDispatchInfo SubscribeReadableSpot = new(
        SpotDispatchEvent.SubscribeReadable,
        SpotDispatchSubjectKind.Spot,
        null,
        IntPtr.Zero,
        null);

    private readonly IntPtr _channelDealerSubject;
    private readonly Action<IntPtr>? _drainChannelReply;
    private int _actorMessageIndex;

    internal SpotDispatchInfo(SpotDispatchEvent @event,
        SpotDispatchSubjectKind subjectKind, IZlinkTimer? timer,
        IntPtr channelDealerSubject, Action<IntPtr>? drainChannelReply,
        IReadOnlyList<ActorReceived>? actorMessages = null)
    {
        Event = @event;
        SubjectKind = subjectKind;
        Timer = timer;
        _channelDealerSubject = channelDealerSubject;
        _drainChannelReply = drainChannelReply;
        ActorMessages = actorMessages ?? Array.Empty<ActorReceived>();
    }

    /// <summary>
    /// Gets or sets the event.
    /// </summary>
    public SpotDispatchEvent Event { get; }
    /// <summary>
    /// Gets or sets the subject kind.
    /// </summary>
    public SpotDispatchSubjectKind SubjectKind { get; }
    internal IntPtr Subject => _channelDealerSubject;
    /// <summary>
    /// Gets or sets the timer.
    /// </summary>
    public IZlinkTimer? Timer { get; }
    /// <summary>
    /// Gets or sets the actor messages.
    /// </summary>
    public IReadOnlyList<ActorReceived> ActorMessages { get; }

    /// <summary>
    /// Receives the next available item.
    /// </summary>
    /// <returns>The operation result.</returns>
    public ActorReceived? RecvActor()
    {
        int index = Interlocked.Increment(ref _actorMessageIndex) - 1;
        return index < ActorMessages.Count ? ActorMessages[index] : null;
    }

    /// <summary>
    /// Drains pending channel replies.
    /// </summary>
    public void DrainChannelReply()
    {
        if (_drainChannelReply == null || _channelDealerSubject == IntPtr.Zero)
        {
            throw new ZlinkConfigException(ConfigResult.InvalidArgument,
                (int)ErrorCode.EInval);
        }

        _drainChannelReply(_channelDealerSubject);
    }
}
