using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests;

public abstract partial class StreamTestSupport
{
    public sealed class GatewayActor(
        string actorId,
        IZLinkActorContext context,
        ActorDispatchRecorder recorder) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public ActorDispatchRecorder Recorder { get; } = recorder;

        public void Configure()
        {
            Context.AddPacket<GatewayActorHandler>("relay.echo");
            Context.AddPacket<GatewaySessionDisconnectHandler>("session.disconnect");
            Context.AddPacket<GatewaySessionDisconnectRequestHandler>("session.disconnect");
        }

    }

    public sealed class GatewayActorFactory(ActorDispatchRecorder recorder) : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            recorder.RecordCreated();
            return ValueTask.FromResult<IZLinkActor>(new GatewayActor(actorId, context, recorder));
        }
    }

    public sealed class ConfigureFailureRecorder
    {
        private int _createdCount;
        private int _configureCount;

        public bool FailConfigure { get; set; } = true;

        public int CreatedCount => Volatile.Read(ref _createdCount);

        public int ConfigureCount => Volatile.Read(ref _configureCount);

        public void RecordCreated()
        {
            Interlocked.Increment(ref _createdCount);
        }

        public void RecordConfigure()
        {
            Interlocked.Increment(ref _configureCount);
        }
    }

    public sealed class ConfigureFailureActor(
        string actorId,
        IZLinkActorContext context,
        ConfigureFailureRecorder recorder) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public void Configure()
        {
            recorder.RecordConfigure();
            if (recorder.FailConfigure)
            {
                throw new InvalidOperationException("configure failed");
            }
        }

    }

    public sealed class ConfigureFailureActorFactory(ConfigureFailureRecorder recorder) : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            recorder.RecordCreated();
            return ValueTask.FromResult<IZLinkActor>(
                new ConfigureFailureActor(actorId, context, recorder));
        }
    }
}
