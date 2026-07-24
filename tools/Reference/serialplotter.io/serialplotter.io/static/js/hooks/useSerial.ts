import { useCallback, useRef, useState,useEffect } from "react";
import { SerialDataPoint } from "../contexts/SerialContext";
import { toast } from "sonner";

export const useSerial = (defaultBaudRate: number = 9600,
  paused: boolean,
  setPaused: (paused: boolean) => void,
  addLogLine: (line: string) => void) => {
  const [port, setPort] = useState<SerialPort>();
  const [reader, setReader] = useState<Promise<void>>();
  const stopReading = useRef(true);
  const pausedRef = useRef(paused);
  const worker = useRef<Worker | null>(null);
  const [isPaused, setIsPaused] = useState(false);

  
  useEffect(() => {
    pausedRef.current = paused;
  }, [paused]);

  useEffect(() => {
    // Create worker instance
    worker.current = new Worker(new URL('../workers/serialWorker.ts', import.meta.url));
    // Cleanup worker on unmount
    return () => {
      worker.current?.terminate();
      worker.current = null;
    };
  }, []);  

  // Connect to serial port
  const connectSerial = useCallback(async () => {
    try {
      if (!("serial" in navigator)) {
        throw new Error("Web Serial API not supported");
      }

      const serial = navigator.serial;
      const newPort = await serial.requestPort({});

      try {
        // Make sure we're using the current baud rate
        console.log('Attempting to connect with baud rate:', defaultBaudRate);
        await newPort.open({ baudRate: defaultBaudRate });
        setPort(newPort);
        stopReading.current = false;
      } catch (error) {
        console.error("Failed to open port:", error);
        throw error;
      }

      toast.success(`Connected at ${defaultBaudRate} baud`,{duration: 2000});
    } catch (err) {
      console.error("Connection error:", err);
      throw err;
    }
  }, [defaultBaudRate]); // Make sure defaultBaudRate is in dependency array

  // Start reading from serial port
  const startReading = useCallback(
    (cb: (dataPoint: SerialDataPoint) => void) => {
      if (!port) throw new Error("No Port");
      if (!port.readable) throw new Error("Port not Readable");

      const textDecoder = new TextDecoderStream();
      const streamClosed = port.readable
        .pipeTo(textDecoder.writable)
        .catch((err) => {
          console.error("Stream error:", err);
        });

      const reader = textDecoder.readable.getReader();
      stopReading.current = false;
      let buffer = "";

      // Set up worker message handler
      worker.current?.addEventListener('message', (e) => {
        if (e.data.type === 'data') {
          e.data.buffer.forEach((dp: SerialDataPoint) => cb(dp));
        } else if (e.data.type === 'log') {
          addLogLine(e.data.line);
        }
      });

      const readLoop = async () => {
        try {
          buffer = "";          
          while (!stopReading.current) {
            
            const { value, done } = await reader.read();
            if (done) break;
            if (value) {
              const receiveTime = Date.now();
              buffer += value;

              if (buffer.includes('\n')) {
                const lines = buffer.split('\n');
                const validLines = lines.slice(0, -1); // All complete lines except the last partial one
                buffer = lines[lines.length - 1]; // Keep the last incomplete line
              
                // Process each line with its own timestamp
                validLines.forEach(line => {
                  if(!pausedRef.current){
                  if (line.trim()) {
                    worker.current?.postMessage({
                      type: 'process',
                      data: line,
                      timestamp: receiveTime,
                    });
                  }
                }
                });
              }
            }
          }
        } catch (err) {
          console.error("Error reading stream:", err);
          toast.error("Error reading data", { duration: 2000 });
        } finally {
          try {
            buffer = "";
            await reader
              ?.cancel()
              .catch((e) => console.warn("Reader cancel error:", e));

            await streamClosed;

            // Release the lock
            reader?.releaseLock();
          } catch (finalErr) {
            console.warn("Cleanup error:", finalErr);
          }
        }
      };

      setReader(readLoop());
      return () => {
        stopReading.current = true;
        worker.current?.postMessage({ type: 'clear' });
      };
    },
    [port,isPaused]
  );

  // Add pause/resume functions
  const pause = useCallback(() => {
    setPaused(true);
    worker.current?.postMessage({ type: 'pause' });
  }, [setPaused]);

  const resume = useCallback(() => {
    setPaused(false);
    worker.current?.postMessage({ type: 'resume' });
  }, [setPaused]);

  // Write to serial port
  const write = useCallback(async (data: string) => {
    if (!port || !port.writable) {
      toast.error("No port open for writing", {duration: 2000});
      return;
    }
    const writer = port.writable.getWriter();
    try {
      const encoder = new TextEncoder();
      const dataWithNewline = data + '\n'; // Add newline to the end of the data
      await writer.write(encoder.encode(dataWithNewline));
      toast.success("Data sent successfully", {duration: 2000});
    } catch (err) {
      console.error("Write error:", err);
      toast.error("Failed to send data", {duration: 2000});
    } finally {
      writer.releaseLock();
    }
  }, [port]);


  // Disconnect from serial port
  const disconnectSerial = useCallback(async () => {
    if (!port) {
      toast.info("No port to disconnect",{duration: 2000});

      return;
    }

    try {
      stopReading.current = true;
      worker.current?.postMessage({ type: 'clear' });

      await reader;

      await port.close();

      setPort(undefined);
      toast.success("Disconnected",{duration: 2000});
    } catch (err) {
      console.error("Disconnection error:", err);
      toast.error("Disconnection failed",{duration: 2000});
    }
  }, [port, reader]);

  const resetData = useCallback(() => {
    return new Promise<void>((resolve) => {
      const handleClearDone = (e: MessageEvent) => {
        if (e.data.type === 'clear_done') {
          worker.current?.removeEventListener('message', handleClearDone);
          resolve();
        }
      };
      worker.current?.addEventListener('message', handleClearDone);
      worker.current?.postMessage({ type: 'clear' });
    });
  }, []);

  return {
    connectSerial,
    disconnectSerial,
    startReading,
    isConnected: !!port,
    pause,
    resume,
    resetData,
    write,
  };
};
