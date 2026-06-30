import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerContext, ZLinkSpotPacketHandler, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import type {
  RemoteSpotYieldCommand,
  RemoteSpotYieldReq,
  YieldCommand,
  YieldDispatchReply
} from '../../../Shared/messages';
import { EvidenceStore } from '../../Support/evidence-store';
import type { YieldProbeSpot } from '../Spots/yield-probe-spot';

@Injectable()
export class RemoteSpotYieldHandler implements ZLinkSpotRequestHandler<YieldProbeSpot, RemoteSpotYieldReq, YieldDispatchReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: YieldProbeSpot,
    request: RemoteSpotYieldReq,
    context: ZLinkHandlerContext
  ): Promise<YieldDispatchReply> {
    void context;
    await runRemoteSpotYield(this.evidence, spot, request);
    return reply('YD-D2', request.requestId, spot, 'remote-yield-completed');
  }
}

@Injectable()
export class RemoteSpotYieldCommandHandler implements ZLinkSpotPacketHandler<YieldProbeSpot, RemoteSpotYieldCommand> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: YieldProbeSpot,
    request: RemoteSpotYieldCommand,
    context: ZLinkHandlerContext
  ): Promise<void> {
    void context;
    await runRemoteSpotYield(this.evidence, spot, request);
  }
}

async function runRemoteSpotYield(
  evidence: EvidenceStore,
  spot: YieldProbeSpot,
  request: RemoteSpotYieldReq | RemoteSpotYieldCommand
): Promise<void> {
  evidence.add(
    `remote-yield-started|rid=${evidence.rid}|spot=${spot.context.spotRid}`
    + `|request=${request.requestId}|target=${request.targetSpotRid}|handler=spot`
  );
  const call = spot.context.outbound
    .requestToSpot(request.targetSpotRid, {
      requestId: request.requestId,
      delayMs: request.delayMs,
      correlationId: 'remote-spot'
    } satisfies YieldCommand)
    .packetName('YieldCommand')
    .timeout(5000);
  evidence.add(
    `remote-yield-released|rid=${evidence.rid}|spot=${spot.context.spotRid}`
    + `|request=${request.requestId}|target=${request.targetSpotRid}|handler=spot`
  );
  const targetReply = await call.yield<YieldDispatchReply>();
  evidence.add(
    `remote-yield-resumed|rid=${evidence.rid}|spot=${spot.context.spotRid}`
    + `|request=${request.requestId}|target=${request.targetSpotRid}|targetNode=${targetReply.nodeRid}|handler=spot`
  );
  evidence.add(
    `remote-yield-completed|rid=${evidence.rid}|spot=${spot.context.spotRid}`
    + `|request=${request.requestId}|target=${request.targetSpotRid}|targetNode=${targetReply.nodeRid}|handler=spot`
  );
}

function reply(
  scenarioId: string,
  requestId: string,
  spot: YieldProbeSpot,
  marker: string
): YieldDispatchReply {
  return {
    scenarioId,
    requestId,
    spotRid: String(spot.context.spotRid),
    nodeRid: String(spot.context.nodeRid),
    marker
  };
}
