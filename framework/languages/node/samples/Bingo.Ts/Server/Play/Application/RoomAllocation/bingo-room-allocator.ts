const { Inject } = require('@nestjs/common');
const { ZLINK_SPOT_MANAGER } = require('@zlink-systems/nestjs');
const { BingoNotificationPublisher } = require('../../Adapters/ZLink/Notifications/bingo-notification-publisher');
const { createRoomSettings } = require('../../Domain/Bingo/bingo-room-models');
const { BingoRoomSpot } = require('../../Adapters/ZLink/Spots/bingo-room-spot');
import type { ZLinkSpotManager } from '../../../../../../packages/framework/dist';
import type { BingoNotificationPublisher as BingoNotificationPublisherType } from '../../Adapters/ZLink/Notifications/bingo-notification-publisher';
import type { BingoRoomSpot as BingoRoomSpotType } from '../../Adapters/ZLink/Spots/bingo-room-spot';
import type { BingoRoomSettings } from '../../Domain/Bingo/bingo-room-models';
import type {
  BingoMode
} from '../../../../Shared/Contracts/messages';

type BingoRoomAllocation = {
  roomId: string;
  room: BingoRoomSpotType;
};

class BingoRoomAllocator {
  private currentRoomId: string | null;
  private currentRoomSettings: BingoRoomSettings | null;
  private reservedSeats: number;
  private roomSeq: number;
  private readonly rooms: Map<string, BingoRoomSpotType>;

  constructor(
    private readonly notifications: BingoNotificationPublisherType,
    private readonly spotManager: ZLinkSpotManager
  ) {
    this.notifications = notifications;
    this.currentRoomId = null;
    this.currentRoomSettings = null;
    this.reservedSeats = 0;
    this.roomSeq = 0;
    this.rooms = new Map();
  }

  async allocate(mode: BingoMode | undefined): Promise<BingoRoomAllocation> {
    let settings = createRoomSettings(mode, this.roomSeq + 1);
    if (
      this.currentRoomId === null ||
      this.currentRoomSettings === null ||
      this.currentRoomSettings.mode !== settings.mode ||
      this.reservedSeats >= this.currentRoomSettings.requiredPlayers
    ) {
      this.roomSeq += 1;
      settings = createRoomSettings(mode, this.roomSeq);
      this.currentRoomId = `bingo-room-${String(this.roomSeq).padStart(3, '0')}`;
      this.currentRoomSettings = settings;
      this.reservedSeats = 0;
      await this.spotManager.getOrCreate(BingoRoomSpot, this.currentRoomId);
      await this.spotManager.executeOnSpot<BingoRoomSpotType, void>(BingoRoomSpot, this.currentRoomId, (room) => {
        room.configureRoom(settings, this.notifications);
        this.rooms.set(this.currentRoomId!, room);
      });
    }

    this.reservedSeats += 1;
    const room = this.rooms.get(this.currentRoomId);
    if (room === undefined) {
      throw new Error(`Bingo room ${this.currentRoomId} was not created.`);
    }
    return {
      roomId: this.currentRoomId,
      room
    };
  }

  require(roomId: string): BingoRoomSpotType {
    const room = this.rooms.get(roomId);
    if (room === undefined) {
      throw new Error(`Bingo room ${roomId} does not exist.`);
    }
    return room;
  }

  async executeInRoom<TResult>(
    roomId: string,
    operation: (room: BingoRoomSpotType) => TResult | Promise<TResult>
  ): Promise<TResult> {
    return await this.spotManager.executeOnSpot<BingoRoomSpotType, TResult>(BingoRoomSpot, roomId, operation);
  }
}

Inject(BingoNotificationPublisher)(BingoRoomAllocator, undefined, 0);
Inject(ZLINK_SPOT_MANAGER)(BingoRoomAllocator, undefined, 1);

export { BingoRoomAllocator };
export type { BingoRoomAllocation };
