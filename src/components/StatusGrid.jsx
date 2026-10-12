export default function StatusGrid({ status, currentCommand, speed }) {
  const lastStopReason = String(status.lastStopReason || "none").trim();
  const lastStopAngleDeg = Number(status.lastStopAngleDeg);
  const fallAngleLimitDeg = Number(status.fallAngleLimitDeg);
  const hasStopReason = lastStopReason.toLowerCase() !== "none";

  const items = [
    {
      label: "Command",
      value: `${formatCommand(currentCommand)} · ${speed}%`,
      led: currentCommand === "stop" ? "off" : "on",
    },
    {
      label: "Height live / target",
      value: status.heightCalibrationValid && status.heightCm
        ? `${Number(status.heightCm).toFixed(1)} / ${Number(status.targetHeightCm || 0).toFixed(1)} cm`
        : "Uncalibrated",
      led: status.standingEnabled ? "on" : "neutral",
    },
    {
      label: "Extension live / target",
      value: status.heightCalibrationValid ? `${Number(status.extensionPercent || 0).toFixed(0)} / ${Number(status.targetExtensionPercent || 0).toFixed(0)}%` : "Uncalibrated",
      led: status.heightCalibrationValid ? "on" : "warn",
    },
    {
      label: "Wheel authority",
      value: `${Number(status.wheelAuthorityPercent || 0).toFixed(0)}% · ${Number(status.scheduledWheelMaxOutputUs || 0).toFixed(0)} µs`,
      led: status.heightCalibrationValid ? "on" : "warn",
    },
    {
      label: "Wheel pulses",
      value: `${status.wheelLeftUs} / ${status.wheelRightUs} µs`,
      led: status.balancingEnabled ? "on" : "neutral",
    },
    {
      label: "Leg pulses",
      value: `${status.legLeftUs} / ${status.legRightUs} µs`,
      led: "neutral",
    },
    {
      label: "Encoder angles",
      value: `${Number(status.leftEncoderDeg || 0).toFixed(1)}° / ${Number(status.rightEncoderDeg || 0).toFixed(1)}°`,
      led: status.encodersHealthy ? "on" : "warn",
    },
    {
      label: "Encoder velocity",
      value: `${Number(status.leftEncoderVelocityDps || 0).toFixed(1)} / ${Number(status.rightEncoderVelocityDps || 0).toFixed(1)}°/s`,
      led: status.encodersHealthy ? "on" : "warn",
    },
    {
      label: "Leg state",
      value: `${formatCommand(status.leftLegDriveState || "neutral")} / ${formatCommand(status.rightLegDriveState || "neutral")}`,
      led: status.standingEnabled ? "on" : "neutral",
    },
    {
      label: "Roll / yaw rate",
      value: `${Number(status.roll || 0).toFixed(1)}° / ${Number(status.yawRate || 0).toFixed(1)}°/s`,
      led: "neutral",
    },
    {
      label: "Acceleration",
      value: `${Number(status.accelMagnitudeG || 0).toFixed(2)} g`,
      led: Math.abs(Number(status.accelMagnitudeG || 0) - 1) < 0.2 ? "on" : "warn",
    },
    {
      label: "Wi-Fi RSSI",
      value: status.online ? `${status.wifiRssi} dBm` : "Offline",
      led: status.wifiRssi > -67 ? "on" : "neutral",
    },
    {
      label: "Sensors",
      value: `IMU ${status.imuErrors || 0} · ENC ${status.encoderErrors || 0}`,
      led: Number(status.imuErrors || 0) === 0 && status.encodersHealthy ? "on" : "warn",
    },
    {
      label: "Controller",
      value: formatCommand(status.autoState || "offline"),
      led: status.balancingEnabled ? "on" : status.online ? "neutral" : "warn",
    },
    {
      label: "Predicted pitch",
      value:
        status.predictedAngle !== null &&
        status.predictedAngle !== undefined &&
        Number.isFinite(Number(status.predictedAngle))
        ? `${Number(status.predictedAngle).toFixed(2)}°`
        : "--",
      led: status.balancingEnabled ? "on" : "neutral",
    },
    {
      label: "Last stop / limit",
      value: `${formatStopReason(lastStopReason)} · ${
        Number.isFinite(lastStopAngleDeg) ? lastStopAngleDeg.toFixed(1) : "--"
      }° / ${Number.isFinite(fallAngleLimitDeg) ? fallAngleLimitDeg.toFixed(0) : "--"}°`,
      led: hasStopReason ? "warn" : "neutral",
    },
  ];

  return (
    <section className="panel telemetry-panel" aria-labelledby="telemetry-heading">
      <div className="panel-heading">
        <div>
          <span className="section-kicker">Telemetry</span>
          <h2 id="telemetry-heading">Robot telemetry</h2>
          <p className="panel-sub">{status.online ? "Live feedback from the ESP32" : "Last reported values · robot offline"}</p>
        </div>
      </div>
      <div className="telemetry-list">
        {items.map((item) => (
          <article className="telemetry-item" key={item.label}>
            <span className="telemetry-label">
              <span className={`telemetry-led telemetry-led--${item.led}`} />
              {item.label}
            </span>
            <strong
              className={
                item.value.length > 14
                  ? "telemetry-value telemetry-value--small"
                  : "telemetry-value"
              }
            >
              {item.value}
            </strong>
          </article>
        ))}
      </div>
    </section>
  );
}

function formatCommand(command) {
  return command.charAt(0).toUpperCase() + command.slice(1);
}

function formatStopReason(reason) {
  return reason
    .replaceAll("_", " ")
    .replaceAll("-", " ")
    .replace(/\b\w/g, (letter) => letter.toUpperCase());
}
