using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.E2ETests;

public abstract partial class SpotTestSupport
{
    public sealed class StageSpot : IZLinkSpot
    {
        private readonly SpotScopeMarker _scopeMarker;
        private readonly SpotEventsRecorder _events;
        private readonly IZLinkSpotClient _spotClient;

        public StageSpot(
            IZLinkSpotContext context,
            SpotScopeMarker scopeMarker,
            SpotEventsRecorder events,
            IZLinkSpotClient spotClient)
        {
            Context = context;
            _scopeMarker = scopeMarker;
            _events = events;
            _spotClient = spotClient;
        }

        public IZLinkSpotContext Context { get; }

        public string ScopeId => _scopeMarker.Id;

        public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _events.RecordInitialized(Context.SpotRid, _scopeMarker.Id);

            await _spotClient.SendChannel("orders", new StageBootCommand(_scopeMarker.Id)).Submit(cancellationToken);
        }

        public ValueTask OnClosingAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            _events.RecordClosing(Context.SpotRid);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class CreatePayloadStageSpot(
        IZLinkSpotContext context,
        SpotCreatePayloadRecorder recorder) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public async ValueTask OnCreateAsync(
            IReadOnlyList<Message> createParts,
            CancellationToken cancellationToken)
        {
            await recorder.RecordAsync(createParts, cancellationToken);
        }
    }
}
