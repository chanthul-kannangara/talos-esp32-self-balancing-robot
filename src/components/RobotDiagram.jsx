export default function RobotDiagram({ angle = 0, extension = 25, animated = false }) {
  const bodyOffset = (100 - Math.max(0, Math.min(100, extension))) * 0.38;
  return (
    <svg className={`robot-diagram ${animated ? "robot-diagram--animated" : ""}`} viewBox={animated ? "100 15 280 280" : "0 0 480 310"} fill="none" aria-hidden="true">
      <ellipse cx="247" cy="262" rx="117" ry="14" className="robot-shadow" />
      <path d="M54 262H426" className="robot-ground" />
      <path d="M240 32V262" className="robot-reference" strokeDasharray="4 6" />
      <g className="robot-assembly" style={{ transform: `rotate(${-angle}deg)`, transformOrigin: "240px 245px" }}>
        <g className="robot-leg robot-leg--rear">
          <path d={`M310 ${130 + bodyOffset}L333 178L306 235`} strokeWidth="13" strokeLinecap="round" strokeLinejoin="round" />
          <circle cx="306" cy="235" r="25" className="robot-tire" />
          <circle cx="306" cy="235" r="14" className="robot-hub" />
        </g>
        <g className="robot-chassis" style={{ "--body-offset": `${bodyOffset}px` }}>
          <path d="M171 57L299 43L327 63V143L299 156L171 139V57Z" className="robot-body-side" />
          <path d="M171 57L299 73V156L171 139V57Z" className="robot-body" />
          <path d="M171 57L299 43L327 63L299 73L171 57Z" className="robot-body-top" />
          <path d="M223 91L251 94M237 93V118" className="robot-monogram" strokeWidth="4" strokeLinecap="round" />
          <path d="M187 73L283 85" className="robot-body-detail" />
          <circle cx="278" cy="133" r="3" className="robot-light" />
          <path d="M185 57V43" className="robot-antenna" strokeWidth="3" strokeLinecap="round" />
        </g>
        <path d={`M196 ${132 + bodyOffset}L182 193L222 233`} className="robot-leg" strokeWidth="14" strokeLinecap="round" strokeLinejoin="round" />
        <path d={`M207 ${137 + bodyOffset}L197 190L231 231`} className="robot-leg-detail" strokeWidth="3" strokeLinecap="round" />
        <circle cx="184" cy="191" r="5" className="robot-joint" />
        <circle cx="230" cy="235" r="31" className="robot-tire" />
        <g className="robot-wheel" style={{ transformOrigin: "230px 235px" }}>
          <circle cx="230" cy="235" r="20" className="robot-hub" />
          <circle cx="230" cy="235" r="7" className="robot-axle" />
          <path d="M230 219V225M230 245V251M214 235H220M240 235H246" className="robot-spokes" strokeWidth="2" strokeLinecap="round" />
        </g>
      </g>
    </svg>
  );
}
