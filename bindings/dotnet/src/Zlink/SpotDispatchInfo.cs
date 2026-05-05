// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;

namespace Zlink;

public sealed class SpotDispatchInfo
{
    private int _actorPartIndex;

    internal SpotDispatchInfo(SpotDispatchEvent @event,
        SpotDispatchSubjectKind subjectKind, IntPtr subject,
        IReadOnlyList<ActorPart>? actorParts = null)
    {
        Event = @event;
        SubjectKind = subjectKind;
        Subject = subject;
        ActorParts = actorParts ?? Array.Empty<ActorPart>();
    }

    public SpotDispatchEvent Event { get; }
    public SpotDispatchSubjectKind SubjectKind { get; }
    public IntPtr Subject { get; }
    public IReadOnlyList<ActorPart> ActorParts { get; }

    public ActorPart? RecvActorPart()
    {
        int index = Interlocked.Increment(ref _actorPartIndex) - 1;
        return index < ActorParts.Count ? ActorParts[index] : null;
    }
}
