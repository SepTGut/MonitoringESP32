import * as _ from "./firebase";
import React, { useEffect } from "react";
import { SerialProvider } from "./contexts/SerialContext";
import { SerialPlot } from "./components/SerialPlot";
import { Toaster } from "sonner";
import { Dashboard } from "./components/dashboard";
import { ThemeProvider } from "./themeProvider";
// import './app.css';

const App: React.FC = () => {
  useEffect(() => {
    document.title = "Serial Plotter";
  }, []);
  return (
    <ThemeProvider>
      <Dashboard />
      <Toaster position="bottom-right" />
    </ThemeProvider>
  );
};

export default App;
