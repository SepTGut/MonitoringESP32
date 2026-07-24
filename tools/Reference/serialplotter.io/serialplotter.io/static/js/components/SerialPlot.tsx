import type React from "react";

import { useState, useMemo, useRef, useEffect } from "react";
import { useSerialContext } from "../contexts/SerialContext";
import { useTheme } from "next-themes";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
} from "@radix-ui/react-select";
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
  type TooltipProps,
  Brush, // Add this import
  ReferenceLine,
} from "recharts";
import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
} from "../components/ui/card";
import { Button } from "../components/ui/button";
import { Slider } from "../components/ui/slider";
import { Switch } from "../components/ui/switch";
import { Label } from "../components/ui/label";
import { Checkbox } from "../components/ui/checkbox";
import { ScrollArea } from "../components/ui/scroll-area";
import { RefreshCw, Maximize2, Minimize2, Pencil, Send } from "lucide-react";
import { SingleChannelDialog } from "./SingleChannelDialog";
import { Input } from "./ui/input";

const predefinedColors = [
  "#3b82f6", // blue
  "#ef4444", // red
  "#10b981", // green
  "#f59e0b", // amber
  "#8b5cf6", // violet
  "#ec4899", // pink
  "#14b8a6", // teal
  "#f97316", // orange
  "#6366f1", // indigo
  "#84cc16", // lime
  "#9333ea", // purple
  "#06b6d4", // cyan
  "#eab308", // yellow
  "#64748b", // slate
  "#d946ef", // fuchsia
];
const samplingIntervalOptions = [
  { label: "5 seconds", value: 5000 },
  { label: "10 seconds", value: 10000 },
  { label: "30 seconds", value: 30000 },
  { label: "1 minute", value: 60000 },
  { label: "5 minutes", value: 300000 },
  { label: "10 minutes", value: 600000 },
  { label: "30 minutes", value: 1800000 },
  { label: "1 hour", value: 3600000 },
];

const GetLineColor = (
  channels: string[],
  channelColors: Record<string, string>,
  channel: string,
) => {
  if (channelColors[channel]) {
    return channelColors[channel];
  }
  const index = channels.indexOf(channel) % predefinedColors.length;
  return predefinedColors[index];
};

export function SerialPlot({
  channelRef,
}: {
  channelRef: React.RefObject<
    {
      name: string;
      color: string;
    }[]
  >;
}) {
  const [channelColors, setChannelColors] = useState<Record<string, string>>(
    {},
  );
  const {
    data,
    isPaused,
    clearData,
    channelAliases,
    setChannelAliases,
    displayName,
    samplingEnabled,
    setSamplingEnabled,
    samplingInterval,
    setSamplingInterval,
    phaseTimeLeft,
  } = useSerialContext();
  const { theme } = useTheme();
  const chartRef = useRef<any>(null);
  const [isChannelDialogOpen, setIsChannelDialogOpen] = useState(false);
  const [channelToRename, setChannelToRename] = useState<string | null>(null);
  const handleSingleChannelRename = (oldName: string, newName: string) => {
    setChannelAliases({
      ...channelAliases,
      [oldName]: newName,
    });
  };
  const handleChannelRename = (oldName: string, newName: string) => {
    setChannelAliases({
      ...channelAliases,
      [oldName]: newName,
    });
  };

  const handleIntervalChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const seconds = parseFloat(e.target.value);
    if (!isNaN(seconds) && seconds >= 0.1) {
      setSamplingInterval(seconds * 1000);
    } else if (e.target.value === "") {
      setSamplingInterval(0);
    }
  };
  const [sendText, setSendText] = useState("");
  const { write } = useSerialContext();

  // const workerRef = useRef<Worker | null>(null);
  // useEffect(() => {
  //   workerRef.current = new Worker(new URL('../workers/reshapeWorker.ts', import.meta.url));
  //   return () => {
  //     workerRef.current?.terminate();
  //     workerRef.current = null;
  //   };
  // }, []);

  // State for chart controls
  const [xAxisScale, setXAxisScale] = useState<"counter" | "time">("counter");
  const [showGrid, setShowGrid] = useState(true);
  const [smoothing, setSmoothing] = useState(50);
  const [lineThickness, setLineThickness] = useState(2);

  // Add these new state variables
  const [showDataPoints, setShowDataPoints] = useState(true);
  const [yAxisRange, setYAxisRange] = useState<[number, number]>([0, 5000]);
  const [yAxisType, setYAxisType] = useState<"auto" | "fixed">("auto");
  const [gridOpacity, setGridOpacity] = useState(0.3);

  const [isFullscreen, setIsFullscreen] = useState(false);
  const [playWindowSize, setPlayWindowSize] = useState<number>(10);
  const [pauseWindowSize, setPauseWindowSize] = useState<number>(100);
  const [statsStartTime, setStatsStartTime] = useState<number>(Date.now());
  const effectiveWindowSize = isPaused
    ? Math.min(Math.max(5, pauseWindowSize), 2000)
    : Math.min(Math.max(5, playWindowSize), 100);

  const toggleFullscreen = () => {
    if (!document.fullscreenElement) {
      document.documentElement.requestFullscreen();
      setIsFullscreen(true);
    } else {
      document.exitFullscreen();
      setIsFullscreen(false);
    }
  };

  // Get all unique channels
  // const channels = useMemo(() => {
  // [  const set = new Set<string>()
  //   data.forEach((d) => {
  //     if (d.channel && typeof d.channel === "string" && !d.channel.includes("�")) {
  //       // Trim and normalize the channel name
  //       set.add(d.channel.trim())
  //     }
  //   })
  //   // Sort for stability
  //   return Array.from(set).sort()]
  // }, [data])

  const [channels, setChannels] = useState<string[]>([]);

  useEffect(() => {
    const set = new Set<string>();
    data.forEach((d) => {
      if (
        d.channel &&
        typeof d.channel === "string" &&
        !d.channel.includes("�")
      ) {
        // Trim and normalize the channel name
        set.add(d.channel.trim());
      }
    });

    const finalChannels = Array.from(set).sort();
    // Sort for stability
    if (!channels.length || channels.some((ch, i) => finalChannels[i] !== ch)) {
      // console.log('Setting channels:', finalChannels)
      setChannels(finalChannels);
      debugger;
      channelRef.current = finalChannels.map((c) => ({
        color: GetLineColor(finalChannels, channelColors, c),
        name: c,
      }));
    }
  }, [data]);

  // State for which channels are visible
  const [selectedChannels, setSelectedChannels] = useState<Set<string>>(
    new Set(),
  );

  useEffect(() => {
    const validChannels = channels.filter(
      (ch) => typeof ch === "string" && !ch.includes("�"),
    );
    setSelectedChannels(new Set(validChannels));
  }, [channels]);

  const reshapedData = useMemo(() => {
    // const dataToProcess = data.slice(-Math.max(effectiveWindowSize, 2000));
    const grouped: Record<number, Record<string, any>> = {};
    // data.forEach((d) => {
    //   if (!d.channel || !selectedChannels.has(d.channel.trim())) return
    //     const ts = d.timestamp
    //     const ch = d.channel
    //     if (!grouped[ts]) grouped[ts] = { timestamp: ts }
    //     grouped[ts][ch] = d.value
    // })
    data.forEach((d) => {
      if (!d.channel || !selectedChannels.has(d.channel.trim())) return;
      const counter = d.counter; // Use counter instead of timestamp
      const ch = d.channel.trim();
      if (!grouped[counter]) {
        grouped[counter] = {
          counter: counter,
          timestamp: d.timestamp,
        };
      }
      grouped[counter][ch] = d.value;
    });

    //   const result = Object.values(grouped)
    // //console.log('Reshaped data points:', result.length)
    // return result
    return Object.values(grouped).sort((a, b) => a.counter - b.counter);
    // return Object.values(grouped).sort((a, b) => a.timestamp - b.timestamp)
  }, [data, selectedChannels]);

  // const [reshapedData, setReshapedData] = useState<Array<Record<string, any>>>([]);
  // useEffect(() => {
  //   if (!workerRef.current || !data.length || !selectedChannels.size) return;

  //   const currentWindowSize = isPaused
  //     ? Math.min(Math.max(25, pauseWindowSize), 2000)
  //     : Math.min(Math.max(25, playWindowSize), 100);

  //   const dataToProcess = data.slice(-currentWindowSize);

  //   if (dataToProcess.length === 0) return;

  //   workerRef.current.postMessage({
  //     data: dataToProcess,
  //     selectedChannels: Array.from(selectedChannels),
  //     effectiveWindowSize: currentWindowSize
  //   });

  //   const handleReshapeMessage = (e: MessageEvent) => {
  //     if (Array.isArray(e.data) && e.data.length > 0) {
  //       setReshapedData(e.data);
  //     }
  //   };

  //   workerRef.current.addEventListener('message', handleReshapeMessage);
  //   return () => {
  //     workerRef.current?.removeEventListener('message', handleReshapeMessage);
  //   };
  // }, [data, selectedChannels, isPaused, playWindowSize, pauseWindowSize]);

  const statistics = useMemo(() => {
    const stats: Record<string, any> = {};

    channels.forEach((channel) => {
      if (typeof channel !== "string") return;

      const values = reshapedData
        .filter((d) => d.timestamp >= statsStartTime) 
        .map((d) => d[channel])
        .filter((v) => typeof v === "number") as number[];

      if (values.length === 0) {
        stats[channel] = {
          mean: 0,
          min: 0,
          max: 0,
          variance: 0,
          stdDev: 0,
          median: 0,
          current: 0,
        };
        return;
      }

      const mean = values.reduce((a, b) => a + b, 0) / values.length;
      const min = Math.min(...values);
      const max = Math.max(...values);
      const variance =
        values.reduce((a, b) => a + Math.pow(b - mean, 2), 0) / values.length;
      const stdDev = Math.sqrt(variance);
      const sorted = [...values].sort((a, b) => a - b);
      const median =
        sorted.length % 2 === 0
          ? (sorted[sorted.length / 2 - 1] + sorted[sorted.length / 2]) / 2
          : sorted[Math.floor(sorted.length / 2)];
      const current = values[values.length - 1];

      stats[channel] = { mean, min, max, variance, stdDev, median, current };
    });

    return stats;
  }, [reshapedData, channels, statsStartTime]); 

  const resetStats = () => {
    setStatsStartTime(Date.now());
  };
  // Replace the color input with a color selector
  const toggleChannel = (channel: string) => {
    setSelectedChannels((prev) => {
      const newSet = new Set(prev);
      if (newSet.has(channel)) {
        newSet.delete(channel);
      } else {
        newSet.add(channel);
      }
      return newSet;
    });
  };

  const handleResetData = () => {
    clearData();
  };

  const getCurveType = () => {
    // Map smoothing (0-100) to curve types
    if (smoothing < 25) return "linear";
    if (smoothing < 50) return "monotone";
    if (smoothing < 75) return "monotoneX";
    return "natural";
  };

  // Get the latest readings
  const latestReadings = useMemo(() => {
    if (data.length === 0) return [];

    // Group by channel and get the latest for each
    const latestByChannel = new Map<
      string,
      { value: number; timestamp: number }
    >();

    for (const point of data) {
      if (!point.channel || !selectedChannels.has(point.channel)) continue;

      if (
        !latestByChannel.has(point.channel) ||
        point.timestamp > latestByChannel.get(point.channel)!.timestamp
      ) {
        latestByChannel.set(point.channel, {
          value: point.value,
          timestamp: point.timestamp,
        });
      }
    }

    return Array.from(latestByChannel.entries()).map(([channel, data]) => ({
      channel,
      value: data.value,
      timestamp: new Date(data.timestamp).toLocaleTimeString(),
    }));
  }, [data, selectedChannels]);
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

  //   const CustomTooltip = ({ active, payload, label }: TooltipProps<number, string>) => {
  //     if (active && payload && payload.length) {
  //       return (
  //         <div className="bg-background border rounded-md shadow-md p-3">
  // <p className="text-sm font-medium mb-1">
  //   {new Date(label).toLocaleTimeString('en-US', {
  //     hour: '2-digit',
  //     minute: '2-digit',
  //     second: '2-digit',
  //     fractionalSecondDigits: 3,
  //     hour12: false,
  //   })}
  // </p>          <div className="space-y-1">
  //             {payload.map((entry, index) => (
  //               <div key={index} className="flex items-center gap-2">
  //                 <div className="w-3 h-3 rounded-full" style={{ backgroundColor: entry.color }} />
  //                 <span className="text-sm font-medium">{entry.name}:</span>
  //                 <span className="text-sm font-mono">{entry.value}</span>
  //               </div>
  //             ))}
  //           </div>
  //         </div>
  //       )
  //     }
  //     return null
  //   }

  const CustomTooltip = ({
    active,
    payload,
    label,
  }: TooltipProps<any, any>) => {
    if (active && payload && payload.length) {
      return (
        <div className="bg-background border rounded-lg p-2 shadow-lg">
          <p className="font-medium">Sample #{label}</p>
          <p className="text-sm text-muted-foreground">
            {new Date(payload[0]?.payload?.timestamp).toLocaleString()}
          </p>
          {payload.map((entry: any) => (
            <p key={entry.name} style={{ color: entry.color }}>
              {entry.name}: {entry.value}
            </p>
          ))}
        </div>
      );
    }
    return null;
  };

  const getLineColor = GetLineColor.bind(null, channels, channelColors);

  return (
    <Card className="w-full bg-background text-foreground">
      <CardHeader className="pb-2 bg-background text-foreground">
        <div className="mb-2">
          <CardTitle>Real-time Data Plot</CardTitle>
        </div>

        <div className="flex w-full items-center justify-between gap-4 ">
          {/* Left Buttons */}
          <div className="flex gap-2">
            <Button
              variant="outline"
              size="sm"
              onClick={handleResetData}
              className="flex items-center gap-1 bg-transparent"
            >
              <RefreshCw className="h-4 w-4" /> Reset Data
            </Button>
            <Button
              variant="outline"
              size="sm"
              onClick={toggleFullscreen}
              className="flex items-center gap-1 bg-transparent"
            >
              {isFullscreen ? (
                <>
                  <Minimize2 className="h-4 w-4" /> Exit
                </>
              ) : (
                <>
                  <Maximize2 className="h-4 w-4" /> Full
                </>
              )}
            </Button>
          </div>

          {/* Right Send Input + Button */}
          <div className="flex gap-2">
            <Input
              type="text"
              value={sendText}
              onChange={(e) => setSendText(e.target.value)}
              onKeyDown={handleKeyPress}
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
        </div>
      </CardHeader>
      <CardContent className="space-y-4">
        {/* Sensor Controls and Latest Readings */}
        {/* Combined Sensor Controls and Latest Readings */}
        <div className="border rounded-lg bg-card/50 backdrop-blur p-2">
          <ScrollArea className="h-[min(300px,max(120px,2.5rem*${channels.length}))]">
            <div className="space-y-2">
              {channels.map((channel) => (
                <div
                  key={channel}
                  className="flex items-center justify-between rounded-md p-2 hover:bg-accent/50 transition-colors"
                >
                  <div className="flex items-center gap-3 flex-1">
                    <Checkbox
                      id={`channel-${channel}`}
                      checked={selectedChannels.has(channel)}
                      onCheckedChange={() => toggleChannel(channel)}
                      className="data-[state=checked]:bg-primary data-[state=checked]:border-primary"
                    />

                    <Select
                      value={channelColors[channel] || getLineColor(channel)}
                      onValueChange={(value) => {
                        setChannelColors((prev) => ({
                          ...prev,
                          [channel]: value,
                        }));

                        const index = channelRef.current?.findIndex(
                          (c) => c.name === channel,
                        );
                        if (index >= 0) {
                          channelRef.current[index].color = value;
                        }
                      }}
                    >
                      <SelectTrigger className="w-[100px] h-7 px-2">
                        <div className="flex items-center gap-2">
                          <div
                            className="w-4 h-4 rounded-full border border-border"
                            style={{
                              backgroundColor:
                                channelColors[channel] || getLineColor(channel),
                            }}
                          />
                          <span className="text-xs truncate">Color</span>
                        </div>
                      </SelectTrigger>
                      <SelectContent className="min-w-[160px] bg-popover border rounded-md shadow-md">
                        <div className="grid grid-cols-5 gap-1 p-2 bg-popover rounded-md">
                          {predefinedColors.map((color, index) => (
                            <SelectItem
                              key={color}
                              value={color}
                              className="p-1 m-0 hover:bg-accent rounded-md transition-colors data-[highlighted]:bg-accent"
                            >
                              <div
                                className="w-6 h-6 rounded-full border border-border hover:scale-110 transition-transform cursor-pointer"
                                style={{ backgroundColor: color }}
                                title={`Color ${index + 1}`}
                              />
                            </SelectItem>
                          ))}
                        </div>
                      </SelectContent>
                    </Select>
                    <Label
                      htmlFor={`channel-${channel}`}
                      className="font-medium text-sm cursor-pointer"
                    >
                      {displayName(channel)}
                    </Label>
                    <button
                      onClick={() => setChannelToRename(channel)}
                      className="p-1 hover:bg-muted rounded-sm"
                      aria-label={`Rename channel ${displayName(channel)}`}
                      title={`Rename channel ${displayName(channel)}`}
                    >
                      <Pencil className="h-3 w-3" aria-hidden="true" />
                    </button>
                    <SingleChannelDialog
                      open={channelToRename !== null}
                      onOpenChange={(open) => !open && setChannelToRename(null)}
                      channel={channelToRename || ""}
                      currentName={
                        channelToRename ? displayName(channelToRename) : ""
                      }
                      onChannelRename={handleSingleChannelRename}
                    />
                  </div>
                  {latestReadings.find((r) => r.channel === channel) && (
                    <div className="flex items-center gap-4 text-right">
                      <span className="font-mono text-sm font-semibold">
                        {latestReadings
                          .find((r) => r.channel === channel)
                          ?.value.toFixed(2)}
                      </span>
                      <span className="text-xs text-muted-foreground">
                        {
                          latestReadings.find((r) => r.channel === channel)
                            ?.timestamp
                        }
                      </span>
                    </div>
                  )}
                </div>
              ))}
            </div>
          </ScrollArea>
        </div>

        {/* Chart Controls */}
        <div className="flex flex-wrap gap-4 p-4 border rounded-lg bg-card">
          <div className="flex gap-6 flex-wrap w-full">
            {/* Line Style Controls */}
            <div className="space-y-4 border-r pr-6 flex-1">
              <div className="flex flex-col gap-2 min-w-[200px]">
                <div className="flex justify-between items-center">
                  <Label htmlFor="smoothing" className="text-sm font-medium">
                    Line Smoothing
                  </Label>
                  <span className="text-sm font-mono text-muted-foreground">
                    {smoothing}%
                  </span>
                </div>
                <Slider
                  id="smoothing"
                  min={0}
                  max={100}
                  step={1}
                  value={[smoothing]}
                  onValueChange={(value) => setSmoothing(value[0])}
                  className="w-full"
                />
              </div>

              <div className="flex flex-col gap-2 min-w-[200px]">
                <div className="flex justify-between items-center">
                  <Label htmlFor="thickness" className="text-sm font-medium">
                    Line Width
                  </Label>
                  <div className="flex items-center gap-2">
                    <span className="text-sm font-mono text-muted-foreground">
                      {lineThickness}px
                    </span>
                    <Switch
                      id="show-points"
                      checked={showDataPoints}
                      onCheckedChange={setShowDataPoints}
                    />
                  </div>
                </div>
                <Slider
                  id="thickness"
                  min={1}
                  max={5}
                  step={0.5}
                  value={[lineThickness]}
                  onValueChange={(value) => setLineThickness(value[0])}
                  className="w-full"
                />
              </div>
            </div>

            {/* Grid and Data Window Controls */}
            <div className="space-y-4 border-r pr-6 flex-1">
              <div className="flex flex-col gap-2 min-w-[200px]">
                <div className="flex justify-between items-center">
                  <div className="flex flex-col">
                    <Label className="text-sm font-medium">X-Axis Range</Label>
                    <span className="text-xs text-muted-foreground">
                      Data Window Limit: 5-{isPaused ? "2000" : "100"} points
                    </span>
                  </div>
                  <div className="flex items-center gap-2">
                    <div className="relative">
                      <input
                        type="number"
                        value={
                          (isPaused ? pauseWindowSize : playWindowSize) || ""
                        }
                        onChange={(e) => {
                          const inputValue = e.target.value;
                          if (inputValue === "") {
                            if (isPaused) {
                              setPauseWindowSize(0);
                            } else {
                              setPlayWindowSize(0);
                            }
                            return;
                          }
                          const value = Number(inputValue);
                          if (isPaused) {
                            setPauseWindowSize(value);
                          } else {
                            setPlayWindowSize(value);
                          }
                        }}
                        onKeyDown={(
                          e: React.KeyboardEvent<HTMLInputElement>,
                        ) => {
                          if (e.key === "Enter") {
                            const value = isPaused
                              ? pauseWindowSize
                              : playWindowSize;
                            if (isPaused) {
                              if (value < 5 || value > 2000) {
                                setPauseWindowSize(500);
                              }
                            } else {
                              if (value < 5 || value > 100) {
                                setPlayWindowSize(10);
                              }
                            }
                          }
                        }}
                        className="w-[120px] px-3 py-1.5 rounded-md border text-sm [appearance:textfield] [&::-webkit-outer-spin-button]:appearance-none [&::-webkit-inner-spin-button]:appearance-none"
                        aria-label="Data window size"
                        title={`Enter data window size (5-${
                          isPaused ? "2000" : "100"
                        } points)`}
                        placeholder="Window size"
                      />
                      <span className="absolute right-3 top-1/2 -translate-y-1/2 text-sm text-muted-foreground pointer-events-none">
                        pts
                      </span>
                    </div>
                  </div>
                </div>
                {!isPaused && playWindowSize > 25 && (
                  <div className="text-xs text-yellow-500 mt-1">
                    Warning: Values over 25 points may decrease performance
                    while running
                  </div>
                )}
              </div>
              <div className="flex flex-col gap-2 min-w-[200px]">
                <div className="flex justify-between items-center">
                  <Label className="text-sm font-medium">Grid Opacity</Label>
                  <div className="flex items-center gap-2">
                    <span className="text-sm font-mono text-muted-foreground">
                      {(gridOpacity * 100).toFixed(0)}%
                    </span>
                    <Switch
                      id="show-grid"
                      checked={showGrid}
                      onCheckedChange={setShowGrid}
                    />
                  </div>
                </div>
                <Slider
                  min={0}
                  max={100}
                  value={[gridOpacity * 100]}
                  onValueChange={(value) => setGridOpacity(value[0] / 100)}
                  className="w-full"
                  disabled={!showGrid}
                />
              </div>
            </div>

            {/* Y-Axis Controls */}
            <div className="space-y-4 flex-1">
              <div className="flex flex-col gap-2 min-w-[200px]">
                <div className="flex justify-between items-center">
                  <Label className="text-sm font-medium">Y-Axis Range</Label>
                  <div className="flex items-center gap-2">
                    <span className="text-sm text-muted-foreground">Auto</span>
                    <Switch
                      checked={yAxisType === "fixed"}
                      onCheckedChange={(checked) =>
                        setYAxisType(checked ? "fixed" : "auto")
                      }
                    />
                    <span className="text-sm text-muted-foreground">Fixed</span>
                  </div>
                </div>
                <div className="flex flex-col gap-2">
                  <div className="flex gap-2 items-center">
                    <Label className="text-sm min-w-[3rem]">Max:</Label>
                    <div className="relative flex-1">
                      <input
                        type="number"
                        value={yAxisRange[1]}
                        onChange={(e) => {
                          const value =
                            e.target.value === "" ? "" : Number(e.target.value);
                          const newMax =
                            value === ""
                              ? value
                              : Math.min(
                                  Math.max(yAxisRange[0] + 1, value),
                                  5000,
                                );
                          setYAxisRange([yAxisRange[0], newMax as number]);
                        }}
                        onKeyDown={(
                          e: React.KeyboardEvent<HTMLInputElement>,
                        ) => {
                          if (
                            e.key === "Enter" &&
                            e.currentTarget.value === ""
                          ) {
                            setYAxisRange([yAxisRange[0], 100]);
                          }
                        }}
                        className="w-[120px] px-3 py-1.5 rounded-md border text-sm"
                        disabled={yAxisType === "auto"}
                        aria-label="Y-axis maximum value"
                        title="Enter Y-axis maximum value"
                        placeholder="Max value"
                      />
                    </div>
                  </div>
                  <div className="flex gap-2 items-center">
                    <Label className="text-sm min-w-[3rem]">Min:</Label>
                    <div className="relative flex-1">
                      <input
                        type="number"
                        value={yAxisRange[0]}
                        onChange={(e) => {
                          const value =
                            e.target.value === "" ? "" : Number(e.target.value);
                          const newMin =
                            value === ""
                              ? value
                              : Math.max(
                                  Math.min(yAxisRange[1] - 1, value),
                                  -5000,
                                );
                          setYAxisRange([newMin as number, yAxisRange[1]]);
                        }}
                        onKeyDown={(
                          e: React.KeyboardEvent<HTMLInputElement>,
                        ) => {
                          if (
                            e.key === "Enter" &&
                            e.currentTarget.value === ""
                          ) {
                            setYAxisRange([-100, yAxisRange[1]]);
                          }
                        }}
                        className="w-[120px] px-3 py-1.5 rounded-md border text-sm"
                        min={-5000}
                        max={yAxisRange[1] - 1}
                        disabled={yAxisType === "auto"}
                        aria-label="Y-axis minimum value"
                        title="Enter Y-axis minimum value (down to -5000)"
                        placeholder="Min value"
                      />
                    </div>
                  </div>
                </div>
              </div>
            </div>
            <div className="flex flex-col gap-4 p-4 border rounded-lg bg-card shadow-sm">
              <div className="flex items-center justify-between border-b pb-2">
                <div className="flex flex-col">
                  <Label className="text-sm font-bold">Snapshot Sampling</Label>
                  <span
                    className={`text-[10px] font-mono ${samplingEnabled ? "text-orange-500 animate-pulse" : "text-muted-foreground"}`}
                  >
                    {samplingEnabled
                      ? `Next sample in: ${(phaseTimeLeft / 1000).toFixed(1)}s`
                      : "Real-time recording"}
                  </span>
                </div>
                <Switch
                  checked={samplingEnabled}
                  onCheckedChange={setSamplingEnabled}
                />
              </div>

              <div className="space-y-3">
                <div className="flex flex-col gap-2">
                  <Label className="text-[11px] uppercase tracking-wider text-muted-foreground">
                    Interval Duration
                  </Label>

                  <div className="flex items-center gap-2">
                    <div className="relative flex-1">
                      <Input
                        type="number"
                        step="0.1"
                        value={samplingInterval / 1000}
                        disabled={!samplingEnabled}
                        onChange={(e) => {
                          const val = parseFloat(e.target.value);
                          setSamplingInterval(val * 1000);
                        }}
                        className="pr-12 font-mono h-9"
                        placeholder="0.0"
                      />
                      <span className="absolute right-3 top-1/2 -translate-y-1/2 text-[10px] font-bold text-muted-foreground pointer-events-none">
                        SEC
                      </span>
                    </div>

                    <select
                      value={samplingInterval}
                      disabled={!samplingEnabled}
                      onChange={(e) =>
                        setSamplingInterval(Number(e.target.value))
                      }
                      className="w-[120px] h-9 px-2 rounded-md border bg-background text-xs focus:ring-1 focus:ring-primary outline-none disabled:opacity-50 transition-all"
                    >
                      <option value="" disabled>
                        Presets
                      </option>
                      {samplingIntervalOptions.map((opt) => (
                        <option key={opt.value} value={opt.value}>
                          {opt.label}
                        </option>
                      ))}
                    </select>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>

        {/* Main Chart */}
        <div className="space-y-2">
          <div className="h-[55vh] min-h-[400px] border rounded-lg p-4">
            <ResponsiveContainer
              width="100%"
              height="100%"
              className="chart-capture-target"
              data-testid="serial-plot-chart"
            >
              <LineChart
                ref={chartRef}
                data={reshapedData.slice(-effectiveWindowSize)}
                margin={{ top: 10, right: 20, left: 10, bottom: 0 }}
                onMouseMove={(e) => {
                  if (isPaused && e?.activeLabel) {
                    // Update tooltip or cursor position
                  }
                }}
              >
                {showGrid && (
                  <CartesianGrid strokeDasharray="3 3" opacity={gridOpacity} />
                )}
                <ReferenceLine
                  y={0}
                  stroke="currentColor"
                  strokeWidth={2}
                  strokeOpacity={0.8}
                />

                <XAxis
                  dataKey="counter"
                  type="number"
                  domain={["dataMin", "dataMax"]}
                  tickFormatter={(value) => `#${value}`}
                  label={{ value: "Sample Count", position: "bottom" }}
                  allowDataOverflow
                />

                <YAxis
                  domain={yAxisType === "fixed" ? yAxisRange : ["auto", "auto"]}
                  allowDataOverflow
                />

                <Tooltip
                  content={<CustomTooltip />}
                  isAnimationActive={false}
                  cursor={{
                    stroke: isPaused ? "#666" : "transparent",
                    strokeWidth: 1,
                    strokeDasharray: "3 3",
                  }}
                />

                <Brush
                  dataKey="counter"
                  height={30}
                  stroke={theme === "dark" ? "#94a3b8" : "#64748b"}
                  fill={theme === "dark" ? "#1e293b" : "#f1f5f9"}
                  tickFormatter={(value: number) => `#${value}`}
                  display={isPaused ? "block" : "none"}
                />

                <Legend
                  wrapperStyle={{ paddingTop: "10px" }}
                  formatter={(value) => (
                    <span className="text-sm">{value}</span>
                  )}
                />

                {channels
                  .filter(
                    (ch) =>
                      selectedChannels.has(ch) &&
                      typeof ch === "string" &&
                      !ch.includes("�"),
                  )
                  .map((channel) => (
                    <Line
                      key={channel}
                      type={getCurveType()}
                      dataKey={channel}
                      name={displayName(channel)}
                      stroke={getLineColor(channel)}
                      dot={showDataPoints ? { r: 2 } : false}
                      strokeWidth={lineThickness}
                      isAnimationActive={true}
                      animationDuration={800}
                      animationEasing="ease-in-out"
                      activeDot={{ r: 4 }}
                      connectNulls
                    />
                  ))}
              </LineChart>
            </ResponsiveContainer>
          </div>
          {/* <div className="h-[60px] border-b">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart 
                data={reshapedData} 
                margin={{ top: 5, right: 5, left: 5, bottom: 5 }}
              >
                {channels
                  .filter((ch) => selectedChannels.has(ch) && typeof ch === "string" && !ch.includes("�"))
                  .map((channel) => (
                    <Line
                      key={channel}
                      type="monotone"
                      dataKey={channel}
                      stroke={getLineColor(channel)}
                      dot={false}
                      strokeWidth={1}
                      isAnimationActive={false}
                    />
                  ))}
              </LineChart>
            </ResponsiveContainer>
          </div> */}
        </div>
      </CardContent>
      <Card>
        <CardContent>
          {/* Statistics Cards */}
          <div className="mb-3">
            <Button onClick={resetStats} variant="outline" size="sm">
              <RefreshCw className="h-4 w-4 mr-2" />
              Reset Statistics
            </Button>
          </div>
          <div className="grid gap-4 md:grid-cols-2 lg:grid-cols-3">
            {Object.entries(statistics).map(([channel, stat]) => (
              <Card key={channel} className="overflow-hidden">
                <CardHeader className="pb-2">
                  <div className="flex items-center gap-2">
                    <div
                      className="w-3 h-3 rounded-full"
                      style={{ backgroundColor: getLineColor(channel) }}
                    />
                    <CardTitle className="text-base">
                      {displayName(channel)}
                    </CardTitle>
                  </div>
                </CardHeader>
                <CardContent>
                  <div className="grid grid-cols-2 gap-2 text-sm">
                    <div className="bg-muted p-2 rounded flex flex-col">
                      <span className="text-muted-foreground">Current</span>
                      <span className="font-mono font-medium">
                        {stat.current.toFixed(2)}
                      </span>
                    </div>
                    <div className="bg-muted p-2 rounded flex flex-col">
                      <span className="text-muted-foreground">Mean</span>
                      <span className="font-mono font-medium">
                        {stat.mean.toFixed(2)}
                      </span>
                    </div>
                    <div className="bg-muted p-2 rounded flex flex-col">
                      <span className="text-muted-foreground">Min</span>
                      <span className="font-mono font-medium">
                        {stat.min.toFixed(2)}
                      </span>
                    </div>
                    <div className="bg-muted p-2 rounded flex flex-col">
                      <span className="text-muted-foreground">Max</span>
                      <span className="font-mono font-medium">
                        {stat.max.toFixed(2)}
                      </span>
                    </div>
                    <div className="bg-muted p-2 rounded flex flex-col">
                      <span className="text-muted-foreground">Std Dev</span>
                      <span className="font-mono font-medium">
                        {stat.stdDev.toFixed(2)}
                      </span>
                    </div>
                    <div className="bg-muted p-2 rounded flex flex-col">
                      <span className="text-muted-foreground">Median</span>
                      <span className="font-mono font-medium">
                        {stat.median.toFixed(2)}
                      </span>
                    </div>
                  </div>
                </CardContent>
              </Card>
            ))}
          </div>
        </CardContent>
      </Card>
    </Card>
  );
}
