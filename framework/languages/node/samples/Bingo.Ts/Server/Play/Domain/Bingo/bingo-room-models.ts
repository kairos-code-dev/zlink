const BingoRoomStatus = {
  waitingForPlayers: 'WaitingForPlayers',
  running: 'Running',
  finished: 'Finished'
};

type BingoRoomSettings = {
  mode: string | undefined;
  roomName: string;
  requiredPlayers: number;
  drawDeck: number[];
};

function createRoomSettings(mode: string | undefined, roomSeq: number): BingoRoomSettings {
  return {
    mode,
    roomName: `Bingo Room ${String(roomSeq).padStart(3, '0')}`,
    requiredPlayers: 2,
    drawDeck: [1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
  };
}

export { BingoRoomStatus, createRoomSettings };
export type { BingoRoomSettings };
