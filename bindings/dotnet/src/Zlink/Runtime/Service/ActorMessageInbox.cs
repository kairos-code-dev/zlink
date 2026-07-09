// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal sealed class ActorMessageInbox
{
    private readonly Dictionary<ActorRef, PendingActorMessage> _pending = new();

    internal PendingActorMessage? Take(ActorRef actor)
    {
        lock (_pending)
        {
            if (!_pending.Remove(actor, out var pending))
                return null;
            return pending;
        }
    }

    internal void Store(
        ActorRef actor,
        ActorRecvInfo info,
        List<Message> parts)
    {
        lock (_pending)
        {
            if (_pending.Remove(actor, out var existing))
                existing.Dispose();
            _pending[actor] = new PendingActorMessage(info, parts);
        }
    }

    internal void Clear()
    {
        lock (_pending)
        {
            foreach (var pending in _pending.Values)
                pending.Dispose();
            _pending.Clear();
        }
    }

    internal sealed class PendingActorMessage(
        ActorRecvInfo info,
        List<Message> parts) : IDisposable
    {
        internal ActorRecvInfo Info { get; } = info;

        internal List<Message> Parts { get; } = parts;

        public void Dispose()
        {
            RequestReplySupport.DisposeParts(Parts);
        }
    }
}
