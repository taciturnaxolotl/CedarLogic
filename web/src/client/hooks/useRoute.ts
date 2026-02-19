import { useState, useEffect, useCallback } from "react";

type Route =
  | { page: "files" }
  | { page: "editor"; fileId: string };

function parseRoute(pathname: string): Route {
  const match = pathname.match(/^\/p\/([a-f0-9-]+)$/);
  if (match) {
    return { page: "editor", fileId: match[1] };
  }
  return { page: "files" };
}

export function useRoute() {
  const [route, setRoute] = useState<Route>(() =>
    parseRoute(window.location.pathname)
  );

  useEffect(() => {
    function onPopState() {
      setRoute(parseRoute(window.location.pathname));
    }
    window.addEventListener("popstate", onPopState);
    return () => window.removeEventListener("popstate", onPopState);
  }, []);

  const navigate = useCallback((path: string) => {
    window.history.pushState(null, "", path);
    setRoute(parseRoute(path));
  }, []);

  return { route, navigate };
}
