import { Liquid } from "liquid-gooey";
import { useCallback, useLayoutEffect, useRef, useState } from "react";

export default function SpeedSlider({ speed, onSpeedChange }) {
  const sliderRef = useRef(null);
  const [sliderWidth, setSliderWidth] = useState(0);

  useLayoutEffect(() => {
    const slider = sliderRef.current;
    if (!slider) return undefined;

    const updateWidth = () => setSliderWidth(slider.getBoundingClientRect().width);
    updateWidth();
    const observer = new ResizeObserver(updateWidth);
    observer.observe(slider);
    return () => observer.disconnect();
  }, []);

  const updateFromClientX = useCallback(
    (clientX) => {
      const slider = sliderRef.current;
      if (!slider) return;
      const rect = slider.getBoundingClientRect();
      const nextSpeed = Math.round(((clientX - rect.left) / rect.width) * 100);
      onSpeedChange(Math.min(100, Math.max(0, nextSpeed)));
    },
    [onSpeedChange],
  );

  const handlePointerDown = useCallback(
    (event) => {
      event.currentTarget.setPointerCapture(event.pointerId);
      updateFromClientX(event.clientX);
    },
    [updateFromClientX],
  );

  const handlePointerMove = useCallback(
    (event) => {
      if (event.buttons === 1) updateFromClientX(event.clientX);
    },
    [updateFromClientX],
  );

  const handleKeyDown = useCallback(
    (event) => {
      const step = event.shiftKey ? 10 : 5;
      const keyActions = {
        ArrowLeft: Math.max(0, speed - step),
        ArrowDown: Math.max(0, speed - step),
        ArrowRight: Math.min(100, speed + step),
        ArrowUp: Math.min(100, speed + step),
        Home: 0,
        End: 100,
      };
      if (Object.prototype.hasOwnProperty.call(keyActions, event.key)) {
        event.preventDefault();
        onSpeedChange(keyActions[event.key]);
      }
    },
    [onSpeedChange, speed],
  );

  return (
    <div className="speed-block" aria-labelledby="speed-heading">
      <div className="setting-title-row">
        <div>
          <span className="setting-label" id="speed-heading">
            Wheel limit
          </span>
          <span className="setting-note">Max drive command</span>
        </div>
        <strong>{speed}%</strong>
      </div>

      <div
        className="slider-control"
        role="slider"
        aria-label="Speed percentage"
        aria-valuemin="0"
        aria-valuemax="100"
        aria-valuenow={speed}
        tabIndex="0"
        ref={sliderRef}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onKeyDown={handleKeyDown}
        style={{ "--speed-percent": `${speed}%` }}
      >
        <span className="slider-track" aria-hidden="true">
          <span className="slider-fill" />
        </span>
        <Liquid
          className="liquid-slider-thumb"
          blur={0}
          contrast={18}
          fill="var(--blue)"
          aria-hidden="true"
        >
          <Liquid.Item
            effect="move"
            move={{ springiness: 0.68, trail: 0.28, wobble: 0.22, stretch: 0.22 }}
            x={Math.max(0, (sliderWidth - 16) * (speed / 100))}
            transition="smooth"
          >
            <span className="slider-thumb slider-thumb--liquid" />
          </Liquid.Item>
        </Liquid>
      </div>

      <div className="slider-scale" aria-hidden="true">
        <span>0</span>
        <span>50</span>
        <span>100</span>
      </div>
    </div>
  );
}
