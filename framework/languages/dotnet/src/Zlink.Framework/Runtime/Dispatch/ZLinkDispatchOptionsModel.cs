namespace Zlink.Framework.Runtime.Dispatch;

internal sealed class ZLinkDispatchOptionsModel : IZLinkDispatchOptions
{
    public ZLinkUnhandledDispatchOptionsModel Unhandled { get; } = new();

    public ZLinkDiagnosticsOptionsModel Diagnostics { get; } = new();

    public Type? RuntimeMessageFlowObserverType { get; private set; }

    public IZLinkRuntimeMessageFlowObserver? RuntimeMessageFlowObserver { get; private set; }

    public Type? RuntimeErrorSinkType { get; private set; }

    public Zlink.Framework.Contracts.Dispatch.IZLinkRuntimeErrorSink? RuntimeErrorSink
    {
        get;
        private set;
    }
    IZLinkUnhandledDispatchOptions IZLinkDispatchOptions.Unhandled => Unhandled;

    IZLinkDiagnosticsOptions IZLinkDispatchOptions.Diagnostics => Diagnostics;

    public IZLinkDispatchOptions TraceSampleRate(double rate)
    {
        Diagnostics.SampleRate = rate;
        return this;
    }

    public IZLinkDispatchOptions IncludeMessageSizes(bool include)
    {
        Diagnostics.IncludeMessageSizes = include;
        return this;
    }

    public IZLinkDispatchOptions TraceLogFile(string path)
    {
        ArgumentException.ThrowIfNullOrEmpty(path);
        Diagnostics.LogFile = path;
        return this;
    }

    public IZLinkDispatchOptions TraceLabel(string label)
    {
        ArgumentException.ThrowIfNullOrEmpty(label);
        Diagnostics.Label = label;
        return this;
    }

    public IZLinkDispatchOptions SetRuntimeMessageFlowObserver<TObserver>()
        where TObserver : class, IZLinkRuntimeMessageFlowObserver
    {
        RuntimeMessageFlowObserverType = typeof(TObserver);
        RuntimeMessageFlowObserver = null;
        return this;
    }

    public IZLinkDispatchOptions SetRuntimeMessageFlowObserver(
        IZLinkRuntimeMessageFlowObserver observer)
    {
        ArgumentNullException.ThrowIfNull(observer);
        RuntimeMessageFlowObserver = observer;
        RuntimeMessageFlowObserverType = null;
        return this;
    }

    public IZLinkDispatchOptions SetRuntimeErrorSink<TSink>()
        where TSink : class, Zlink.Framework.Contracts.Dispatch.IZLinkRuntimeErrorSink
    {
        RuntimeErrorSinkType = typeof(TSink);
        RuntimeErrorSink = null;
        return this;
    }

    public IZLinkDispatchOptions SetRuntimeErrorSink(
        Zlink.Framework.Contracts.Dispatch.IZLinkRuntimeErrorSink sink)
    {
        ArgumentNullException.ThrowIfNull(sink);
        RuntimeErrorSink = sink;
        RuntimeErrorSinkType = null;
        return this;
    }

    public IZLinkDispatchOptions MessageFlow(ZLinkRuntimeMessageFlowMode mode)
    {
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        Diagnostics.MessageFlow = mode;
        return this;
    }
}

// Shared, runtime-mutable message-flow mode (the C++ live_mode shared atomic). Held
// by the diagnostics model and shared across surfaces, so set_message_flow_mode
// flips it once and every surface observes it live.
internal sealed class ZLinkMessageFlowModeCell
{
    private int _mode;

    public ZLinkMessageFlowModeCell(ZLinkRuntimeMessageFlowMode seed)
    {
        _mode = (int)seed;
    }

    public ZLinkRuntimeMessageFlowMode Mode
    {
        get => (ZLinkRuntimeMessageFlowMode)Volatile.Read(ref _mode);
        set => Volatile.Write(ref _mode, (int)value);
    }
}

internal sealed class ZLinkUnhandledDispatchOptionsModel : IZLinkUnhandledDispatchOptions
{
    public ZLinkUnhandledDispatchAction Request { get; set; } = ZLinkUnhandledDispatchAction.ReplyError;

    public ZLinkUnhandledDispatchAction Send { get; set; } = ZLinkUnhandledDispatchAction.LogAndDrop;

    public ZLinkUnhandledDispatchAction Publish { get; set; } = ZLinkUnhandledDispatchAction.LogAndDrop;
}

internal sealed class ZLinkDiagnosticsOptionsModel : IZLinkDiagnosticsOptions
{
    // Installed by the host at apply (ZLinkFrameworkServiceRegistrar). Shared across
    // surfaces so SetMessageFlowMode flips it live. Null before apply.
    public ZLinkMessageFlowModeCell? LiveMode { get; internal set; }
    public ZLinkRuntimeMessageFlowMode MessageFlow { get; internal set; } =
        ZLinkRuntimeMessageFlowMode.ErrorsOnly;

    public double SampleRate { get; internal set; } = 1.0d;

    public bool IncludeMessageSizes { get; internal set; } = true;

    public string? LogFile { get; internal set; }

    public string? Label { get; internal set; }

    public ZLinkRuntimeMessageFlowMode EffectiveMessageFlow =>
        LiveMode is { } cell ? cell.Mode : MessageFlow;
}
