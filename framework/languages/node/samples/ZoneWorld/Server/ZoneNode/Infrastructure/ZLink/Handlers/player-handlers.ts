import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_SPOT_HANDLE_RESOLVER, ZLINK_SPOT_OUTBOUND } from '@zlink-systems/nestjs';
import { ZLinkSpotActorRequest, ZLinkSpotActorSend } from '@zlink-systems/framework';
import {
  EnterWorldRes,
  EnterZoneMsg,
  BotTickRes,
  JoinWorldRes,
  MoveRejectedNotify,
  PacketNames,
} from '../../../../../Shared/contracts';
import { MoveRejectReasons, nodeOf, ZoneIds, ZoneWorldNames, zoneOf } from '../../../../../Shared/spec';
import type { ZoneId } from '../../../../../Shared/spec';
import { nextBotStep } from '../../../Domain/bot-patrol';
import { validateMove } from '../../../Domain/move-policy';
import { NodeRuntimeState } from '../../../Domain/node-runtime-state';
import type { BotTickReq, EnterWorldReq, JoinWorldReq, MoveMsg } from '../../../../../Shared/contracts';
import type {
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorSendContext,
  ZLinkSpotHandleResolver,
  ZLinkSpotOutbound
} from '@zlink-systems/framework';
import type { PlayerActor } from '../Actors/player-actor';
import { UpdateZonePositionMsg } from './zone-runtime-handlers';

@Injectable()
class EntryEnterWorldHandler {
  @ZLinkSpotActorRequest(PacketNames.enterWorldReq)
  async handle(
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
    if (joined.status === 'accepted') {
      actor.x = request.x;
      actor.y = request.y;
      actor.zoneId = targetZone;
      actor.isBot = request.isBot;
    }
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
class EntryJoinWorldHandler {
  @ZLinkSpotActorRequest(PacketNames.joinWorldReq)
  async handle(
    actor: PlayerActor,
    _context: ZLinkSpotActorRequestContext,
    request: JoinWorldReq
  ): Promise<JoinWorldRes> {
    assertPlayerId(request.playerId, actor.actorId);
    if (Object.values(ZoneIds).includes(actor.context.spotRid as ZoneId)) {
      return new JoinWorldRes(actor.actorId, actor.zoneId, nodeOf(actor.zoneId), actor.x, actor.y);
    }
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
    actor.zoneId = targetZone;
    return new JoinWorldRes(actor.actorId, targetZone, nodeOf(targetZone), actor.x, actor.y);
  }
}

@Injectable()
@Injectable()
class PlayerMovement {
  constructor(
    private readonly nodeState: NodeRuntimeState,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly spotOutbound: ZLinkSpotOutbound
  ) {}

  async move(actor: PlayerActor, x: number, y: number): Promise<void> {
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
      const spotRid = actor.context.spotRid;
      if (spotRid === undefined) throw new Error(`Player '${actor.actorId}' is not joined to a zone.`);
      const spot = await this.spotHandles.resolveSpotHandle(ZoneWorldNames.zoneMesh, spotRid);
      if (spot === undefined) throw new Error(`Zone '${String(spotRid)}' could not be resolved.`);
      await this.spotOutbound.sendToSpot(spot, new UpdateZonePositionMsg(actor.actorId, x, y)).submit();
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
    actor.x = x;
    actor.y = y;
    actor.zoneId = targetZone;
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
class PlayerMoveHandler {
  constructor(private readonly movement: PlayerMovement) {}

  @ZLinkSpotActorSend(PacketNames.moveMsg)
  async handle(
    actor: PlayerActor,
    _context: ZLinkSpotActorSendContext,
    message: MoveMsg
  ): Promise<void> {
    await this.movement.move(actor, message.x, message.y);
  }
}

@Injectable()
class PlayerBotTickHandler {
  constructor(private readonly movement: PlayerMovement) {}

  @ZLinkSpotActorRequest(PacketNames.botTickReq)
  async handle(
    actor: PlayerActor,
    _context: ZLinkSpotActorRequestContext,
    _message: BotTickReq
  ): Promise<BotTickRes> {
    if (!actor.isBot) return new BotTickRes();
    const next = nextBotStep(actor.x, actor.y, actor.dirX, actor.dirY);
    await this.movement.move(actor, next.x, next.y);
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
};
