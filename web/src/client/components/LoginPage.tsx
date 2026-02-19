import { useAuth } from "../hooks/useAuth";

export function LoginPage() {
  const { login } = useAuth();

  return (
    <div className="flex items-center justify-center h-screen bg-gray-950">
      <div className="text-center">
        <h1 className="text-4xl font-bold text-white mb-2">CedarLogic Web</h1>
        <p className="text-gray-400 mb-8">Digital logic simulator</p>
        <button
          onClick={login}
          className="px-6 py-3 bg-white text-gray-900 rounded-lg font-medium hover:bg-gray-100 transition-colors cursor-pointer"
        >
          Sign in with Google
        </button>
      </div>
    </div>
  );
}
