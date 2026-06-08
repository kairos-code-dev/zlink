const BingoRoomStatus = {
  waitingForPlayers: 'WaitingForPlayers',
  running: 'Running',
  finished: 'Finished'
};

import type {
  BingoMode
} from '../../../Shared/Contracts/messages';

type BingoRoomSettings = {
  mode: BingoMode | undefined;
  roomName: string;
  requiredPlayers: number;
  drawDeck: number[];
};

function createRoomSettings(mode: BingoMode | undefined, roomSeq: number): BingoRoomSettings {
  return {
    mode,
    roomName: `Bingo Room ${String(roomSeq).padStart(3, '0')}`,
    requiredPlayers: 4,
    drawDeck: [1, 2, 3, 4, 5, 6, 7, 8]
  };
}

export { BingoRoomStatus, createRoomSettings };
