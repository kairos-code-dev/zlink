import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_SPOT_HANDLE_RESOLVER } from '@zlink-systems/nestjs';
import { ZLinkPacket, type ZLinkHandlerContext, type ZLinkSpotHandleResolver, type ZLinkSpotPacketHandler, type ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import type {
  RemoteSpotAwaitMsg,
  RemoteSpotAwaitReq,
  AwaitMsg,
  AutomaticTurnDispatchRes
} from '../../../Shared/messages';
import { AwaitReq } from '../../../Shared/messages';
import { EvidenceStore } from '../Support/evidence-store';
import type { AwaitProbeSpot } from '../Spots/await-probe-spot';

@Injectable()
@ZLinkPacket('RemoteSpotAwaitReq')
export class RemoteSpotAwaitHandler implements ZLinkSpotRequestHandler<AwaitProbeSpot, RemoteSpotAwaitReq, AutomaticTurnDispatchRes> {
  constructor(
    private readonly evidence: EvidenceStore,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver
  ) {}

  async handle(
    spot: AwaitProbeSpot,
    request: RemoteSpotAwaitReq,
    context: ZLinkHandlerContext
  ): Promise<AutomaticTurnDispatchRes> {
    void context;
    await runRemoteSpotAwait(this.evidence, this.spotHandles, spot, request);
    return reply('ATD-D2', request.requestId, spot, 'remote-await-completed');
  }
}

@Injectable()
@ZLinkPacket('RemoteSpotAwaitMsg')
export class RemoteSpotAwaitCommandHandler implements ZLinkSpotPacketHandler<AwaitProbeSpot, RemoteSpotAwaitMsg> {
  constructor(
    private readonly evidence: EvidenceStore,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver
  ) {}

  async handle(
    spot: AwaitProbeSpot,
    request: RemoteSpotAwaitMsg,
    context: ZLinkHandlerContext
  ): Promise<void> {
    void context;
    await runRemoteSpotAwait(this.evidence, this.spotHandles, spot, request);
  }
}

async function runRemoteSpotAwait(
  evidence: EvidenceStore,
  spotHandles: ZLinkSpotHandleResolver,
  spot: AwaitProbeSpot,
  request: RemoteSpotAwaitReq | RemoteSpotAwaitMsg
): Promise<void> {
  evidence.add(
    `remote-await-started|rid=${evidence.rid}|spot=${spot.context.spotRid}`
    + `|request=${request.requestId}|target=${request.targetSpotRid}|handler=spot`
  );
  const targetSpot = request.targetSpot ?? await spotHandles.resolveSpotHandle(request.targetSpotRid);
  if (targetSpot === undefined) {
    throw new Error(`Remote spot target ref is required for '${request.targetSpotRid}'.`);
  }
  const call = spot.context.outbound
    .requestToSpot(targetSpot, Object.assign(new AwaitReq(), {
      requestId: request.requestId,
      delayMs: request.delayMs,
      correlationId: 'remote-spot'
    }))
    .timeout(5000);
  evidence.add(
    `remote-await-released|rid=${evidence.rid}|spot=${spot.context.spotRid}`
    + `|request=${request.requestId}|target=${request.targetSpotRid}|handler=spot`
  );
  const targetReply = await call.submit<AutomaticTurnDispatchRes>();
  evidence.add(
    `remote-await-resumed|rid=${evidence.rid}|spot=${spot.context.spotRid}`
    + `|request=${request.requestId}|target=${request.targetSpotRid}|targetNode=${targetReply.nodeRid}|handler=spot`
  );
  evidence.add(
    `remote-await-completed|rid=${evidence.rid}|spot=${spot.context.spotRid}`
    + `|request=${request.requestId}|target=${request.targetSpotRid}|targetNode=${targetReply.nodeRid}|handler=spot`
  );
}

function reply(
  scenarioId: string,
  requestId: string,
  spot: AwaitProbeSpot,
  marker: string
): AutomaticTurnDispatchRes {
  return {
    scenarioId,
    requestId,
    spotRid: String(spot.context.spotRid),
    nodeRid: String(spot.context.nodeRid),
    marker
  };
}
