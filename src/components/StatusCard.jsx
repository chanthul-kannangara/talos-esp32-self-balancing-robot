export default function StatusCard({
  icon: Icon,
  label,
  value,
  tone = "neutral",
  detail,
  wide = false,
  sparkline = false,
}) {
  return (
    <article className={`status-card status-card--${tone}${wide ? " status-card--wide" : ""}`}>
      <div className="status-card__icon" aria-hidden="true">
        <Icon size={20} />
      </div>
      <div>
        <p>{label}</p>
        <strong>{value}</strong>
        {detail ? <span>{detail}</span> : null}
      </div>
      {sparkline ? <span className="status-card__sparkline" aria-hidden="true" /> : null}
    </article>
  );
}
