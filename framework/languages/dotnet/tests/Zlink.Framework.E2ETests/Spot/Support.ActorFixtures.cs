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
    public sealed class ActorStageSpot : IZLinkSpot
    {
        private readonly ActorIntegrationRecorder _recorder;
        private int _inFlight;

        public ActorStageSpot(IZLinkSpotContext context, ActorIntegrationRecorder recorder)
        {
            Context = context;
            _recorder = recorder;
        }

        public IZLinkSpotContext Context { get; }

        public void Configure()
        {
            Context.AddActorJoin<ActorJoinHandler, TestActor, JoinStageRequest, JoinStageReply>();
            Context.AddPostActorJoined<ActorStageJoinedHandler, TestActor>();
            Context.AddActorLeft<ActorStageLeftHandler, TestActor>();
        }

        internal IDisposable EnterScope(string source)
        {
            if (Interlocked.Increment(ref _inFlight) != 1)
            {
                _recorder.ConcurrentViolation = true;
                _recorder.ScopeViolations.Enqueue(source);
            }

            return new ScopeLease(this);
        }

        internal async ValueTask<JoinStageReply> JoinActorAsync(
            TestActor actor,
            JoinStageRequest request,
            CancellationToken cancellationToken)
        {
            using var _ = EnterScope("join");

            if (actor.Spot is ActorStageSpot current && !ReferenceEquals(current, this))
            {
                await current.Context.LeaveActorAsync(actor, cancellationToken);
                actor.DetachSpot(current);
            }

            await Context.JoinActorAsync(actor, cancellationToken);
            actor.AttachSpot(this);
            actor.CurrentRoomId = request.RoomId;

            return new JoinStageReply(request.RoomId);
        }

        internal async ValueTask LeaveActorAsync(
            TestActor actor,
            CancellationToken cancellationToken)
        {
            using var _ = EnterScope("leave");

            await Context.LeaveActorAsync(actor, cancellationToken);
            actor.DetachSpot(this);
            actor.CurrentRoomId = null;
        }

        private sealed class ScopeLease(ActorStageSpot spot) : IDisposable
        {
            public void Dispose()
            {
                Interlocked.Decrement(ref spot._inFlight);
            }
        }
    }

    public sealed class ActorStageJoinedHandler(ActorIntegrationRecorder recorder)
        : IZLinkSpotPostActorJoinedHandler<ActorStageSpot, TestActor>
    {
        public ValueTask HandleAsync(
            ActorStageSpot spot,
            TestActor actor,
            ZLinkSpotActorLifecycleContext context,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            if (context.CurrentSpotRid is { } spotRid)
            {
                recorder.SpotActorJoins.Enqueue($"{actor.ActorId}@{spotRid.ToHex()}");
            }

            return ValueTask.CompletedTask;
        }
    }

    public sealed class ActorStageLeftHandler(ActorIntegrationRecorder recorder)
        : IZLinkSpotActorLeftHandler<ActorStageSpot, TestActor>
    {
        public ValueTask HandleAsync(
            ActorStageSpot spot,
            TestActor actor,
            ZLinkSpotActorLifecycleContext context,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            if (context.PreviousSpotRid is { } spotRid)
            {
                recorder.SpotActorLeaves.Enqueue($"{actor.ActorId}@{spotRid.ToHex()}");
            }

            return ValueTask.CompletedTask;
        }
    }

    public sealed record JoinStageRequest(string RoomId);

    public sealed record JoinStageReply(string RoomId);

    public sealed class ActorJoinHandler : IZLinkSpotActorJoinHandler<ActorStageSpot, TestActor, JoinStageRequest, JoinStageReply>
    {
        public ValueTask<JoinStageReply> HandleAsync(
            ActorStageSpot spot,
            TestActor actor,
            JoinStageRequest request,
            CancellationToken cancellationToken)
        {
            return spot.JoinActorAsync(actor, request, cancellationToken);
        }
    }

    public sealed class TestActor(
        string actorId,
        IZLinkActorContext context,
        ActorIntegrationRecorder recorder) : IZLinkActor
    {

        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public ActorIntegrationRecorder Recorder { get; } = recorder;

        public ActorStageSpot? Spot { get; private set; }

        public string? CurrentRoomId { get; set; }

        public void Configure()
        {
            Context.AddPacket<ActorJoinViaContextHandler>("join-via-context");
            Context.AddPacket<ActorDispatchHandler>("dispatch");
            Context.AddPacket<ActorDispatchHandler>("dispatch-after-context-join");
        }

        public void AttachSpot(ActorStageSpot spot)
        {
            Spot = spot;
        }

        public void DetachSpot(ActorStageSpot spot)
        {
            if (ReferenceEquals(Spot, spot))
            {
                Spot = null;
            }
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            Interlocked.Increment(ref Recorder.DisconnectCount);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class ActorJoinViaContextHandler
        : IZLinkActorPacketHandler<TestActor, string>
    {
        public async ValueTask HandleAsync(
            TestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            var reply = await actor.Context.JoinSpot(
                    global::Systems.Zlink.RoutingId.FromString(message),
                    new JoinStageRequest("room-context"))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<JoinStageReply>(cancellationToken);

            actor.CurrentRoomId = reply.Reply.RoomId;
        }
    }

    public sealed class ActorDispatchHandler
        : IZLinkActorPacketHandler<TestActor, string>
    {
        public ValueTask HandleAsync(
            TestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            var stageSpot = actor.Context.GetSpot<ActorStageSpot>();

            if (!ReferenceEquals(actor.Spot, stageSpot))
            {
                throw new InvalidOperationException("Actor context SPOT does not match actor SPOT.");
            }

            using var scope = stageSpot.EnterScope("dispatch");
            actor.Recorder.DispatchBodies.Enqueue(message);
            actor.Recorder.DispatchRooms.Enqueue(actor.CurrentRoomId ?? string.Empty);
            actor.Recorder.DispatchSpotRids.Enqueue(stageSpot.Context.SpotRid.ToHex());
            return ValueTask.CompletedTask;
        }
    }
}
