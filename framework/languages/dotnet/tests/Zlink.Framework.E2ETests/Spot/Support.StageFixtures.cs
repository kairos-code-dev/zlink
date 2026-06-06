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
        public StageSpot(
            IZLinkSpotContext context,
            SpotScopeMarker scopeMarker,
            SpotEventsRecorder events)
        {
            Context = context;
            _scopeMarker = scopeMarker;
            _events = events;
        }

        public IZLinkSpotContext Context { get; }

        public string ScopeId => _scopeMarker.Id;

        public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _events.RecordInitialized(Context.SpotRid, _scopeMarker.Id);

            await Context.Outbound.SendToChannel("orders", new StageBootCommand(_scopeMarker.Id)).Submit(cancellationToken);
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

        public async ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
            Message request,
            CancellationToken cancellationToken)
        {
            await recorder.RecordAsync(request, cancellationToken);
            return ZLinkSpotCreateResponse.Accept();
        }
    }

    public sealed class RejectingCreateStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
            Message request,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.FromResult(
                ZLinkSpotCreateResponse.Reject(Message.From($"reject:{request.GetString()}")));
        }
    }
}
