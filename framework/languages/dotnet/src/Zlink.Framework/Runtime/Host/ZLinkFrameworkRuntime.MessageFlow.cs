using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal bool HasRuntimeMessageFlowObservers
    {
        get
        {
            var options = Registration.DispatchOptions;
            return options.RuntimeMessageFlowObserver is not null
                   || options.RuntimeMessageFlowObserverType is not null
                   || Services.GetService<ZLinkMessageFlowRuntimeService>()
                       is { HasSubscribers: true };
        }
    }

    internal void PublishRuntimeMessageFlow(ZLinkRuntimeMessageFlowEvent flow)
    {
        Services.GetService<ZLinkMessageFlowRuntimeService>()?.Publish(flow);
        Volatile.Read(ref _state)?.MessageFlowObservers.EnqueueRuntime(flow);
    }
}
