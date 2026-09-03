import { useCallback, useEffect, useState } from "react";
import { listenToAuthState, signOutUser } from "../services/authApi";

export function useAuthSession() {
  const [authUser, setAuthUser] = useState(null);
  const [authLoading, setAuthLoading] = useState(true);

  useEffect(() => {
    return listenToAuthState((user) => {
      setAuthUser(user);
      setAuthLoading(false);
    });
  }, []);

  const handleSignOut = useCallback(() => signOutUser(), []);

  return {
    authLoading,
    authUser,
    handleSignOut,
  };
}
