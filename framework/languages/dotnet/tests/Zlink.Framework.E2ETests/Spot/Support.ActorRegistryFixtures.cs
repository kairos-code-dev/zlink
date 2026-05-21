using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.E2ETests;

public abstract partial class SpotTestSupport
{
    public sealed class RegistryStageSpot(IZLinkSpotContext context)
        : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddActorJoin<RegistryStageJoinHandler, RegistryTestActor, RegistryJoinRequest, RegistryJoinReply>();
            Context.AddActorPacket<RegistryStageDispatchHandler, RegistryTestActor>("spot-dispatch");
            Context.AddActorJoined<RegistryStageJoinedHandler, RegistryTestActor>();
            Context.AddActorLeft<RegistryStageLeftHandler, RegistryTestActor>();
        }

        public async ValueTask<RegistryJoinReply> JoinAsync(
            RegistryTestActor actor,
            RegistryJoinRequest request,
            CancellationToken cancellationToken)
        {
            if (actor.Spot is RegistryStageSpot current && !ReferenceEquals(current, this))
            {
                await current.Context.LeaveActorAsync(actor, cancellationToken);
                actor.DetachSpot(current);
            }

            await Context.JoinActorAsync(actor, cancellationToken);
            actor.AttachSpot(this);
            actor.CurrentRoomId = request.RoomId;
            return new RegistryJoinReply(request.RoomId);
        }
    }

    public sealed class RegistryEntryJoinHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryEntrySpot, RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryEntrySpot entrySpot,
            RegistryTestActor actor,
            string spotRid,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            var reply = await actor.Context.JoinSpot(
                    global::Systems.Zlink.RoutingId.FromString(spotRid),
                    new RegistryJoinRequest("entry-room"))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<RegistryJoinReply>(cancellationToken);

            actor.CurrentRoomId = reply.RoomId;
            recorder.Events.Enqueue($"entry:{actor.ActorId}:{spotRid}");
        }
    }

    public sealed class EntrySpotBlockingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryEntrySpot, RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryEntrySpot entrySpot,
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            recorder.Events.Enqueue($"block-start:{actor.ActorId}:{message}");
            recorder.BlockingStarted.TrySetResult();
            await recorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            recorder.Events.Enqueue($"block-end:{actor.ActorId}:{message}");
        }
    }

    public sealed class EntrySpotJoinBlockingHandler(
        EntrySpotMailboxRecorder mailboxRecorder,
        EntrySpotActorRegistryRecorder registryRecorder)
        : IZLinkEntrySpotActorSendHandler<RegistryEntrySpot, RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryEntrySpot entrySpot,
            RegistryTestActor actor,
            string spotRid,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            mailboxRecorder.BlockingStarted.TrySetResult();
            await mailboxRecorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);

            var reply = await actor.Context.JoinSpot(
                    global::Systems.Zlink.RoutingId.FromString(spotRid),
                    new RegistryJoinRequest("entry-room"))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<RegistryJoinReply>(cancellationToken);

            actor.CurrentRoomId = reply.RoomId;
            registryRecorder.Events.Enqueue($"entry-block-joined:{actor.ActorId}:{spotRid}");
        }
    }

    public sealed class EntrySpotRecordingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryEntrySpot, RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryEntrySpot entrySpot,
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = cancellationToken;
            recorder.Events.Enqueue($"record:{actor.ActorId}:{message}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class LocalActorBlockingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkActorPacketHandler<RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            recorder.Events.Enqueue($"local-block-start:{actor.ActorId}:{message}");
            recorder.BlockingStarted.TrySetResult();
            await recorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            recorder.Events.Enqueue($"local-block-end:{actor.ActorId}:{message}");
        }
    }

    public sealed class LocalActorRecordingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkActorPacketHandler<RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"local-record:{actor.ActorId}:{message}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryStageJoinHandler
        : IZLinkSpotActorJoinHandler<RegistryStageSpot, RegistryTestActor, RegistryJoinRequest, RegistryJoinReply>
    {
        public ValueTask<RegistryJoinReply> HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            RegistryJoinRequest request,
            CancellationToken cancellationToken)
        {
            return spot.JoinAsync(actor, request, cancellationToken);
        }
    }

    public sealed class RegistryStageDispatchHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkSpotActorSendHandler<RegistryStageSpot, RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue(
                $"dispatch:{actor.ActorId}:{actor.CurrentRoomId}:{message}:{spot.Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryStageJoinedHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkSpotActorJoinedHandler<RegistryStageSpot, RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"joined:{actor.ActorId}:{spot.Context.SpotRid.ToHex()}");
            recorder.Events.Enqueue($"joined-kind:{actor.ActorId}:{info.Kind}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryStageLeftHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkSpotActorLeftHandler<RegistryStageSpot, RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"left:{actor.ActorId}:{spot.Context.SpotRid.ToHex()}");
            recorder.Events.Enqueue($"left-kind:{actor.ActorId}:{info.Kind}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryEntryJoinedHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkEntrySpotActorJoinedHandler<RegistryEntrySpot, RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryEntrySpot entrySpot,
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"entry-joined:{actor.ActorId}:{info.PreviousSpotRid?.ToHex()}");
            recorder.Events.Enqueue($"entry-joined-kind:{actor.ActorId}:{info.Kind}");
            recorder.Events.Enqueue($"entry-spot:{actor.ActorId}:{entrySpot.Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryEntryLeftHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkEntrySpotActorLeftHandler<RegistryEntrySpot, RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryEntrySpot entrySpot,
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"entry-left:{actor.ActorId}:{info.CurrentSpotRid?.ToHex()}");
            recorder.Events.Enqueue($"entry-left-kind:{actor.ActorId}:{info.Kind}");
            recorder.Events.Enqueue($"entry-spot:{actor.ActorId}:{entrySpot.Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed record RegistryJoinRequest(string RoomId);

    public sealed record RegistryJoinReply(string RoomId);

    public sealed class RegistryTestActor(
        string actorId,
        IZLinkActorContext context,
        EntrySpotActorRegistryRecorder recorder) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public EntrySpotActorRegistryRecorder Recorder { get; } = recorder;

        public RegistryStageSpot? Spot { get; private set; }

        public string? CurrentRoomId { get; set; }

        public void Configure()
        {
            Context.AddPacket<LocalActorBlockingHandler>("local-block");
            Context.AddPacket<LocalActorRecordingHandler>("local-record");
        }

        public void AttachSpot(RegistryStageSpot spot)
        {
            Spot = spot;
        }

        public void DetachSpot(RegistryStageSpot spot)
        {
            if (ReferenceEquals(Spot, spot))
            {
                Spot = null;
            }
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryTestActorFactory(
        EntrySpotActorRegistryRecorder recorder) : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(
                new RegistryTestActor(actorId, context, recorder));
        }
    }

    public sealed class EntrySpotActorRegistryRecorder
    {
        public ConcurrentQueue<string> Events { get; } = new();
    }

    public sealed class EntrySpotMailboxRecorder
    {
        public ConcurrentQueue<string> Events { get; } = new();

        public TaskCompletionSource BlockingStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseBlocking { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}
