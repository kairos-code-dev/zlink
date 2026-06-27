using Systems.Zlink.Stream.Connector.Contracts;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Support;

internal sealed class YieldDispatchScenarioContext : IAsyncDisposable
{
    private YieldDispatchScenarioContext(ClientOptions options, IZlinkStreamConnector client)
    {
        Options = options;
        Client = client;
    }

    public ClientOptions Options { get; }

    public IZlinkStreamConnector Client { get; }

    public static async Task<YieldDispatchScenarioContext> ConnectAsync(ClientOptions options)
    {
        var client = YieldConnectorFactory.Create(options.SessionAStreamEndpoint);
        await client.Connect.Async();
        return new YieldDispatchScenarioContext(options, client);
    }

    public async Task<YieldEvidenceReply> WaitForPlayEvidenceAsync(
        string requestId,
        string marker,
        string targetNodeRid = "play-a")
    {
        return await Client.Request(new YieldEvidenceWaitReq(requestId, marker))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, targetNodeRid)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
    }

    public async Task<YieldEvidenceReply> ReadPlayEvidenceAsync(
        string requestId,
        string targetNodeRid)
    {
        return await Client.Request(new YieldEvidenceReq(requestId))
            .PacketName("YieldEvidenceReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, targetNodeRid)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
    }

    public async ValueTask DisposeAsync()
    {
        await Client.DisposeAsync();
    }
}
