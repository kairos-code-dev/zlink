using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Spot;


public sealed class EntryMailboxExecutionTests : SpotTestSupport
{
    [Fact]
    public async Task EntrySpot_ActorPackets_Are_Serialized_Per_Actor_And_Parallel_Across_Actors()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<EntrySpotBlockingHandler>();
        builder.Services.AddScoped<EntrySpotRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.entry-mailbox", _ => { });
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotNode("entry-mailbox-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<RegistryEntrySpot>();
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var registryRecorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var actorA = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("entry-actor-a", "registry")).Actor;
        var actorB = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("entry-actor-b", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actorA, new TestStream("entry-session-a"));
        await actorRuntime.AttachActorAsync(actorB, new TestStream("entry-session-b"));

        Task? actorABlocked = null;
        Task? actorASecond = null;
        try
        {
            actorABlocked = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "entry-block",
                "block-a");
            await mailboxRecorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

            actorASecond = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "entry-record",
                "after-a");
            var actorBPacket = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorB,
                "entry-record",
                "record-b");

            await actorBPacket.WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Contains("record:entry-actor-b:record-b", mailboxRecorder.Events);
            Assert.False(actorASecond.IsCompleted, "same actor packet ran before the blocking packet completed.");
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await actorABlocked!.WaitAsync(TimeSpan.FromSeconds(5));
        await actorASecond!.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Contains("record:entry-actor-a:after-a", mailboxRecorder.Events);

        await actorRuntime.DisconnectActorAsync(actorA, new TestStream("entry-session-a"));
        await actorRuntime.DisconnectActorAsync(actorB, new TestStream("entry-session-b"));

        await host.StopAsync();
    }

    [Fact]
    public async Task EntrySpot_PacketHandlers_Are_Dispatched_Without_EntrySpot_Serialization()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotCallbackRecorder>();
        builder.Services.AddScoped<GeneralEntrySpot>();
        builder.Services.AddScoped<EntrySpotGeneralBlockingHandler>();
        builder.Services.AddScoped<EntrySpotGeneralRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.entry-general", _ => { });
            options.AddSpotNode("entry-general-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<GeneralEntrySpot>();
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotCallbackRecorder>();
        var nodeRuntime = GetSpotNodeRuntime(runtime, "entry-general-node");
        var activation = nodeRuntime.EntrySpotActivation
            ?? throw new InvalidOperationException("Entry Spot activation was not created.");
        var blocking = new EntrySpotGeneralBlockingCommand("block");
        var blockingHeader = CreateEntrySpotEnvelopeHeader("game.entry-general", blocking);
        Assert.True(activation.TryResolvePacket(blockingHeader, out var blockingDescriptor));
        var blockingDispatch = activation
            .InvokePacketAsync(blockingDescriptor!, blocking, CancellationToken.None)
            .AsTask();
        await recorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var record = new EntrySpotGeneralRecordCommand("record");
        var recordHeader = CreateEntrySpotEnvelopeHeader("game.entry-general", record);
        Assert.True(activation.TryResolvePacket(recordHeader, out var recordDescriptor));
        var recordDispatch = activation
            .InvokePacketAsync(recordDescriptor!, record, CancellationToken.None)
            .AsTask();

        await RetryAsync(
            () => recorder.Events.Contains($"record:record:{activation.SpotRid.ToHex()}"),
            TimeSpan.FromSeconds(5));
        Assert.DoesNotContain("block-end:block", recorder.Events);

        recorder.ReleaseBlocking.TrySetResult();
        await Task.WhenAll(blockingDispatch, recordDispatch).WaitAsync(TimeSpan.FromSeconds(5));
        await RetryAsync(
            () => recorder.Events.Contains("block-end:block"),
            TimeSpan.FromSeconds(5));

        await host.StopAsync();
    }

    [Fact]
    public async Task EntrySpot_NativeActorReadableBatch_Dispatches_Actors_In_Parallel()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<EntrySpotBlockingHandler>();
        builder.Services.AddScoped<EntrySpotRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.entry-native-batch", _ => { });
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotNode("entry-native-batch-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<RegistryEntrySpot>();
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var registryRecorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var activation = actorRuntime
            .GetSpotNodeRuntime("entry-native-batch-node")
            .EntrySpotActivation
            ?? throw new InvalidOperationException("Entry Spot activation was not created.");
        var actorA = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("entry-native-a", "registry")).Actor;
        var actorB = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("entry-native-b", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actorA, new TestStream("entry-native-session-a"));
        await actorRuntime.AttachActorAsync(actorB, new TestStream("entry-native-session-b"));

        var dispatch = ZLinkEntrySpotActorDispatcher.DispatchAsync(
            actorRuntime,
            activation,
            [
                CreateEntryActorHeaderPart(actorA, "entry-block"),
                CreateEntryActorBodyPart(actorA, "block-a"),
                CreateEntryActorHeaderPart(actorB, "entry-record"),
                CreateEntryActorBodyPart(actorB, "record-b"),
                CreateEntryActorHeaderPart(actorA, "entry-record"),
                CreateEntryActorBodyPart(actorA, "after-a"),
            ],
            CancellationToken.None);

        try
        {
            await mailboxRecorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
            await RetryAsync(
                () => mailboxRecorder.Events.Contains("record:entry-native-b:record-b"),
                TimeSpan.FromSeconds(5));
            Assert.DoesNotContain("record:entry-native-a:after-a", mailboxRecorder.Events);
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await dispatch.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Contains("record:entry-native-a:after-a", mailboxRecorder.Events);

        await actorRuntime.DisconnectActorAsync(actorA, new TestStream("entry-native-session-a"));
        await actorRuntime.DisconnectActorAsync(actorB, new TestStream("entry-native-session-b"));

        await host.StopAsync();
    }
}
