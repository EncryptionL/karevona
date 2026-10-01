"use client";

import { useCallback, useEffect, useState } from "react";

export interface ApiState<T> {
  data: T | undefined;
  error: string | undefined;
  loading: boolean;
  reload: () => void;
}

/** Runs an SDK call on mount and exposes loading/error state. */
export function useApi<T>(call: () => Promise<T>): ApiState<T> {
  const [data, setData] = useState<T>();
  const [error, setError] = useState<string>();
  const [loading, setLoading] = useState(true);
  const [tick, setTick] = useState(0);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    call()
      .then((value) => {
        if (!cancelled) {
          setData(value);
          setError(undefined);
        }
      })
      .catch((e: unknown) => {
        if (!cancelled) setError(e instanceof Error ? e.message : String(e));
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
    // `call` is intentionally excluded: callers pass inline arrows; reload is explicit.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [tick]);

  const reload = useCallback(() => setTick((t) => t + 1), []);
  return { data, error, loading, reload };
}
