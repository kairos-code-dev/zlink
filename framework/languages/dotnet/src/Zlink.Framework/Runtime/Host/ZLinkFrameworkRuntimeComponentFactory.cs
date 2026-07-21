using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Host;

internal sealed record ZLinkFrameworkRuntimeComponents(
    ZLinkChannelRuntimeManager Channels,
    ZLinkStreamRuntimeManager Streams,
    ZLinkSpotRuntimeManager Spots,
    ZLinkFrameworkRuntimeStateFactory StateFactory,
    ZLinkActorSessionManager ActorSessionManager,
    ZLinkFrameworkActorFacade Actors);

internal static class ZLinkFrameworkRuntimeComponentFactory
{
    public static ZLinkFrameworkRuntimeComponents Create(
        ZLinkFrameworkRuntime runtime,
        IServiceProvider services,
        IZLinkBackendAdapterFactory backendAdapterFactory,
        ZLinkFrameworkRegistration registration,
        ZLinkLocationLifecycle? locationLifecycle,
        ZLinkHandlerRegistry handlerRegistry,
        ZLinkHandlerDispatcher dispatcher,
        Func<ZLinkFrameworkRuntimeState> getOrStartState,
        Func<IZLinkBackendSpotNode?> getActorSpotNode)
    {
        var channels = new ZLinkChannelRuntimeManager(
            backendAdapterFactory,
            registration,
            new ZLinkChannelReceiveLoop(
                new ZLinkFanoutPacketDispatcher(
                    handlerRegistry,
                    dispatcher,
                    registration,
                    runtime,
                    services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkFanoutPacketDispatcher>())));
        var streams = new ZLinkStreamRuntimeManager(services, backendAdapterFactory, registration);
        var spots = new ZLinkSpotRuntimeManager(
            services,
            runtime,
            backendAdapterFactory,
            registration,
            locationLifecycle);
        var stateFactory = new ZLinkFrameworkRuntimeStateFactory(
            runtime,
            backendAdapterFactory,
            registration,
            channels,
            streams,
            spots);
        var actorSessionManager = new ZLinkActorSessionManager(
            runtime,
            services,
            getActorSpotNode,
            locationLifecycle,
            new ZLinkBoundSessionService(runtime));
        var actors = new ZLinkFrameworkActorFacade(
            runtime,
            registration,
            services,
            spots,
            actorSessionManager,
            getOrStartState,
            getActorSpotNode);
        return new ZLinkFrameworkRuntimeComponents(
            channels,
            streams,
            spots,
            stateFactory,
            actorSessionManager,
            actors);
    }
}
