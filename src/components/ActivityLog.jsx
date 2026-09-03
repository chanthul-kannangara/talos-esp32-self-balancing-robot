export default function ActivityLog({ entries }) {
  return (
    <section className="panel activity-panel" aria-labelledby="activity-heading">
      <div className="panel-heading">
        <div>
          <span className="section-kicker">Activity</span>
          <h2 id="activity-heading">Recent commands</h2>
        </div>
        <span className="live-label">
          <span aria-hidden="true" />
          Live
        </span>
      </div>

      <div
        className="activity-list"
        role="log"
        aria-live="polite"
        aria-label="Recent local commands"
      >
        {entries.map((entry) => (
          <div className={`activity-row ${getLineClass(entry.message)}`} key={entry.id}>
            <span className="activity-dot" aria-hidden="true" />
            <span className="activity-message">{entry.message}</span>
            <time className="activity-time">{entry.time}</time>
          </div>
        ))}
      </div>
    </section>
  );
}

function getLineClass(message) {
  if (message.includes("stop")) return "is-danger";
  if (message.includes("mode")) return "is-mode";
  return "";
}
