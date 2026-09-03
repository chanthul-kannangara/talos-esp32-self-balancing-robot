import { KeyRound, LockKeyhole, Mail, ShieldCheck } from "lucide-react";
import { useCallback, useState } from "react";
import { getAuthErrorMessage, resetPassword, signInWithEmail } from "../services/authApi";
import ThemeToggle from "./ThemeToggle";

export default function LoginPage({ theme, onThemeToggle }) {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [busyAction, setBusyAction] = useState("");
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");

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
    <main className="auth-shell">
      <section className="auth-card" aria-labelledby="auth-heading">
        <div className="auth-card__header">
          <div className="auth-brand">
            <div className="brand__mark" aria-hidden="true">
              T
            </div>
            <div>
              <strong>TALOS ONE</strong>
              <span>Robot control</span>
            </div>
          </div>
          <ThemeToggle theme={theme} onToggle={onThemeToggle} />
        </div>

        <div className="auth-panel">
          <div className="auth-copy">
            <span className="auth-lock">
              <ShieldCheck size={14} aria-hidden="true" strokeWidth={2} />
              Secure access
            </span>
            <h1 id="auth-heading">Sign in</h1>
            <p>Use your account to open the robot console.</p>
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
              {busyAction === "email" ? "Signing in…" : "Sign in"}
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
        </div>
      </section>
    </main>
  );
}
