const BingoRoomStatus = {
  waitingForPlayers: 'WaitingForPlayers',
  running: 'Running',
  finished: 'Finished'
};

function createRoomSettings(mode, roomSeq) {
  return {
    mode,
    roomName: `Bingo Room ${String(roomSeq).padStart(3, '0')}`,
    requiredPlayers: 4,
    drawDeck: [1, 2, 3, 4, 5, 6, 7, 8]
  };
}

export { BingoRoomStatus, createRoomSettings };
