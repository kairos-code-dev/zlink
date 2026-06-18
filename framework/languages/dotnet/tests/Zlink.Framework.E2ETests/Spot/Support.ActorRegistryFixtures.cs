using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.E2ETests;

public abstract partial class SpotTestSupport
{
    public sealed class RegistryStageSpot(
        IZLinkSpotContext context,
        EntrySpotActorRegistryRecorder recorder)
        : IZLinkSpot<RegistryTestActor>
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddActorPacket<RegistryStageDispatchHandler, RegistryTestActor>("spot-dispatch");
        }

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            RegistryTestActor actor,
            Message request,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            var joinRequest = request.FromJson<RegistryJoinRequest>();
            actor.CurrentRoomId = joinRequest.RoomId;
            return ValueTask.FromResult(
                ZLinkSpotActorJoinResult.Accept(new RegistryJoinReply(joinRequest.RoomId).ToJson()));
        }

        public ValueTask OnJoinedActorAsync(
            RegistryTestActor actor,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"joined:{actor.ActorId}:{Context.SpotRid.ToHex()}");
            actor.AttachSpot(this);
            return ValueTask.CompletedTask;
        }

        public ValueTask OnLeaveActorAsync(
            RegistryTestActor actor,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"left:{actor.ActorId}:{Context.SpotRid.ToHex()}");
            actor.DetachSpot(this);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryEntryJoinHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryEntrySpot, RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryEntrySpot entrySpot,
            RegistryTestActor actor,
            ZLinkSpotActorSendContext context,
            string spotRid,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            var reply = await actor.Context.JoinSpot(
                    global::Systems.Zlink.RoutingId.FromHex(spotRid),
                    new RegistryJoinRequest("entry-room").ToJson())
                .Timeout(TimeSpan.FromSeconds(5))
                .Async(cancellationToken);

            actor.CurrentRoomId = reply.Reply.FromJson<RegistryJoinReply>().RoomId;
            recorder.Events.Enqueue($"entry:{actor.ActorId}:{spotRid}");
        }
    }

    public sealed class EntrySpotBlockingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryEntrySpot, RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryEntrySpot entrySpot,
            RegistryTestActor actor,
            ZLinkSpotActorSendContext context,
            string message,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
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
            ZLinkSpotActorSendContext context,
            string spotRid,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            mailboxRecorder.BlockingStarted.TrySetResult();
            await mailboxRecorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);

            var reply = await actor.Context.JoinSpot(
                    global::Systems.Zlink.RoutingId.FromHex(spotRid),
                    new RegistryJoinRequest("entry-room").ToJson())
                .Timeout(TimeSpan.FromSeconds(5))
                .Async(cancellationToken);

            actor.CurrentRoomId = reply.Reply.FromJson<RegistryJoinReply>().RoomId;
            registryRecorder.Events.Enqueue($"entry-block-joined:{actor.ActorId}:{spotRid}");
        }
    }

    public sealed class EntrySpotRecordingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryEntrySpot, RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryEntrySpot entrySpot,
            RegistryTestActor actor,
            ZLinkSpotActorSendContext context,
            string message,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            _ = cancellationToken;
            recorder.Events.Enqueue($"record:{actor.ActorId}:{message}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class LocalActorBlockingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryEntrySpot, RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryEntrySpot entrySpot,
            RegistryTestActor actor,
            ZLinkSpotActorSendContext context,
            string message,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            recorder.Events.Enqueue($"local-block-start:{actor.ActorId}:{message}");
            recorder.BlockingStarted.TrySetResult();
            await recorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            recorder.Events.Enqueue($"local-block-end:{actor.ActorId}:{message}");
        }
    }

    public sealed class LocalActorRecordingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryEntrySpot, RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryEntrySpot entrySpot,
            RegistryTestActor actor,
            ZLinkSpotActorSendContext context,
            string message,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            _ = cancellationToken;
            recorder.Events.Enqueue($"local-record:{actor.ActorId}:{message}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryStageDispatchHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkSpotActorSendHandler<RegistryStageSpot, RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            ZLinkSpotActorSendContext context,
            string message,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            recorder.Events.Enqueue(
                $"dispatch:{actor.ActorId}:{actor.CurrentRoomId}:{message}:{spot.Context.SpotRid.ToHex()}");
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
