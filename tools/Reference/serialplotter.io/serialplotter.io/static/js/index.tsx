import React from 'react';
import ReactDOM from 'react-dom/client';
import App from './App';
import './index.css';
import { SerialProvider } from './contexts/SerialContext';
import './firebase';


ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <SerialProvider>
      <App />
    </SerialProvider>
  </React.StrictMode>
);