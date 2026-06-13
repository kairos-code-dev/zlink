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
    public async Task LocalActorPackets_Are_Serialized_On_The_EntrySpot_Line()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<LocalActorBlockingHandler>();
        builder.Services.AddScoped<LocalActorRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotMesh("actor-node", mesh =>
            {
                mesh.AddNode("actor-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.BindRouter(spotNode);
                });
                spot.AddEntrySpot<RegistryEntrySpot>();
            });
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var actorA = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("local-actor-a", "registry")).Actor;
        var actorB = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("local-actor-b", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actorA, new TestStream("local-session-a"));
        await actorRuntime.AttachActorAsync(actorB, new TestStream("local-session-b"));

        Task? actorABlocked = null;
        Task? actorASecond = null;
        Task? actorBPacket = null;
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
            actorBPacket = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorB,
                "local-record",
                "record-b");

            // The Entry Spot line is held by the blocking handler: local actor
            // packets are serialized regardless of which actor they target.
            await Task.Delay(TimeSpan.FromMilliseconds(300));
            Assert.DoesNotContain(
                mailboxRecorder.Events,
                e => e.StartsWith("local-record:", StringComparison.Ordinal));
            Assert.False(actorASecond.IsCompleted, "same actor packet ran before the blocking packet completed.");
            Assert.False(actorBPacket.IsCompleted, "other actor packet ran before the blocking packet completed.");
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await actorABlocked!.WaitAsync(TimeSpan.FromSeconds(5));
        await actorASecond!.WaitAsync(TimeSpan.FromSeconds(5));
        await actorBPacket!.WaitAsync(TimeSpan.FromSeconds(5));

        var events = mailboxRecorder.Events.ToArray();
        Assert.Contains("local-record:local-actor-a:after-a", events);
        Assert.Contains("local-record:local-actor-b:record-b", events);

        var blockEnd = Array.IndexOf(events, "local-block-end:local-actor-a:block-a");
        Assert.True(blockEnd >= 0, "blocking handler end event missing.");
        Assert.True(
            blockEnd < Array.IndexOf(events, "local-record:local-actor-a:after-a"),
            "queued same-actor packet ran before the blocking handler completed.");
        Assert.True(
            blockEnd < Array.IndexOf(events, "local-record:local-actor-b:record-b"),
            "queued other-actor packet ran before the blocking handler completed.");

        await actorRuntime.DisconnectActorAsync(actorA, new TestStream("local-session-a"));
        await actorRuntime.DisconnectActorAsync(actorB, new TestStream("local-session-b"));

        await host.StopAsync();
    }
}
