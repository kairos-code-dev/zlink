// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;

namespace Systems.Zlink;

public sealed class SpotDispatchInfo
{
    private readonly IntPtr _channelDealerSubject;
    private readonly Action<IntPtr>? _drainChannelReply;
    private int _actorPartIndex;

    internal SpotDispatchInfo(SpotDispatchEvent @event,
        SpotDispatchSubjectKind subjectKind, Timer? timer,
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
    public IntPtr Subject => _channelDealerSubject;
    public Timer? Timer { get; }
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
