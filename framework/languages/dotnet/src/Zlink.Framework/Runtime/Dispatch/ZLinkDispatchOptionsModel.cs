namespace Zlink.Framework.Runtime.Dispatch;

internal sealed class ZLinkDispatchOptionsModel : IZLinkDispatchOptions
{
    public ZLinkUnhandledDispatchOptionsModel Unhandled { get; } = new();

    public ZLinkDiagnosticsOptionsModel Diagnostics { get; } = new();

    public Type? MessageFlowObserverType { get; private set; }

    public IZLinkMessageFlowObserver? MessageFlowObserver { get; private set; }
    IZLinkUnhandledDispatchOptions IZLinkDispatchOptions.Unhandled => Unhandled;

    IZLinkDiagnosticsOptions IZLinkDispatchOptions.Diagnostics => Diagnostics;

    public IZLinkDispatchOptions SetMessageFlowObserver<TObserver>()
        where TObserver : class, IZLinkMessageFlowObserver
    {
        MessageFlowObserverType = typeof(TObserver);
        MessageFlowObserver = null;
        return this;
    }

    public IZLinkDispatchOptions SetMessageFlowObserver(IZLinkMessageFlowObserver observer)
    {
        ArgumentNullException.ThrowIfNull(observer);
        MessageFlowObserver = observer;
        MessageFlowObserverType = null;
        return this;
    }

    public IZLinkDispatchOptions MessageFlow(ZLinkMessageFlowLogMode mode)
    {
        Diagnostics.MessageFlow = mode;
        return this;
    }

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
}

// Shared, runtime-mutable message-flow mode (the C++ live_mode shared atomic). Held
// by the diagnostics model and shared across surfaces, so set_message_flow_mode
// flips it once and every surface observes it live.
internal sealed class ZLinkMessageFlowModeCell
{
    private int _mode;

    public ZLinkMessageFlowModeCell(ZLinkMessageFlowLogMode seed)
    {
        _mode = (int)seed;
    }

    public ZLinkMessageFlowLogMode Mode
    {
        get => (ZLinkMessageFlowLogMode)Volatile.Read(ref _mode);
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
    public ZLinkMessageFlowLogMode MessageFlow { get; internal set; } = ZLinkMessageFlowLogMode.ErrorsOnly;

    public double SampleRate { get; internal set; } = 1.0d;

    public bool IncludeMessageSizes { get; internal set; } = true;

    public bool IncludeNativeDiagnostics { get; internal set; }

    public string? LogFile { get; internal set; }

    public string? Label { get; internal set; }

    public ZLinkMessageFlowLogMode EffectiveMessageFlow =>
        LiveMode is { } cell ? cell.Mode : MessageFlow;
}
