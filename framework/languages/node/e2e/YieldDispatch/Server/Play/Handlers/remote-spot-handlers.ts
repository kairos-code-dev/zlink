import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerContext, ZLinkSpotPacketHandler, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import type {
  RemoteSpotYieldMsg,
  RemoteSpotYieldReq,
  YieldMsg,
  YieldReq,
  YieldDispatchRes
} from '../../../Shared/messages';
import { EvidenceStore } from '../Support/evidence-store';
import type { YieldProbeSpot } from '../Spots/yield-probe-spot';

@Injectable()
export class RemoteSpotYieldHandler implements ZLinkSpotRequestHandler<YieldProbeSpot, RemoteSpotYieldReq, YieldDispatchRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: YieldProbeSpot,
    request: RemoteSpotYieldReq,
    context: ZLinkHandlerContext
  ): Promise<YieldDispatchRes> {
    void context;
    await runRemoteSpotYield(this.evidence, spot, request);
    return reply('YD-D2', request.requestId, spot, 'remote-yield-completed');
  }
}

@Injectable()
export class RemoteSpotYieldCommandHandler implements ZLinkSpotPacketHandler<YieldProbeSpot, RemoteSpotYieldMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: YieldProbeSpot,
    request: RemoteSpotYieldMsg,
    context: ZLinkHandlerContext
  ): Promise<void> {
    void context;
    await runRemoteSpotYield(this.evidence, spot, request);
  }
}

async function runRemoteSpotYield(
  evidence: EvidenceStore,
  spot: YieldProbeSpot,
  request: RemoteSpotYieldReq | RemoteSpotYieldMsg
): Promise<void> {
  evidence.add(
    `remote-yield-started|rid=${evidence.rid}|spot=${spot.context.spotRid}`
    + `|request=${request.requestId}|target=${request.targetSpotRid}|handler=spot`
  );
  if (request.targetSpot === undefined) {
    throw new Error(`Remote spot target ref is required for '${request.targetSpotRid}'.`);
  }
  const call = spot.context.outbound
    .requestToSpot(request.targetSpot, {
      requestId: request.requestId,
      delayMs: request.delayMs,
      correlationId: 'remote-spot'
    } satisfies YieldReq)
    .packetName('YieldReq')
    .timeout(5000);
  evidence.add(
    `remote-yield-released|rid=${evidence.rid}|spot=${spot.context.spotRid}`
    + `|request=${request.requestId}|target=${request.targetSpotRid}|handler=spot`
  );
  const targetReply = await call.yield<YieldDispatchRes>();
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
): YieldDispatchRes {
  return {
    scenarioId,
    requestId,
    spotRid: String(spot.context.spotRid),
    nodeRid: String(spot.context.nodeRid),
    marker
  };
}
