using System.Collections.Concurrent;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace Zlink.Framework.E2ETests.Channels;

[Collection(nameof(FrameworkTestsCollection))]
public sealed class RegistrationTests
{
    [Fact(DisplayName = "REG-003 manual channel handlers dispatch registered packets and report missing packets")]
    public async Task Manual_Channel_Handlers_Dispatch_Registered_Packets_And_Report_Missing_Packets()
    {
        var endpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var publishEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var dispatchEvidence = new ChannelDispatchErrorEvidence();
        var sendEvidence = new ManualRegistrationSendEvidence();
        var publishEvidence = new ManualRegistrationPublishEvidence();
        using var serverHost = BuildManualServer(endpoint, dispatchEvidence, sendEvidence);
        using var clientHost = BuildManualClient(endpoint);
        using var subscriberHost = BuildManualSubscriber(publishEndpoint, dispatchEvidence, publishEvidence);
        using var publisherHost = BuildManualPublisher(publishEndpoint);

        await serverHost.StartAsync();
        await clientHost.StartAsync();
        await subscriberHost.StartAsync();
        await publisherHost.StartAsync();

        try
        {
            var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
            var publisher = publisherHost.Services.GetRequiredService<IZLinkFanoutClient>();
            var registeredReply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client
                    .RequestToChannel("manual-reg", new ManualRegistrationRequest { Value = "registered" })
                    .PacketName("ManualRegisteredReq")
                    .Timeout(TimeSpan.FromSeconds(1))
                    .Async<ManualRegistrationReply>(),
                reply => reply.Value == "manual:registered");

            Assert.Equal("manual:registered", registeredReply.Value);

            await client
                .SendToChannel("manual-reg", new ManualRegistrationCommand { Value = "command" })
                .PacketName("ManualRegisteredCommand")
                .Async();
            var command = await sendEvidence.WaitAsync(TimeSpan.FromSeconds(2));
            Assert.Equal("command", command);

            await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () =>
                {
                    await publisher
                        .Publish(
                            "manual-events",
                            "manual.registered",
                            new ManualRegistrationEvent { Value = "published" })
                        .PacketName("ManualRegisteredEvent")
                        .Async();
                    await Task.Yield();
                    return publishEvidence.Events.ToArray();
                },
                events => events.Contains("published"));

            var missingRequest = await Assert.ThrowsAsync<InvalidOperationException>(async () => await client
                .RequestToChannel("manual-reg", new ManualRegistrationRequest { Value = "missing" })
                .PacketName("ManualMissingReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<ManualRegistrationReply>());
            Assert.Contains("ManualMissingReq", missingRequest.Message, StringComparison.Ordinal);

            await client
                .SendToChannel("manual-reg", new ManualRegistrationCommand { Value = "missing-command" })
                .PacketName("ManualMissingCommand")
                .Async();

            await publisher
                .Publish(
                    "manual-events",
                    "manual.missing",
                    new ManualRegistrationEvent { Value = "missing-publish" })
                .PacketName("ManualMissingEvent")
                .Async();

            await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                () => Task.FromResult(dispatchEvidence.Events.ToArray()),
                events => events.Any(static error =>
                    error.PacketName == "ManualMissingReq"
                    && error.MessageKind == ZLinkDispatchMessageKind.Request
                    && error.Reason == ZLinkDispatchErrorReason.HandlerMissing
                    && error.Action == ZLinkDispatchErrorAction.ReplyError)
                    && events.Any(static error =>
                        error.PacketName == "ManualMissingCommand"
                        && error.MessageKind == ZLinkDispatchMessageKind.Send
                        && error.Reason == ZLinkDispatchErrorReason.HandlerMissing
                        && error.Action == ZLinkDispatchErrorAction.Drop)
                    && events.Any(static error =>
                        error.PacketName == "ManualMissingEvent"
                        && error.MessageKind == ZLinkDispatchMessageKind.Publish
                        && error.Reason == ZLinkDispatchErrorReason.HandlerMissing
                        && error.Action == ZLinkDispatchErrorAction.Drop));
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(
                publisherHost,
                subscriberHost,
                clientHost,
                serverHost);
        }
    }

    private static IHost BuildManualServer(
        string endpoint,
        ChannelDispatchErrorEvidence dispatchEvidence,
        ManualRegistrationSendEvidence sendEvidence)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(dispatchEvidence);
        builder.Services.AddSingleton(sendEvidence);
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch().SetMessageDispatchErrorObserver<ChannelDispatchErrorObserver>();
            var channel = options.AddClientServerChannel("manual-reg");
            channel.EnableServer(endpoint);
            channel.AddRequestHandler<ManualRegistrationRequestHandler, ManualRegistrationRequest, ManualRegistrationReply>(
                "ManualRegisteredReq");
            channel.AddSendHandler<ManualRegistrationCommandHandler, ManualRegistrationCommand>(
                "ManualRegisteredCommand");
        });
        return builder.Build();
    }

    private static IHost BuildManualClient(string endpoint)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            var channel = options.AddClientServerChannel("manual-reg");
            channel.EnableClient(endpoint);
        });
        return builder.Build();
    }

    private static IHost BuildManualSubscriber(
        string publishEndpoint,
        ChannelDispatchErrorEvidence dispatchEvidence,
        ManualRegistrationPublishEvidence publishEvidence)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(dispatchEvidence);
        builder.Services.AddSingleton(publishEvidence);
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch().SetMessageDispatchErrorObserver<ChannelDispatchErrorObserver>();
            var channel = options.AddFanoutChannel("manual-events");
            channel.EnableSubscriber(publishEndpoint);
            channel.AddPublishHandler<ManualRegistrationPublishHandler, ManualRegistrationEvent>(
                "ManualRegisteredEvent");
        });
        return builder.Build();
    }

    private static IHost BuildManualPublisher(string publishEndpoint)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            var channel = options.AddFanoutChannel("manual-events");
            channel.EnablePublisher(publishEndpoint);
        });
        return builder.Build();
    }
}

public sealed class ManualRegistrationSendEvidence
{
    private readonly TaskCompletionSource<string> _first =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public ConcurrentQueue<string> Commands { get; } = [];

    public void Record(string value)
    {
        Commands.Enqueue(value);
        _first.TrySetResult(value);
    }

    public async Task<string> WaitAsync(TimeSpan timeout)
    {
        return await _first.Task.WaitAsync(timeout);
    }
}

public sealed class ManualRegistrationPublishEvidence
{
    public ConcurrentQueue<string> Events { get; } = [];

    public void Record(string value)
    {
        Events.Enqueue(value);
    }
}

public sealed class ManualRegistrationRequest
{
    public string Value { get; init; } = string.Empty;
}

public sealed class ManualRegistrationReply
{
    public string Value { get; init; } = string.Empty;
}

public sealed class ManualRegistrationCommand
{
    public string Value { get; init; } = string.Empty;
}

public sealed class ManualRegistrationEvent
{
    public string Value { get; init; } = string.Empty;
}

public sealed class ManualRegistrationRequestHandler
    : IZLinkRequestHandler<ManualRegistrationRequest, ManualRegistrationReply>
{
    public ValueTask<ManualRegistrationReply> HandleAsync(
        ManualRegistrationRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        Assert.Equal("manual-reg", context.ChannelName);
        Assert.Equal("ManualRegisteredReq", context.PacketName);
        _ = cancellationToken;
        return ValueTask.FromResult(new ManualRegistrationReply { Value = $"manual:{request.Value}" });
    }
}

public sealed class ManualRegistrationCommandHandler(ManualRegistrationSendEvidence evidence)
    : IZLinkSendHandler<ManualRegistrationCommand>
{
    public ValueTask HandleAsync(
        ManualRegistrationCommand message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        Assert.Equal("manual-reg", context.ChannelName);
        Assert.Equal("ManualRegisteredCommand", context.PacketName);
        _ = cancellationToken;
        evidence.Record(message.Value);
        return ValueTask.CompletedTask;
    }
}

public sealed class ManualRegistrationPublishHandler(ManualRegistrationPublishEvidence evidence)
    : IZLinkPublishHandler<ManualRegistrationEvent>
{
    public ValueTask HandleAsync(
        ManualRegistrationEvent message,
        ZLinkPublishContext context,
        CancellationToken cancellationToken)
    {
        Assert.Equal("manual-events", context.ChannelName);
        Assert.Equal("ManualRegisteredEvent", context.PacketName);
        Assert.Equal("manual.registered", context.Topic);
        _ = cancellationToken;
        evidence.Record(message.Value);
        return ValueTask.CompletedTask;
    }
}
