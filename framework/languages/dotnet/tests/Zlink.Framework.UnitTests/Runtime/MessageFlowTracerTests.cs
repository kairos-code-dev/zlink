using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.UnitTests;

// MFLOW-001/002/003/011: mode gating + runtime live toggle for the .NET tracer.
public sealed class MessageFlowTracerTests
{
    [Fact]
    public void Off_SuppressesAllTransitions()
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.Off);
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received));
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Dropped));
        Assert.Empty(logger.Messages);
    }

    [Fact]
    public void ErrorsOnly_EmitsDroppedButNotHealthyTransitions()
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.ErrorsOnly);
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received));
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Dropped));
        Assert.DoesNotContain(logger.Messages, m => m.Contains("outcome=received"));
        Assert.Contains(logger.Messages, m => m.Contains("outcome=dropped"));
    }

    [Fact]
    public void KeyTransitions_EmitsLifecycleKeyedByCorrelation()
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.KeyTransitions);
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received));
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Replied));
        Assert.Contains(logger.Messages, m => m.Contains("outcome=received"));
        Assert.Contains(logger.Messages, m => m.Contains("outcome=replied"));
        Assert.Contains(logger.Messages, m => m.Contains("corr=corr-1"));
    }

    [Fact]
    public void LiveMode_OverridesStaticAndTogglesAtRuntime()
    {
        var (tracer, logger, options) = Build(ZLinkMessageFlowLogMode.Off);
        var cell = new ZLinkMessageFlowModeCell(ZLinkMessageFlowLogMode.Off);
        options.Diagnostics.LiveMode = cell;

        Assert.False(tracer.Enabled(ZLinkMessageFlowOutcome.Received));

        cell.Mode = ZLinkMessageFlowLogMode.KeyTransitions;
        Assert.True(tracer.Enabled(ZLinkMessageFlowOutcome.Received));
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received));
        Assert.Contains(logger.Messages, m => m.Contains("outcome=received"));

        cell.Mode = ZLinkMessageFlowLogMode.Off;
        Assert.False(tracer.Enabled(ZLinkMessageFlowOutcome.Received));
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

    private static ZLinkMessageFlowEvent Flow(ZLinkMessageFlowOutcome outcome)
    {
        return new ZLinkMessageFlowEvent(
            outcome,
            ZLinkDispatchErrorSurface.Channel,
            ZLinkDispatchMessageKind.Request,
            "PlaceOrder",
            "orders",
            CorrelationId: "corr-1");
    }

    private sealed class RecordingLogger : ILogger
    {
        public List<string> Messages { get; } = [];

        public IDisposable? BeginScope<TState>(TState state) where TState : notnull
        {
            return null;
        }

        public bool IsEnabled(LogLevel logLevel)
        {
            return true;
        }

        public void Log<TState>(
            LogLevel logLevel,
            EventId eventId,
            TState state,
            Exception? exception,
            Func<TState, Exception?, string> formatter)
        {
            Messages.Add(formatter(state, exception));
        }
    }
}