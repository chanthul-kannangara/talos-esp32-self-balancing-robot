import { ArrowUpRight, KeyRound, LockKeyhole, Mail, Pause, Play, ShieldCheck } from "lucide-react";
import { lazy, Suspense, useCallback, useState } from "react";
import { getAuthErrorMessage, resetPassword, signInWithEmail } from "../services/authApi";
import ThemeToggle from "./ThemeToggle";
import RobotDiagram from "./RobotDiagram";

const RobotModel = lazy(() => import("./RobotModel"));

export default function LoginPage({ theme, onThemeToggle }) {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [busyAction, setBusyAction] = useState("");
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");
  const [animationPaused, setAnimationPaused] = useState(false);

  const runAuthAction = useCallback(async (actionName, action) => {
    setBusyAction(actionName);
    setError("");
    setMessage("");
    try {
      await action();
    } catch (authError) {
      setError(getAuthErrorMessage(authError));
    } finally {
      setBusyAction("");
    }
  }, []);

  const handleEmailSignIn = useCallback(
    (event) => {
      event.preventDefault();
      runAuthAction("email", () => signInWithEmail(email, password));
    },
    [email, password, runAuthAction],
  );

  const handlePasswordReset = useCallback(() => {
    if (!email.trim()) {
      setError("Enter your email first, then click reset password.");
      return;
    }
    runAuthAction("reset", async () => {
      await resetPassword(email);
      setMessage("Password reset email sent.");
    });
  }, [email, runAuthAction]);

  const isBusy = Boolean(busyAction);

  return (
    <main className="auth-shell auth-shell--workspace">
      <header className="auth-topbar">
        <div className="brand"><div className="brand__mark" aria-hidden="true">T<span /></div><div className="brand__text"><strong>TALOS ONE</strong><p>Robot workspace</p></div></div>
        <div className="auth-topbar__actions"><span>Made to move.</span><ThemeToggle theme={theme} onToggle={onThemeToggle} /></div>
      </header>
      <div className="auth-layout">
        <section className="auth-intro" aria-labelledby="welcome-heading">
          <span className="auth-intro__tag"><span aria-hidden="true" />MEET YOUR ROBOT WORKSPACE</span>
          <h2 id="welcome-heading">BALANCE.<br />BY DESIGN.</h2>
          <p>A clear view. Complete control.<br /> Bring every TALOS movement into focus.</p>
          <div className={`auth-robot-stage ${animationPaused ? "is-paused" : ""}`}>
            <span className="auth-robot-annotation">TALOS / 01</span>
            <Suspense fallback={<RobotDiagram animated={!animationPaused} extension={40} />}>
              <RobotModel paused={animationPaused} />
            </Suspense>
            <span className="auth-robot-caption">Your CAD, brought to life. Assem1 / 3D preview.</span>
            <button className="animation-toggle" type="button" onClick={() => setAnimationPaused((paused) => !paused)} aria-label={animationPaused ? "Play robot animation" : "Pause robot animation"} aria-pressed={animationPaused}>
              {animationPaused ? <Play size={15} aria-hidden="true" /> : <Pause size={15} aria-hidden="true" />}
            </button>
          </div>
        </section>
      <section className="auth-card" aria-labelledby="auth-heading">
        <div className="auth-card__header">
          <span className="section-kicker">Operator access</span>
          <ArrowUpRight size={22} aria-hidden="true" />
        </div>

        <div className="auth-panel">
          <div className="auth-copy">
            <span className="auth-lock">
              <ShieldCheck size={14} aria-hidden="true" strokeWidth={2} />
              Secure access
            </span>
            <h1 id="auth-heading">WELCOME<br />BACK.</h1>
            <p>Sign in to your TALOS workspace.</p>
          </div>

          <form className="auth-form" onSubmit={handleEmailSignIn}>
            <label htmlFor="login-email">
              <Mail size={14} aria-hidden="true" strokeWidth={2} />
              Email
            </label>
            <input
              id="login-email"
              type="email"
              autoComplete="email"
              value={email}
              onChange={(event) => setEmail(event.target.value)}
              placeholder="name@example.com"
              required
            />

            <label htmlFor="login-password">
              <LockKeyhole size={14} aria-hidden="true" strokeWidth={2} />
              Password
            </label>
            <input
              id="login-password"
              type="password"
              autoComplete="current-password"
              value={password}
              onChange={(event) => setPassword(event.target.value)}
              placeholder="••••••••"
              required
            />

            {error ? <p className="auth-alert auth-alert--error">{error}</p> : null}
            {message ? <p className="auth-alert auth-alert--success">{message}</p> : null}

            <button className="auth-submit" type="submit" disabled={isBusy}>
              <KeyRound size={15} aria-hidden="true" strokeWidth={2} />
              {busyAction === "email" ? "Signing in…" : "Open workspace"}
              <ArrowUpRight size={17} aria-hidden="true" />
            </button>
          </form>

          <button
            className="auth-link-button"
            type="button"
            disabled={isBusy}
            onClick={handlePasswordReset}
          >
            Reset password
          </button>
          <p className="auth-privacy-note"><LockKeyhole size={13} aria-hidden="true" />Authenticated access to your robot controls.</p>
        </div>
      </section>
      </div>
      <footer className="auth-footer"><span>01 / MOVE WITH INTENTION</span><span>TALOS ONE · ROBOT CONTROL</span></footer>
    </main>
  );
}
