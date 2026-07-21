using Systems.Zlink;
using Xunit;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;
using ZoneWorld.Server.Ops.Infrastructure.ZLink.Sessions;
using ZoneWorld.Shared.Contracts;

namespace Zlink.Framework.SampleRegressionTests;

public sealed class ZoneWorldOpsConsoleRegistryTests
{
    [Fact]
    public async Task StaleConsoleFailureDoesNotBlockHealthyConsoleAndIsReported()
    {
        var registry = new OpsConsoleRegistry();
        var stale = new TestSessionContext("stale", failSend: true);
        var healthy = new TestSessionContext("healthy", failSend: false);
        await registry.AddAsync(stale, CancellationToken.None);
        await registry.AddAsync(healthy, CancellationToken.None);

        var error = await Assert.ThrowsAsync<AggregateException>(async () =>
            await registry.BroadcastAsync(Status("node-a"), CancellationToken.None));

        Assert.Single(error.InnerExceptions);
        Assert.Equal(1, healthy.SendCount);
        await registry.BroadcastAsync(Status("node-a"), CancellationToken.None);
        Assert.Equal(2, healthy.SendCount);
        Assert.Equal(1, stale.SendCount);
    }

    [Fact]
    public async Task LateDisconnectCannotRemoveReplacementWithTheSameSessionId()
    {
        var registry = new OpsConsoleRegistry();
        var previous = new TestSessionContext("console", failSend: false);
        var replacement = new TestSessionContext("console", failSend: false);
        await registry.AddAsync(previous, CancellationToken.None);
        await registry.AddAsync(replacement, CancellationToken.None);

        registry.Remove(previous);
        await registry.BroadcastAsync(Status("node-b"), CancellationToken.None);

        Assert.Equal(0, previous.SendCount);
        Assert.Equal(1, replacement.SendCount);
    }

    private static NodeStatusNotify Status(string nodeId) =>
        new(nodeId, Registered: true, Connected: true, Maintenance: false, [], PlayerCount: 0);

    private sealed class TestSessionContext(string sessionId, bool failSend) : IZLinkSessionContext
    {
        private readonly TestSessionClient _client = new(failSend);

        public string SessionId { get; } = sessionId;
        public RoutingId? RoutingId => null;
        public string? LocalAddr => null;
        public string? RemoteAddr => null;
        public IZLinkSessionClient Client => _client;
        public IZLinkSessionActors Actors => null!;
        public IZLinkSessionHandlerRegistry Handlers => null!;
        public int SendCount => _client.SendCount;
        public ValueTask CloseAsync() => ValueTask.CompletedTask;
    }

    private sealed class TestSessionClient(bool failSend) : IZLinkSessionClient
    {
        public int SendCount { get; private set; }

        public IZLinkSessionSendCall Send<TMessage>(TMessage message)
        {
            _ = message;
            return new TestSendCall(() =>
            {
                SendCount++;
                if (failSend) throw new InvalidOperationException("session transport unavailable");
            });
        }

        public IZLinkSessionReplyCall Reply<TMessage>(TMessage message) => throw new NotSupportedException();
    }

    private sealed class TestSendCall(Action submit) : IZLinkSessionSendCall
    {
        public IZLinkSessionSendCall Metadata(string key, string value) => this;
        public IZLinkSessionSendCall Metadata(ZLinkMessageMetadata metadata) => this;
        public IZLinkSessionSendCall Compress() => this;
        public ValueTask<ZLinkSubmitResult> SubmitAsync(CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            submit();
            return ValueTask.FromResult(new ZLinkSubmitResult(ZLinkSubmitStatus.Submitted));
        }
    }
}
