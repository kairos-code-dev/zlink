using System.Net.Http.Json;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using ResilienceLifecycle.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);

using var host = Host.CreateDefaultBuilder(args)
    .ConfigureServices(services =>
    {
        services.AddZLinkFramework(framework =>
        {
            framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, "client-flow.log"))
                .TraceNodeId("client");
            framework.AddClientServerChannel(ResilienceLifecycleNames.Channel).EnableClient();
        });
    })
    .Build();
await host.StartAsync();
var client = host.Services.GetRequiredService<IZLinkChannelClient>();
using var http = new HttpClient();

await WaitForProvidersAsync(client, "rl-bootstrap", ["api-a", "api-b"]);
await RunRlB4Async(client, http, options);
await RunRlB5Async(client, http, options);

Console.WriteLine("resilience-lifecycle e2e result=passed");
await host.StopAsync();

static async Task RunRlB4Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    var beforeDrain = await ReadEvidenceAsync(http, options.ProviderBUrl);
    await PostAsync(http, $"{options.ProviderBUrl}/admin/drain");
    await WaitForWeightAsync(http, options.ProviderBUrl, 0);

    for (var i = 0; i < 20; i++)
    {
        var marker = $"rl-b4-drained-{i}";
        var reply = await RequestAsync(client, "fast", marker);
        Ensure(reply.ProviderRid == "api-a", "RL-B4 drained api-b received a new request.");
    }

    var afterDrain = await ReadEvidenceAsync(http, options.ProviderBUrl);
    Ensure(CountNew(afterDrain, beforeDrain, "profile-request|rid=api-b|marker=rl-b4-drained-") == 0,
        "RL-B4 api-b evidence changed after drain.");
    var duringDrainA = await ReadEvidenceAsync(http, options.ProviderAUrl);
    Ensure(duringDrainA.Any(line => line.Contains("profile-request|rid=api-a|marker=rl-b4-drained-", StringComparison.Ordinal)),
        "RL-B4 api-a did not receive drained traffic.");

    await PostAsync(http, $"{options.ProviderBUrl}/admin/restore");
    await WaitForWeightAsync(http, options.ProviderBUrl, 100);
    await WaitUntilAsync(async () =>
    {
        var marker = $"rl-b4-restored-{Guid.NewGuid():N}";
        var reply = await RequestAsync(client, "fast", marker);
        return reply.ProviderRid == "api-b";
    }, "RL-B4 restored api-b did not re-enter routing.");

    Console.WriteLine("scenario RL-B4 passed");
}

static async Task RunRlB5Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    await PostAsync(http, $"{options.ProviderBUrl}/admin/restore");
    await WaitForWeightAsync(http, options.ProviderBUrl, 100);

    var slowMarker = $"rl-b5-slow-{Guid.NewGuid():N}";
    var slowTask = RequestAsync(client, "slow", slowMarker);
    var slowProvider = await WaitForSlowStartAsync(http, options, slowMarker);
    var drainedUrl = slowProvider == "api-a" ? options.ProviderAUrl : options.ProviderBUrl;
    var healthyProvider = slowProvider == "api-a" ? "api-b" : "api-a";
    var drainedPattern = $"profile-request|rid={slowProvider}|marker=rl-b5-drained-";

    var beforeDrain = await ReadEvidenceAsync(http, drainedUrl);
    await PostAsync(http, $"{drainedUrl}/admin/drain");
    await WaitForWeightAsync(http, drainedUrl, 0);

    for (var i = 0; i < 12; i++)
    {
        var reply = await RequestAsync(client, "fast", $"rl-b5-drained-{i}");
        Ensure(reply.ProviderRid == healthyProvider, "RL-B5 drain did not block new requests to the drained provider.");
    }

    var slowReply = await slowTask;
    Ensure(slowReply.ProviderRid == slowProvider && slowReply.Marker == slowMarker,
        "RL-B5 in-flight slow reply did not complete on the drained provider.");
    var afterDrain = await ReadEvidenceAsync(http, drainedUrl);
    Ensure(CountNew(afterDrain, beforeDrain, drainedPattern) == 0,
        "RL-B5 drained provider accepted new requests after drain.");
    Ensure(afterDrain.Any(line => line.Contains($"profile-request|rid={slowProvider}|marker={slowMarker}", StringComparison.Ordinal)),
        "RL-B5 in-flight completion evidence missing.");
    Console.WriteLine("scenario RL-B5 passed");
}

static async Task<string> WaitForSlowStartAsync(HttpClient http, ClientOptions options, string marker)
{
    string? provider = null;
    await WaitUntilAsync(async () =>
    {
        var apiA = await ReadEvidenceAsync(http, options.ProviderAUrl);
        if (apiA.Any(line => line.Contains($"profile-start|rid=api-a|marker={marker}", StringComparison.Ordinal)))
        {
            provider = "api-a";
            return true;
        }

        var apiB = await ReadEvidenceAsync(http, options.ProviderBUrl);
        if (apiB.Any(line => line.Contains($"profile-start|rid=api-b|marker={marker}", StringComparison.Ordinal)))
        {
            provider = "api-b";
            return true;
        }

        return false;
    }, "RL-B5 slow request did not start on either provider.");
    return provider!;
}

static async Task WaitForProvidersAsync(
    IZLinkChannelClient client,
    string markerPrefix,
    string[] expectedProviders)
{
    var seen = new HashSet<string>(StringComparer.Ordinal);
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
    var attempt = 0;
    while (!timeout.IsCancellationRequested && !expectedProviders.All(seen.Contains))
    {
        var reply = await RequestAsync(client, "fast", $"{markerPrefix}-{attempt++}");
        seen.Add(reply.ProviderRid);
        await Task.Delay(50, timeout.Token).ContinueWith(_ => { });
    }

    Ensure(expectedProviders.All(seen.Contains),
        $"Expected providers {string.Join(",", expectedProviders)}, saw {string.Join(",", seen)}.");
}

static async Task<ProfileReply> RequestAsync(IZLinkChannelClient client, string value, string marker)
{
    return await client.RequestToChannel(
            ResilienceLifecycleNames.Channel,
            new ProfileRequest(value, marker))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(3))
        .Async<ProfileReply>();
}

static async Task PostAsync(HttpClient http, string url)
{
    using var response = await http.PostAsync(url, content: null);
    response.EnsureSuccessStatusCode();
}

static async Task WaitForWeightAsync(HttpClient http, string baseUrl, int expected)
{
    await WaitUntilAsync(async () =>
    {
        var weight = await http.GetFromJsonAsync<WeightResponse>($"{baseUrl}/admin/weight");
        return weight?.Weight == expected;
    }, $"Expected {baseUrl} weight {expected}.");
}

static async Task<string[]> ReadEvidenceAsync(HttpClient http, string baseUrl)
{
    return await http.GetFromJsonAsync<string[]>($"{baseUrl}/evidence") ?? [];
}

static int CountNew(string[] after, string[] before, string pattern)
{
    return after.Count(line => line.Contains(pattern, StringComparison.Ordinal))
        - before.Count(line => line.Contains(pattern, StringComparison.Ordinal));
}

static async Task WaitUntilAsync(Func<Task<bool>> condition, string failureMessage)
{
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
    while (!timeout.IsCancellationRequested)
    {
        if (await condition())
        {
            return;
        }

        await Task.Delay(100, timeout.Token).ContinueWith(_ => { });
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

internal sealed record WeightResponse(int Weight);

internal sealed record ClientOptions(
    string RegistryRouterEndpoint,
    string ProviderAUrl,
    string ProviderBUrl,
    string LogDir)
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

        string Get(string name) => values.TryGetValue(name, out var bucket)
            ? bucket[^1]
            : throw new ArgumentException($"{name} is required.");
        return new ClientOptions(
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            ProviderAUrl: Get("--provider-a-url"),
            ProviderBUrl: Get("--provider-b-url"),
            LogDir: Get("--log-dir"));
    }
}
