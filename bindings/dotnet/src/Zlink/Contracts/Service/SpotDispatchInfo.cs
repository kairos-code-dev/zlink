// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;

namespace Systems.Zlink;

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
    private int _actorPartIndex;

    internal SpotDispatchInfo(SpotDispatchEvent @event,
        SpotDispatchSubjectKind subjectKind, IZlinkTimer? timer,
        IntPtr channelDealerSubject, Action<IntPtr>? drainChannelReply,
        IReadOnlyList<ActorPart>? actorParts = null)
    {
        Event = @event;
        SubjectKind = subjectKind;
        Timer = timer;
        _channelDealerSubject = channelDealerSubject;
        _drainChannelReply = drainChannelReply;
        ActorParts = actorParts ?? Array.Empty<ActorPart>();
    }

    public SpotDispatchEvent Event { get; }
    public SpotDispatchSubjectKind SubjectKind { get; }
    internal IntPtr Subject => _channelDealerSubject;
    public IZlinkTimer? Timer { get; }
    public IReadOnlyList<ActorPart> ActorParts { get; }

    public ActorPart? RecvActorPart()
    {
        int index = Interlocked.Increment(ref _actorPartIndex) - 1;
        return index < ActorParts.Count ? ActorParts[index] : null;
    }

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
