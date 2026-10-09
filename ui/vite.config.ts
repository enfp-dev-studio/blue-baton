import path from 'node:path';
import tailwindcss from '@tailwindcss/vite';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

export default defineConfig({
  plugins: [react(), tailwindcss()],
  /* Relative asset paths: the same build is served by the app, the dev bridge, or opened from disk */
  base: './',
  resolve: { alias: { '@': path.resolve(__dirname, './src') } },
  server: { port: 5180, strictPort: true },
  build: { outDir: 'dist', emptyOutDir: true, target: 'es2022' },
});
