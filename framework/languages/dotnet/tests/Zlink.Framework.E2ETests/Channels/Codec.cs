using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Channels;

[Collection(nameof(FrameworkTestsCollection))]
public sealed class CodecTests
{
    [Fact(DisplayName = "CDC-001 JSON codec round-trips nested arrays and nullable fields")]
    public async Task JsonCodec_RoundTrips_Nested_Arrays_And_Nullable_Fields()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var serverBuilder = Host.CreateApplicationBuilder();
        serverBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddClientServerChannel("codec");
                channel.EnableServer(apiEndpoint);
                channel.AddRequestHandler<JsonCodecEchoHandler, JsonCodecProbe, JsonCodecProbe>(
                    "JsonCodecProbe");
            }
        });
        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddClientServerChannel("codec");
                channel.EnableClient(apiEndpoint);
            }
        });

        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
        var request = new JsonCodecProbe
        {
            Name = "codec",
            Count = 3,
            Optional = null,
            Tags = ["alpha", "beta"],
            Child = new JsonCodecChild
            {
                Enabled = true,
                Scores = [7, 11, 13],
            },
        };

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client
                .RequestToChannel("codec", request)
                .PacketName("JsonCodecProbe")
                .Async<JsonCodecProbe>(),
            static result => result.Name == "codec");

        Assert.Equal(request.Name, reply.Name);
        Assert.Equal(request.Count, reply.Count);
        Assert.Null(reply.Optional);
        Assert.Equal(request.Tags, reply.Tags);
        Assert.NotNull(reply.Child);
        Assert.True(reply.Child.Enabled);
        Assert.Equal(request.Child.Scores, reply.Child.Scores);

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
    }
}

public sealed class JsonCodecEchoHandler : IZLinkRequestHandler<JsonCodecProbe, JsonCodecProbe>
{
    public ValueTask<JsonCodecProbe> HandleAsync(
        JsonCodecProbe request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        Assert.Equal("codec", context.ChannelName);
        Assert.Equal("JsonCodecProbe", context.PacketName);
        _ = cancellationToken;
        return ValueTask.FromResult(request);
    }
}

public sealed class JsonCodecProbe
{
    public string Name { get; init; } = string.Empty;

    public int Count { get; init; }

    public string? Optional { get; init; }

    public IReadOnlyList<string> Tags { get; init; } = [];

    public JsonCodecChild Child { get; init; } = new();
}

public sealed class JsonCodecChild
{
    public bool Enabled { get; init; }

    public IReadOnlyList<int> Scores { get; init; } = [];
}
