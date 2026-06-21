using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Contracts.Channels;
using Zlink.HttpClient;
using static Zlink.Framework.ScenarioE2E.Scenario;

namespace Zlink.Framework.ScenarioE2E;

// E2E doc chapter 7 — dispatch errors and observability. The failure path has two
// halves that must agree: the client sees a public error (or a silent drop), and
// the server's dispatch-error observer records a matching marker as evidence.
internal static class Ch07DispatchError
{
    // DERR-001 — a request whose packet has no handler returns a public error and
    // is reported as handlerMissing / replyError.
    public static async Task UnregisteredRequest(ScenarioContext ctx)
    {
        using var host = await ScenarioClient.ConnectChannelAsync(ctx);
        var client = host.Services.GetRequiredService<IZLinkChannelClient>();

        var clientError = false;
        try
        {
            _ = await client
                .RequestToChannel(ctx.Channel, new ScenarioProfileRequest("derr-001-missing-1", "missing"))
                .PacketName("MissingScenarioRequest")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<ScenarioProfileReply>();
        }
        catch (Exception error)
        {
            clientError = true;
            Ensure(error.Message.Contains("MissingScenarioRequest", StringComparison.Ordinal));
        }

        Ensure(clientError);
        await EnsureDispatchMarkerAsync(
            ctx,
            "surface=channel",
            "kind=request",
            "reason=handlerMissing",
            "action=replyError",
            "packetName=MissingScenarioRequest");

        await host.StopAsync();
    }

    // DERR-002 — a one-way send whose packet has no handler is dropped silently and
    // reported as handlerMissing / drop.
    public static async Task UnregisteredSend(ScenarioContext ctx)
    {
        using var host = await ScenarioClient.ConnectChannelAsync(ctx);
        var client = host.Services.GetRequiredService<IZLinkChannelClient>();

        await client
            .SendToChannel(ctx.Channel, new ScenarioProfileCommand("derr-002-missing-1", "missing"))
            .PacketName("MissingScenarioCommand")
            .Async();

        await EnsureDispatchMarkerAsync(
            ctx,
            "surface=channel",
            "kind=send",
            "reason=handlerMissing",
            "action=drop",
            "packetName=MissingScenarioCommand");

        await host.StopAsync();
    }

    // DERR-007 — a handler that throws returns a public error and is reported as
    // handlerException / replyError, carrying the exception message.
    public static async Task HandlerException(ScenarioContext ctx)
    {
        using var host = await ScenarioClient.ConnectChannelAsync(ctx);
        var client = host.Services.GetRequiredService<IZLinkChannelClient>();

        var clientError = false;
        try
        {
            _ = await client
                .RequestToChannel(ctx.Channel, new ScenarioProfileRequest("derr-007-throw-1", "throw"))
                .PacketName("ScenarioProfileRequest")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<ScenarioProfileReply>();
        }
        catch (Exception)
        {
            clientError = true;
        }

        Ensure(clientError);
        await EnsureDispatchMarkerAsync(
            ctx,
            "surface=channel",
            "kind=request",
            "reason=handlerException",
            "action=replyError",
            "packetName=ScenarioProfileRequest",
            "error=DERR-007 handler exception");

        await host.StopAsync();
    }

    // The observer reports asynchronously, so poll the server evidence until a
    // single marker contains every expected token.
    private static async Task EnsureDispatchMarkerAsync(ScenarioContext ctx, params string[] tokens)
    {
        using var serverApi = ZLinkHttpClient.Create(ctx.HttpEndpoints[0])
            .Json()
            .Timeout(TimeSpan.FromSeconds(3))
            .Build();
        var observed = false;
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTimeOffset.UtcNow < deadline && !observed)
        {
            var evidence = (await serverApi.Get("/evidence").SubmitAsync<ScenarioEvidenceSnapshot>()).Body;
            observed = evidence.DispatchErrors.Any(marker =>
                tokens.All(token => marker.Contains(token, StringComparison.Ordinal)));
            if (!observed)
            {
                await Task.Delay(50);
            }
        }

        Ensure(observed);
    }
}
