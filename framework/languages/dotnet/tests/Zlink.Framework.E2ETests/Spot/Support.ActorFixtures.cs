using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.E2ETests;

public abstract partial class SpotTestSupport
{
    public sealed class ActorStageSpot : IZLinkSpot<TestActor>
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
            Context.Handlers.AddActorPacket<ActorDispatchHandler, TestActor>("dispatch");
            Context.Handlers.AddActorPacket<ActorDispatchHandler, TestActor>("dispatch-after-context-join");
        }

        public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            TestActor actor,
            Message request,
            CancellationToken cancellationToken)
        {
            var joinRequest = request.FromJson<JoinStageRequest>();
            var reply = await JoinActorAsync(actor, joinRequest, cancellationToken);
            return ZLinkSpotActorJoinResult.Accept(reply.ToJson());
        }

        public ValueTask OnJoinedActorAsync(
            TestActor actor,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            _recorder.SpotActorJoins.Enqueue($"{actor.ActorId}@{Context.SpotRid.ToHex()}");
            actor.AttachSpot(this);
            return ValueTask.CompletedTask;
        }

        public ValueTask OnLeaveActorAsync(
            TestActor actor,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            _recorder.SpotActorLeaves.Enqueue($"{actor.ActorId}@{Context.SpotRid.ToHex()}");
            actor.DetachSpot(this);
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectActorAsync(
            TestActor actor,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = cancellationToken;
            Interlocked.Increment(ref _recorder.DisconnectCount);
            return ValueTask.CompletedTask;
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

            actor.CurrentRoomId = request.RoomId;

            return new JoinStageReply(request.RoomId);
        }

        internal async ValueTask leaveActor(
            TestActor actor,
            CancellationToken cancellationToken)
        {
            using var _ = EnterScope("leave");

            await Context.leaveActor(actor, cancellationToken);
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

    public sealed class ActorLifecycleEntrySpot(
        IZLinkEntrySpotContext context,
        ActorIntegrationRecorder recorder) : IZLinkEntrySpot<TestActor>
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
        }

        public ValueTask OnCreateActorAsync(
            TestActor actor,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.EntrySpotActorCreates.Enqueue($"entry-created:{actor.ActorId}");
            return ValueTask.CompletedTask;
        }

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            TestActor actor,
            Message request,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = cancellationToken;
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(Message.From(request)));
        }

        public ValueTask OnJoinedActorAsync(
            TestActor actor,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.EntrySpotActorJoins.Enqueue($"entry-joined:{actor.ActorId}");
            return ValueTask.CompletedTask;
        }

        public ValueTask OnLeaveActorAsync(
            TestActor actor,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.EntrySpotActorLeaves.Enqueue($"entry-left:{actor.ActorId}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed record JoinStageRequest(string RoomId);

    public sealed record JoinStageReply(string RoomId);

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

        public bool DestroyAgainOnEntryLeft { get; set; }

        public void Configure()
        {
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

    }

    public sealed class ActorDispatchHandler
        : IZLinkSpotActorSendHandler<ActorStageSpot, TestActor, string>
    {
        public ValueTask HandleAsync(
            ActorStageSpot spot,
            TestActor actor,
            ZLinkSpotActorSendContext context,
            string message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = context;
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
