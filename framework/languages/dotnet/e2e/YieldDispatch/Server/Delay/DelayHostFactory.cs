using Systems.Zlink;
using YieldDispatch.Shared;
using Zlink.Framework.AspNetCore;

namespace YieldDispatch.Server.Delay;

internal static class DelayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = DelayOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton(new NodeOptions(options.Rid));
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.AddClientServerChannel(YieldDispatchNames.DelayChannel)
                .EnableServer(options.DelayEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddRequestHandler<DelayHandler, DelayReq, DelayReply>("DelayReq");
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "delay", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        return app;
    }
}