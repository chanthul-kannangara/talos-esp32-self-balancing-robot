import {
  onAuthStateChanged,
  sendPasswordResetEmail,
  signInWithEmailAndPassword,
  signOut,
} from "firebase/auth";
import { auth } from "./firebaseClient";

export function listenToAuthState(callback) {
  return onAuthStateChanged(auth, callback);
}

export function signInWithEmail(email, password) {
  return signInWithEmailAndPassword(auth, email.trim(), password);
}

export function resetPassword(email) {
  return sendPasswordResetEmail(auth, email.trim());
}

export function signOutUser() {
  return signOut(auth);
}

export function getAuthErrorMessage(error) {
  const code = error?.code;

  const messages = {
    "auth/invalid-credential": "Email or password is incorrect.",
    "auth/invalid-email": "Enter a valid email address.",
    "auth/missing-password": "Enter your password.",
    "auth/popup-closed-by-user": "Sign-in was closed before it finished.",
    "auth/too-many-requests": "Too many attempts. Wait a little and try again.",
    "auth/user-disabled": "This account has been disabled.",
    "auth/user-not-found": "No account exists for this email.",
    "auth/operation-not-allowed": "Enable this sign-in method in Firebase Authentication first.",
  };

  return messages[code] || "Sign-in failed. Check Firebase Authentication settings and try again.";
}
