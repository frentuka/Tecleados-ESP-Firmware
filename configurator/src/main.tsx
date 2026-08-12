import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.tsx'
import { ConfirmModalProvider } from './hooks/useConfirm'
import { ResponsiveProvider } from './contexts/ResponsiveContext'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <ConfirmModalProvider>
      <ResponsiveProvider>
        <App />
      </ResponsiveProvider>
    </ConfirmModalProvider>
  </StrictMode>,
)
