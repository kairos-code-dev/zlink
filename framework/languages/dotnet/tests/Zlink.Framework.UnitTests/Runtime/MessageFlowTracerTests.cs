using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.UnitTests;

// MFLOW-001/002/003/011: mode gating + runtime live toggle for the .NET tracer.
public sealed class MessageFlowTracerTests
{
    [Fact]
    public void Off_SuppressesAllTransitions()
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.Off);
        tracer.Trace(Flow(ZLinkMessageFlowPhase.Received));
        tracer.Trace(Flow(ZLinkMessageFlowPhase.Dropped));
        Assert.Empty(logger.Messages);
    }

    [Fact]
    public void ErrorsOnly_EmitsDroppedButNotHealthyTransitions()
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.ErrorsOnly);
        tracer.Trace(Flow(ZLinkMessageFlowPhase.Received));
        tracer.Trace(Flow(ZLinkMessageFlowPhase.Dropped));
        Assert.DoesNotContain(logger.Messages, m => m.Contains("phase=Received"));
        Assert.Contains(logger.Messages, m => m.Contains("phase=Dropped"));
    }

    [Fact]
    public void KeyTransitions_EmitsLifecycleKeyedByCorrelation()
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.KeyTransitions);
        tracer.Trace(Flow(ZLinkMessageFlowPhase.Received));
        tracer.Trace(Flow(ZLinkMessageFlowPhase.Replied));
        Assert.Contains(logger.Messages, m => m.Contains("phase=Received"));
        Assert.Contains(logger.Messages, m => m.Contains("phase=Replied"));
        Assert.Contains(logger.Messages, m => m.Contains("corr=corr-1"));
    }

    [Fact]
    public void LiveMode_OverridesStaticAndTogglesAtRuntime()
    {
        var (tracer, logger, options) = Build(ZLinkMessageFlowLogMode.Off);
        var cell = new ZLinkMessageFlowModeCell(ZLinkMessageFlowLogMode.Off);
        options.Diagnostics.LiveMode = cell;

        Assert.False(tracer.Enabled(ZLinkMessageFlowPhase.Received));

        cell.Mode = ZLinkMessageFlowLogMode.KeyTransitions;
        Assert.True(tracer.Enabled(ZLinkMessageFlowPhase.Received));
        tracer.Trace(Flow(ZLinkMessageFlowPhase.Received));
        Assert.Contains(logger.Messages, m => m.Contains("phase=Received"));

        cell.Mode = ZLinkMessageFlowLogMode.Off;
        Assert.False(tracer.Enabled(ZLinkMessageFlowPhase.Received));
    }

    private static (ZLinkMessageFlowTracer Tracer, RecordingLogger Logger, ZLinkDispatchOptionsModel Options) Build(
        ZLinkMessageFlowLogMode mode)
    {
        var options = new ZLinkDispatchOptionsModel();
        options.MessageFlow(mode);
        var logger = new RecordingLogger();
        var services = new ServiceCollection().BuildServiceProvider();
        return (new ZLinkMessageFlowTracer(options, services, logger), logger, options);
    }

    private static ZLinkMessageFlowEvent Flow(ZLinkMessageFlowPhase phase) =>
        new(
            phase,
            ZLinkDispatchErrorSurface.Channel,
            ZLinkDispatchMessageKind.Request,
            PacketName: "PlaceOrder",
            ChannelName: "orders",
            CorrelationId: "corr-1");

    private sealed class RecordingLogger : ILogger
    {
        public List<string> Messages { get; } = [];

        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) => true;

        public void Log<TState>(
            LogLevel logLevel,
            EventId eventId,
            TState state,
            Exception? exception,
            Func<TState, Exception?, string> formatter) => Messages.Add(formatter(state, exception));
    }
}
