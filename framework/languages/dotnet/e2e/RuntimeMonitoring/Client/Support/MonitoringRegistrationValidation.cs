using System.Net;
using System.Net.Sockets;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using RuntimeMonitoring.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Contracts.Handlers;

namespace RuntimeMonitoring.Client.Support;

internal static class MonitoringRegistrationValidation
{
    public static string VerifyDuplicateSocketSource()
    {
        var error = AssertThrows<ZLinkConfigurationException>(() =>
        {
            var builder = Host.CreateApplicationBuilder();
            builder.Services.AddZLinkMonitoring(monitor =>
            {
                monitor.AddSocketEvents("duplicate.server");
                monitor.AddSocketEvents("duplicate.server");
            });
        });
        return $"mon-b2|duplicate={error.Message}";
    }

    public static string VerifyPollingInterval()
    {
        var error = AssertThrows<ZLinkConfigurationException>(() =>
        {
            var builder = Host.CreateApplicationBuilder();
            builder.Services.AddZLinkMonitoring(monitor => monitor.AddSpotEvents("spot", TimeSpan.Zero));
        });
        return $"mon-b2|interval={error.Message}";
    }

    public static async Task<string> VerifyMissingSpotSourceAsync()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(_ => { });
        builder.Services.AddZLinkMonitoring(monitor =>
            monitor.AddSpotEvents("missing.spot", TimeSpan.FromMilliseconds(100)));
        using var host = builder.Build();
        var error = await AssertThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());
        ScenarioAssert.That(error.Message.Contains("not registered", StringComparison.Ordinal),
            "MON-B2 missing spot source startup error was not explicit.");
        return "mon-b2|missing-spot=not registered";
    }

    public static async Task<string> VerifyMissingSocketSourceAsync()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(framework => framework
            .AddClientServerChannel("validation.profile")
            .EnableServer(PickTcpEndpoint())
            .AddRequestHandler<ValidationRequestHandler, ProfileReq, ProfileRes>("ProfileReq"));
        builder.Services.AddZLinkMonitoring(monitor =>
            monitor.AddSocketEvents("missing.server", ZLinkSocketEventKind.ConnectionReady));
        using var host = builder.Build();
        var error = await AssertThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());
        ScenarioAssert.That(error.Message.Contains("not registered", StringComparison.Ordinal),
            "MON-B2 missing socket source startup error was not explicit.");
        return "mon-b2|missing-socket=not registered";
    }

    private static string PickTcpEndpoint()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return $"tcp://127.0.0.1:{port}";
    }

    private static T AssertThrows<T>(Action action) where T : Exception
    {
        try { action(); } catch (T error) { return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static async Task<T> AssertThrowsAsync<T>(Func<Task> action) where T : Exception
    {
        try { await action(); } catch (T error) { return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }
}

internal sealed class ValidationRequestHandler : IZLinkRequestHandler<ProfileReq, ProfileRes>
{
    public ValueTask<ProfileRes> HandleAsync(ProfileReq request, ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(new ProfileRes(request.Value, "validation", request.Marker));
    }
}
