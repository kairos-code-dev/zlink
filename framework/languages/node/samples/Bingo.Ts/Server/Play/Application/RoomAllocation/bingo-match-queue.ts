import type { BingoMode } from '../../Domain/Bingo/bingo-room-models';

const BINGO_MATCH_QUEUE = Symbol('BINGO_MATCH_QUEUE');

type WaitingRoomRecord = {
  roomId: string;
  reservedActorIds: string[];
  requiredPlayers: number;
  createdAtUnixMs: number;
};

type ReserveWaitingRoomInput = {
  mode: BingoMode;
  actorId: string;
  requiredPlayers: number;
  createRoomId: () => string;
};

type ReserveWaitingRoomResult = {
  record: WaitingRoomRecord;
  created: boolean;
  completed: boolean;
};

type BingoMatchQueue = {
  reserve(input: ReserveWaitingRoomInput): Promise<ReserveWaitingRoomResult>;
};

export { BINGO_MATCH_QUEUE };
export type {
  BingoMatchQueue,
  ReserveWaitingRoomInput,
  ReserveWaitingRoomResult,
  WaitingRoomRecord
};
