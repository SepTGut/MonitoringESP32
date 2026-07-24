
import { SerialDataPoint } from "../contexts/SerialContext";

declare const self: Worker;

interface WorkerMessage {
  type: 'process' | 'clear' | 'pause' | 'resume';
  data?: string;
  timestamp?: number;
}

let dataBuffer: SerialDataPoint[] = [];
let linesToSkip = 4;
let dataCounter = 1;
let isPaused = false;
// Track channel occurrences with timestamps
interface ChannelTracker {
  lastSeen: number;
  occurrences: number;
}
let channelTracking: Record<string, ChannelTracker> = {};

// Cleanup threshold in milliseconds (time window to check for reoccurrence)
const CLEANUP_THRESHOLD = 2000;

const processSerialData = (data: string, timestamp: number) => {
  const newDataPoints: SerialDataPoint[] = [];
  const currentChannels = new Set<string>();
  
  // Process data points
  const lines = data.split('\n');
  for (const line of lines) {
    if (!line.trim() || linesToSkip>0) {
      linesToSkip--;
      dataCounter--;
      continue;
    }
    self.postMessage({ type: 'log', line: line.trim() });
  //   const pairs = line.trim().split(',');
  //   for (const pair of pairs) {
  //     const [channelRaw, valStrRaw] = pair.split(':');
  //     if (!channelRaw || !valStrRaw || channelRaw.includes('�')) continue;

  //     const channel = channelRaw.trim();
  //     const value = parseFloat(valStrRaw.trim());
  //     if (!isNaN(value)) {
  //       currentChannels.add(channel);
  //       newDataPoints.push({ counter:dataCounter,timestamp, channel, value });
  //     }
  //   }
  // }
  if (!line.includes(':')) {
    const values = line.trim().split(',').map(v => parseFloat(v.trim()));
    values.forEach((value, index) => {
      if (!isNaN(value)) {
        const channel = `Channel ${index + 1}`;
        currentChannels.add(channel);
        newDataPoints.push({ counter: dataCounter, timestamp, channel, value });
      }
    });
  } else {
    // Original parsing for key:value pairs
    const pairs = line.trim().split(',');
    for (const pair of pairs) {
      const [channelRaw, valStrRaw] = pair.split(':');
      if (!channelRaw || !valStrRaw || channelRaw.includes('�')) continue;

      const channel = channelRaw.trim();
      const value = parseFloat(valStrRaw.trim());
      if (!isNaN(value)) {
        currentChannels.add(channel);
        newDataPoints.push({ counter: dataCounter, timestamp, channel, value });
      }
    }
  }
}
dataCounter++;
  // Update channel tracking - convert Set to Array for iteration
  Array.from(currentChannels).forEach(channel => {
    if (channelTracking[channel]) {
      channelTracking[channel].lastSeen = dataCounter;
      channelTracking[channel].occurrences++;
    } else {
      channelTracking[channel] = { lastSeen: dataCounter, occurrences: 1 };
    }
  });

  // Clean up channels that haven't reappeared
  const currentCount = dataCounter;
  Object.entries(channelTracking).forEach(([channel, tracking]) => {
    if (tracking.occurrences <= 1 && currentCount - tracking.lastSeen > 10) {
      delete channelTracking[channel];
      // Filter out data points from this channel
      dataBuffer = dataBuffer.filter(dp => dp.channel !== channel);
    }
  });
  

  // Add new data points and send update
  if (newDataPoints.length > 0) {
    // Only add data points from tracked channels
    const validDataPoints = newDataPoints.filter(dp => 
      channelTracking[dp.channel] && 
      (channelTracking[dp.channel].occurrences > 1 || 
        currentCount - channelTracking[dp.channel].lastSeen <= 100)
    );
    // console.log(validDataPoints);
    if (validDataPoints.length > 0) {
      dataBuffer.push(...validDataPoints);
      self.postMessage({ type: 'data', buffer: validDataPoints });
    }
  }
};

self.onmessage = (e: MessageEvent<WorkerMessage>) => {
  switch (e.data.type) {
    case 'process':
      if (!isPaused && e.data.data && e.data.timestamp) {
        processSerialData(e.data.data,e.data.timestamp);
      }
      break;
    case 'clear':
      linesToSkip = 4;
      dataBuffer = [];
      channelTracking = {};
      dataCounter = 1;
      self.postMessage({ type: 'clear_done' });
      self.postMessage({ type: 'data', buffer: dataBuffer });
      break;
      case 'pause':
      linesToSkip = 4;
      dataBuffer=[];
      channelTracking = {};
      isPaused = true;
      break;
    case 'resume':
      linesToSkip = 4;
      isPaused = false;
      dataCounter++;
      break;
  }
};