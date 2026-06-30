// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public sealed partial class SpotDispatchInfo
{
    internal static readonly SpotDispatchInfo SubscribeReadableSpot = new(
        SpotDispatchEvent.SubscribeReadable,
        SpotDispatchSubjectKind.Spot,
        null,
        IntPtr.Zero,
        null);

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
        Subject = channelDealerSubject;
        _drainChannelReply = drainChannelReply;
        ActorMessages = actorMessages ?? Array.Empty<ActorReceived>();
    }

    internal IntPtr Subject { get; }

    private ActorReceived? RecvActorCore()
    {
        var index = Interlocked.Increment(ref _actorMessageIndex) - 1;
        return index < ActorMessages.Count ? ActorMessages[index] : null;
    }

    private void DrainChannelReplyCore()
    {
        if (_drainChannelReply == null || Subject == IntPtr.Zero)
            throw new ZlinkConfigException(ConfigResult.InvalidArgument,
                (int)ErrorCode.EInval);

        _drainChannelReply(Subject);
    }
}