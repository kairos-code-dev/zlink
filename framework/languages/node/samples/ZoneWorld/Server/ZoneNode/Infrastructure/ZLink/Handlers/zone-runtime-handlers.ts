import { Injectable } from '@nestjs/common';
import { zlinkSpotPacketHandler, zlinkSpotSubscriptionHandler } from '@zlink-systems/nestjs';
import type {
  ZLinkPublishMessageContext,
  ZLinkSpotSubscriptionHandler,
  ZLinkSpotPacketHandler,
  ZLinkMessageContext,
  ZLinkSpotTimerHandler,
  ZLinkTimerTick
} from '@zlink-systems/framework';
import { PacketNames, WorldAnnounceNotify } from '../../../../../Shared/contracts';
import { ZoneIds, ZoneWorldNames } from '../../../../../Shared/spec';
import type { DeliverAnnounceMsg, ZoneBorderEvent } from '../../../../../Shared/contracts';
import { ZoneSpot } from '../Spots/zone-spot';
import { adjacentZones } from '../../../Domain/world';
import { Inject } from '@nestjs/common';
import { ZONEWORLD_CONFIG } from '../../../../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../../../../Configuration/configuration';

@Injectable()
class ZoneTickHandler implements ZLinkSpotTimerHandler<ZoneSpot> {
  private faultInjected = false;

  constructor(@Inject(ZONEWORLD_CONFIG) private readonly config: ZoneWorldConfiguration) {}

  async handle(spot: ZoneSpot, _tick: ZLinkTimerTick): Promise<void> {
    const zoneId = String(spot.context.spotId);
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
@zoneBorderSubscriptionHandler()
class FirstBorderSubscriptionHandler implements ZLinkSpotSubscriptionHandler<ZoneSpot, ZoneBorderEvent> {
  async handle(spot: ZoneSpot, event: ZoneBorderEvent, _context: ZLinkPublishMessageContext): Promise<void> {
    if (event.toZoneId !== String(spot.context.spotId)) return;
    spot.applyBorder(event);
  }
}

@Injectable()
@zlinkSpotPacketHandler({ spot: () => ZoneSpot, packetName: PacketNames.deliverAnnounceMsg })
class DeliverAnnounceHandler implements ZLinkSpotPacketHandler<ZoneSpot, DeliverAnnounceMsg> {
  async handle(spot: ZoneSpot, message: DeliverAnnounceMsg, _context: ZLinkMessageContext): Promise<void> {
    await spot.pushHumans(new WorldAnnounceNotify(message.announcementId, message.text));
    console.log(`zone spot announcement delivered zone=${String(spot.context.spotId)} id=${message.announcementId}`);
  }
}

class UpdateZonePositionMsg {
  constructor(readonly actorId: string, readonly x: number, readonly y: number) {}
}

@Injectable()
@zlinkSpotPacketHandler({ spot: () => ZoneSpot, packetName: 'UpdateZonePositionMsg' })
class UpdateZonePositionHandler implements ZLinkSpotPacketHandler<ZoneSpot, UpdateZonePositionMsg> {
  async handle(spot: ZoneSpot, message: UpdateZonePositionMsg, _context: ZLinkMessageContext): Promise<void> {
    spot.updatePosition(message.actorId, message.x, message.y);
  }
}

export {
  BotTickHandler,
  DeliverAnnounceHandler,
  FirstBorderSubscriptionHandler,
  UpdateZonePositionHandler,
  UpdateZonePositionMsg,
  ZoneTickHandler
};

function zoneBorderSubscriptionHandler(): ClassDecorator {
  const topics = Object.values(ZoneIds).flatMap((from) =>
    adjacentZones(from).map((to) => ZoneWorldNames.borderTopic(from, to))
  );
  return (target) => {
    for (const topic of topics) {
      zlinkSpotSubscriptionHandler({
        spot: () => ZoneSpot,
        channelName: ZoneWorldNames.bridgeMesh,
        topic
      })(target);
    }
  };
}
