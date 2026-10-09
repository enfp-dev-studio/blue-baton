import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import './index.css';
import { createController } from '@/lib/board/controller.js';
import { localizePage } from '@/lib/board/i18n.js';
import type { Controller } from '@/lib/board/types';
import { applyTheme } from '@/lib/theme';

applyTheme();
localizePage();
/* One controller for the page, outside React (StrictMode runs effects twice in development) */
const ctl = createController() as unknown as Controller;

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App ctl={ctl} />
  </StrictMode>,
);
