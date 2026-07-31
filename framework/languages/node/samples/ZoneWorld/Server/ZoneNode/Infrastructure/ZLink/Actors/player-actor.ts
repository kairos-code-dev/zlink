import { zlinkSpotActorSendHandler } from '@zlink-systems/nestjs';
import { ZoneSpot } from '../Spots/zone-spot';
import type { ZLinkActor, ZLinkActorContext } from '@zlink-systems/framework';
import { ZoneIds } from '../../../../../Shared/spec';
import {
  WorldAnnounceNotify,
  ZoneChangedNotify,
  ZoneStateNotify
} from '../../../../../Shared/contracts';
import type { ZoneId } from '../../../../../Shared/spec';

class PlayerActor implements ZLinkActor {
  readonly context!: ZLinkActorContext;

  constructor(
    readonly actorId: string,
    public x = 25,
    public y = 25,
    public zoneId: ZoneId = ZoneIds.northWest,
    public isBot = false,
    public dirX = 0,
    public dirY = 0
  ) {}

  push(payload: unknown): void {
    if (this.isBot) return;
    this.context.boundSession.send(payload).submit();
  }
}

class DeliverZoneNotification {
  readonly packetName: string;

  constructor(readonly payload: unknown) {
    this.packetName = typeof payload === 'object' && payload !== null
      ? payload.constructor.name
      : '';
  }
}

@zlinkSpotActorSendHandler({
  spot: () => ZoneSpot,
  actor: () => PlayerActor,
  packetName: 'DeliverZoneNotification'
})
class DeliverZoneNotificationHandler {
  async handle(actor: PlayerActor, _context: unknown, message: DeliverZoneNotification): Promise<void> {
    const value = message.payload as Record<string, unknown>;
    switch (message.packetName) {
      case 'ZoneStateNotify':
        actor.push(new ZoneStateNotify(
          value.zoneId as string,
          value.tick as number,
          value.players as ConstructorParameters<typeof ZoneStateNotify>[2]
        ));
        return;
      case 'ZoneChangedNotify':
        actor.push(new ZoneChangedNotify(
          value.playerId as string,
          value.zoneId as string,
          value.nodeId as string,
          value.transferred as boolean
        ));
        return;
      case 'WorldAnnounceNotify':
        actor.push(new WorldAnnounceNotify(value.announcementId as string, value.text as string));
        return;
      default:
        throw new Error(`Unsupported ZoneWorld notification '${message.packetName}'.`);
    }
  }
}

export { DeliverZoneNotification, DeliverZoneNotificationHandler, PlayerActor };
