import { render } from 'preact';
import { OpsPage } from '../pages/ops/ops-page';
import '../shared/ui/theme.css';

render(<OpsPage />, document.getElementById('app')!);
