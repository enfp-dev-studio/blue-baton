// SPDX-License-Identifier: GPL-3.0-only
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { Panel } from './components/Panel';
import './index.css';
import { createController } from '@/lib/board/controller.js';
import { localizePage } from '@/lib/board/i18n.js';
import type { Controller } from '@/lib/board/types';
import { applyTheme } from '@/lib/theme';

applyTheme();
localizePage();
/* One controller for the page, outside React (StrictMode runs effects twice in development) */
const params = new URLSearchParams(location.search);
const panel = params.get('view') === 'panel';   /* the panel under the menu bar icon (app/src-tauri, window "panel") */
if (panel) document.documentElement.classList.add('bb-panel');
const ctl = createController(params, { role: panel ? 'panel' : 'main' }) as unknown as Controller;

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    {panel ? <Panel ctl={ctl} /> : <App ctl={ctl} />}
  </StrictMode>,
);
