using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Systems.Zlink.Stream.Connector.Runtime.Protocol;
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

    [Fact]
    public void Sampling_Decision_Is_Stable_For_The_Whole_Flow()
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.KeyTransitions, 0.5d);
        var flowId = ZlinkStreamFlowId.Create();

        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received, flowId));
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Replied, flowId));

        Assert.True(logger.Messages.Count is 0 or 2);
    }

    [Fact]
    public void Sampling_Can_Keep_And_Drop_Different_Flows()
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.KeyTransitions, 0.5d);

        for (var index = 0; index < 256; index++)
            tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received, ZlinkStreamFlowId.Create()));

        Assert.NotEmpty(logger.Messages);
        Assert.True(logger.Messages.Count < 256);
    }

    [Fact]
    public void Dropped_And_Error_Bypass_Zero_Sample_Rate()
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.KeyTransitions, 0.0d);

        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received, ZlinkStreamFlowId.Create()));
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Dropped, ZlinkStreamFlowId.Create()));
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Error, ZlinkStreamFlowId.Create()));

        Assert.DoesNotContain(logger.Messages, message => message.Contains("outcome=received"));
        Assert.Contains(logger.Messages, message => message.Contains("outcome=dropped"));
        Assert.Contains(logger.Messages, message => message.Contains("outcome=error"));
    }

    private static (ZLinkMessageFlowTracer Tracer, RecordingLogger Logger, ZLinkDispatchOptionsModel Options) Build(
        ZLinkMessageFlowLogMode mode,
        double sampleRate = 1.0d)
    {
        var options = new ZLinkDispatchOptionsModel();
        options.MessageFlow(mode);
        options.TraceSampleRate(sampleRate);
        var logger = new RecordingLogger();
        return (new ZLinkMessageFlowTracer(options, logger), logger, options);
    }

    private static ZLinkMessageFlowEvent Flow(
        ZLinkMessageFlowOutcome outcome,
        string? flowId = null)
    {
        var flow = new ZLinkMessageFlowEvent(
            outcome,
            ZLinkDispatchErrorSurface.Channel,
            ZLinkDispatchMessageKind.Request,
            "PlaceOrder",
            "orders",
            CorrelationId: "corr-1");
        return flowId is null
            ? flow
            : flow with { FlowId = flowId, FlowOrigin = ZLinkFlowOrigin.Inbound };
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
