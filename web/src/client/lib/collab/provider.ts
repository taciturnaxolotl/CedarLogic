import { HocuspocusProvider } from "@hocuspocus/provider";
import { IndexeddbPersistence } from "y-indexeddb";
import * as Y from "yjs";

export interface CollabProvider {
  doc: Y.Doc;
  provider: HocuspocusProvider;
  indexeddb: IndexeddbPersistence;
  destroy: () => void;
}

export function createCollabProvider(
  fileId: string,
  token: string
): CollabProvider {
  const doc = new Y.Doc();

  // In production, use the same host. In dev, connect directly to Hocuspocus on :3001.
  const wsUrl = import.meta.env.PROD
    ? `${window.location.protocol === "https:" ? "wss:" : "ws:"}//${window.location.host}/ws`
    : `ws://${window.location.hostname}:3001`;

  const provider = new HocuspocusProvider({
    url: wsUrl,
    name: fileId,
    document: doc,
    token,
  });

  const indexeddb = new IndexeddbPersistence(`cedarlogic-${fileId}`, doc);

  return {
    doc,
    provider,
    indexeddb,
    destroy() {
      provider.destroy();
      indexeddb.destroy();
      doc.destroy();
    },
  };
}
