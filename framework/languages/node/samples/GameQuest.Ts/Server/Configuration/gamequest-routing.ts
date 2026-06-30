function questMissionRouteRid(playerId: string): 'mission-a' | 'mission-b' {
  return ownerIndex(playerId) === 1 ? 'mission-b' : 'mission-a';
}

function ownerIndex(playerId: string): 0 | 1 {
  let sum = 0;
  for (const byte of Buffer.from(playerId, 'utf8')) {
    sum += byte;
  }
  return sum % 2 === 1 ? 1 : 0;
}

export {
  ownerIndex,
  questMissionRouteRid
};
