using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Channels;
public sealed class FanoutTests
{
    [Fact]
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
}
