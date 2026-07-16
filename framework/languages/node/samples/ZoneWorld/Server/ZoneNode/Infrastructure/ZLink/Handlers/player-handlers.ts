import { Injectable } from '@nestjs/common';
import { ZLinkSpotActorRequest, ZLinkSpotActorSend } from '@zlink-systems/framework';
import {
  EnterWorldRes,
  EnterZoneMsg,
  BotTickRes,
  JoinWorldRes,
  MoveRejectedNotify,
  PacketNames,
} from '../../../../../Shared/contracts';
import { MoveRejectReasons, nodeOf, zoneOf } from '../../../../../Shared/spec';
import { nextBotStep } from '../../../Domain/bot-patrol';
import { validateMove } from '../../../Domain/move-policy';
import { NodeRuntimeState } from '../../../Domain/node-runtime-state';
import type { BotTickReq, EnterWorldReq, JoinWorldReq, MoveMsg } from '../../../../../Shared/contracts';
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
class EntryEnterWorldHandler implements
  ZLinkEntrySpotActorRequestHandler<ZoneEntrySpot, PlayerActor, EnterWorldReq, EnterWorldRes> {
  @ZLinkSpotActorRequest(PacketNames.enterWorldReq)
  async handle(
    _spot: ZoneEntrySpot,
    actor: PlayerActor,
    _context: ZLinkSpotActorRequestContext,
    request: EnterWorldReq
  ): Promise<EnterWorldRes> {
    actor.dirX = request.dirX;
    actor.dirY = request.dirY;
    const targetZone = zoneOf(request.x, request.y);
    const joined = await actor.context.joinSpot(
      targetZone,
      new EnterZoneMsg(actor.actorId, request.x, request.y, request.isBot, null)
    ).timeout(10_000).submit();
    return new EnterWorldRes(
      targetZone,
      nodeOf(targetZone),
      request.x,
      request.y,
      joined.status === 'accepted' ? null : MoveRejectReasons.zoneMaintenance
    );
  }
}

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
      new EnterZoneMsg(actor.actorId, actor.x, actor.y, false, null)
    ).timeout(10_000).submit();
    if (joined.status !== 'accepted') {
      return new JoinWorldRes(
        actor.actorId,
        targetZone,
        nodeOf(targetZone),
        actor.x,
        actor.y,
        MoveRejectReasons.zoneMaintenance
      );
    }
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
class PlayerMovement {
  constructor(private readonly nodeState: NodeRuntimeState) {}

  async move(spot: ZoneSpot, actor: PlayerActor, x: number, y: number): Promise<void> {
    const previousZone = actor.zoneId;
    const decision = validateMove(actor, x, y, (zoneId) => this.nodeState.targetUnavailable(zoneId));
    if (decision.kind === 'rejected') {
      await this.reject(actor, decision.reason);
      return;
    }
    const targetZone = zoneOf(x, y);
    if (!decision.zoneChanged) {
      actor.x = x;
      actor.y = y;
      spot.updatePosition(actor);
      return;
    }
    const joined = await actor.context.joinSpot(
      targetZone,
      new EnterZoneMsg(actor.actorId, x, y, actor.isBot, nodeOf(previousZone))
    ).timeout(10_000).submit();
    console.log(`zone change result player=${actor.actorId} from=${previousZone} to=${targetZone} status=${joined.status}`);
    if (joined.status !== 'accepted') {
      await this.reject(actor, MoveRejectReasons.zoneMaintenance);
      return;
    }
  }

  private async reject(actor: PlayerActor, reason: typeof MoveRejectReasons[keyof typeof MoveRejectReasons]): Promise<void> {
    if (actor.isBot) {
      actor.dirX *= -1;
      actor.dirY *= -1;
      console.log(`bot direction reversed bot=${actor.actorId} x=${actor.x} y=${actor.y}`);
      return;
    }
    await actor.push(new MoveRejectedNotify(reason, actor.x, actor.y));
  }
}

@Injectable()
class PlayerMoveHandler implements ZLinkSpotActorSendHandler<ZoneSpot, PlayerActor, MoveMsg> {
  constructor(private readonly movement: PlayerMovement) {}

  @ZLinkSpotActorSend(PacketNames.moveMsg)
  async handle(
    spot: ZoneSpot,
    actor: PlayerActor,
    _context: ZLinkSpotActorSendContext,
    message: MoveMsg
  ): Promise<void> {
    await this.movement.move(spot, actor, message.x, message.y);
  }
}

@Injectable()
class PlayerBotTickHandler implements ZLinkSpotActorRequestHandler<ZoneSpot, PlayerActor, BotTickReq, BotTickRes> {
  constructor(private readonly movement: PlayerMovement) {}

  @ZLinkSpotActorRequest(PacketNames.botTickReq)
  async handle(
    spot: ZoneSpot,
    actor: PlayerActor,
    _context: ZLinkSpotActorRequestContext,
    _message: BotTickReq
  ): Promise<BotTickRes> {
    if (!actor.isBot) return new BotTickRes();
    const next = nextBotStep(actor.x, actor.y, actor.dirX, actor.dirY);
    await this.movement.move(spot, actor, next.x, next.y);
    return new BotTickRes();
  }
}

function assertPlayerId(requestPlayerId: string, actorId: string): void {
  if (!/^[a-z0-9-]{1,32}$/.test(requestPlayerId) || requestPlayerId !== actorId) {
    throw new Error(`Invalid player id '${requestPlayerId}'.`);
  }
}

export {
  EntryEnterWorldHandler,
  EntryJoinWorldHandler,
  PlayerBotTickHandler,
  PlayerMoveHandler,
  PlayerMovement,
  ZoneJoinWorldHandler
};
