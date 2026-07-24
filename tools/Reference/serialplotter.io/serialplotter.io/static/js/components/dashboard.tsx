"use client";
import { SerialPlot } from "./SerialPlot";
import type React from "react";
import logo from "../components/assets/Logo.png";
import { ModeToggle } from "./ModeToggle";
import { useSerialContext } from "../contexts/SerialContext";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "../components/ui/card";
import { Button } from "../components/ui/button";
import { Badge } from "../components/ui/badge";
import { ScrollArea } from "../components/ui/scroll-area";
import { Label } from "../components/ui/label";
import { Input } from "../components/ui/input";
import { Checkbox } from "../components/ui/checkbox";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "../components/ui/select";

import {
  Download,
  Pause,
  Play,
  Plug,
  PlugIcon as PlugOff,
  Terminal,
  Trash2,
  FileText,
  Table,
  ArrowDown,
  Star,
  Send,
  ImageIcon,
  FileImage,
} from "lucide-react";
import { useState, useEffect, useRef } from "react";
import { toast } from "sonner";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from "./ui/dropdown-menu";
import { FeedbackDialog } from "./FeedbackDialog";
import { InfoSection, IntroductionCard } from "./InfoSection";

export function Dashboard() {
  const channelsRef = useRef<{ name: string; color: string }[]>([]);
  const [isFeedbackOpen, setIsFeedbackOpen] = useState(false);
  const [logMessages, setLogMessages] = useState<string[]>([]);
  const lastProcessedCounter = useRef<number | null>(null); // Moved to top level
  const {
    data,
    isConnected,
    isPaused,
    setIsPaused,
    clearData,
    connect,
    disconnect,
    displayName,
    setBaudRate,
    baudRate,
    write,
    logLines,
  } = useSerialContext();
  const [sendText, setSendText] = useState("");
  const [localBaudRate, setLocalBaudRate] = useState(baudRate.toString());
  const [showAdvanced, setShowAdvanced] = useState(false);
  const [customBaudRate, setCustomBaudRate] = useState("");
  const worker = useRef<Worker | null>(null);

  const scrollRef = useRef<HTMLDivElement>(null);
  const [isAtBottom, setIsAtBottom] = useState(true);

  const serialPlotRef = useRef<HTMLDivElement>(null);

  // Scroll to bottom if needed
  useEffect(() => {
    if (scrollRef.current && isAtBottom) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [logLines, isAtBottom]);

  const handleScroll = () => {
    const el = scrollRef.current;
    if (!el) return;

    const threshold = 50; // px from bottom
    const isNearBottom =
      el.scrollHeight - el.scrollTop - el.clientHeight < threshold;

    setIsAtBottom(isNearBottom);
  };

  const baudRates = [
    "9600",
    "19200",
    "38400",
    "57600",
    "115200",
    "230400",
    "460800",
    "921600",
  ];

  const handleBaudRateChange = (value: string) => {
    setLocalBaudRate(value);
    setBaudRate(Number.parseInt(value, 10));
  };

  const handleCustomBaudRateSave = () => {
    const rate = Number.parseInt(customBaudRate, 10);
    if (!isNaN(rate) && rate > 0) {
      setLocalBaudRate(customBaudRate);
      setBaudRate(rate);
    }
  };

  const handleConnectClick = async () => {
    if (isConnected) {
      try {
        await disconnect();
        clearData();
        setLogMessages([]);
        setIsPaused(false);
      } catch (err) {}
    } else {
      try {
        await connect();
      } catch (err) {
        if (err instanceof Error && err.message.includes("No port selected")) {
          toast.warning("Connection Failed", {
            description:
              "No port was selected. Please select a port to connect.",
            duration: 2000,
          });
        } else {
          toast.error("Connection Failed", {
            description:
              err instanceof Error ? err.message : "Unknown error occurred",
            duration: 2000,
          });
        }
      }
    }
  };

  const exportToXLSX = async () => {
    if (!data.length) return;

    // Dynamic import XLSX library
    const XLSX = await import("xlsx");

    // Get all unique channels and counters
    const channels = Array.from(new Set(data.map((d) => d.channel)));
    const counterGroups = new Map();

    // Group data by counter
    data.forEach((point) => {
      if (!counterGroups.has(point.counter)) {
        counterGroups.set(point.counter, {
          counter: point.counter,
          timestamp: new Date(point.timestamp),
          channels: {},
        });
      }
      const channelName = displayName(point.channel); // Use displayName for export
      counterGroups.get(point.counter).channels[channelName] = point.value;
    });

    // Create rows with proper structure
    const rows = Array.from(counterGroups.values()).map((group) => ({
      Counter: group.counter,
      Date: group.timestamp.toLocaleDateString("en-US", {
        year: "numeric",
        month: "2-digit",
        day: "2-digit",
      }),
      Time: group.timestamp.toLocaleTimeString("en-US", {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
        fractionalSecondDigits: 3,
        hour12: false,
      }),
      ...channels.reduce(
        (acc, ch) => ({
          ...acc,
          [displayName(ch)]: group.channels[displayName(ch)] || "",
        }),
        {}
      ),
    }));

    // Create workbook
    const wb = XLSX.utils.book_new();
    const ws = XLSX.utils.json_to_sheet(rows);

    // Set column widths
    ws["!cols"] = [
      { wch: 10 }, // Counter
      { wch: 12 }, // Date
      { wch: 15 }, // Time
      ...channels.map(() => ({ wch: 15 })), // Channel columns
    ];

    XLSX.utils.book_append_sheet(wb, ws, "Data");
    XLSX.writeFile(
      wb,
      `ez-plotter-data-${new Date().toISOString().slice(0, 10)}.xlsx`
    );
  };

  const handlePauseClick = async () => {
    if (isPaused) {
      setIsPaused(false);
    } else {
      setIsPaused(true);
    }
  };

  useEffect(() => {
    if (data.length > 0) {
      // Process all data points and maintain order
      const counterGroups = new Map<
        number,
        {
          counter: number;
          timestamp: number;
          channels: Map<string, number>;
        }
      >();

      // Group all points by counter
      data.forEach((point) => {
        if (!counterGroups.has(point.counter)) {
          counterGroups.set(point.counter, {
            counter: point.counter,
            timestamp: point.timestamp,
            channels: new Map(),
          });
        }
        counterGroups
          .get(point.counter)
          ?.channels.set(point.channel, point.value);
      });

      // Convert to array and ensure sequential ordering
      const orderedMessages = Array.from(counterGroups.values())
        .sort((a, b) => a.counter - b.counter)
        .map((group) => {
          const date = new Date(group.timestamp);
          const formattedTime =
            date.toLocaleString("en-US", {
              year: "numeric",
              month: "2-digit",
              day: "2-digit",
              hour: "2-digit",
              minute: "2-digit",
              second: "2-digit",
              hour12: false,
            }) +
            "." +
            date.getMilliseconds().toString().padStart(3, "0");

          const channelValues = Array.from(group.channels.entries())
            .sort(([aChannel], [bChannel]) => aChannel.localeCompare(bChannel))
            .map(([channel, value]) => `${displayName(channel)}: ${value}`)
            .join(", ");

          // Add the counter at the beginning of the line
          return ` #${group.counter} ${formattedTime} ${channelValues}`;
        });
      // Update logs with ordered messages
      setLogMessages(orderedMessages);
    }
  }, [data]);

  const exportToCSV = () => {
    if (!data.length) return;

    const pad = (num: number, size: number): string =>
      num.toString().padStart(size, "0");

    const channels = Array.from(new Set(data.map((d) => d.channel)));
    const counterGroups = new Map();

    data.forEach((point) => {
      if (!counterGroups.has(point.counter)) {
        counterGroups.set(point.counter, {
          counter: point.counter,
          timestamp: new Date(point.timestamp),
          channels: {},
        });
      }
      const channelName = displayName(point.channel); // Use displayName for export
      counterGroups.get(point.counter).channels[channelName] = point.value;
    });

    const headers = ["Counter", "Date", "Time", ...channels];

    const rows = Array.from(counterGroups.values()).map((group) => {
      const timestamp = new Date(group.timestamp);
      const date = ` ${timestamp.toLocaleDateString("en-US", {
        year: "numeric",
        month: "2-digit",
        day: "2-digit",
      })}`;
      const time = ` ${pad(timestamp.getHours(), 2)}:${pad(
        timestamp.getMinutes(),
        2
      )}:${pad(timestamp.getSeconds(), 2)}.${pad(
        timestamp.getMilliseconds(),
        3
      )}`;

      return [
        group.counter,
        date,
        time,
        ...channels.map((ch) => group.channels[ch] || ""),
      ];
    });

    const csvContent = [
      headers.join(","),
      ...rows.map((row) => row.join(",")),
    ].join("\n");

    const blob = new Blob([csvContent], { type: "text/csv;charset=utf-8;" });
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = "serial_data.csv";
    link.click();
  };

  const handleImageExport = async () => {
    if (!data.length) {
      toast.error("No data to export", { duration: 2000 });
      return;
    }

    try {
      // Dynamic import html-to-image library
      const { toPng } = await import("html-to-image");

      // Find the SerialPlot component in the DOM
      const plotElement =
        document.querySelector('[data-testid="serial-plot-chart"]') ||
        document.querySelector(".chart-capture-target") ||
        serialPlotRef.current;

      if (!plotElement) {
        toast.error("Could not find plot element to export", {
          duration: 2000,
        });
        return;
      }

      toast.info("Generating image...", { duration: 1000 });

      // Generate image with high quality settings
      const dataUrl = await toPng(plotElement as HTMLElement, {
        quality: 1.0,
        pixelRatio: 2,
        backgroundColor: "#ffffff",
        width: plotElement.clientWidth,
        height: plotElement.clientHeight,
      });

      // Create download link
      const link = document.createElement("a");
      link.download = `serial-plot-${new Date()
        .toISOString()
        .slice(0, 19)
        .replace(/:/g, "-")}.png`;
      link.href = dataUrl;
      link.click();

      toast.success("Image exported successfully", { duration: 2000 });
    } catch (error) {
      console.error("Image export failed:", error);
      toast.error("Failed to export image", { duration: 2000 });
    }
  };

  const handlePdfExport = async () => {
    if (!data.length) {
      toast.error("No data to export", { duration: 2000 });
      return;
    }

    try {
      // Dynamic imports
      const [{ toPng }, { default: jsPDF }] = await Promise.all([
        import("html-to-image"),
        import("jspdf"),
      ]);

      toast.info("Generating PDF...", { duration: 1000 });
      const plotElement =
        document.querySelector('[data-testid="serial-plot-chart"]') ||
        document.querySelector(".chart-capture-target");

      if (!plotElement) {
        toast.error("Could not find plot element to export", {
          duration: 2000,
        });
        return;
      }

      // Generate image of the plot
      const imageDataUrl = await toPng(plotElement as HTMLElement, {
        quality: 1.0,
        pixelRatio: 2,
        width: plotElement.clientWidth,
        height: plotElement.clientHeight,
      });
      debugger;
      const pdf = new jsPDF({
        orientation: "landscape",
        unit: "mm",
        format: "a4",
      });

      const currentDate = new Date().toLocaleString();
      const channels = Array.from(new Set(data.map((d) => d.channel)));

      pdf.setFontSize(16);
      pdf.text("Serial Plotter Data Export", 20, 20);

      pdf.setFontSize(12);
      pdf.text(`Export Date: ${currentDate}`, 20, 30);
      pdf.text(`Baud Rate: ${baudRate} bps`, 20, 37);
      pdf.text(`Data Points: ${data.length}`, 20, 44);

      let legendY = 51;
      const legendX = 20;
      const legendBoxSize = 5;
      console.log(channels);
      channels.forEach((ch, index) => {
        const colorIndex = channelsRef.current!.findIndex((c) => c.name === ch);
        if (colorIndex >= 0) {
          pdf.setFillColor(channelsRef.current![colorIndex].color);
        }
        pdf.rect(legendX, legendY - 4, legendBoxSize, legendBoxSize, "F");

        pdf.setTextColor(0, 0, 0);
        pdf.text(`${displayName(ch)}`, legendX + legendBoxSize + 3, legendY);

        legendY += 7;
      });

      // Add the plot image
      const imgWidth = 250;
      const imgHeight =
        (plotElement.clientHeight / plotElement.clientWidth) * imgWidth;

      pdf.addImage(imageDataUrl, "PNG", 20, legendY + 5, imgWidth, imgHeight);

      // Save the PDF
      pdf.save(`serial-plot-${new Date().toISOString().slice(0, 10)}.pdf`);

      toast.success("PDF exported successfully", { duration: 2000 });
    } catch (error) {
      console.error("PDF export failed:", error);
      toast.error("Failed to export PDF", { duration: 2000 });
    }
  };

  const handleReset = () => {
    clearData();
    setLogMessages([]);
    lastProcessedCounter.current = null;
  };

  const handleSend = () => {
    if (sendText.trim()) {
      write(sendText.trim());
      setSendText("");
    }
  };

  // Add handler for Enter key
  const handleKeyPress = (e: React.KeyboardEvent) => {
    if (e.key === "Enter") {
      handleSend();
    }
  };

  if (!isConnected) {
    return (
      <div className="min-h-screen bg-background flex flex-col">
        {/* Header */}
        <div className="p-4 border-b sticky top-0 bg-background/95 backdrop-blur z-50">
          <div className="container mx-auto px-4 flex items-center justify-between">
            <div className="flex items-center gap-2">
              <img src={logo} alt="logo" width={60} />
              <h1 className="text-2xl font-bold">Serial Plotter</h1>
            </div>

            <div className="flex items-center gap-4">
              <a
                href="https://thedevhouse.web.app/"
                target="_blank"
                rel="noopener noreferrer"
                className="hidden sm:block text-sm text-muted-foreground hover:text-foreground transition-colors"
              >
                Powered By The DevHouse
              </a>
              <div className="flex items-end gap-2">
                <Button
                  onClick={() => setIsFeedbackOpen(true)}
                  variant="outline"
                  className="flex items-center gap-2"
                >
                  <Star className="h-4 w-4" />
                  <span className="hidden sm:inline">Give Feedback</span>
                </Button>
                <ModeToggle />
                <FeedbackDialog
                  open={isFeedbackOpen}
                  onOpenChange={setIsFeedbackOpen}
                />
              </div>
            </div>
          </div>
        </div>

        {/* Content */}
        <main className="flex-1 flex flex-col">
          {/* Top Section - Full Height */}
          <div className="min-h-[calc(100vh-4rem)] flex items-center py-8">
            <div className="container mx-auto px-4 w-full">
              {/* Connection & Features Grid */}
              <div className="grid gap-8 lg:grid-cols-2 lg:gap-12">
                <div className="space-y-8">
                  {/* Get Started */}
                  <Card>
                    <CardHeader>
                      <CardTitle className="flex items-center gap-3 text-2xl">
                        <Plug className="h-6 w-6 text-primary" />
                        Get Started
                      </CardTitle>
                      <CardDescription className="text-lg">
                        Configure your connection settings and start plotting
                        data
                      </CardDescription>
                    </CardHeader>
                    <CardContent className="space-y-8">
                      <div className="space-y-6">
                        <div className="flex flex-col gap-6">
                          <div className="grid gap-4 py-4">
                            <div className="grid grid-cols-4 items-center gap-4">
                              <Label htmlFor="baud-rate" className="text-right">
                                Baud Rate
                              </Label>
                              <Select
                                value={localBaudRate}
                                onValueChange={handleBaudRateChange}
                                disabled={isConnected}
                              >
                                <SelectTrigger
                                  id="baud-rate"
                                  className="col-span-3"
                                >
                                  <SelectValue placeholder="Select baud rate" />
                                </SelectTrigger>
                                <SelectContent>
                                  {baudRates.map((rate) => (
                                    <SelectItem key={rate} value={rate}>
                                      {rate} bps
                                    </SelectItem>
                                  ))}
                                </SelectContent>
                              </Select>
                            </div>
                            <div className="flex items-center space-x-2">
                              <Checkbox
                                id="advanced"
                                checked={showAdvanced}
                                onCheckedChange={(checked) =>
                                  setShowAdvanced(checked as boolean)
                                }
                                disabled={isConnected}
                              />
                              <Label htmlFor="advanced">
                                Show advanced baud rate options
                              </Label>
                            </div>

                            {showAdvanced && (
                              <div className="grid grid-cols-4 items-center gap-4">
                                <Label
                                  htmlFor="advanced-baud"
                                  className="text-right"
                                >
                                  Advanced Rate
                                </Label>
                                <div className="col-span-3 flex gap-2">
                                  <Input
                                    id="advanced-baud"
                                    type="number"
                                    value={customBaudRate}
                                    onChange={(e) =>
                                      setCustomBaudRate(e.target.value)
                                    }
                                    disabled={isConnected}
                                    className="flex-1"
                                    placeholder="Enter custom baud rate"
                                  />
                                  <Button
                                    onClick={handleCustomBaudRateSave}
                                    disabled={isConnected}
                                    variant="secondary"
                                  >
                                    Save
                                  </Button>
                                </div>
                              </div>
                            )}
                          </div>
                          <Button
                            onClick={handleConnectClick}
                            className="w-full justify-start text-lg py-6"
                          >
                            <Plug className="h-5 w-5 mr-3" />
                            Connect Device
                          </Button>
                        </div>
                      </div>
                    </CardContent>
                  </Card>

                  {/* Introduction Card */}
                  <IntroductionCard />
                </div>

                <Card className="h-full flex flex-col">
                  <CardHeader>
                    <CardTitle className="flex items-center gap-3 text-2xl">
                      <FileText className="h-6 w-6 text-primary" />
                      Supported Data Formats
                    </CardTitle>
                  </CardHeader>
                  <CardContent className="flex-1 flex flex-col justify-around gap-6 space-y-4">
                    <div>
                      <h4 className="text-lg font-semibold mb-3">
                        Named Channels
                      </h4>
                      <div className="flex flex-col gap-4">
                        <code className="block bg-muted p-4 rounded-lg text-base font-mono">
                          channel1: 100, channel2: 200, channel3: 300
                        </code>
                        <div>
                          <code className="block bg-muted p-4 rounded-lg text-base font-mono">
                            Serial.print("channel1: ");
                            Serial.print(channel1_variable); Serial.print(",
                            channel2: "); Serial.print(channel2_variable);
                            Serial.print(", channel3: ");
                            Serial.println(channel3_variable);
                          </code>
                        </div>
                      </div>
                    </div>
                    <div>
                      <h4 className="text-lg font-semibold mb-3">
                        Values Only
                      </h4>
                      <div className="flex flex-col gap-4">
                        <code className="block bg-muted p-4 rounded-lg text-base font-mono">
                          100, 200, 300
                        </code>
                        <div>
                          <code className="block bg-muted p-4 rounded-lg text-base font-mono">
                            Serial.print(channel1_variable); Serial.print(", ");
                            Serial.print(channel2_variable); Serial.print(", ");
                            Serial.println(channel3_variable);
                          </code>
                        </div>
                      </div>
                    </div>
                  </CardContent>
                </Card>
              </div>
            </div>
          </div>

          {/* SEO Info Section */}
          <div className="container mx-auto px-4 pb-12">
            <InfoSection />
          </div>
        </main>
      </div>
    );
  }

  return (
    <div className="flex min-h-screen flex-col bg-background text-foreground">
      <header className="sticky top-0 z-10 border-b bg-background/95 backdrop-blur supports-[backdrop-filter]:bg-background/60 shadow-sm">
        <div className="flex h-16 items-center justify-between px-12">
          <div className="flex items-center gap-4">
            <img src={logo} alt="logo" width={60} />
            <h1 className="text-2xl font-bold">Serial Plotter</h1>
            <div className="flex items-center gap-2">
              <div className="flex items-center gap-2 border-r pr-4 mr-4 divide-x divide-border">
                <div className="pr-3">
                  <Button
                    onClick={handleConnectClick}
                    variant={isConnected ? "destructive" : "default"}
                    size="sm"
                    className="min-w-[120px] justify-center"
                  >
                    {isConnected ? (
                      <>
                        <PlugOff className="mr-2 h-4 w-4" /> Disconnect
                      </>
                    ) : (
                      <>
                        <Plug className="mr-2 h-4 w-4" /> Connect
                      </>
                    )}
                  </Button>
                </div>

                <div className="pl-4 flex items-center gap-2">
                  {isConnected && (
                    <Button
                      onClick={handlePauseClick}
                      variant="outline"
                      size="sm"
                    >
                      {isPaused ? (
                        <>
                          <Play className="m-2 h-4 w-4" />
                        </>
                      ) : (
                        <>
                          <Pause className="m-2 h-4 w-4" />
                        </>
                      )}
                    </Button>
                  )}

                  {isConnected && (
                    <Badge
                      variant={isPaused ? "outline" : "default"}
                      className={`
    min-w-[80px] h-8 flex items-center justify-center 
    text-sm font-medium transition-all duration-300
    ${
      !isPaused
        ? "bg-emerald-700 hover:bg-emerald-800 shadow-[0_0_8px_rgba(74,222,128,0.4)]"
        : "border-amber-300 text-amber-300 hover:bg-amber-500/10 shadow-[0_0_8px_rgba(234,179,8,0.2)]"
    }
  `}
                    >
                      {isPaused ? (
                        <span className="flex items-center gap-1.5">
                          <Pause className="h-3.5 w-3.5 animate-[pulse_1.5s_ease-in-out_infinite]" />
                          <span>Paused</span>
                        </span>
                      ) : (
                        <span className="flex items-center gap-1.5">
                          <div className="relative flex h-3 w-3">
                            <span className="absolute inline-flex h-full w-full animate-ping rounded-full bg-emerald-400 opacity-75 duration-1000" />
                            <span className="relative inline-flex rounded-full h-3 w-3 bg-emerald-200" />
                          </div>
                          <span className="animate-[fadeIn_0.5s_ease-out]">
                            Live
                          </span>
                        </span>
                      )}
                    </Badge>
                  )}
                </div>
              </div>
            </div>
          </div>
          <div className="flex items-center gap-2">
            <a
              href="https://thedevhouse.web.app/"
              target="_blank"
              rel="noopener noreferrer"
              className="text-sm text-muted-foreground hover:text-foreground transition-colors mr-4"
            >
              Powered By The DevHouse
            </a>

            <DropdownMenu>
              <Button
                onClick={() => setIsFeedbackOpen(true)}
                variant="outline"
                className="flex items-center gap-2"
              >
                <Star className="h-4 w-4" />
                Give Feedback
              </Button>
              <DropdownMenuTrigger asChild>
                <Button
                  variant="outline"
                  size="sm"
                  disabled={!data.length}
                  className="flex items-center gap-2 min-w-[100px] justify-center bg-transparent"
                >
                  <Download className="h-4 w-4" />
                  Export
                </Button>
              </DropdownMenuTrigger>
              <DropdownMenuContent>
                <DropdownMenuItem onClick={exportToCSV}>
                  <FileText className="h-4 w-4 mr-2" />
                  Export as CSV
                </DropdownMenuItem>
                <DropdownMenuItem onClick={exportToXLSX}>
                  <Table className="h-4 w-4 mr-2" />
                  Export as XLSX
                </DropdownMenuItem>
                <DropdownMenuItem onClick={handleImageExport}>
                  <ImageIcon className="h-4 w-4 mr-2" />
                  Export as Image
                </DropdownMenuItem>
                <DropdownMenuItem onClick={handlePdfExport}>
                  <FileImage className="h-4 w-4 mr-2" />
                  Export as PDF
                </DropdownMenuItem>
              </DropdownMenuContent>
            </DropdownMenu>
            <ModeToggle />
          </div>
          <FeedbackDialog
            open={isFeedbackOpen}
            onOpenChange={setIsFeedbackOpen}
          />
        </div>
      </header>

      <main className="flex-1 px-12 py-4 flex flex-col gap-4">
        {/* Main Plot */}
        <div
          ref={serialPlotRef}
          data-testid="serial-plot"
          className="serial-plot-container"
        >
          <SerialPlot channelRef={channelsRef} />
        </div>
        {/* Console Log */}
        <Card className="bg-card/50 backdrop-blur">
          <CardHeader className="flex flex-row items-center justify-between py-2">
            <CardTitle className="text-sm flex items-center gap-2">
              <Terminal className="h-4 w-4" />
              Console Log
            </CardTitle>
            <Button
              variant="ghost"
              size="sm"
              className="h-8 w-8 p-0"
              onClick={handleReset}
            >
              <Trash2 className="h-4 w-4" />
              <span className="sr-only">Clear logs</span>
            </Button>
          </CardHeader>
          <CardContent className="p-0">
            <ScrollArea className="h-[405px] w-full rounded-md border p-4 overflow-hidden">
              {logLines.length === 0 ? (
                <div className="flex items-center justify-center h-full text-muted-foreground">
                  <Terminal className="h-6 w-6 mr-2 opacity-50" />
                  No data received yet...
                </div>
              ) : (
                <div
                  ref={scrollRef}
                  onScroll={handleScroll}
                  className="space-y-1.5 overflow-y-auto max-h-[370px] overflow-hidden"
                  id="log-messages"
                >
                  {logLines.map((line, index) => (
                    <div
                      key={index}
                      className="log-message break-all rounded-sm px-3 py-1.5 hover:bg-accent/50 transition-colors bg-card/50 flex items-center gap-4 group"
                    >
                      <span className="flex-1 font-medium">{line}</span>
                    </div>
                  ))}
                </div>
              )}
            </ScrollArea>
          </CardContent>
          <CardContent className="p-4 border-t">
            <div className="flex items-center gap-2">
              <Input
                type="text"
                value={sendText}
                onChange={(e) => setSendText(e.target.value)}
                onKeyPress={handleKeyPress}
                placeholder="Send data..."
                className="w-[200px] h-9"
              />
              <Button
                variant="outline"
                size="sm"
                onClick={handleSend}
                className="flex items-center gap-1 bg-transparent"
              >
                <Send className="h-4 w-4" /> Send
              </Button>
            </div>
          </CardContent>
        </Card>
      </main>
    </div>
  );
}
