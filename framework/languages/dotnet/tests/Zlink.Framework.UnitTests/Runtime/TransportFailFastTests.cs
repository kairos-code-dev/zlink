using Microsoft.Extensions.DependencyInjection;
using System.Diagnostics.Metrics;
using Systems.Zlink;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.UnitTests;

[Collection(RuntimeMetricsCollection.Name)]
public sealed class TransportFailFastTests
{
    [Fact]
    public async Task NotConnected_Fails_Fast_On_AutoConnect_Managed_Submitters()
    {
        var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(5),
            CancellationToken.None,
            failFastNotConnected: () => true);

        var exception = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await submitter.Async(
                Message.From(new byte[] { 1 }),
                _ => throw new ZlinkSubmitException(ZlinkSubmitException.ErrorCode.NotConnected)));

        Assert.Equal(ZLinkFrameworkErrorKind.RouteNotConnected, exception.Kind);
        Assert.True(exception.IsRetriable);
        await submitter.DisposeAsync();
    }

    [Fact]
    public async Task NotConnected_Still_Buffers_Without_The_Opt_In()
    {
        Action ready = () => { };
        var submitter = new ZLinkAsyncSubmitter(
            handler => ready = handler,
            TimeSpan.FromSeconds(30),
            CancellationToken.None);

        var connected = false;
        var pending = submitter.Async(
            Message.From(new byte[] { 1 }),
            _ => connected
                ? true
                : throw new ZlinkSubmitException(ZlinkSubmitException.ErrorCode.NotConnected));

        Assert.False(pending.IsCompleted);

        connected = true;
        ready();
        await pending;
        await submitter.DisposeAsync();
    }

    [Fact]
    public async Task Unawaited_Submit_Failures_Reach_The_Error_Sink()
    {
        Exception? seen = null;
        Action<Exception> handler = exception => seen = exception;
        using var errorSink = new ZLinkRuntimeErrorSink();
        errorSink.UnhandledCallbackException += handler;
        try
        {
            ZLinkUnawaitedSubmit.Observe(
                ValueTask.FromException(new InvalidOperationException("submit died")),
                "test submit",
                errorSink);

            for (var i = 0; i < 100 && seen is null; i++)
            {
                await Task.Delay(10);
            }

            Assert.IsType<InvalidOperationException>(seen);
        }
        finally
        {
            errorSink.UnhandledCallbackException -= handler;
        }
    }

    [Fact]
    public void Unawaited_Submit_Cancellation_Is_Not_A_Failure()
    {
        Exception? seen = null;
        Action<Exception> handler = exception => seen = exception;
        using var errorSink = new ZLinkRuntimeErrorSink();
        errorSink.UnhandledCallbackException += handler;
        try
        {
            ZLinkUnawaitedSubmit.Observe(
                ValueTask.FromException(new OperationCanceledException()),
                "test submit",
                errorSink);

            Assert.Null(seen);
        }
        finally
        {
            errorSink.UnhandledCallbackException -= handler;
        }
    }

    [Theory]
    [InlineData("timeout", "backpressure")]
    [InlineData("stale", "stale_route")]
    public async Task Unawaited_Channel_Submit_Records_Closed_Drop_Reason(
        string failure,
        string expectedReason)
    {
        var reasons = new List<string>();
        using var listener = new MeterListener
        {
            InstrumentPublished = (instrument, meterListener) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework
                    && instrument.Name == "zlink.channel.messages.dropped")
                    meterListener.EnableMeasurementEvents(instrument);
            }
        };
        listener.SetMeasurementEventCallback<long>((_, _, tags, _) =>
        {
            foreach (var tag in tags)
                if (tag.Key == "reason" && tag.Value is string reason)
                    reasons.Add(reason);
        });
        listener.Start();

        using var errorSink = new ZLinkRuntimeErrorSink();
        Exception exception = failure == "timeout"
            ? new TimeoutException("backpressured")
            : new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RouteNotConnected,
                "stale route",
                true);
        ZLinkUnawaitedSubmit.Observe(
            ValueTask.FromException(exception),
            "channel send",
            errorSink,
            "Channel",
            "send");

        for (var attempt = 0; attempt < 100 && reasons.Count == 0; attempt++)
            await Task.Delay(10);

        Assert.Equal([expectedReason], reasons);
    }

    [Fact]
    public async Task Runtime_Error_Sinks_Are_Isolated_And_Stop_Delivery_After_Shutdown()
    {
        await using var providerA = CreateServices();
        await using var providerB = CreateServices();
        var runtimeA = CreateRuntime(providerA);
        var runtimeB = CreateRuntime(providerB);
        await runtimeA.StartAsync(CancellationToken.None);
        await runtimeB.StartAsync(CancellationToken.None);
        var sinkA = runtimeA.ErrorSink;
        var sinkB = runtimeB.ErrorSink;
        var seenA = new List<string>();
        var seenB = new List<string>();
        sinkA.UnhandledCallbackException += exception => seenA.Add(exception.Message);
        sinkB.UnhandledCallbackException += exception => seenB.Add(exception.Message);

        sinkA.ReportHandlerException(new InvalidOperationException("runtime-a"));
        sinkB.ReportHandlerException(new InvalidOperationException("runtime-b"));

        Assert.Equal(["runtime-a"], seenA);
        Assert.Equal(["runtime-b"], seenB);

        await runtimeA.StopAsync(CancellationToken.None);
        sinkA.ReportHandlerException(new InvalidOperationException("after-stop"));
        sinkB.ReportHandlerException(new InvalidOperationException("runtime-b-still-active"));

        Assert.Equal(["runtime-a"], seenA);
        Assert.Equal(["runtime-b", "runtime-b-still-active"], seenB);
        await runtimeB.StopAsync(CancellationToken.None);
    }

    private static ServiceProvider CreateServices()
    {
        var services = new ServiceCollection();
        services.AddSingleton(new ZLinkFrameworkRegistration());
        return services.BuildServiceProvider();
    }

    private static ZLinkFrameworkRuntime CreateRuntime(ServiceProvider services)
    {
        var registration = services.GetRequiredService<ZLinkFrameworkRegistration>();
        return new ZLinkFrameworkRuntime(
            services,
            new ZLinkDotNetBackendAdapterFactory(),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration));
    }
}
