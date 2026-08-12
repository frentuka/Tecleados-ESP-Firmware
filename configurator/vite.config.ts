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
            if (id.includes('@mediapipe')) return 'mediapipe';
            if (id.includes('react') || id.includes('react-dom')) return 'react-core';
          }
        }
      }
    },
    chunkSizeWarningLimit: 1500
  }
})
