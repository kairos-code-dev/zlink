using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Core;

internal sealed record ZLinkFrameworkRuntimeComponents(
    ZLinkChannelRuntimeManager Channels,
    ZLinkStreamRuntimeManager Streams,
    ZLinkSpotRuntimeManager Spots,
    ZLinkFrameworkRuntimeStateFactory StateFactory,
    ZLinkActorSessionManager ActorSessionManager,
    ZLinkFrameworkActorFacade Actors,
    ZLinkFrameworkChannelFacade ChannelFacade,
    ZLinkFrameworkSpotFacade SpotFacade);

internal static class ZLinkFrameworkRuntimeComponentFactory
{
    public static ZLinkFrameworkRuntimeComponents Create(
        ZLinkFrameworkRuntime runtime,
        IServiceProvider services,
        IZLinkBackendAdapterFactory backendAdapterFactory,
        ZLinkFrameworkRegistration registration,
        ZLinkHandlerRegistry handlerRegistry,
        ZLinkHandlerDispatcher dispatcher,
        Func<ZLinkFrameworkRuntimeState> getOrStartState,
        Func<CancellationToken, ValueTask<ZLinkFrameworkRuntimeState>> getStartedStateAsync,
        Func<IZLinkBackendSpotNode?> getActorSpotNode)
    {
        var channels = new ZLinkChannelRuntimeManager(
            services,
            backendAdapterFactory,
            registration,
            new ZLinkChannelMessagePump(
                handlerRegistry,
                dispatcher,
                registration,
                runtime,
                services.GetService<ILoggerFactory>()));
        var streams = new ZLinkStreamRuntimeManager(services, backendAdapterFactory, registration);
        var spots = new ZLinkSpotRuntimeManager(services, runtime, backendAdapterFactory, registration);
        var stateFactory = new ZLinkFrameworkRuntimeStateFactory(
            backendAdapterFactory,
            registration,
            channels,
            streams,
            spots);
        var actorSessionManager = new ZLinkActorSessionManager(runtime, services, getActorSpotNode);
        var actors = new ZLinkFrameworkActorFacade(
            registration,
            spots,
            actorSessionManager,
            getOrStartState,
            getActorSpotNode);
        var channelFacade = new ZLinkFrameworkChannelFacade(
            channels,
            getOrStartState,
            getStartedStateAsync);
        var spotFacade = new ZLinkFrameworkSpotFacade(
            spots,
            getOrStartState,
            getStartedStateAsync);

        return new ZLinkFrameworkRuntimeComponents(
            channels,
            streams,
            spots,
            stateFactory,
            actorSessionManager,
            actors,
            channelFacade,
            spotFacade);
    }
}
