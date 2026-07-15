import { render } from 'preact';
import { GamePage } from '../pages/game/game-page';
import '../shared/ui/theme.css';

render(<GamePage />, document.getElementById('app')!);
