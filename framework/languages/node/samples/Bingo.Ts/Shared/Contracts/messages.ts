function actorDisplayName(actorId) {
  return actorId.replace('player-', 'Player ');
}

function deterministicCard(actorId) {
  const base = {
    'player-1': [1, 2, 3, 4, 5],
    'player-2': [6, 7, 8, 9, 10],
    'player-3': [1, 2, 3, 4, 5],
    'player-4': [11, 12, 13, 14, 15]
  }[actorId] ?? [16, 17, 18, 19, 20];
  const card = [];
  for (let row = 0; row < 5; row += 1) {
    for (let col = 0; col < 5; col += 1) {
      card.push(row === 2 && col === 2 ? 0 : base[col] + row * 20);
    }
  }
  return card;
}

export { actorDisplayName, deterministicCard };
