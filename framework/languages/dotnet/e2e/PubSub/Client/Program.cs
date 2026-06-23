using System.Net.Http.Json;
using System.Diagnostics;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using PubSub.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);
Process? restartedPublisher = null;

using var host = Host.CreateDefaultBuilder(args)
    .ConfigureServices(services =>
    {
        services.AddZLinkFramework(framework =>
        {
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, "client-flow.log"))
                .TraceNodeId("client");
        });
    })
    .Build();
await host.StartAsync();

using var http = new HttpClient();
var runId = Guid.NewGuid().ToString("N");

var warmupStart = 1;
for (var i = warmupStart; i < warmupStart + 20; i++)
{
    await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, runId, i, $"warmup-{i}");
}

await WaitUntilAsync(async () =>
{
    var snapshots = await ReadSubscribers(http, options);
    return snapshots.All(lines => lines.Any(line => IsEvent(line, runId, PubSubNames.MainTopic)));
}, "PS-A1 expected all initial subscribers to receive warm-up events.");

var measureStart = 100;
var measureCount = 12;
for (var i = measureStart; i < measureStart + measureCount; i++)
{
    await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, runId, i, $"measure-{i}");
}

await WaitUntilAsync(async () =>
{
    var snapshots = await ReadSubscribers(http, options);
    return CommonContiguousSequence(snapshots, runId, PubSubNames.MainTopic, measureStart, measureStart + measureCount - 1).Count >= 3;
}, "PS-A1 expected common contiguous sequence on all subscribers.");
Console.WriteLine("scenario PS-A1 passed");

var filterRun = Guid.NewGuid().ToString("N");
await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, filterRun, 1, "accepted");
await PublishEvent(http, options.PublisherUrl, PubSubNames.OtherTopic, filterRun, 2, "ignored");
await WaitUntilAsync(async () =>
{
    var snapshots = await ReadSubscribers(http, options);
    return snapshots.All(lines => lines.Any(line => IsEvent(line, filterRun, PubSubNames.MainTopic)))
        && snapshots.All(lines => lines.Any(line => IsIgnored(line, filterRun, PubSubNames.OtherTopic)));
}, "PS-A2 expected topic-based application filtering evidence.");
var filterSnapshots = await ReadSubscribers(http, options);
Ensure(filterSnapshots.All(lines => lines.Any(line => IsEvent(line, filterRun, PubSubNames.MainTopic))),
    "PS-A2 accepted topic was not recorded.");
Ensure(filterSnapshots.All(lines => lines.All(line =>
    !line.Contains($"event|", StringComparison.Ordinal)
    || !line.Contains($"run={filterRun}", StringComparison.Ordinal)
    || !line.Contains($"topic={PubSubNames.OtherTopic}", StringComparison.Ordinal))),
    "PS-A2 non-interest topic was recorded as accepted.");
Console.WriteLine("scenario PS-A2 passed");

var beforeLateRun = Guid.NewGuid().ToString("N");
for (var i = 1; i <= 5; i++)
{
    await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, beforeLateRun, i, $"before-late-{i}");
}

using var lateSubscriber = StartSubscriber(
    options,
    name: "sub-late",
    httpUrl: options.LateSubscriberUrl,
    evidenceFile: "sub-late.evidence.log");
try
{
    await WaitUntilAsync(async () => await IsHealthy(http, options.LateSubscriberUrl),
        "PS-A3 expected late subscriber to become healthy.");

    var afterLateRun = Guid.NewGuid().ToString("N");
    for (var i = 1; i <= 8; i++)
    {
        await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, afterLateRun, i, $"after-late-{i}");
    }

    await WaitUntilAsync(async () =>
    {
        var late = await ReadEvidence(http, options.LateSubscriberUrl);
        return late.Any(line => IsEvent(line, afterLateRun, PubSubNames.MainTopic));
    }, "PS-A3 expected late subscriber to receive post-join events.");
    var lateEvidence = await ReadEvidence(http, options.LateSubscriberUrl);
    Ensure(lateEvidence.All(line => !line.Contains($"run={beforeLateRun}", StringComparison.Ordinal)),
        "PS-A3 late subscriber replayed pre-join events.");
    Console.WriteLine("scenario PS-A3 passed");
}
finally
{
    if (!lateSubscriber.HasExited)
    {
        lateSubscriber.Kill(entireProcessTree: true);
        await lateSubscriber.WaitForExitAsync();
    }
}

var reconnectRun = Guid.NewGuid().ToString("N");
using (var reconnectSubscriber = StartSubscriber(
    options,
    name: "sub-reconnect",
    httpUrl: options.LateSubscriberUrl,
    evidenceFile: "sub-reconnect.evidence.log"))
{
    await WaitUntilAsync(async () => await IsHealthy(http, options.LateSubscriberUrl),
        "PS-A4 expected reconnect subscriber to become healthy before disconnect.");
    await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, reconnectRun, 1, "before-disconnect");
    await WaitUntilAsync(async () =>
    {
        var evidence = await ReadEvidence(http, options.LateSubscriberUrl);
        return evidence.Any(line => IsEvent(line, reconnectRun, PubSubNames.MainTopic));
    }, "PS-A4 expected subscriber to receive before disconnect.");

    reconnectSubscriber.Kill(entireProcessTree: true);
    await reconnectSubscriber.WaitForExitAsync();
}

await WaitUntilAsync(async () => !await IsHealthy(http, options.LateSubscriberUrl),
    "PS-A4 expected reconnect subscriber to leave before gap publish.");
for (var i = 2; i <= 4; i++)
{
    await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, reconnectRun, i, $"gap-{i}");
}

await WaitUntilAsync(async () =>
{
    var fastSnapshots = await ReadFastSubscribers(http, options);
    return fastSnapshots.All(lines => lines.Any(line => IsEvent(line, reconnectRun, PubSubNames.MainTopic)));
}, "PS-A4 expected other subscribers to keep receiving while one subscriber is disconnected.");

using (var restartedSubscriber = StartSubscriber(
    options,
    name: "sub-reconnect",
    httpUrl: options.LateSubscriberUrl,
    evidenceFile: "sub-reconnect.evidence.log"))
{
    await WaitUntilAsync(async () => await IsHealthy(http, options.LateSubscriberUrl),
        "PS-A4 expected reconnect subscriber to become healthy after reconnect.");
    for (var i = 5; i <= 8; i++)
    {
        await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, reconnectRun, i, $"after-reconnect-{i}");
    }

    await WaitUntilAsync(async () =>
    {
        var evidence = await ReadEvidence(http, options.LateSubscriberUrl);
        return evidence.Any(line => IsEvent(line, reconnectRun, PubSubNames.MainTopic));
    }, "PS-A4 expected reconnected subscriber to receive post-reconnect events.");
    var reconnectEvidence = await ReadEvidence(http, options.LateSubscriberUrl);
    Ensure(reconnectEvidence.All(line =>
        !line.Contains($"run={reconnectRun}", StringComparison.Ordinal)
        || !line.Contains("value=gap-", StringComparison.Ordinal)),
        "PS-A4 reconnected subscriber replayed disconnect-gap events.");
    Console.WriteLine("scenario PS-A4 passed");

    restartedSubscriber.Kill(entireProcessTree: true);
    await restartedSubscriber.WaitForExitAsync();
}

var slowRun = Guid.NewGuid().ToString("N");
for (var i = 1; i <= 8; i++)
{
    var value = i == 1 ? "slow-first" : $"fast-after-slow-{i}";
    await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, slowRun, i, value);
}

await WaitUntilAsync(async () =>
{
    var fastSnapshots = await ReadFastSubscribers(http, options);
    return fastSnapshots.All(lines => lines.Any(line => IsEvent(line, slowRun, PubSubNames.MainTopic)
        && line.Contains("seq=8", StringComparison.Ordinal)));
}, "PS-B1 expected fast subscribers to keep receiving while another handler is slow.", timeout: TimeSpan.FromSeconds(2));
var slowEvidence = await ReadEvidence(http, options.SubscriberUrls[^1]);
Ensure(slowEvidence.Any(line => line.Contains("delay-start|", StringComparison.Ordinal)
    && line.Contains($"run={slowRun}", StringComparison.Ordinal)),
    "PS-B1 expected slow subscriber delay evidence.");
Console.WriteLine("scenario PS-B1 passed");

var publisherRestartRun = Guid.NewGuid().ToString("N");
await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, publisherRestartRun, 1, "before-publisher-restart");
await WaitUntilAsync(async () =>
{
    var snapshots = await ReadSubscribers(http, options);
    return snapshots.All(lines => lines.Any(line => IsEvent(line, publisherRestartRun, PubSubNames.MainTopic)));
}, "PS-B2 expected baseline publish before publisher restart.");

using (var shutdownResponse = await http.PostAsync($"{options.PublisherUrl}/shutdown", content: null))
{
    shutdownResponse.EnsureSuccessStatusCode();
}

await WaitUntilAsync(async () => !await IsHealthy(http, options.PublisherUrl),
    "PS-B2 expected publisher to stop before restart.");
try
{
    await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, publisherRestartRun, 2, "during-publisher-down");
    throw new InvalidOperationException("PS-B2 expected publish attempt to fail while publisher is down.");
}
catch (HttpRequestException)
{
}

restartedPublisher = StartPublisher(options);
await WaitUntilAsync(async () => await IsHealthy(http, options.PublisherUrl),
    "PS-B2 expected restarted publisher to become healthy.");
await Task.Delay(500);
for (var i = 3; i <= 42; i++)
{
    await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, publisherRestartRun, i, $"after-publisher-restart-{i}");
    await Task.Delay(100);
}

await WaitUntilAsync(async () =>
{
    var snapshots = await ReadSubscribers(http, options);
    return snapshots.All(lines => lines.Any(line => IsEvent(line, publisherRestartRun, PubSubNames.MainTopic)
        && ExtractInt(line, "seq") >= 20));
}, "PS-B2 expected publish delivery after publisher restart.");
Console.WriteLine("scenario PS-B2 passed");

var negativeRun = Guid.NewGuid().ToString("N");
await PublishMissing(http, options.PublisherUrl, PubSubNames.MainTopic, negativeRun, 1, "missing");
await WaitUntilAsync(async () =>
{
    var snapshots = await ReadSubscribers(http, options);
    return snapshots.All(lines => lines.Any(line =>
        line.Contains("dispatch-error|", StringComparison.Ordinal)
        && line.Contains($"packet=MissingEventNotify", StringComparison.Ordinal)
        && line.Contains($"topic={PubSubNames.MainTopic}", StringComparison.Ordinal)));
}, "PS-C1 expected subscriber dispatch-error evidence for missing publish handler.");
await PublishEvent(http, options.PublisherUrl, PubSubNames.MainTopic, negativeRun, 2, "after-missing");
await WaitUntilAsync(async () =>
{
    var snapshots = await ReadSubscribers(http, options);
    return snapshots.All(lines => lines.Any(line => IsEvent(line, negativeRun, PubSubNames.MainTopic)));
}, "PS-C1 expected normal publish after missing handler to keep working.");
Console.WriteLine("scenario PS-C1 passed");

if (restartedPublisher is { HasExited: false })
{
    using var shutdownResponse = await http.PostAsync($"{options.PublisherUrl}/shutdown", content: null);
    shutdownResponse.EnsureSuccessStatusCode();
    await restartedPublisher.WaitForExitAsync();
}

Console.WriteLine("pubsub e2e result=passed");
await host.StopAsync();

static async Task PublishEvent(
    HttpClient http,
    string publisherUrl,
    string topic,
    string runId,
    int sequence,
    string value)
{
    var url = $"{publisherUrl}/publish/event?topic={Uri.EscapeDataString(topic)}"
        + $"&runId={Uri.EscapeDataString(runId)}"
        + $"&sequence={sequence}"
        + $"&value={Uri.EscapeDataString(value)}";
    using var response = await http.PostAsync(url, content: null);
    response.EnsureSuccessStatusCode();
}

static async Task PublishMissing(
    HttpClient http,
    string publisherUrl,
    string topic,
    string runId,
    int sequence,
    string value)
{
    var url = $"{publisherUrl}/publish/missing?topic={Uri.EscapeDataString(topic)}"
        + $"&runId={Uri.EscapeDataString(runId)}"
        + $"&sequence={sequence}"
        + $"&value={Uri.EscapeDataString(value)}";
    using var response = await http.PostAsync(url, content: null);
    response.EnsureSuccessStatusCode();
}

static Process StartSubscriber(
    ClientOptions options,
    string name,
    string httpUrl,
    string evidenceFile)
{
    var stdout = Path.Combine(options.LogDir, $"{name}.stdout.log");
    var stderr = Path.Combine(options.LogDir, $"{name}.stderr.log");
    var startInfo = new ProcessStartInfo("dotnet")
    {
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false,
    };
    startInfo.Environment["ZLINK_E2E_RID"] = name;
    startInfo.ArgumentList.Add("run");
    startInfo.ArgumentList.Add("--project");
    startInfo.ArgumentList.Add(options.ServerProject);
    startInfo.ArgumentList.Add("--");
    startInfo.ArgumentList.Add("--role");
    startInfo.ArgumentList.Add("subscriber");
    startInfo.ArgumentList.Add("--rid");
    startInfo.ArgumentList.Add(name);
    startInfo.ArgumentList.Add("--http-url");
    startInfo.ArgumentList.Add(httpUrl);
    startInfo.ArgumentList.Add("--registry-router-endpoint");
    startInfo.ArgumentList.Add(options.RegistryRouterEndpoint);
    startInfo.ArgumentList.Add("--publisher-endpoint");
    startInfo.ArgumentList.Add(options.PublisherEndpoint);
    startInfo.ArgumentList.Add("--evidence-file");
    startInfo.ArgumentList.Add(Path.Combine(options.LogDir, evidenceFile));
    startInfo.ArgumentList.Add("--log-dir");
    startInfo.ArgumentList.Add(options.LogDir);

    var process = Process.Start(startInfo)
        ?? throw new InvalidOperationException($"Failed to start {name}.");
    _ = Task.Run(async () => await File.WriteAllTextAsync(stdout, await process.StandardOutput.ReadToEndAsync()));
    _ = Task.Run(async () => await File.WriteAllTextAsync(stderr, await process.StandardError.ReadToEndAsync()));
    return process;
}

static Process StartPublisher(ClientOptions options)
{
    var stdout = Path.Combine(options.LogDir, "pub-restart.stdout.log");
    var stderr = Path.Combine(options.LogDir, "pub-restart.stderr.log");
    var startInfo = new ProcessStartInfo("dotnet")
    {
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false,
    };
    startInfo.Environment["ZLINK_E2E_RID"] = "pub-a";
    startInfo.ArgumentList.Add("run");
    startInfo.ArgumentList.Add("--project");
    startInfo.ArgumentList.Add(options.ServerProject);
    startInfo.ArgumentList.Add("--");
    startInfo.ArgumentList.Add("--role");
    startInfo.ArgumentList.Add("publisher");
    startInfo.ArgumentList.Add("--rid");
    startInfo.ArgumentList.Add("pub-a");
    startInfo.ArgumentList.Add("--http-url");
    startInfo.ArgumentList.Add(options.PublisherUrl);
    startInfo.ArgumentList.Add("--registry-router-endpoint");
    startInfo.ArgumentList.Add(options.RegistryRouterEndpoint);
    startInfo.ArgumentList.Add("--publisher-endpoint");
    startInfo.ArgumentList.Add(options.PublisherEndpoint);
    startInfo.ArgumentList.Add("--evidence-file");
    startInfo.ArgumentList.Add(Path.Combine(options.LogDir, "pub-restart.evidence.log"));
    startInfo.ArgumentList.Add("--log-dir");
    startInfo.ArgumentList.Add(options.LogDir);

    var process = Process.Start(startInfo)
        ?? throw new InvalidOperationException("Failed to start restarted publisher.");
    _ = Task.Run(async () => await File.WriteAllTextAsync(stdout, await process.StandardOutput.ReadToEndAsync()));
    _ = Task.Run(async () => await File.WriteAllTextAsync(stderr, await process.StandardError.ReadToEndAsync()));
    return process;
}

static async Task<string[][]> ReadSubscribers(HttpClient http, ClientOptions options)
{
    var results = new List<string[]>();
    foreach (var url in options.SubscriberUrls)
    {
        results.Add(await ReadEvidence(http, url));
    }

    return results.ToArray();
}

static async Task<string[][]> ReadFastSubscribers(HttpClient http, ClientOptions options)
{
    var results = new List<string[]>();
    foreach (var url in options.SubscriberUrls.Take(2))
    {
        results.Add(await ReadEvidence(http, url));
    }

    return results.ToArray();
}

static async Task<string[]> ReadEvidence(HttpClient http, string baseUrl)
{
    return await http.GetFromJsonAsync<string[]>($"{baseUrl}/evidence") ?? [];
}

static async Task<bool> IsHealthy(HttpClient http, string baseUrl)
{
    try
    {
        using var response = await http.GetAsync($"{baseUrl}/health");
        return response.IsSuccessStatusCode;
    }
    catch (HttpRequestException)
    {
        return false;
    }
}

static IReadOnlyList<int> CommonContiguousSequence(
    string[][] snapshots,
    string runId,
    string topic,
    int min,
    int max)
{
    var common = snapshots
        .Select(lines => lines
            .Where(line => IsEvent(line, runId, topic))
            .Select(line => ExtractInt(line, "seq"))
            .Where(seq => seq >= min && seq <= max)
            .ToHashSet())
        .Aggregate((left, right) =>
        {
            left.IntersectWith(right);
            return left;
        });
    var sorted = common.Order().ToArray();
    var best = new List<int>();
    var current = new List<int>();
    foreach (var seq in sorted)
    {
        if (current.Count == 0 || current[^1] + 1 == seq)
        {
            current.Add(seq);
        }
        else
        {
            if (current.Count > best.Count)
            {
                best = [.. current];
            }

            current = [seq];
        }
    }

    if (current.Count > best.Count)
    {
        best = current;
    }

    return best;
}

static bool IsEvent(string line, string runId, string topic)
{
    return line.Contains("event|", StringComparison.Ordinal)
        && line.Contains($"run={runId}", StringComparison.Ordinal)
        && line.Contains($"topic={topic}", StringComparison.Ordinal);
}

static bool IsIgnored(string line, string runId, string topic)
{
    return line.Contains("ignored|", StringComparison.Ordinal)
        && line.Contains($"run={runId}", StringComparison.Ordinal)
        && line.Contains($"topic={topic}", StringComparison.Ordinal);
}

static int ExtractInt(string line, string key)
{
    var marker = key + "=";
    var start = line.IndexOf(marker, StringComparison.Ordinal);
    if (start < 0)
    {
        return 0;
    }

    start += marker.Length;
    var end = line.IndexOf('|', start);
    var value = end < 0 ? line[start..] : line[start..end];
    return int.Parse(value);
}

static async Task WaitUntilAsync(
    Func<Task<bool>> condition,
    string failureMessage,
    TimeSpan? timeout = null)
{
    using var timeoutSource = new CancellationTokenSource(timeout ?? TimeSpan.FromSeconds(20));
    while (!timeoutSource.IsCancellationRequested)
    {
        if (await condition())
        {
            return;
        }

        await Task.Delay(100, timeoutSource.Token).ContinueWith(_ => { });
    }

    throw new InvalidOperationException(failureMessage);
}

static void Ensure(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

internal sealed record ClientOptions(
    string PublisherUrl,
    string LateSubscriberUrl,
    string RegistryRouterEndpoint,
    string PublisherEndpoint,
    string ServerProject,
    string LogDir,
    string[] SubscriberUrls)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Unexpected argument '{key}'.");
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for '{key}'.");
            }

            var value = args[++i];
            if (!values.TryGetValue(key, out var bucket))
            {
                bucket = [];
                values.Add(key, bucket);
            }

            bucket.Add(value);
        }

        string GetOne(string name) => values.TryGetValue(name, out var bucket)
            ? bucket[^1]
            : throw new ArgumentException($"{name} is required.");
        string[] GetMany(string name) => values.TryGetValue(name, out var bucket)
            ? [.. bucket]
            : throw new ArgumentException($"{name} is required.");

        return new ClientOptions(
            PublisherUrl: GetOne("--publisher-url"),
            LateSubscriberUrl: GetOne("--late-subscriber-url"),
            RegistryRouterEndpoint: GetOne("--registry-router-endpoint"),
            PublisherEndpoint: GetOne("--publisher-endpoint"),
            ServerProject: GetOne("--server-project"),
            LogDir: GetOne("--log-dir"),
            SubscriberUrls: GetMany("--subscriber-url"));
    }
}
