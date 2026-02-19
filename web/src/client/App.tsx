import { useAuth } from "./hooks/useAuth";
import { LoginPage } from "./components/LoginPage";
import { FilesPage } from "./components/FilesPage";
import { EditorPage } from "./components/EditorPage";
import { useRoute } from "./hooks/useRoute";

export function App() {
  const { user, loading } = useAuth();
  const { route, navigate } = useRoute();

  if (loading) {
    return (
      <div className="flex items-center justify-center h-screen bg-gray-950 text-gray-400">
        Loading...
      </div>
    );
  }

  // Allow unauthenticated access to editor pages (for link-shared files)
  if (route.page === "editor") {
    return (
      <EditorPage
        fileId={route.fileId}
        onBack={user ? () => navigate("/") : null}
        user={user}
      />
    );
  }

  // All other pages require auth
  if (!user) {
    return <LoginPage />;
  }

  return (
    <FilesPage
      onOpenFile={(id) => navigate(`/p/${id}`)}
    />
  );
}
