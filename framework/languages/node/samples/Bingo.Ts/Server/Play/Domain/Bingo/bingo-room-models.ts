import { BingoModes, BingoRoomStatus } from '../../../../Shared/Contracts/messages';
import type { BingoMode } from '../../../../Shared/Contracts/messages';

type BingoRoomSettings = {
  mode: BingoMode;
  roomName: string;
  requiredPlayers: number;
  maxDrawNumber: number;
  drawDeck: number[];
};

function createRoomSettings(mode: BingoMode | undefined, roomSeq: number): BingoRoomSettings {
  const selectedMode = mode ?? BingoModes.twoPlayer;
  if (selectedMode !== BingoModes.twoPlayer) {
    throw new Error(`Unsupported bingo mode. mode=${selectedMode}`);
  }
  const maxDrawNumber = 15;
  return {
    mode: selectedMode,
    roomName: `Bingo Room ${String(roomSeq).padStart(3, '0')}`,
    requiredPlayers: 2,
    maxDrawNumber,
    drawDeck: Array.from({ length: maxDrawNumber }, (_value, index) => index + 1)
  };
}

function roomSettingsFromPayload(payload: {
  roomName?: string | null;
  mode?: BingoMode | null;
  requiredPlayers?: number | null;
  maxDrawNumber?: number | null;
}): BingoRoomSettings {
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
