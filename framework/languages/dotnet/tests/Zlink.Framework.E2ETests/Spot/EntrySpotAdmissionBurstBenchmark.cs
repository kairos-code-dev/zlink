using System.Diagnostics;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Spot;

public sealed class EntrySpotAdmissionBurstBenchmark : SpotTestSupport
{
    private const int ActorCount = 200;
    private const int PacketsPerActor = 50;

    [Fact]
    public async Task EntrySpot_AdmissionBurst_Measures_Throughput_And_Latency()
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
            {
                var mesh = options.AddSpotMesh("game.entry-burst");
                {
                    var spot = mesh.AddNode("entry-burst-node");
                    {
                        var router = spot.EnableRouter(spotNode);

                    }
                    spot.AddEntrySpot<RegistryEntrySpot>();

                }

            }
        });

        using var host = builder.Build();
        await host.StartAsync();

        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();

        var actors = new IZLinkActor[ActorCount];
        for (var i = 0; i < ActorCount; i++)
        {
            actors[i] = (await actorRuntime.CreateLocalActorAsync($"admit-{i:D4}", "registry")).Actor;
            await actorRuntime.AttachActorAsync(actors[i], new TestStream($"burst-session-{i:D4}"));
        }

        // Warmup pass so JIT/registry costs do not skew the measured burst.
        await Task.WhenAll(actors.Select(actor =>
            SubmitEntrySpotStringAsync(actorRuntime, actor, "entry-record", "warmup")));

        var latencies = new long[ActorCount * PacketsPerActor];
        var total = Stopwatch.StartNew();
        await Task.WhenAll(actors.Select(async (actor, actorIndex) =>
        {
            for (var packet = 0; packet < PacketsPerActor; packet++)
            {
                var start = Stopwatch.GetTimestamp();
                await SubmitEntrySpotStringAsync(
                    actorRuntime,
                    actor,
                    "entry-record",
                    $"burst-{packet}");
                latencies[(actorIndex * PacketsPerActor) + packet] =
                    Stopwatch.GetTimestamp() - start;
            }
        }));
        total.Stop();

        var handled = recorder.Events.Count(static entry => entry.Contains(":burst-", StringComparison.Ordinal));
        Assert.Equal(ActorCount * PacketsPerActor, handled);

        ReportResults(total.Elapsed, latencies);

        for (var i = 0; i < ActorCount; i++)
        {
            await actorRuntime.DisconnectActorAsync(actors[i], new TestStream($"burst-session-{i:D4}"));
        }

        await host.StopAsync();
    }

    private static void ReportResults(TimeSpan total, long[] latencyTimestamps)
    {
        var latenciesMs = latencyTimestamps
            .Select(static ticks => ticks * 1000.0 / Stopwatch.Frequency)
            .OrderBy(static value => value)
            .ToArray();

        var packets = latenciesMs.Length;
        var throughput = packets / total.TotalSeconds;
        var p50 = Percentile(latenciesMs, 0.50);
        var p95 = Percentile(latenciesMs, 0.95);
        var p99 = Percentile(latenciesMs, 0.99);

        var line =
            $"{DateTime.UtcNow:O} actors={ActorCount} packetsPerActor={PacketsPerActor} " +
            $"totalMs={total.TotalMilliseconds:F1} throughputPerSec={throughput:F0} " +
            $"p50Ms={p50:F3} p95Ms={p95:F3} p99Ms={p99:F3}";

        var resultPath = Path.Combine(
            FindRepoDotnetRoot(),
            "entry-spot-baseline.txt");
        File.AppendAllText(resultPath, line + Environment.NewLine);
    }

    private static double Percentile(double[] sorted, double percentile)
    {
        if (sorted.Length == 0)
        {
            return 0;
        }

        var index = (int)Math.Ceiling(percentile * sorted.Length) - 1;
        return sorted[Math.Clamp(index, 0, sorted.Length - 1)];
    }

    private static string FindRepoDotnetRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (string.Equals(directory.Name, "dotnet", StringComparison.Ordinal)
                && Directory.Exists(Path.Combine(directory.FullName, "src", "Zlink.Framework")))
            {
                return directory.FullName;
            }

            directory = directory.Parent!;
        }

        throw new InvalidOperationException("Could not locate framework dotnet root.");
    }
}
