using SpotService.Client;
using SpotService.Client.Scenarios;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);

using var gateway = ZLinkHttpClient.Create(options.GatewayUrl)
    .Json()
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var playA = ZLinkHttpClient.Create(options.PlayAUrl)
    .Json()
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var playB = ZLinkHttpClient.Create(options.PlayBUrl)
    .Json()
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var multiA = ZLinkHttpClient.Create(options.MultiAUrl)
    .Json()
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var multiB = ZLinkHttpClient.Create(options.MultiBUrl)
    .Json()
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var sessionA = ZLinkHttpClient.Create(options.SessionAUrl)
    .Json()
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();

await (options.OperationGroup switch
{
    "sm-b1-b2-b3-b5" => RunB1B2B3B5Async(playA, playB, options.SessionAStreamEndpoint),
    "sm-b6" => SmB6Scenario.RunAsync(playA, sessionA, options.SessionAStreamEndpoint),
    "sm-b8" => SmB8Scenario.RunAsync(playA, options.SessionAStreamEndpoint),
    "sm-d1-d6" => RunD1D2D6Async(sessionA, options.SessionAStreamEndpoint, options.SessionBStreamEndpoint),
    "sm-d3" => SmD3Scenario.RunAsync(playA, options.SessionAStreamEndpoint),
    "sm-d4" => SmD4Scenario.RunAsync(options.SessionAStreamEndpoint),
    "sm-d5" => SmD5Scenario.RunAsync(sessionA, options.SessionAStreamEndpoint),
    "sm-d7" => SmD7Scenario.RunAsync(options.SessionAStreamEndpoint),
    "sm-d8" => SmD8Scenario.RunAsync(options.SessionAStreamEndpoint),
    "sm-d9-d11-d13" => RunD9D11D13Async(sessionA, options.SessionAStreamEndpoint),
    "sm-d10" => SmD10Scenario.RunAsync(options.SessionAStreamEndpoint, options.SessionBStreamEndpoint),
    "sm-d12" => SmD12Scenario.RunAsync(options.SessionAStreamEndpoint, options.SessionBStreamEndpoint),
    "sm-d14" => SmD14Scenario.RunAsync(options.SessionATlsStreamEndpoint),
    "sm-c1-c2" => RunC1C2Async(playA),
    "sm-c3" => SmC3Scenario.RunAsync(playA),
    "sm-g2" => SmG2Scenario.RunAsync(playA, playB),
    "sm-g3" => SmG3Scenario.RunAsync(playA, options.SessionAStreamEndpoint),
    "sm-g4" => SmG4Scenario.RunAsync(options.SessionAStreamEndpoint),
    "sm-g1" => SmG1Scenario.RunAsync(options.PlayAUrl, options.SessionAStreamEndpoint, options.SessionBStreamEndpoint),
    "sm-q9" => SmQ9Scenario.RunAsync(multiA, multiB),
    "sm-e1-f4" => RunE1F4Async(playA),
    "sm-e2-e3" => RunE2E3Async(playA),
    "sm-a7-a8-c4" => RunA7A8C4Async(playA, gateway),
    "sm-a3-a6-b4-b7" => RunA3A6B4B7Async(playA, playB, options.SessionAStreamEndpoint),
    "sm-a5" => SmA5Scenario.RunAsync(playA),
    "sm-a1-a2-a4-f1-f2" => RunA1A2A4F1F2Async(playA),
    "sm-e4" => SmE4Scenario.RunAsync(playA),
    _ => throw new InvalidOperationException($"Unsupported SpotService operation group '{options.OperationGroup}'."),
});

Console.WriteLine($"spot-service client operation_group={options.OperationGroup} result=passed");

static async Task RunA1A2A4F1F2Async(ZLinkHttpClient playA)
{
    var context = new SpotLifecycleOrderContext();
    await SmA1Scenario.RunAsync(playA, context);
    await SmA4Scenario.RunAsync(playA, context);
    await SmF1Scenario.RunAsync(playA, context);
    await SmF2Scenario.RunAsync(playA, context);
    await SmA2Scenario.RunAsync(playA, context);
}

static async Task RunA3A6B4B7Async(
    ZLinkHttpClient playA,
    ZLinkHttpClient playB,
    string sessionAStreamEndpoint)
{
    await SmA3Scenario.RunAsync(playA, playB);
    await SmA6Scenario.RunAsync(playA);
    await SmB4Scenario.RunAsync(playB, sessionAStreamEndpoint);
    await SmB7Scenario.RunAsync(playA, sessionAStreamEndpoint);
}

static async Task RunB1B2B3B5Async(
    ZLinkHttpClient playA,
    ZLinkHttpClient playB,
    string sessionAStreamEndpoint)
{
    await SmB1Scenario.RunAsync(playA, sessionAStreamEndpoint);
    await SmB2Scenario.RunAsync(playB, sessionAStreamEndpoint);
    await SmB3Scenario.RunAsync(playA, sessionAStreamEndpoint);
    await SmB5Scenario.RunAsync(playA, sessionAStreamEndpoint);
}

static async Task RunD1D2D6Async(
    ZLinkHttpClient sessionA,
    string sessionAStreamEndpoint,
    string sessionBStreamEndpoint)
{
    await SmD1Scenario.RunAsync(sessionA, sessionAStreamEndpoint);
    await SmD2Scenario.RunAsync(sessionA, sessionAStreamEndpoint);
    await SmD6Scenario.RunAsync(sessionAStreamEndpoint, sessionBStreamEndpoint);
}

static async Task RunC1C2Async(ZLinkHttpClient playA)
{
    await SmC1Scenario.RunAsync(playA);
    await SmC2Scenario.RunAsync(playA);
}

static async Task RunE1F4Async(ZLinkHttpClient playA)
{
    await SmE1Scenario.RunAsync(playA);
    await SmF4Scenario.RunAsync(playA);
}

static async Task RunE2E3Async(ZLinkHttpClient playA)
{
    await SmE2Scenario.RunAsync(playA);
    await SmE3Scenario.RunAsync(playA);
}

static async Task RunA7A8C4Async(ZLinkHttpClient playA, ZLinkHttpClient gateway)
{
    await SmA7Scenario.RunAsync(playA);
    await SmA8Scenario.RunAsync(playA);
    await SmC4Scenario.RunAsync(playA, gateway);
}

static async Task RunD9D11D13Async(ZLinkHttpClient sessionA, string sessionAStreamEndpoint)
{
    await SmD9Scenario.RunAsync(sessionAStreamEndpoint);
    await SmD11Scenario.RunAsync(sessionA, sessionAStreamEndpoint);
    await SmD13Scenario.RunAsync(sessionAStreamEndpoint);
}
