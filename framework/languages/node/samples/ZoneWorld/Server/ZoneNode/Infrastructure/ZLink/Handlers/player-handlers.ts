import { Injectable } from '@nestjs/common';
import {
  ZLinkSpotActorRequest,
  ZLinkSpotActorSend
} from '@zlink-systems/framework';
import {
  EnterZoneMsg,
  JoinWorldRes,
  MoveRejectedNotify,
} from '../../../../../Shared/contracts';
import { PacketNames } from '../../../../../Shared/contracts';
import { MoveRejectReasons, nodeOf, zoneOf } from '../../../../../Shared/spec';
import { validateMove } from '../../../Domain/move-policy';
import type { JoinWorldReq, MoveMsg } from '../../../../../Shared/contracts';
import type {
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotActorSendContext,
  ZLinkSpotActorSendHandler
} from '@zlink-systems/framework';
import { PlayerActor } from '../Actors/player-actor';
import { ZoneEntrySpot } from '../Spots/zone-entry-spot';
import { ZoneSpot } from '../Spots/zone-spot';

@Injectable()
class EntryJoinWorldHandler implements
  ZLinkEntrySpotActorRequestHandler<ZoneEntrySpot, PlayerActor, JoinWorldReq, JoinWorldRes> {
  @ZLinkSpotActorRequest(PacketNames.joinWorldReq)
  async handle(
    _spot: ZoneEntrySpot,
    actor: PlayerActor,
    _context: ZLinkSpotActorRequestContext,
    request: JoinWorldReq
  ): Promise<JoinWorldRes> {
    assertPlayerId(request.playerId, actor.actorId);
    const targetZone = zoneOf(actor.x, actor.y);
    const joined = await actor.context.joinSpot(
      targetZone,
      new EnterZoneMsg(actor.actorId, actor.x, actor.y, actor.isBot, null)
    ).timeout(10_000).submit();
    if (joined.status !== 'accepted') {
      return new JoinWorldRes(actor.actorId, targetZone, nodeOf(targetZone), actor.x, actor.y, MoveRejectReasons.zoneMaintenance);
    }
    actor.zoneId = targetZone;
    return new JoinWorldRes(actor.actorId, targetZone, nodeOf(targetZone), actor.x, actor.y);
  }
}

@Injectable()
class ZoneJoinWorldHandler implements
  ZLinkSpotActorRequestHandler<ZoneSpot, PlayerActor, JoinWorldReq, JoinWorldRes> {
  @ZLinkSpotActorRequest(PacketNames.joinWorldReq)
  async handle(
    _spot: ZoneSpot,
    actor: PlayerActor,
    _context: ZLinkSpotActorRequestContext,
    request: JoinWorldReq
  ): Promise<JoinWorldRes> {
    assertPlayerId(request.playerId, actor.actorId);
    return new JoinWorldRes(actor.actorId, actor.zoneId, nodeOf(actor.zoneId), actor.x, actor.y);
  }
}

@Injectable()
class PlayerMoveHandler implements ZLinkSpotActorSendHandler<ZoneSpot, PlayerActor, MoveMsg> {
  @ZLinkSpotActorSend(PacketNames.moveMsg)
  async handle(
    spot: ZoneSpot,
    actor: PlayerActor,
    _context: ZLinkSpotActorSendContext,
    message: MoveMsg
  ): Promise<void> {
    const previous = { x: actor.x, y: actor.y, zoneId: actor.zoneId };
    const decision = validateMove(actor, message.x, message.y, () => false);
    if (decision.kind === 'rejected') {
      await actor.push(new MoveRejectedNotify(decision.reason, actor.x, actor.y));
      return;
    }
    const targetZone = zoneOf(message.x, message.y);
    if (!decision.zoneChanged) {
      actor.x = message.x;
      actor.y = message.y;
      spot.updatePosition(actor);
      return;
    }
    const joined = await actor.context.joinSpot(
      targetZone,
      new EnterZoneMsg(actor.actorId, message.x, message.y, actor.isBot, nodeOf(previous.zoneId))
    ).timeout(10_000).submit();
    console.log(
      `zone change result player=${actor.actorId} from=${previous.zoneId} to=${targetZone} status=${joined.status}`
    );
    if (joined.status !== 'accepted') {
      await actor.push(new MoveRejectedNotify(MoveRejectReasons.zoneMaintenance, actor.x, actor.y));
    }
  }
}

function assertPlayerId(requestPlayerId: string, actorId: string): void {
  if (!/^[a-z0-9-]{1,32}$/.test(requestPlayerId) || requestPlayerId !== actorId) {
    throw new Error(`Invalid player id '${requestPlayerId}'.`);
  }
}

export { EntryJoinWorldHandler, PlayerMoveHandler, ZoneJoinWorldHandler };
