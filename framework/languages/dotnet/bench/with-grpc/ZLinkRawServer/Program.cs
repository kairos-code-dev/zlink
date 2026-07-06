using Systems.Zlink;
using WithGrpcBench.Shared;

var endpoint = ArgValue(args, "--endpoint") ?? "tcp://127.0.0.1:5075";
var commandEndpoint = ArgValue(args, "--command-endpoint") ?? "tcp://127.0.0.1:5077";
var metricsUrl = ArgValue(args, "--metrics-url") ?? "http://127.0.0.1:5076";

var builder = WebApplication.CreateBuilder(args);
ConfigureQuietLogging(builder);
builder.WebHost.UseUrls(metricsUrl);
builder.Services.AddSingleton<BenchServerMetrics>();

var app = builder.Build();
app.MapGet("/ready", () => Results.Ok("ready"));
app.MapPost("/bench/reset", (BenchServerMetrics metrics) =>
{
    metrics.Reset();
    return Results.Ok();
});
app.MapGet("/bench/stats", (BenchServerMetrics metrics) => Results.Ok(metrics.Snapshot()));

using var context = Zlink.CreateContext();
using var requestRouter = context.CreateRouterSocket();
using var commandRouter = context.CreateRouterSocket();
requestRouter.Bind(endpoint);
commandRouter.Bind(commandEndpoint);

var metrics = app.Services.GetRequiredService<BenchServerMetrics>();
var requestReceiver = Task.Run(() => RunRequestRouter(requestRouter, metrics));
var commandReceiver = Task.Run(() => RunCommandRouter(commandRouter, metrics));

await app.RunAsync();
await Task.WhenAll(requestReceiver, commandReceiver);

static void ConfigureQuietLogging(WebApplicationBuilder builder)
{
    builder.Logging.ClearProviders();
    builder.Logging.AddConsole();
    builder.Logging.SetMinimumLevel(LogLevel.Warning);
}

static void RunRequestRouter(IRouterSocket router, BenchServerMetrics metrics)
{
    using var received = Received.Create();
    while (true)
    {
        try
        {
            if (!router.Recv(received))
            {
                continue;
            }

            var body = PayloadPart(received);
            if (received.RequestSeq.HasValue)
            {
                received.Reply()
                    .Message(body)
                    .Submit();
            }
            else
            {
                received.Send()
                    .Message(body)
                    .Submit();
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"raw zlink request loop failed: {ex.Message}");
            metrics.RecordError();
        }
    }
}

static void RunCommandRouter(IRouterSocket router, BenchServerMetrics metrics)
{
    using var received = Received.Create();
    while (true)
    {
        try
        {
            if (!router.Recv(received))
            {
                continue;
            }

            var body = PayloadPart(received);
            metrics.Record(body.AsReadOnlySpan());
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"raw zlink command loop failed: {ex.Message}");
            metrics.RecordError();
        }
    }
}

static Message PayloadPart(Received received)
{
    if (received.IsSinglePart)
    {
        return received.FirstPart();
    }

    return received.Parts[^1];
}

static string? ArgValue(string[] args, string name)
{
    for (var i = 0; i < args.Length - 1; i++)
    {
        if (args[i] == name) return args[i + 1];
    }

    return null;
}
