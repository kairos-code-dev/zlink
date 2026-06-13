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
    public sealed class TestStream(string sessionId) : IZLinkStream
    {
        public string SessionId { get; } = sessionId;

        public global::Systems.Zlink.RoutingId? RoutingId => null;

        public string? LocalAddr => "local";

        public string? RemoteAddr => "remote";

        public bool Write(global::Systems.Zlink.Message payload, global::Systems.Zlink.SendFlags flags = global::Systems.Zlink.SendFlags.None)
        {
            _ = payload;
            _ = flags;
            return true;
        }

        public ValueTask CloseAsync()
        {
            return ValueTask.CompletedTask;
        }
    }

    public sealed class TestActorFactory(ActorIntegrationRecorder recorder) : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new TestActor(actorId, context, recorder));
        }
    }

    public sealed class ConcurrentActorFactory(
        ConcurrentActorFactoryRecorder factoryRecorder,
        ActorIntegrationRecorder actorRecorder) : IZLinkActorFactory
    {
        public async ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref factoryRecorder.CreateCount);
            factoryRecorder.FirstFactoryCall.TrySetResult();
            await factoryRecorder.ReleaseFactory.Task.WaitAsync(cancellationToken);
            return new TestActor(actorId, context, actorRecorder);
        }
    }

    public sealed class ConcurrentActorFactoryRecorder
    {
        public int CreateCount;

        public TaskCompletionSource FirstFactoryCall { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseFactory { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    public sealed class ActorIntegrationRecorder
    {
        public ConcurrentQueue<string> DispatchBodies { get; } = new();

        public ConcurrentQueue<string> DispatchRooms { get; } = new();

        public ConcurrentQueue<string> DispatchSpotRids { get; } = new();

        public ConcurrentQueue<string> ScopeViolations { get; } = new();

        public ConcurrentQueue<string> SpotActorJoins { get; } = new();

        public ConcurrentQueue<string> SpotActorLeaves { get; } = new();

        public ConcurrentQueue<string> EntrySpotActorJoins { get; } = new();

        public ConcurrentQueue<string> EntrySpotActorCreates { get; } = new();

        public ConcurrentQueue<string> EntrySpotActorLeaves { get; } = new();

        public volatile bool ConcurrentViolation;

        public int DisconnectCount;
    }
}
