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
using RuntimeMonitoring.Server.Trigger;

namespace RuntimeMonitoring.Server.Trigger.Support;

internal static class TriggerValidation
{
    public static string VerifyDuplicateSocketSource()
    {
        var duplicate = AssertThrows<ZLinkConfigurationException>(() =>
        {
            var builder = Host.CreateApplicationBuilder();
            builder.Services.AddZLinkMonitoring(monitor =>
            {
                monitor.AddSocketEvents("duplicate.server");
                monitor.AddSocketEvents("duplicate.server");
            });
        });
        return $"mon-b2|duplicate={duplicate.Message}";
    }

    public static string VerifyPollingInterval()
    {
        var interval = AssertThrows<ZLinkConfigurationException>(() =>
        {
            var builder = Host.CreateApplicationBuilder();
            builder.Services.AddZLinkMonitoring(monitor =>
            {
                monitor.AddRegistryEvents("registry", TimeSpan.Zero);
            });
        });
        return $"mon-b2|interval={interval.Message}";
    }

    public static async Task<string> VerifyMissingSpotSourceAsync()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(_ => { });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSpotEvents("missing.spot", TimeSpan.FromMilliseconds(100));
        });
        using var host = builder.Build();
        var exception = await AssertThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());
        if (!exception.Message.Contains("not registered", StringComparison.Ordinal))
        {
            throw new InvalidOperationException("MON-B2 missing spot source startup error was not explicit.");
        }

        return "mon-b2|missing-spot=not registered";
    }

    public static async Task<string> VerifyMissingSocketSourceAsync()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddClientServerChannel("validation.profile")
                .EnableServer(PickTcpEndpoint())
                .AddRequestHandler<ValidationRequestHandler, ProfileReq, ProfileRes>("ProfileReq");
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSocketEvents("missing.server", ZLinkSocketEventKind.ConnectionReady);
        });
        using var host = builder.Build();
        var exception = await AssertThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());
        if (!exception.Message.Contains("not registered", StringComparison.Ordinal))
        {
            throw new InvalidOperationException("MON-B2 missing socket source startup error was not explicit.");
        }

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

    private static TException AssertThrows<TException>(Action action)
        where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException ex)
        {
            return ex;
        }

        throw new InvalidOperationException($"Expected {typeof(TException).Name}.");
    }

    private static async Task<TException> AssertThrowsAsync<TException>(Func<Task> action)
        where TException : Exception
    {
        try
        {
            await action();
        }
        catch (TException ex)
        {
            return ex;
        }

        throw new InvalidOperationException($"Expected {typeof(TException).Name}.");
    }
}
