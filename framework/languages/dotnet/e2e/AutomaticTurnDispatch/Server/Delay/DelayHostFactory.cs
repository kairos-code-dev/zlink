using Microsoft.Extensions.Configuration;

using Systems.Zlink;
using AutomaticTurnDispatch.Shared;
using Zlink.Framework.AspNetCore;

namespace AutomaticTurnDispatch.Server.Delay;

internal static class DelayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = DelayOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
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
            var mesh = framework.AddRouteMesh(AutomaticTurnDispatchNames.DelayChannel)
                .Listen(options.DelayEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid));
            mesh.Channel(AutomaticTurnDispatchNames.DelayChannel).Server()
                .AddRequestHandler<DelayHandler, DelayReq, DelayRes>("DelayReq");
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "delay", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        return app;
    }
}
