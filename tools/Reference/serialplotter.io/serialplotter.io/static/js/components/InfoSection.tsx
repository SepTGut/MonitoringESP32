import React from "react";
import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
  CardDescription,
} from "./ui/card";
import { Cpu, Globe, Zap, BarChart2, Settings, Download } from "lucide-react";

export function IntroductionCard() {
  return (
    <Card className="bg-card/50 backdrop-blur">
      <CardHeader>
        <CardTitle className="text-2xl">
          The Ultimate Web-Based Serial Plotter
        </CardTitle>
        <CardDescription className="text-lg">
          Visualize your Arduino, ESP32, and serial data directly in the browser
          using the Web Serial API.
        </CardDescription>
      </CardHeader>
      <CardContent className="prose dark:prose-invert max-w-none text-muted-foreground">
        <p>
          SerialPlotter.io is a powerful, zero-install{" "}
          <strong>online serial plotter</strong> designed for engineers, makers,
          and developers. It allows you to connect your embedded devices—such as{" "}
          <strong>Arduino</strong>, <strong>ESP32</strong>,{" "}
          <strong>STM32</strong>, and other microcontrollers—directly to your
          web browser to visualize sensor data in real-time.
        </p>
        <p className="mt-4">
          Built on the modern <strong>Web Serial API</strong>, this tool
          eliminates the need for downloading bulky desktop software. Simply
          plug in your device, select the port, and start plotting high-speed
          serial data instantly.
        </p>
      </CardContent>
    </Card>
  );
}

export function InfoSection() {
  return (
    <div className="space-y-6">
      {/* Features Grid */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
        <FeatureCard
          icon={<Globe className="h-6 w-6 text-primary" />}
          title="Browser Based"
          description="No software installation required. Works on Chrome, Edge, and Opera using the Web Serial API."
        />
        <FeatureCard
          icon={<BarChart2 className="h-6 w-6 text-primary" />}
          title="Multi-Channel Plotting"
          description="Visualize multiple data streams simultaneously with automatic color coding and legend support."
        />
        <FeatureCard
          icon={<Cpu className="h-6 w-6 text-primary" />}
          title="Universal Support"
          description="Compatible with Arduino, ESP8266, ESP32, Raspberry Pi Pico, and any device with UART serial output."
        />
        <FeatureCard
          icon={<Zap className="h-6 w-6 text-primary" />}
          title="Real-Time Performance"
          description="High-performance rendering capable of handling high baud rates and fast data streams."
        />
        <FeatureCard
          icon={<Settings className="h-6 w-6 text-primary" />}
          title="Advanced Controls"
          description="Customize baud rates, toggle auto-scroll, pause/resume, and adjust graph smoothing on the fly."
        />
        <FeatureCard
          icon={<Download className="h-6 w-6 text-primary" />}
          title="Data Export"
          description="Export your captured serial data to CSV, Excel (XLSX), or save plot images for documentation."
        />
      </div>

      {/* How it Works / FAQ */}
      <Card className="bg-card/50 backdrop-blur">
        <CardHeader>
          <CardTitle>How to Use the Online Serial Plotter</CardTitle>
        </CardHeader>
        <CardContent className="space-y-4 text-muted-foreground">
          <div className="space-y-2">
            <h3 className="font-semibold text-foreground">
              1. Connect Your Device
            </h3>
            <p>
              Connect your microcontroller (Arduino, ESP32, etc.) to your
              computer via USB. Ensure your browser supports the Web Serial API
              (Chrome 89+, Edge 89+, Opera 75+).
            </p>
          </div>
          <div className="space-y-2">
            <h3 className="font-semibold text-foreground">
              2. Format Your Serial Output
            </h3>
            <p>
              To plot multiple variables, format your serial output as
              comma-separated values (e.g., <code>100,200,300</code>) or
              key-value pairs (e.g., <code>temp:25, humidity:60</code>). Use{" "}
              <code>Serial.println()</code> at the end of each data packet.
            </p>
          </div>
          <div className="space-y-2">
            <h3 className="font-semibold text-foreground">
              3. Select Port & Baud Rate
            </h3>
            <p>
              Click the "Connect" button, select your device from the list, and
              choose the matching baud rate (e.g., 9600, 115200).
            </p>
          </div>
          <div className="space-y-2">
            <h3 className="font-semibold text-foreground">
              4. Visualize & Analyze
            </h3>
            <p>
              Watch your data stream in real-time. Use the controls to pause,
              zoom, or export the data for further analysis.
            </p>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}

function FeatureCard({
  icon,
  title,
  description,
}: {
  icon: React.ReactNode;
  title: string;
  description: string;
}) {
  return (
    <Card>
      <CardHeader className="space-y-1">
        <div className="mb-2">{icon}</div>
        <CardTitle className="text-xl">{title}</CardTitle>
      </CardHeader>
      <CardContent>
        <p className="text-muted-foreground text-sm">{description}</p>
      </CardContent>
    </Card>
  );
}
