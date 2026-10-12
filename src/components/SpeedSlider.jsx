export default function SpeedSlider({ speed, onSpeedChange }) {
  return (
    <div className="speed-block">
      <div className="setting-title-row"><div><label className="setting-label" htmlFor="wheel-speed">Wheel limit</label><span className="setting-note">Maximum drive command</span></div><strong>{speed}<small>%</small></strong></div>
      <input
        id="wheel-speed" className="wheel-speed-range" type="range" min="0" max="100" step="1"
        value={speed} onChange={(event) => onSpeedChange(Number(event.target.value))}
        style={{ "--speed-percent": `${speed}%` }} aria-valuetext={`${speed} percent`}
      />
      <div className="slider-scale" aria-hidden="true"><span>0%</span><span>50%</span><span>100%</span></div>
    </div>
  );
}
