using System.Reflection;
using Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

namespace Zlink.Framework.UnitTests;

public sealed class BackendStreamSocketConcurrencyTests
{
    [Fact]
    public async Task ConcurrentBoundActorMessages_AreSubmittedSerially()
    {
        var native = DispatchProxy.Create<IStreamSocket, BlockingStreamSocketProxy>();
        var proxy = (BlockingStreamSocketProxy)(object)native;
        await using var backend = new ZLinkBackendStreamSocketWrapper(native);
        using var firstHeader = Message.From("first-header");
        using var firstBody = Message.From("first-body");
        using var secondHeader = Message.From("second-header");
        using var secondBody = Message.From("second-body");
        var sessionRid = RoutingId.From("session");

        var first = Task.Run(() => backend.SendBoundActor(
            sessionRid,
            "actor-1",
            [firstHeader, firstBody],
            SendFlags.None));
        await proxy.FirstSubmitEntered.Task.WaitAsync(TimeSpan.FromSeconds(2));

        var secondStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var second = Task.Run(() =>
        {
            secondStarted.TrySetResult();
            return backend.SendBoundActor(
                sessionRid,
                "actor-2",
                [secondHeader, secondBody],
                SendFlags.None);
        });
        await secondStarted.Task.WaitAsync(TimeSpan.FromSeconds(2));

        await Task.Delay(100);
        Assert.Equal(1, proxy.SubmitCount);
        Assert.Equal(1, proxy.MaximumConcurrentSubmits);

        proxy.AllowFirstSubmit.TrySetResult();
        Assert.True(await first.WaitAsync(TimeSpan.FromSeconds(2)));
        Assert.True(await second.WaitAsync(TimeSpan.FromSeconds(2)));
        Assert.Equal(2, proxy.SubmitCount);
        Assert.Equal(1, proxy.MaximumConcurrentSubmits);
    }

    private class BlockingStreamSocketProxy : DispatchProxy
    {
        private int _activeSubmits;
        private int _maximumConcurrentSubmits;
        private int _submitCount;

        public TaskCompletionSource FirstSubmitEntered { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource AllowFirstSubmit { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int MaximumConcurrentSubmits => Volatile.Read(ref _maximumConcurrentSubmits);

        public int SubmitCount => Volatile.Read(ref _submitCount);

        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            ArgumentNullException.ThrowIfNull(targetMethod);
            return targetMethod.Name switch
            {
                "SendBoundActor" => new BlockingSendOperation(this),
                "DisposeAsync" => ValueTask.CompletedTask,
                _ => throw new NotSupportedException(targetMethod.Name)
            };
        }

        private bool Submit()
        {
            var active = Interlocked.Increment(ref _activeSubmits);
            UpdateMaximum(active);
            var submit = Interlocked.Increment(ref _submitCount);
            try
            {
                if (submit == 1)
                {
                    FirstSubmitEntered.TrySetResult();
                    AllowFirstSubmit.Task.GetAwaiter().GetResult();
                }

                return true;
            }
            finally
            {
                Interlocked.Decrement(ref _activeSubmits);
            }
        }

        private void UpdateMaximum(int active)
        {
            while (true)
            {
                var current = Volatile.Read(ref _maximumConcurrentSubmits);
                if (current >= active) return;
                if (Interlocked.CompareExchange(ref _maximumConcurrentSubmits, active, current) == current)
                    return;
            }
        }

        private sealed class BlockingSendOperation(BlockingStreamSocketProxy owner)
            : SendOperation, SendSubmitOperation
        {
            public SendSubmitOperation Message(Message message) => this;
            public SendSubmitOperation Messages(IReadOnlyList<Message> messages) => this;
            public SendSubmitOperation Flags(SendFlags flags) => this;
            public bool Submit() => owner.Submit();
        }
    }
}
