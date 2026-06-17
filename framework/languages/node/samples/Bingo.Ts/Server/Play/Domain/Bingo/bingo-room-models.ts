import { BingoModes, BingoRoomStatus } from '../../../../Shared/Contracts/messages';
import type { BingoMode, BingoRoomSettingsPayload } from '../../../../Shared/Contracts/messages';

type BingoRoomSettings = {
  mode: BingoMode;
  roomName: string;
  requiredPlayers: number;
  maxDrawNumber: number;
  drawDeck: number[];
};

function createRoomSettings(roomSeq: number, mode: BingoMode = BingoModes.twoPlayer): BingoRoomSettings {
  const maxDrawNumber = 15;
  return {
    mode,
    roomName: `Bingo Room ${String(roomSeq).padStart(3, '0')}`,
    requiredPlayers: 2,
    maxDrawNumber,
    drawDeck: Array.from({ length: maxDrawNumber }, (_value, index) => index + 1)
  };
}

function roomSettingsFromPayload(payload: Partial<BingoRoomSettingsPayload>): BingoRoomSettings {
  const maxDrawNumber = payload.maxDrawNumber ?? 15;
  return {
    mode: payload.mode ?? BingoModes.twoPlayer,
    roomName: payload.roomName ?? 'Bingo Room 000',
    requiredPlayers: payload.requiredPlayers ?? 2,
    maxDrawNumber,
    drawDeck: Array.from({ length: maxDrawNumber }, (_value, index) => index + 1)
  };
}

export { createRoomSettings, roomSettingsFromPayload };
export type { BingoRoomSettings };
