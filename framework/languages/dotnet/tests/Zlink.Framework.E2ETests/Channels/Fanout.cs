using System.Collections.Concurrent;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Channels;
public sealed class FanoutTests
{
    [Fact(DisplayName = "PUB-001 partial fanout publish dispatches across hosts")]
    public async Task Publisher_And_Subscriber_Work_Across_Hosts()
    {
        var pubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<ProfileEventRecorder>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<FanoutTests>();
            {
                var channel = options.AddFanoutChannel("profile");
                channel.EnableSubscriber(pubEndpoint);
                channel.AddHandlerGroup("profile-events");

            }
        });
        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddFanoutChannel("profile");
                channel.EnablePublisher(pubEndpoint);

            }
        });

        using var subscriberHost = subscriberBuilder.Build();
        using var publisherHost = publisherBuilder.Build();

        await subscriberHost.StartAsync();
        await publisherHost.StartAsync();

        var publisher = publisherHost.Services.GetRequiredService<IZLinkFanoutClient>();
        var recorder = subscriberHost.Services.GetRequiredService<ProfileEventRecorder>();

        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                await publisher.Publish(
                    "profile",
                    "profile.cache-invalidated",
                    new ProfileInvalidated { UserId = "alice" }).Async();
                await Task.Yield();
                return recorder.Events.Count;
            },
            count => count > 0);

        Assert.Contains("alice", recorder.Events.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(publisherHost, subscriberHost);
    }

    [Fact(DisplayName = "PUB-001 fanout delivers the same sequence to three subscribers")]
    public async Task Publisher_Delivers_Same_Sequence_To_Three_Subscribers()
    {
        var pubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var firstSubscriberBuilder = CreateSequenceSubscriberBuilder(pubEndpoint);
        var secondSubscriberBuilder = CreateSequenceSubscriberBuilder(pubEndpoint);
        var thirdSubscriberBuilder = CreateSequenceSubscriberBuilder(pubEndpoint);
        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddFanoutChannel("sequence");
                channel.EnablePublisher(pubEndpoint);
            }
        });

        using var firstSubscriber = firstSubscriberBuilder.Build();
        using var secondSubscriber = secondSubscriberBuilder.Build();
        using var thirdSubscriber = thirdSubscriberBuilder.Build();
        using var publisherHost = publisherBuilder.Build();

        await firstSubscriber.StartAsync();
        await secondSubscriber.StartAsync();
        await thirdSubscriber.StartAsync();
        await publisherHost.StartAsync();

        var publisher = publisherHost.Services.GetRequiredService<IZLinkFanoutClient>();
        var firstRecorder = firstSubscriber.Services.GetRequiredService<FanoutSequenceRecorder>();
        var secondRecorder = secondSubscriber.Services.GetRequiredService<FanoutSequenceRecorder>();
        var thirdRecorder = thirdSubscriber.Services.GetRequiredService<FanoutSequenceRecorder>();
        var nextSequence = 0;

        var deliveredSequence = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                var sequence = Interlocked.Increment(ref nextSequence);
                await publisher.Publish(
                    "sequence",
                    "sequence.tick",
                    new FanoutSequenceEvent
                    {
                        Sequence = sequence,
                        Payload = $"fanout-p0:{sequence}",
                    }).Async();
                await Task.Yield();
                return FindCommonSequence(firstRecorder, secondRecorder, thirdRecorder);
            },
            sequence => sequence.HasValue);

        Assert.NotNull(deliveredSequence);
        var expectedPayload = $"fanout-p0:{deliveredSequence.Value}";
        Assert.Contains(firstRecorder.Events, entry => entry.Sequence == deliveredSequence.Value && entry.Payload == expectedPayload);
        Assert.Contains(secondRecorder.Events, entry => entry.Sequence == deliveredSequence.Value && entry.Payload == expectedPayload);
        Assert.Contains(thirdRecorder.Events, entry => entry.Sequence == deliveredSequence.Value && entry.Payload == expectedPayload);

        await ChannelMessagingTestSupport.StopHostsAsync(
            publisherHost,
            thirdSubscriber,
            secondSubscriber,
            firstSubscriber);
    }

    private static HostApplicationBuilder CreateSequenceSubscriberBuilder(string pubEndpoint)
    {
        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<FanoutSequenceRecorder>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<FanoutTests>();
            {
                var channel = options.AddFanoutChannel("sequence");
                channel.EnableSubscriber(pubEndpoint);
                channel.AddHandlerGroup("fanout-sequence");
            }
        });
        return subscriberBuilder;
    }

    private static int? FindCommonSequence(
        FanoutSequenceRecorder first,
        FanoutSequenceRecorder second,
        FanoutSequenceRecorder third)
    {
        var secondSequences = second.Events.Select(static entry => entry.Sequence).ToHashSet();
        var thirdSequences = third.Events.Select(static entry => entry.Sequence).ToHashSet();
        foreach (var entry in first.Events)
        {
            if (secondSequences.Contains(entry.Sequence) && thirdSequences.Contains(entry.Sequence))
            {
                return entry.Sequence;
            }
        }
        return null;
    }
}

public sealed class FanoutSequenceRecorder
{
    public ConcurrentQueue<FanoutSequenceEvent> Events { get; } = [];
}

public sealed class FanoutSequenceEvent
{
    public int Sequence { get; init; }

    public string Payload { get; init; } = string.Empty;
}

[ZLinkHandlerGroup("fanout-sequence")]
public sealed class FanoutSequenceHandlers(FanoutSequenceRecorder recorder)
{
    [ZLinkPublish]
    public ValueTask OnSequenceAsync(
        FanoutSequenceEvent @event,
        ZLinkPublishContext context,
        CancellationToken cancellationToken)
    {
        Assert.Equal("sequence.tick", context.Topic);
        _ = cancellationToken;
        recorder.Events.Enqueue(@event);
        return ValueTask.CompletedTask;
    }
}
