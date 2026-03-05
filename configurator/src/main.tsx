import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.tsx'
import { ConfirmModalProvider } from './hooks/useConfirm'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <ConfirmModalProvider>
      <App />
    </ConfirmModalProvider>
  </StrictMode>,
)
