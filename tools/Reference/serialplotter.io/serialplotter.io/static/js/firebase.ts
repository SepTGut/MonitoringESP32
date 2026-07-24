// src/firebase.ts
import { initializeApp, FirebaseApp, getApps, getApp } from "firebase/app";
import { getAnalytics, Analytics } from "firebase/analytics";

const firebaseConfig = {
  apiKey: "AIzaSyBtiIZK2M8r4qydJ7eijuqU1r8ryxa4RCI",
  authDomain: "serial-plotter.firebaseapp.com",
  projectId: "serial-plotter",
  storageBucket: "serial-plotter.firebasestorage.app",
  messagingSenderId: "205368182607",
  appId: "1:205368182607:web:68005bad0db3f7f0c7e73a",
  measurementId: "G-FPBGD0CM45",
};

// Initialize Firebase
let app: FirebaseApp;
let analytics: Analytics;

app = initializeApp(firebaseConfig, "serial-plotter-app");

analytics = getAnalytics(app);

export { app, analytics };
