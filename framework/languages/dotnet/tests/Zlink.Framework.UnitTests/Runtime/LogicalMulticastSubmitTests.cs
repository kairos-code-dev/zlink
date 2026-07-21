using System.Reflection;

using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.UnitTests;

public sealed class LogicalMulticastSubmitTests
{
    [Fact]
    public async Task Publish_UsesOneNativeOperationPerPublicSubmit()
    {
        var spot = DispatchProxy.Create<IZLinkBackendSpot, PublishSpotProxy>();
        var proxy = (PublishSpotProxy)(object)spot;
        proxy.Result = new MeshPublishResult(
            SubmitResult.Backpressured,
            new MeshPublishDetail(2, 1, 1, 0, 0, 0, 0));

        await using var transport = new ZLinkSpotOutboundTransport(
            spot,
            TimeSpan.FromMilliseconds(250),
            CancellationToken.None);
        await using var pool = CreatePool();
        using var nonBlockingMessage = Message.From("non-blocking");
        var nonBlocking = transport.TryPublishCurrentOnce(
            "events-channel", "events", [nonBlockingMessage], ReadOnlyMemory<byte>.Empty);

        using var blockingMessage = Message.From("blocking");
        var blocking = await ZLinkLogicalMulticastSubmitter.SubmitAsync(
            pool,
            () => transport.PublishCurrentBlocking(
                "events-channel",
                "events",
                [blockingMessage]),
            CancellationToken.None,
            CancellationToken.None);

        Assert.Equal(2, proxy.PublishCount);
        Assert.Equal([SendFlags.DontWait, SendFlags.None], proxy.Flags);
        Assert.Equal(SubmitResult.Backpressured, nonBlocking.Result);
        Assert.Equal((uint)1, nonBlocking.Detail.AdmittedRemoteTargets);
        Assert.Equal((uint)1, nonBlocking.Detail.DroppedRemoteTargets);
        Assert.Equal(SubmitResult.Backpressured, blocking.Result);
        Assert.Equal((uint)1, blocking.Detail.AdmittedRemoteTargets);
        Assert.Equal((uint)1, blocking.Detail.DroppedRemoteTargets);
    }

    [Fact]
    public async Task Publish_ReturnsBackpressuredWithoutStartingCore_WhenNoWorkerCanBeReserved()
    {
        await using var pool = CreatePool();
        using var release = new ManualResetEventSlim(false);
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Assert.Equal(ZLinkWorkerSubmitResult.Accepted, pool.TrySubmitDirect(_ =>
        {
            started.TrySetResult();
            release.Wait();
        }));
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var publishCount = 0;

        var result = await ZLinkLogicalMulticastSubmitter.SubmitAsync(
            pool,
            () =>
            {
                publishCount++;
                return new MeshPublishResult(
                    SubmitResult.Ok,
                    new MeshPublishDetail(0, 0, 0, 0, 0, 0, 0));
            },
            CancellationToken.None,
            CancellationToken.None);

        Assert.Equal(SubmitResult.Backpressured, result.Result);
        Assert.Equal(0, publishCount);
        release.Set();
    }

    [Fact]
    public async Task Publish_CancellationAfterCoreStarts_PreservesCoreResult()
    {
        await using var pool = CreatePool();
        using var release = new ManualResetEventSlim(false);
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var cancellation = new CancellationTokenSource();
        var expected = new MeshPublishResult(
            SubmitResult.Ok,
            new MeshPublishDetail(1, 1, 0, 0, 0, 0, 0));

        var pending = ZLinkLogicalMulticastSubmitter.SubmitAsync(
            pool,
            () =>
            {
                started.TrySetResult();
                release.Wait();
                return expected;
            },
            cancellation.Token,
            CancellationToken.None).AsTask();
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5));

        cancellation.Cancel();
        Assert.False(pending.IsCompleted);
        release.Set();

        Assert.Equal(expected, await pending.WaitAsync(TimeSpan.FromSeconds(5)));
    }

    private static ZLinkWorkerPool CreatePool()
    {
        return new ZLinkWorkerPool(0, 1, TimeSpan.FromSeconds(30), 1);
    }

    private class PublishSpotProxy : DispatchProxy
    {
        internal MeshPublishResult Result { get; set; } =
            new(SubmitResult.Ok, new MeshPublishDetail(0, 0, 0, 0, 0, 0, 0));

        internal int PublishCount { get; private set; }

        internal List<SendFlags> Flags { get; } = [];

        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            ArgumentNullException.ThrowIfNull(targetMethod);
            return targetMethod.Name switch
            {
                nameof(IZLinkBackendSpot.Publish) => Publish(args),
                nameof(IZLinkBackendSpot.OnSendReady) => null,
                nameof(IAsyncDisposable.DisposeAsync) => ValueTask.CompletedTask,
                _ => throw new NotSupportedException(targetMethod.Name)
            };
        }

        private MeshPublishResult Publish(object?[]? args)
        {
            PublishCount++;
            Flags.Add((SendFlags)args![3]!);
            return Result;
        }
    }
}
