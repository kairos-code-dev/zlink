using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

public sealed class ChannelMessagingIntegrationTests
{
    [Fact]
    public async Task ManualClient_Request_And_Send_Work_Across_Hosts()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{GetEphemeralPort()}";
        var serverBuilder = Host.CreateApplicationBuilder();
        serverBuilder.Services.AddSingleton<ProfileCommandRecorder>();
        serverBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
            });
        });
        serverBuilder.Services.AddZLinkHandlersFromAssemblyContaining<ChannelMessagingIntegrationTests>();

        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("api", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(apiEndpoint));
                });
            });
        });

        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkClient>();
        var recorder = serverHost.Services.GetRequiredService<ProfileCommandRecorder>();

        var reply = await ExecuteWithRetryAsync(
            async () => await client.Request("api", new GetProfileRequest { UserId = "alice" }).ExecAsync(),
            static result => result.Name == "user:alice");

        Assert.Equal("user:alice", reply.Name);

        await ExecuteWithRetryAsync(
            async () =>
            {
                client.Send("api", new RefreshProfileCacheCommand { UserId = "alice" }).Exec();
                await Task.Yield();
                return recorder.Commands.Count;
            },
            count => count > 0);

        Assert.Contains("alice", recorder.Commands);

        await clientHost.StopAsync();
        await serverHost.StopAsync();
    }

    [Fact]
    public async Task Publisher_And_Subscriber_Work_Across_Hosts()
    {
        var pubEndpoint = $"tcp://127.0.0.1:{GetEphemeralPort()}";
        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<ProfileEventRecorder>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("profile", channel =>
            {
                channel.EnableSubscriber(subscriber =>
                {
                    subscriber.UseManualConnections(connections => connections.Connect(pubEndpoint));
                });
            });
        });
        subscriberBuilder.Services.AddZLinkHandlersFromAssemblyContaining<ChannelMessagingIntegrationTests>();

        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("profile", channel =>
            {
                channel.EnablePublisher(publisher => publisher.Bind(pubEndpoint));
            });
        });

        using var subscriberHost = subscriberBuilder.Build();
        using var publisherHost = publisherBuilder.Build();

        await subscriberHost.StartAsync();
        await publisherHost.StartAsync();

        var publisher = publisherHost.Services.GetRequiredService<IZLinkEventPublisher>();
        var recorder = subscriberHost.Services.GetRequiredService<ProfileEventRecorder>();

        await ExecuteWithRetryAsync(
            async () =>
            {
                publisher.Publish(
                    "profile",
                    "profile.cache-invalidated",
                    new ProfileInvalidated { UserId = "alice" }).Exec();
                await Task.Yield();
                return recorder.Events.Count;
            },
            count => count > 0);

        Assert.Contains("alice", recorder.Events);

        await publisherHost.StopAsync();
        await subscriberHost.StopAsync();
    }

    private static async Task<T> ExecuteWithRetryAsync<T>(
        Func<Task<T>> action,
        Func<T, bool> predicate,
        int attempts = 20,
        int delayMs = 100)
    {
        Exception? lastError = null;

        for (var attempt = 0; attempt < attempts; attempt++)
        {
            try
            {
                var result = await action();
                if (predicate(result))
                {
                    return result;
                }
            }
            catch (Exception ex)
            {
                lastError = ex;
            }

            await Task.Delay(delayMs);
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("ZLink integration retry timed out.");
    }

    private static int GetEphemeralPort()
    {
        using var listener = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, 0);
        listener.Start();
        return ((System.Net.IPEndPoint)listener.LocalEndpoint).Port;
    }
}

public sealed class GetProfileRequest : IZLinkRequest<ProfileReply>
{
    public string UserId { get; init; } = string.Empty;
}

public sealed class ProfileReply
{
    public string Name { get; init; } = string.Empty;
}

public sealed class RefreshProfileCacheCommand
{
    public string UserId { get; init; } = string.Empty;
}

public sealed class ProfileInvalidated
{
    public string UserId { get; init; } = string.Empty;
}

public sealed class ProfileCommandRecorder
{
    public List<string> Commands { get; } = [];
}

public sealed class ProfileEventRecorder
{
    public List<string> Events { get; } = [];
}

public sealed class ProfileHandlers(ProfileCommandRecorder recorder)
{
    [ZLinkRequest]
    public ValueTask<ProfileReply> GetAsync(
        GetProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = cancellationToken;
        return ValueTask.FromResult(new ProfileReply
        {
            Name = $"user:{request.UserId}",
        });
    }

    [ZLinkSend]
    public ValueTask RefreshAsync(
        RefreshProfileCacheCommand command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = cancellationToken;
        recorder.Commands.Add(command.UserId);
        return ValueTask.CompletedTask;
    }
}

public sealed class ProfileEventHandlers(ProfileEventRecorder recorder)
{
    [ZLinkEvent]
    public ValueTask OnInvalidatedAsync(
        ProfileInvalidated @event,
        ZLinkEventContext context,
        CancellationToken cancellationToken)
    {
        Assert.Equal("profile.cache-invalidated", context.Topic);
        _ = cancellationToken;
        recorder.Events.Add(@event.UserId);
        return ValueTask.CompletedTask;
    }
}
