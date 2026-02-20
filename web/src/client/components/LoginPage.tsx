import { useAuth } from "../hooks/useAuth";

export function LoginPage() {
  const { login } = useAuth();

  return (
    <div className="flex items-center justify-center h-screen bg-white dark:bg-gray-950">
      <div className="text-center">
        <h1 className="text-4xl font-bold text-gray-900 dark:text-white mb-2">CedarLogic Web</h1>
        <p className="text-gray-500 dark:text-gray-400 mb-8">Digital logic simulator</p>
        <button
          onClick={login}
          className="px-6 py-3 bg-gray-900 text-white hover:bg-gray-800 dark:bg-white dark:text-gray-900 dark:hover:bg-gray-100 rounded-lg font-medium transition-colors cursor-pointer"
        >
          Sign in with Google
        </button>
      </div>
    </div>
  );
}
