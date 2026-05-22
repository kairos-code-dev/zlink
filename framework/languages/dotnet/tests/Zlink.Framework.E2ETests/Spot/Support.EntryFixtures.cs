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
    public sealed class RegistryEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddActorPacket<RegistryEntryJoinHandler, RegistryTestActor>("entry-join");
            Context.AddActorPacket<EntrySpotJoinBlockingHandler, RegistryTestActor>("entry-join-block");
            Context.AddActorPacket<EntrySpotBlockingHandler, RegistryTestActor>("entry-block");
            Context.AddActorPacket<EntrySpotRecordingHandler, RegistryTestActor>("entry-record");
            Context.AddActorJoined<RegistryEntryJoinedHandler, RegistryTestActor>();
            Context.AddActorLeft<RegistryEntryLeftHandler, RegistryTestActor>();
        }
    }

    public sealed class GeneralEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddPacket<EntrySpotGeneralBlockingHandler>();
            Context.AddPacket<EntrySpotGeneralRecordingHandler>();
            Context.AddPacket<EntrySpotChannelRequestHandler>();
        }
    }

    public sealed class EntryTimerSpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddPacket<EntryTimerRecordingHandler>();
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
            var reply = await spot.Context.RequestChannel(
                    "orders",
                    new EntrySpotOrderRequest(message.Value))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<EntrySpotOrderReply>(cancellationToken)
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

    public sealed class EntrySpotCallbackRecorder
    {
        public ConcurrentQueue<string> Events { get; } = new();

        public TaskCompletionSource BlockingStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseBlocking { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}
