import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import basicSsl from '@vitejs/plugin-basic-ssl'

// https://vite.dev/config/
export default defineConfig({
  plugins: [
    react(),
    basicSsl()
  ],
  server: {
    host: true
  },
  build: {
    rollupOptions: {
      output: {
        manualChunks(id) {
          if (id.includes('node_modules')) {
            if (id.includes('three') || id.includes('@react-three')) return 'three';
            if (id.includes('lottie')) return 'lottie';
            if (id.includes('react') || id.includes('react-dom')) return 'react-core';
          }
        }
      },
      onwarn(warning, warn) {
        // Suppress EVAL warning for lottie-web, as it relies on eval for advanced after-effects expressions internally
        if (warning.code === 'EVAL' && warning.id?.includes('lottie-web')) {
          return;
        }
        warn(warning);
      }
    },
    chunkSizeWarningLimit: 1500
  }
})
