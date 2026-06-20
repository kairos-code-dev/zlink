namespace Zlink.Framework.Runtime.Dispatch;

using Microsoft.Extensions.Logging;

internal sealed class ZLinkDispatchOptionsModel : IZLinkDispatchOptions
{
    public ZLinkDispatchMode SpotDispatchMode { get; set; } = ZLinkDispatchMode.Compiled;

    public ZLinkDispatchMode StreamDispatchMode { get; set; } = ZLinkDispatchMode.Compiled;

    public ZLinkUnhandledDispatchOptionsModel Unhandled { get; } = new();

    public ZLinkDiagnosticsOptionsModel Diagnostics { get; } = new();

    public Type? MessageDispatchErrorObserverType { get; private set; }

    public IZLinkMessageDispatchErrorObserver? MessageDispatchErrorObserver { get; private set; }

    IZLinkUnhandledDispatchOptions IZLinkDispatchOptions.Unhandled => Unhandled;

    IZLinkDiagnosticsOptions IZLinkDispatchOptions.Diagnostics => Diagnostics;

    public IZLinkDispatchOptions SetMessageDispatchErrorObserver<TObserver>()
        where TObserver : class, IZLinkMessageDispatchErrorObserver
    {
        MessageDispatchErrorObserverType = typeof(TObserver);
        MessageDispatchErrorObserver = null;
        return this;
    }

    public IZLinkDispatchOptions SetMessageDispatchErrorObserver(
        IZLinkMessageDispatchErrorObserver observer)
    {
        ArgumentNullException.ThrowIfNull(observer);
        MessageDispatchErrorObserver = observer;
        MessageDispatchErrorObserverType = null;
        return this;
    }
}

internal sealed class ZLinkUnhandledDispatchOptionsModel : IZLinkUnhandledDispatchOptions
{
    public ZLinkUnhandledDispatchAction Request { get; set; } = ZLinkUnhandledDispatchAction.ReplyError;

    public ZLinkUnhandledDispatchAction Send { get; set; } = ZLinkUnhandledDispatchAction.LogAndDrop;

    public ZLinkUnhandledDispatchAction Publish { get; set; } = ZLinkUnhandledDispatchAction.LogAndDrop;

    public LogLevel SendLogLevel { get; set; } = LogLevel.Warning;

    public LogLevel PublishLogLevel { get; set; } = LogLevel.Debug;
}

internal sealed class ZLinkDiagnosticsOptionsModel : IZLinkDiagnosticsOptions
{
    public ZLinkMessageFlowLogMode MessageFlow { get; set; } = ZLinkMessageFlowLogMode.ErrorsOnly;

    public double SampleRate { get; set; } = 1.0d;

    public bool IncludeMessageSizes { get; set; } = true;

    public bool IncludeNativeDiagnostics { get; set; }
}
