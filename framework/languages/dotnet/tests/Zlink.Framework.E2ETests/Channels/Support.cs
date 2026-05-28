using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Channels;
internal static class ChannelSupport
{
    internal static async Task RunMeshRouterAsync(
        IRouterSocket router,
        MeshProfileRecorder recorder,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            using var received = Received.Create();
            if (!router.Recv(received, RecvFlags.DontWait))
            {
                await Task.Delay(10).ConfigureAwait(false);
                continue;
            }

            var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
            var request = (MeshProfileRequest?)ZLinkEnvelopeCodec.DecodeBody(
                received.Parts,
                typeof(MeshProfileRequest));
            if (request is null)
            {
                continue;
            }

            if (header.Kind == ZLinkMessageKind.Command)
            {
                recorder.Commands.Enqueue(request.UserId);
                continue;
            }

            recorder.Requests.Enqueue(request.UserId);
            var reply = ZLinkEnvelopeCodec.EncodeParts(
                new ZLinkEnvelopeHeader(
                    ZLinkMessageKind.Response,
                    header.ChannelName,
                    header.MessageName,
                    ZLinkEnvelopeCodec.DefaultContentType,
                    header.CorrelationId,
                    null,
                    null,
                    null,
                    null),
                new MeshProfileReply { Name = $"mesh:{request.UserId}" },
                typeof(MeshProfileReply));
            received.Reply().Messages(reply).Submit();
        }
    }
}
