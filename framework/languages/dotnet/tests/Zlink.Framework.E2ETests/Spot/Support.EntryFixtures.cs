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
    public sealed class RegistryEntrySpot(
        IZLinkEntrySpotContext context,
        EntrySpotActorRegistryRecorder recorder) : IZLinkEntrySpot<RegistryTestActor>
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddActorPacket<RegistryEntryJoinHandler, RegistryTestActor>("entry-join");
            Context.Handlers.AddActorPacket<EntrySpotJoinBlockingHandler, RegistryTestActor>("entry-join-block");
            Context.Handlers.AddActorPacket<EntrySpotBlockingHandler, RegistryTestActor>("entry-block");
            Context.Handlers.AddActorPacket<EntrySpotRecordingHandler, RegistryTestActor>("entry-record");
            Context.Handlers.AddActorPacket<LocalActorBlockingHandler, RegistryTestActor>("local-block");
            Context.Handlers.AddActorPacket<LocalActorRecordingHandler, RegistryTestActor>("local-record");
        }

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            RegistryTestActor actor,
            Message request,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = cancellationToken;
            if (request.GetString() == "reject-entry")
            {
                return ValueTask.FromResult(
                    ZLinkSpotActorJoinResult.Reject(Message.From("entry-reject:reject-entry")));
            }

            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(Message.From(request)));
        }

        public ValueTask OnJoinedActorAsync(
            RegistryTestActor actor,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"entry-joined:{actor.ActorId}");
            recorder.Events.Enqueue($"entry-spot:{actor.ActorId}:{Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }

        public ValueTask OnLeaveActorAsync(
            RegistryTestActor actor,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"entry-left:{actor.ActorId}");
            recorder.Events.Enqueue($"entry-spot:{actor.ActorId}:{Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class GeneralEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddPacket<EntrySpotGeneralBlockingHandler>();
            Context.Handlers.AddPacket<EntrySpotGeneralRecordingHandler>();
            Context.Handlers.AddPacket<EntrySpotChannelRequestHandler>();
        }
    }

    public sealed class EntryTimerSpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddPacket<EntryTimerRecordingHandler>();
        }

        public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _ = await Context.AddTimer<EntryTimerBlockingHandler>(
                "entry-blocking",
                TimeSpan.FromMilliseconds(10),
                cancellationToken: cancellationToken);
        }
    }

    public sealed record EntryTimerRecordCommand(string Value);

    public sealed class EntryTimerBlockingHandler(EntrySpotTimerCallbackRecorder recorder)
        : IZLinkSpotTimerHandler<EntryTimerSpot>
    {
        public async ValueTask HandleAsync(
            EntryTimerSpot spot,
            ZLinkTimerTick tick,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = tick;
            recorder.Events.Enqueue("timer-start");
            recorder.TimerStarted.TrySetResult();
            await recorder.ReleaseTimer.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            recorder.Events.Enqueue("timer-end");
        }
    }

    public sealed class EntryTimerRecordingHandler(EntrySpotTimerCallbackRecorder recorder)
        : IZLinkSpotPacketHandler<EntryTimerSpot, EntryTimerRecordCommand>
    {
        public ValueTask HandleAsync(
            EntryTimerSpot spot,
            EntryTimerRecordCommand message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.Events.Enqueue($"record:{message.Value}");
            recorder.Recorded.TrySetResult();
            return ValueTask.CompletedTask;
        }
    }

    public sealed class EntrySpotTimerCallbackRecorder
    {
        public ConcurrentQueue<string> Events { get; } = new();

        public TaskCompletionSource TimerStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseTimer { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Recorded { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    public sealed record EntrySpotGeneralBlockingCommand(string Value);

    public sealed record EntrySpotGeneralRecordCommand(string Value);

    public sealed record EntrySpotChannelRequestCommand(string Value);

    public sealed record EntrySpotOrderRequest(string Value);

    public sealed record EntrySpotOrderReply(string Value);

    public sealed class EntrySpotGeneralBlockingHandler(EntrySpotCallbackRecorder recorder)
        : IZLinkSpotPacketHandler<GeneralEntrySpot, EntrySpotGeneralBlockingCommand>
    {
        public async ValueTask HandleAsync(
            GeneralEntrySpot spot,
            EntrySpotGeneralBlockingCommand message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            recorder.Events.Enqueue($"block-start:{message.Value}");
            recorder.BlockingStarted.TrySetResult();
            await recorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            recorder.Events.Enqueue($"block-end:{message.Value}");
        }
    }

    public sealed class EntrySpotGeneralRecordingHandler(EntrySpotCallbackRecorder recorder)
        : IZLinkSpotPacketHandler<GeneralEntrySpot, EntrySpotGeneralRecordCommand>
    {
        public ValueTask HandleAsync(
            GeneralEntrySpot spot,
            EntrySpotGeneralRecordCommand message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"record:{message.Value}:{spot.Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class EntrySpotChannelRequestHandler(EntrySpotCallbackRecorder recorder)
        : IZLinkSpotPacketHandler<GeneralEntrySpot, EntrySpotChannelRequestCommand>
    {
        public async ValueTask HandleAsync(
            GeneralEntrySpot spot,
            EntrySpotChannelRequestCommand message,
            CancellationToken cancellationToken)
        {
            var reply = await spot.Context.Outbound.RequestToChannel(
                    "orders.api",
                    new EntrySpotOrderRequest(message.Value))
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<EntrySpotOrderReply>(cancellationToken)
                .ConfigureAwait(false);

            recorder.Events.Enqueue($"channel-reply:{reply.Value}");
        }
    }

    public sealed class EntrySpotOrdersRequestHandler
        : IZLinkRequestHandler<EntrySpotOrderRequest, EntrySpotOrderReply>
    {
        public ValueTask<EntrySpotOrderReply> HandleAsync(
            EntrySpotOrderRequest request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            return ValueTask.FromResult(new EntrySpotOrderReply($"order:{request.Value}"));
        }
    }

    public sealed class EntrySpotGatedOrdersRequestHandler(EntrySpotCallbackRecorder recorder)
        : IZLinkRequestHandler<EntrySpotOrderRequest, EntrySpotOrderReply>
    {
        public async ValueTask<EntrySpotOrderReply> HandleAsync(
            EntrySpotOrderRequest request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            recorder.BlockingStarted.TrySetResult();
            await recorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            return new EntrySpotOrderReply($"order:{request.Value}");
        }
    }

    public sealed class EntrySpotCallbackRecorder
    {
        public ConcurrentQueue<string> Events { get; } = new();

        public TaskCompletionSource BlockingStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseBlocking { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}
