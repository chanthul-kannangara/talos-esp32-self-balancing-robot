import { createInterface } from "node:readline";

const statusUrl =
  "https://esp32-self-balancing-robot-default-rtdb.asia-southeast1.firebasedatabase.app/robot/status.json";
const lines = createInterface({ input: process.stdin, crlfDelay: Infinity });

for await (const line of lines) {
  if (!line.trim()) continue;
  try {
    const response = await fetch(statusUrl, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: line,
    });
    if (!response.ok) {
      console.error(`Firebase HTTP ${response.status}: ${await response.text()}`);
    }
  } catch (error) {
    console.error(`Firebase update failed: ${error.message}`);
  }
}
