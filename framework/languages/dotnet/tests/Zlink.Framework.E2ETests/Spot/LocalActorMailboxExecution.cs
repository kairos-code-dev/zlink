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


public sealed class LocalActorMailboxExecutionTests : SpotTestSupport
{
    [Fact]
    public async Task LocalActorPackets_Are_Serialized_Per_Actor_And_Parallel_Across_Actors()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<LocalActorBlockingHandler>();
        builder.Services.AddScoped<LocalActorRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind(spotNode);
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var registryRecorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var actorA = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("local-actor-a", "registry")).Actor;
        var actorB = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("local-actor-b", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actorA, new TestStream("local-session-a"));
        await actorRuntime.AttachActorAsync(actorB, new TestStream("local-session-b"));

        Task? actorABlocked = null;
        Task? actorASecond = null;
        try
        {
            actorABlocked = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "local-block",
                "block-a");
            await mailboxRecorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

            actorASecond = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "local-record",
                "after-a");
            var actorBPacket = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorB,
                "local-record",
                "record-b");

            await actorBPacket.WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Contains("local-record:local-actor-b:record-b", mailboxRecorder.Events);
            Assert.False(actorASecond.IsCompleted, "same actor packet ran before the blocking packet completed.");
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await actorABlocked!.WaitAsync(TimeSpan.FromSeconds(5));
        await actorASecond!.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Contains("local-record:local-actor-a:after-a", mailboxRecorder.Events);

        await actorRuntime.DisconnectActorAsync(actorA, new TestStream("local-session-a"));
        await actorRuntime.DisconnectActorAsync(actorB, new TestStream("local-session-b"));

        await host.StopAsync();
    }
}
