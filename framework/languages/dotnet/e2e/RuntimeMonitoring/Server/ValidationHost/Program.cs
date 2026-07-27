using Microsoft.Extensions.Configuration;

using System.Net;
using System.Net.Sockets;
using Systems.Zlink;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using RuntimeMonitoring.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.E2E.Configuration;

var validationCase = E2eConfiguration.Load<ValidationOptions>(args).Case;
var builder = Host.CreateApplicationBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();

switch (validationCase)
{
    case "duplicate-socket":
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSocketEvents("duplicate.server");
            monitor.AddSocketEvents("duplicate.server");
        });
        break;
    case "missing-socket":
        builder.Services.AddZLinkFramework(framework =>
        {
            var mesh = framework.AddRouteMesh("validation.profile")
                .Listen(PickTcpEndpoint())
                .SetRoutingId(RoutingId.From("validation"));
            mesh.ChannelName("validation.profile")
                .AddRequestHandler<ValidationRequestHandler, ProfileReq, ProfileRes>("ProfileReq");
        });
        builder.Services.AddZLinkMonitoring(monitor =>
            monitor.AddSocketEvents("missing.server", ZLinkSocketEventKind.ConnectionReady));
        break;
    default:
        throw new ArgumentException($"Unknown validation case '{validationCase}'.");
}

using var host = builder.Build();
await host.StartAsync();
throw new InvalidOperationException($"Validation case '{validationCase}' unexpectedly started.");

static string PickTcpEndpoint()
{
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    return $"tcp://127.0.0.1:{((IPEndPoint)listener.LocalEndpoint).Port}";
}

internal sealed class ValidationRequestHandler : IZLinkRequestHandler<ProfileReq, ProfileRes>
{
    public ValueTask<ProfileRes> HandleAsync(ProfileReq request, IZLinkMessageContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(new ProfileRes(request.Value, "validation", request.Marker));
    }
}

internal sealed record ValidationOptions(string Case);
