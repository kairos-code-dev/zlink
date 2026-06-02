const { createRoomSettings } = require('../BingoRoomSpots/bingo-room-models');
const { BingoRoomSpot } = require('../BingoRoomSpots/bingo-room-spot');

class BingoRoomDirectory {
  [key: string]: any;
  constructor(notifications) {
    this.notifications = notifications;
    this.currentRoomId = null;
    this.currentRoomSettings = null;
    this.reservedSeats = 0;
    this.roomSeq = 0;
    this.rooms = new Map();
  }

  async allocate(mode) {
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

  require(roomId) {
    const room = this.rooms.get(roomId);
    if (room === undefined) {
      throw new Error(`Bingo room ${roomId} does not exist.`);
    }
    return room;
  }
}

export { BingoRoomDirectory };
