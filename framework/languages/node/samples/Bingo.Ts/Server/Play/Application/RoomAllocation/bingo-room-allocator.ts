const { Inject } = require('@nestjs/common');
const { BingoNotificationPublisher } = require('../../Adapters/ZLink/Notifications/bingo-notification-publisher');
const { createRoomSettings } = require('../../Domain/Bingo/bingo-room-models');
const { BingoRoomSpot } = require('../../Adapters/ZLink/Spots/bingo-room-spot');
import type {
  BingoMode
} from '../../../../Shared/Contracts/messages';

type BingoRoomAllocation = {
  roomId: string;
  room: any;
};

class BingoRoomAllocator {
  [key: string]: any;
  constructor(notifications: any) {
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
      this.rooms.set(this.currentRoomId, new BingoRoomSpot(this.currentRoomId, settings, this.notifications));
    }

    this.reservedSeats += 1;
    return {
      roomId: this.currentRoomId,
      room: this.rooms.get(this.currentRoomId)
    };
  }

  require(roomId: string): any {
    const room = this.rooms.get(roomId);
    if (room === undefined) {
      throw new Error(`Bingo room ${roomId} does not exist.`);
    }
    return room;
  }
}

Inject(BingoNotificationPublisher)(BingoRoomAllocator, undefined, 0);

export { BingoRoomAllocator };
