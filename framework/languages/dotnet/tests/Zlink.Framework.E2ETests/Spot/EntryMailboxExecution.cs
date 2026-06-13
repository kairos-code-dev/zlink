using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Spot;


public sealed class EntryMailboxExecutionTests : SpotTestSupport
{
    [Fact]
    public async Task EntrySpot_ActorPackets_Are_Serialized_Across_Actors()
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

            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotMesh("game.entry-mailbox", mesh =>
            {
                mesh.AddNode("entry-mailbox-node", spot =>
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
        var actorA = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("entry-actor-a", "registry")).Actor;
        var actorB = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("entry-actor-b", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actorA, new TestStream("entry-session-a"));
        await actorRuntime.AttachActorAsync(actorB, new TestStream("entry-session-b"));

        Task? actorABlocked = null;
        Task? actorASecond = null;
        Task? actorBPacket = null;
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
            actorBPacket = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorB,
                "entry-record",
                "record-b");

            // The Entry Spot line is held by the blocking handler: no other
            // callback may run — neither for the same actor nor for another actor.
            await Task.Delay(TimeSpan.FromMilliseconds(300));
            Assert.DoesNotContain(
                mailboxRecorder.Events,
                e => e.StartsWith("record:", StringComparison.Ordinal));
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
        Assert.Contains("record:entry-actor-a:after-a", events);
        Assert.Contains("record:entry-actor-b:record-b", events);

        // The blocking handler completes before any queued record handler starts.
        var blockEnd = Array.IndexOf(events, "block-end:entry-actor-a:block-a");
        Assert.True(blockEnd >= 0, "blocking handler end event missing.");
        Assert.True(
            blockEnd < Array.IndexOf(events, "record:entry-actor-a:after-a"),
            "queued same-actor packet ran before the blocking handler completed.");
        Assert.True(
            blockEnd < Array.IndexOf(events, "record:entry-actor-b:record-b"),
            "queued other-actor packet ran before the blocking handler completed.");

        await actorRuntime.DisconnectActorAsync(actorA, new TestStream("entry-session-a"));
        await actorRuntime.DisconnectActorAsync(actorB, new TestStream("entry-session-b"));

        await host.StopAsync();
    }

    [Fact]
    public async Task EntrySpot_PacketHandlers_Are_Serialized_On_EntrySpot_Line()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotCallbackRecorder>();
        builder.Services.AddScoped<GeneralEntrySpot>();
        builder.Services.AddScoped<EntrySpotGeneralBlockingHandler>();
        builder.Services.AddScoped<EntrySpotGeneralRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {

            options.AddSpotMesh("game.entry-general", mesh =>
            {
                mesh.AddNode("entry-general-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.BindRouter(spotNode);
                });
                spot.AddEntrySpot<GeneralEntrySpot>();
            });
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

        // The Entry Spot line is held: the record handler must not run.
        await Task.Delay(TimeSpan.FromMilliseconds(300));
        Assert.DoesNotContain(
            $"record:record:{activation.SpotRid.ToHex()}",
            recorder.Events);
        Assert.False(recordDispatch.IsCompleted, "packet handler ran while the Entry Spot line was held.");

        recorder.ReleaseBlocking.TrySetResult();
        await Task.WhenAll(blockingDispatch, recordDispatch).WaitAsync(TimeSpan.FromSeconds(5));

        var events = recorder.Events.ToArray();
        var blockEnd = Array.IndexOf(events, "block-end:block");
        var recordIndex = Array.IndexOf(events, $"record:record:{activation.SpotRid.ToHex()}");
        Assert.True(blockEnd >= 0, "blocking handler end event missing.");
        Assert.True(recordIndex >= 0, "record handler event missing.");
        Assert.True(blockEnd < recordIndex, "record handler ran before the blocking handler completed.");

        await host.StopAsync();
    }

    [Fact]
    public async Task EntrySpot_NativeActorReadableBatch_Dispatches_Actors_Serially()
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

            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotMesh("game.entry-native-batch", mesh =>
            {
                mesh.AddNode("entry-native-batch-node", spot =>
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

            // The blocking handler holds the Entry Spot line: no later batch
            // entry may run, regardless of which actor it belongs to.
            await Task.Delay(TimeSpan.FromMilliseconds(300));
            Assert.DoesNotContain("record:entry-native-b:record-b", mailboxRecorder.Events);
            Assert.DoesNotContain("record:entry-native-a:after-a", mailboxRecorder.Events);
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await dispatch.WaitAsync(TimeSpan.FromSeconds(5));

        var events = mailboxRecorder.Events.ToArray();
        var blockEnd = Array.IndexOf(events, "block-end:entry-native-a:block-a");
        var recordB = Array.IndexOf(events, "record:entry-native-b:record-b");
        var afterA = Array.IndexOf(events, "record:entry-native-a:after-a");
        Assert.True(blockEnd >= 0, "blocking handler end event missing.");
        Assert.True(recordB >= 0, "other-actor record event missing.");
        Assert.True(afterA >= 0, "same-actor record event missing.");
        Assert.True(blockEnd < recordB, "batch entry ran before the blocking handler completed.");
        Assert.True(recordB < afterA, "batch entries did not run in batch order.");

        await actorRuntime.DisconnectActorAsync(actorA, new TestStream("entry-native-session-a"));
        await actorRuntime.DisconnectActorAsync(actorB, new TestStream("entry-native-session-b"));

        await host.StopAsync();
    }
}
