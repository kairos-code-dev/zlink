using System.Collections.Concurrent;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System.Diagnostics.Metrics;
using Systems.Zlink.Stream.Connector.Runtime.Protocol;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.UnitTests;

// MFLOW-001/002/003/011: mode gating + runtime live toggle for the .NET tracer.
[Collection(RuntimeMetricsCollection.Name)]
public sealed class MessageFlowTracerTests
{
    private static readonly object StandardErrorGate = new();

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
        Assert.DoesNotContain(logger.Messages, m => m.Contains("phase=received"));
        Assert.Contains(logger.Messages, m => m.Contains("phase=dropped"));
    }

    [Fact]
    public void HandlerExceptionFlowUsesErrorLogLevel()
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.ErrorsOnly);

        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Error) with
        {
            ErrorReason = ZLinkDispatchErrorReason.HandlerException
        });

        Assert.Equal(LogLevel.Error, Assert.Single(logger.Levels));
    }

    [Theory]
    [InlineData(ZLinkDispatchMessageKind.Send, LogLevel.Warning)]
    [InlineData(ZLinkDispatchMessageKind.Publish, LogLevel.Debug)]
    public void MissingHandlerFlowUsesMessageKindLogLevel(
        ZLinkDispatchMessageKind messageKind,
        LogLevel expected)
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.ErrorsOnly);

        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Error) with
        {
            MessageKind = messageKind,
            ErrorReason = ZLinkDispatchErrorReason.HandlerMissing
        });

        Assert.Equal(expected, Assert.Single(logger.Levels));
    }

    [Fact]
    public void KeyTransitions_EmitsLifecycleKeyedByCorrelation()
    {
        var (tracer, logger, _) = Build(ZLinkMessageFlowLogMode.KeyTransitions);
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received));
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Replied));
        Assert.Contains(logger.Messages, m => m.Contains("phase=received"));
        Assert.Contains(logger.Messages, m => m.Contains("phase=replied"));
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
        Assert.Contains(logger.Messages, m => m.Contains("phase=received"));

        cell.Mode = ZLinkMessageFlowLogMode.Off;
        Assert.False(tracer.Enabled(ZLinkMessageFlowOutcome.Received));
    }

    [Fact]
    public void Public_MessageFlowControl_Toggles_All_Shared_Runtime_Tracers()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(_ => { });
        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var control = provider.GetRequiredService<IZLinkMessageFlowControl>();
        var channelLogger = new RecordingLogger();
        var spotLogger = new RecordingLogger();
        var channelTracer = new ZLinkMessageFlowTracer(registration.DispatchOptions, channelLogger);
        var spotTracer = new ZLinkMessageFlowTracer(registration.DispatchOptions, spotLogger);

        control.SetMessageFlowMode(ZLinkMessageFlowLogMode.Off);
        Assert.Equal(ZLinkMessageFlowLogMode.Off, control.MessageFlowMode);
        control.SetMessageFlowMode(ZLinkMessageFlowLogMode.KeyTransitions);
        channelTracer.Trace(Flow(ZLinkMessageFlowOutcome.Received) with
        {
            Surface = ZLinkDispatchErrorSurface.Channel
        });
        spotTracer.Trace(Flow(ZLinkMessageFlowOutcome.Sent) with
        {
            Surface = ZLinkDispatchErrorSurface.SpotSubscription
        });

        Assert.Single(channelLogger.Messages);
        Assert.Single(spotLogger.Messages);
        Assert.Equal(ZLinkMessageFlowLogMode.KeyTransitions, control.MessageFlowMode);

        control.SetMessageFlowMode(ZLinkMessageFlowLogMode.Off);
        channelTracer.Trace(Flow(ZLinkMessageFlowOutcome.Replied));
        spotTracer.Trace(Flow(ZLinkMessageFlowOutcome.Dispatched));
        Assert.Single(channelLogger.Messages);
        Assert.Single(spotLogger.Messages);
        Assert.Equal(ZLinkMessageFlowLogMode.Off, control.MessageFlowMode);
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

        Assert.DoesNotContain(logger.Messages, message => message.Contains("phase=received"));
        Assert.Contains(logger.Messages, message => message.Contains("phase=dropped"));
        Assert.Contains(logger.Messages, message => message.Contains("phase=error"));
    }

    [Fact]
    public void Missing_Logger_Uses_Standard_Error_But_Still_Respects_Off()
    {
        lock (StandardErrorGate)
        {
            var original = Console.Error;
            using var output = new StringWriter();
            Console.SetError(output);
            try
            {
                var enabledOptions = new ZLinkDispatchOptionsModel();
                enabledOptions.MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions);
                new ZLinkMessageFlowTracer(enabledOptions)
                    .Trace(Flow(ZLinkMessageFlowOutcome.Received));

                var offOptions = new ZLinkDispatchOptionsModel();
                offOptions.MessageFlow(ZLinkMessageFlowLogMode.Off);
                new ZLinkMessageFlowTracer(offOptions)
                    .Trace(Flow(ZLinkMessageFlowOutcome.Replied));
            }
            finally
            {
                Console.SetError(original);
            }

            var text = output.ToString();
            Assert.Contains("zlink flow:", text, StringComparison.Ordinal);
            Assert.Contains("PlaceOrder", text, StringComparison.Ordinal);
            Assert.Contains("zlink flow: phase=received", text, StringComparison.Ordinal);
            Assert.DoesNotContain("phase=replied", text, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void Verbose_size_and_structured_hook_fields_follow_the_frozen_gates()
    {
        var (tracer, logger, options) = Build(ZLinkMessageFlowLogMode.KeyTransitions);
        options.IncludeMessageSizes(true);
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received) with
        {
            SourceRid = "sender-rid",
            MessageSize = 42
        });
        Assert.DoesNotContain(logger.Messages, message => message.Contains("size=42", StringComparison.Ordinal));

        options.MessageFlow(ZLinkMessageFlowLogMode.Verbose);
        foreach (var (outcome, surface, source) in new[]
                 {
                     (ZLinkMessageFlowOutcome.Received, ZLinkDispatchErrorSurface.Channel, "sender"),
                     (ZLinkMessageFlowOutcome.Dispatched, ZLinkDispatchErrorSurface.RouteMeshChannel, "sender"),
                     (ZLinkMessageFlowOutcome.Replied, ZLinkDispatchErrorSurface.SpotActor, "sender"),
                     (ZLinkMessageFlowOutcome.Sent, ZLinkDispatchErrorSurface.SpotSubscription, "destination"),
                     (ZLinkMessageFlowOutcome.ReplyReceived, ZLinkDispatchErrorSurface.StreamSession, "destination")
                 })
            tracer.Trace(Flow(outcome) with { Surface = surface, SourceRid = source, MessageSize = 42 });

        Assert.Equal(5, logger.Messages.Count(message => message.Contains("size=42", StringComparison.Ordinal)));
        Assert.Contains(logger.Messages, message => message.Contains("surface=Channel", StringComparison.Ordinal)
                                                    && message.Contains("src=sender", StringComparison.Ordinal));
        Assert.Contains(logger.Messages, message => message.Contains("surface=StreamSession", StringComparison.Ordinal)
                                                    && message.Contains("src=destination", StringComparison.Ordinal));
    }

    [Fact]
    public void Off_gate_prevents_event_construction()
    {
        var (tracer, _, _) = Build(ZLinkMessageFlowLogMode.Off);
        var constructed = 0;

        if (tracer.Enabled(ZLinkMessageFlowOutcome.Received))
        {
            constructed++;
            tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received));
        }

        Assert.Equal(0, constructed);
        Assert.False(tracer.CaptureEnabled);
        using (ZLinkFlowContext.EnterCurrentOrCreate(
                   ZLinkFlowOrigin.Application,
                   tracer.CaptureEnabled))
            Assert.Null(ZLinkFlowContext.Current);
    }

    [Fact]
    public void Tracer_Without_Operation_Scope_Does_Not_Invent_Flow_Identity()
    {
        var root = Path.Combine(Path.GetTempPath(), $"zlink-no-scope-{Guid.NewGuid():N}");
        var path = Path.Combine(root, "flow.log");
        try
        {
            var (tracer, _, options) = Build(ZLinkMessageFlowLogMode.KeyTransitions);
            options.TraceLogFile(path);
            Assert.Null(ZLinkFlowContext.Current);

            tracer.Trace(Flow(ZLinkMessageFlowOutcome.Sent));

            var line = Assert.Single(File.ReadAllLines(path));
            Assert.DoesNotContain(" flow=", line, StringComparison.Ordinal);
            Assert.DoesNotContain(" origin=", line, StringComparison.Ordinal);
            Assert.Null(ZLinkFlowContext.Current);
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, true);
        }
    }

    [Fact]
    public void Flow_origin_token_is_absent_when_flow_id_is_absent()
    {
        var line = ZLinkTraceFormat.FlowLine(
            Flow(ZLinkMessageFlowOutcome.Received),
            "node-a",
            null);

        Assert.DoesNotContain(" flow=", line, StringComparison.Ordinal);
        Assert.DoesNotContain(" origin=", line, StringComparison.Ordinal);
    }

    [Fact]
    public void Dedicated_file_is_structured_creates_parent_and_takes_precedence_over_app_logger()
    {
        var root = Path.Combine(Path.GetTempPath(), $"zlink-flow-{Guid.NewGuid():N}");
        var path = Path.Combine(root, "nested", "flow.log");
        try
        {
            var (tracer, logger, options) = Build(ZLinkMessageFlowLogMode.KeyTransitions);
            options.TraceLogFile(path);
            options.TraceLabel("node-a");
            tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received, ZlinkStreamFlowId.Create()));
            tracer.Trace(Flow(ZLinkMessageFlowOutcome.Error, ZlinkStreamFlowId.Create()));

            var lines = File.ReadAllLines(path);
            Assert.Equal(2, lines.Length);
            var line = lines[0];
            Assert.Empty(logger.Messages);
            Assert.Contains("phase=received", line, StringComparison.Ordinal);
            Assert.Contains("label=node-a", line, StringComparison.Ordinal);
            Assert.Contains("corr=corr-1", line, StringComparison.Ordinal);
            Assert.Contains("flow=", line, StringComparison.Ordinal);
            Assert.Contains("origin=inbound", line, StringComparison.Ordinal);
            Assert.Contains(lines, value => value.Contains("phase=error", StringComparison.Ordinal));
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, true);
        }
    }

    [Fact]
    public void Dispatch_failure_uses_only_the_dedicated_file_and_the_fixed_phase_schema()
    {
        var root = Path.Combine(Path.GetTempPath(), $"zlink-flow-dispatch-{Guid.NewGuid():N}");
        var path = Path.Combine(root, "nested", "dispatch.log");
        try
        {
            var options = new ZLinkDispatchOptionsModel();
            options.MessageFlow(ZLinkMessageFlowLogMode.ErrorsOnly);
            options.TraceLogFile(path);
            var appLogger = new RecordingLogger();
            var reporter = new ZLinkDispatchErrorReporter(options, appLogger);
            var scope = new ZLinkDispatchFlowScope(
                ZLinkDispatchErrorSurface.Channel,
                "Channel",
                ZLinkDispatchMessageKind.Request,
                "Request",
                "MissingRequest",
                channelName: "api",
                correlationId: "corr-production");

            scope.HandlerMissing(
                appLogger,
                reporter,
                LogLevel.Error,
                ZLinkDispatchErrorAction.ReplyError);

            Assert.Empty(appLogger.Messages);
            var line = Assert.Single(File.ReadAllLines(path));
            Assert.Contains("phase=error", line, StringComparison.Ordinal);
            Assert.Contains("corr=corr-production", line, StringComparison.Ordinal);
            Assert.DoesNotContain("outcome=", line, StringComparison.Ordinal);
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, true);
        }
    }

    [Fact]
    public void Production_flow_logger_uses_the_fixed_dispatch_category()
    {
        using var factory = new RecordingLoggerFactory();

        _ = ZLinkMessageFlowTracer.CreateLogger(factory);

        Assert.Equal(ZLinkMessageFlowTracer.LoggerCategory, Assert.Single(factory.Categories));
        Assert.Equal("zlink.framework.dispatch", factory.Categories[0]);
    }

    [Fact]
    public void Gateway_logger_prefers_the_explicit_factory_falls_back_and_keeps_Off_silent()
    {
        using var factory = new RecordingLoggerFactory();
        var fallback = new RecordingLogger();
        var explicitLogger = ZLinkMessageFlowTracer.CreateLogger(factory, fallback);
        var enabled = new ZLinkDispatchOptionsModel();
        enabled.MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions);

        new ZLinkMessageFlowTracer(enabled, explicitLogger)
            .Trace(Flow(ZLinkMessageFlowOutcome.Received, ZlinkStreamFlowId.Create()));

        var factoryLogger = Assert.Single(factory.CreatedLoggers);
        Assert.Single(factoryLogger.Messages);
        Assert.Empty(fallback.Messages);

        var fallbackLogger = ZLinkMessageFlowTracer.CreateLogger(null, fallback);
        new ZLinkMessageFlowTracer(enabled, fallbackLogger)
            .Trace(Flow(ZLinkMessageFlowOutcome.Received, ZlinkStreamFlowId.Create()));
        Assert.Single(fallback.Messages);

        var off = new ZLinkDispatchOptionsModel();
        off.MessageFlow(ZLinkMessageFlowLogMode.Off);
        new ZLinkMessageFlowTracer(off, explicitLogger)
            .Trace(Flow(ZLinkMessageFlowOutcome.Error, ZlinkStreamFlowId.Create()));
        Assert.Single(factoryLogger.Messages);
    }

    [Fact]
    public void Actor_gateway_runtime_construction_uses_explicit_factory_fallback_and_Off_gate()
    {
        using var factory = new RecordingLoggerFactory();
        var registration = new ZLinkFrameworkRegistration();
        registration.DispatchOptions.MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions);
        using (var provider = new ServiceCollection()
                   .AddSingleton<ILoggerFactory>(factory)
                   .BuildServiceProvider())
        {
            var runtime = CreateRuntime(provider, registration);
            runtime.Flow.Trace(Flow(ZLinkMessageFlowOutcome.Sent) with
            {
                Surface = ZLinkDispatchErrorSurface.SpotActor
            });

            var logger = Assert.Single(factory.CreatedLoggers.Where(static logger => logger.Messages.Count == 1));
            Assert.Contains(ZLinkMessageFlowTracer.LoggerCategory, factory.Categories);
            Assert.Single(logger.Messages);

            registration.DispatchOptions.MessageFlow(ZLinkMessageFlowLogMode.Off);
            runtime.Flow.Trace(Flow(ZLinkMessageFlowOutcome.Error) with
            {
                Surface = ZLinkDispatchErrorSurface.SpotActor
            });
            Assert.Single(logger.Messages);
        }

        lock (StandardErrorGate)
        {
            var original = Console.Error;
            using var output = new StringWriter();
            Console.SetError(output);
            try
            {
                var fallbackRegistration = new ZLinkFrameworkRegistration();
                fallbackRegistration.DispatchOptions.MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions);
                using var provider = new ServiceCollection().BuildServiceProvider();
                var runtime = CreateRuntime(provider, fallbackRegistration);
                runtime.Flow.Trace(Flow(ZLinkMessageFlowOutcome.Sent) with
                {
                    Surface = ZLinkDispatchErrorSurface.SpotActor
                });
            }
            finally
            {
                Console.SetError(original);
            }

            Assert.Contains("zlink flow: phase=sent", output.ToString(), StringComparison.Ordinal);
        }
    }

    [Fact]
    public async Task Observer_is_offloaded_bounded_and_failure_does_not_break_later_delivery()
    {
        var overflows = new List<(long Value, string? Event)>();
        using var meterListener = new MeterListener
        {
            InstrumentPublished = (instrument, owner) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework
                    && instrument.Name == "zlink.observability.observer.overflow")
                    owner.EnableMeasurementEvents(instrument);
            }
        };
        meterListener.SetMeasurementEventCallback<long>((_, value, tags, _) =>
        {
            string? eventName = null;
            foreach (var tag in tags)
                if (tag.Key == "event") eventName = tag.Value as string;
            overflows.Add((value, eventName));
        });
        meterListener.Start();
        var observer = new BlockingFailOnceObserver();
        var options = new ZLinkDispatchOptionsModel();
        options.MessageFlow(ZLinkMessageFlowLogMode.Off);
        options.SetMessageFlowObserver(observer);
        await using var services = new ServiceCollection().BuildServiceProvider();
        var runner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), CancellationToken.None);
        await using var pump = new ZLinkMessageFlowObserverPump(options, services, runner);
        var tracer = new ZLinkMessageFlowTracer(options, new RecordingLogger(), observerPump: pump);

        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received) with { PacketName = "first" });
        await observer.Started.Task.WaitAsync(TimeSpan.FromSeconds(2));
        for (var index = 0; index < 1024; index++)
            tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received) with { PacketName = index.ToString() });
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received) with { PacketName = "overflow-new" });

        observer.Release.TrySetResult();
        await observer.LastAccepted.Task.WaitAsync(TimeSpan.FromSeconds(2));

        Assert.Equal(1025, observer.ObservedCount);
        Assert.DoesNotContain("overflow-new", observer.ObservedPacketNames);
        Assert.Contains("0", observer.ObservedPacketNames);
        Assert.Contains("1023", observer.ObservedPacketNames);
        Assert.True(overflows.Sum(static sample => sample.Value) > 0);
        Assert.All(overflows, sample => Assert.Equal("received", sample.Event));
        await runner.StopAsync();
    }

    [Fact]
    public async Task Flow_Id_And_Origin_Are_Always_Delivered_As_One_Optional_Pair()
    {
        var observer = new PairCapturingObserver();
        var options = new ZLinkDispatchOptionsModel();
        options.MessageFlow(ZLinkMessageFlowLogMode.Off);
        options.SetMessageFlowObserver(observer);
        await using var services = new ServiceCollection().BuildServiceProvider();
        var runner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), CancellationToken.None);
        await using var pump = new ZLinkMessageFlowObserverPump(options, services, runner);
        var tracer = new ZLinkMessageFlowTracer(options, new RecordingLogger(), observerPump: pump);

        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Received) with { FlowId = "orphan-id" });
        tracer.Trace(Flow(ZLinkMessageFlowOutcome.Error) with { FlowOrigin = ZLinkFlowOrigin.Timer });
        var observed = await observer.Completed.Task.WaitAsync(TimeSpan.FromSeconds(2));

        Assert.All(observed, flow =>
        {
            Assert.Equal(string.Empty, flow.FlowId);
            Assert.Null(flow.FlowOrigin);
        });
        var malformed = Flow(ZLinkMessageFlowOutcome.Received) with { FlowId = "orphan-id" };
        var line = ZLinkTraceFormat.FlowLine(malformed, null, null);
        Assert.DoesNotContain(" flow=", line, StringComparison.Ordinal);
        Assert.DoesNotContain(" origin=", line, StringComparison.Ordinal);
        await runner.StopAsync();
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
        public List<LogLevel> Levels { get; } = [];

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
            Levels.Add(logLevel);
            Messages.Add(formatter(state, exception));
        }
    }

    private static ZLinkFrameworkRuntime CreateRuntime(
        IServiceProvider provider,
        ZLinkFrameworkRegistration registration) => new(
        provider,
        null!,
        registration,
        new ZLinkHandlerRegistry([]),
        new ZLinkHandlerDispatcher(
            provider.GetRequiredService<IServiceScopeFactory>(),
            registration));

    private sealed class RecordingLoggerFactory : ILoggerFactory
    {
        public List<string> Categories { get; } = [];

        public List<RecordingLogger> CreatedLoggers { get; } = [];

        public void AddProvider(ILoggerProvider provider)
        {
            _ = provider;
        }

        public ILogger CreateLogger(string categoryName)
        {
            Categories.Add(categoryName);
            var logger = new RecordingLogger();
            CreatedLoggers.Add(logger);
            return logger;
        }

        public void Dispose()
        {
        }
    }

    private sealed class BlockingFailOnceObserver : IZLinkMessageFlowObserver
    {
        private int _first = 1;
        private int _observedCount;

        public TaskCompletionSource Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Release { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource LastAccepted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int ObservedCount => Volatile.Read(ref _observedCount);

        public ConcurrentQueue<string> ObservedPacketNames { get; } = new();

        public async ValueTask OnMessageFlowAsync(
            ZLinkMessageFlowEvent flow,
            CancellationToken cancellationToken)
        {
            Interlocked.Increment(ref _observedCount);
            if (flow.PacketName is { } packetName) ObservedPacketNames.Enqueue(packetName);
            if (Interlocked.Exchange(ref _first, 0) != 0)
            {
                Started.TrySetResult();
                await Release.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
                throw new InvalidOperationException("expected observer failure");
            }

            if (flow.PacketName == "1023") LastAccepted.TrySetResult();
        }
    }

    private sealed class PairCapturingObserver : IZLinkMessageFlowObserver
    {
        private readonly List<ZLinkMessageFlowEvent> _events = [];

        public TaskCompletionSource<IReadOnlyList<ZLinkMessageFlowEvent>> Completed { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask OnMessageFlowAsync(
            ZLinkMessageFlowEvent flow,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            lock (_events)
            {
                _events.Add(flow);
                if (_events.Count == 2) Completed.TrySetResult(_events.ToArray());
            }
            return ValueTask.CompletedTask;
        }
    }
}
