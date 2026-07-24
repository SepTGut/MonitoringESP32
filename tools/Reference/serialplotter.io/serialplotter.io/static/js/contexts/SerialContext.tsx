"use client";

import React, {
  createContext,
  useContext,
  useState,
  useCallback,
  useMemo,
  useEffect,
  useRef,
} from "react";
import { useSerial } from "../hooks/useSerial";

export interface SerialDataPoint {
  counter: number;
  timestamp: number;
  value: number;
  channel: string;
}

interface SerialContextType {
  baudRate: number;
  setBaudRate: (rate: number) => void;
  isConnected: boolean;
  isPaused: boolean;
  setIsPaused: (paused: boolean) => void;
  data: SerialDataPoint[];
  addDataPoint: (point: SerialDataPoint) => void;
  clearData: () => void;
  connect: () => Promise<void>;
  disconnect: () => Promise<void>;
  channelAliases: { [key: string]: string };
  setChannelAliases: (aliases: { [key: string]: string }) => void;
  displayName: (channel: string) => string;
  write: (data: string) => Promise<void>;
  logLines: string[];
  samplingEnabled: boolean;
  setSamplingEnabled: (enabled: boolean) => void;
  samplingInterval: number;
  setSamplingInterval: (ms: number) => void;
  phaseTimeLeft: number;
}

class CircularBuffer<T> {
  private buffer: T[];
  private head: number = 0;
  private tail: number = 0;
  private size: number = 0;

  constructor(private capacity: number) {
    this.buffer = new Array(capacity);
  }

  push(item: T): void {
    this.buffer[this.tail] = item;
    this.tail = (this.tail + 1) % this.capacity;
    if (this.size < this.capacity) {
      this.size++;
    } else {
      this.head = (this.head + 1) % this.capacity;
    }
  }

  toArray(): T[] {
    const result = new Array(this.size);
    let count = 0;
    let current = this.head;
    while (count < this.size) {
      result[count++] = this.buffer[current];
      current = (current + 1) % this.capacity;
    }
    return result;
  }

  clear(): void {
    this.head = 0;
    this.tail = 0;
    this.size = 0;
  }
}

const SerialContext = createContext<SerialContextType | undefined>(undefined);

export const SerialProvider: React.FC<{ children: React.ReactNode }> = ({
  children,
}) => {
  const [baudRate, setBaudRate] = useState(9600);
  const [isPaused, setIsPaused] = useState<boolean>(false);
  const [data, setData] = useState<SerialDataPoint[]>([]);
  const [dataBuffer] = useState(
    () => new CircularBuffer<SerialDataPoint>(100000),
  );
  const [channelAliases, setChannelAliases] = useState<{
    [key: string]: string;
  }>({});
  const [logLines, setLogLines] = useState<string[]>([]);

  const [samplingEnabled, setSamplingEnabled] = useState(false);
  const [samplingInterval, setSamplingInterval] = useState(10000);
  const [phaseTimeLeft, setPhaseTimeLeft] = useState(0);

  const lastSampleTimeRef = useRef<Record<string, number>>({});
  const updateTimeoutRef = useRef<NodeJS.Timeout | undefined>(undefined);
  const samplingEnabledRef = useRef(false);
  const samplingIntervalRef = useRef(10000);

  useEffect(() => {
    samplingEnabledRef.current = samplingEnabled;
  }, [samplingEnabled]);

  useEffect(() => {
    samplingIntervalRef.current = samplingInterval;
  }, [samplingInterval]);

  useEffect(() => {
    if (!samplingEnabled) {
      setPhaseTimeLeft(0);
      return;
    }

    const timer = setInterval(() => {
      const now = Date.now();
      const times = Object.values(lastSampleTimeRef.current);
      const lastTime = times.length > 0 ? Math.max(...times) : now;
      const elapsed = now - lastTime;
      const left = Math.max(
        0,
        samplingIntervalRef.current - (elapsed % samplingIntervalRef.current),
      );
      setPhaseTimeLeft(left);
    }, 100);

    return () => clearInterval(timer);
  }, [samplingEnabled, samplingInterval]);

  const addLogLine = useCallback((line: string) => {
    setLogLines((prev) => [...prev.slice(-999), line]);
  }, []);

  const {
    isConnected,
    connectSerial,
    disconnectSerial,
    startReading,
    resetData,
    write,
  } = useSerial(baudRate, isPaused, setIsPaused, addLogLine);

  const displayName = useCallback(
    (channel: string) => channelAliases[channel] || channel,
    [channelAliases],
  );

  const addDataPoint = useCallback(
    (point: SerialDataPoint) => {
      const now = Date.now();

      if (samplingEnabledRef.current) {
        const lastTime = lastSampleTimeRef.current[point.channel] || 0;
        if (now - lastTime < samplingIntervalRef.current) {
          return;
        }
        lastSampleTimeRef.current[point.channel] = now;
      }

      dataBuffer.push(point);

      if (updateTimeoutRef.current) clearTimeout(updateTimeoutRef.current);
      updateTimeoutRef.current = setTimeout(() => {
        setData(dataBuffer.toArray());
      }, 16);
    },
    [dataBuffer],
  );

  const clearData = useCallback(async () => {
    await resetData();
    dataBuffer.clear();
    setData([]);
    setLogLines([]);
  }, [resetData]);

  const value = useMemo(
    () => ({
      baudRate,
      setBaudRate,
      isConnected,
      isPaused,
      setIsPaused,
      data,
      addDataPoint,
      clearData,
      connect: connectSerial,
      disconnect: disconnectSerial,
      channelAliases,
      setChannelAliases,
      displayName,
      write,
      logLines,
      samplingEnabled,
      setSamplingEnabled,
      samplingInterval,
      setSamplingInterval,
      phaseTimeLeft,
    }),
    [
      baudRate,
      isConnected,
      data,
      isPaused,
      addDataPoint,
      clearData,
      connectSerial,
      disconnectSerial,
      channelAliases,
      displayName,
      write,
      logLines,
      samplingEnabled,
      samplingInterval,
      phaseTimeLeft,
    ],
  );

  useEffect(() => {
    let unsubscriber: () => void | undefined;
    if (isConnected) {
      unsubscriber = startReading((dp) => addDataPoint(dp));
    }

    return () => {
      unsubscriber?.();
    };
  }, [isConnected, startReading, addDataPoint]);

  return (
    <SerialContext.Provider value={value}>{children}</SerialContext.Provider>
  );
};

export const useSerialContext = (): SerialContextType => {
  const context = useContext(SerialContext);
  if (!context) {
    throw new Error("useSerialContext must be used within a SerialProvider");
  }
  return context;
};
