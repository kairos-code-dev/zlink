import { Injectable } from '@nestjs/common';
import { ZLinkPacket } from '@zlink-systems/framework';
import type {
  ZLinkPublishContext,
  ZLinkSpotSubscriptionHandler,
  ZLinkSpotPacketHandler,
  ZLinkHandlerContext,
  ZLinkSpotTimerHandler,
  ZLinkTimerTick
} from '@zlink-systems/framework';
import { PacketNames, WorldAnnounceNotify } from '../../../../../Shared/contracts';
import type { DeliverAnnounceMsg, ZoneBorderEvent } from '../../../../../Shared/contracts';
import type { ZoneSpot } from '../Spots/zone-spot';
import { Inject } from '@nestjs/common';
import { ZONEWORLD_CONFIG } from '../../../../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../../../../Configuration/configuration';

@Injectable()
class ZoneTickHandler implements ZLinkSpotTimerHandler<ZoneSpot> {
  private faultInjected = false;

  constructor(@Inject(ZONEWORLD_CONFIG) private readonly config: ZoneWorldConfiguration) {}

  async handle(spot: ZoneSpot, _tick: ZLinkTimerTick): Promise<void> {
    const zoneId = String(spot.context.spotRid);
    if (this.config.zoneNode?.faultTickZone === zoneId && !this.faultInjected) {
      this.faultInjected = true;
      throw new Error(`injected tick failure for ${zoneId}`);
    }
    await spot.tick();
  }
}

@Injectable()
class BotTickHandler implements ZLinkSpotTimerHandler<ZoneSpot> {
  async handle(spot: ZoneSpot, _tick: ZLinkTimerTick): Promise<void> {
    await spot.tickBots();
  }
}

@Injectable()
class FirstBorderSubscriptionHandler implements ZLinkSpotSubscriptionHandler<ZoneSpot, ZoneBorderEvent> {
  async handle(spot: ZoneSpot, event: ZoneBorderEvent, _context: ZLinkPublishContext): Promise<void> {
    spot.applyBorder(event);
  }
}

@Injectable()
class SecondBorderSubscriptionHandler implements ZLinkSpotSubscriptionHandler<ZoneSpot, ZoneBorderEvent> {
  async handle(spot: ZoneSpot, event: ZoneBorderEvent, _context: ZLinkPublishContext): Promise<void> {
    spot.applyBorder(event);
  }
}

@Injectable()
@ZLinkPacket(PacketNames.deliverAnnounceMsg)
class DeliverAnnounceHandler implements ZLinkSpotPacketHandler<ZoneSpot, DeliverAnnounceMsg> {
  async handle(spot: ZoneSpot, message: DeliverAnnounceMsg, _context: ZLinkHandlerContext): Promise<void> {
    await spot.pushHumans(new WorldAnnounceNotify(message.announcementId, message.text));
    console.log(`zone spot announcement delivered zone=${String(spot.context.spotRid)} id=${message.announcementId}`);
  }
}

class UpdateZonePositionMsg {
  constructor(readonly actorId: string, readonly x: number, readonly y: number) {}
}

@Injectable()
@ZLinkPacket('UpdateZonePositionMsg')
class UpdateZonePositionHandler implements ZLinkSpotPacketHandler<ZoneSpot, UpdateZonePositionMsg> {
  async handle(spot: ZoneSpot, message: UpdateZonePositionMsg, _context: ZLinkHandlerContext): Promise<void> {
    spot.updatePosition(message.actorId, message.x, message.y);
  }
}

export {
  BotTickHandler,
  DeliverAnnounceHandler,
  FirstBorderSubscriptionHandler,
  SecondBorderSubscriptionHandler,
  UpdateZonePositionHandler,
  UpdateZonePositionMsg,
  ZoneTickHandler
};
