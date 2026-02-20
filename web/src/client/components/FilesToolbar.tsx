export type FilterTab = "all" | "my" | "shared";
export type SortBy = "date" | "name" | "owner";

interface FilesToolbarProps {
  filterTab: FilterTab;
  onFilterTab: (tab: FilterTab) => void;
  searchQuery: string;
  onSearchQuery: (q: string) => void;
  sortBy: SortBy;
  onSortBy: (s: SortBy) => void;
  onCreateFile: () => void;
}

const filterTabs: { key: FilterTab; label: string }[] = [
  { key: "all", label: "All" },
  { key: "my", label: "My Circuits" },
  { key: "shared", label: "Shared with me" },
];

export function FilesToolbar({
  filterTab,
  onFilterTab,
  searchQuery,
  onSearchQuery,
  sortBy,
  onSortBy,
  onCreateFile,
}: FilesToolbarProps) {
  return (
    <div className="flex flex-col sm:flex-row sm:items-center gap-3 mb-6">
      {/* Filter tabs */}
      <div className="flex gap-1">
        {filterTabs.map((tab) => (
          <button
            key={tab.key}
            onClick={() => onFilterTab(tab.key)}
            className={`px-3 py-1.5 text-sm rounded-full cursor-pointer transition-colors ${
              filterTab === tab.key
                ? "bg-gray-700 text-white"
                : "text-gray-400 hover:text-white hover:bg-gray-800"
            }`}
          >
            {tab.label}
          </button>
        ))}
      </div>

      {/* Spacer */}
      <div className="flex-1" />

      {/* Search */}
      <div className="relative">
        <svg
          className="absolute left-2.5 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-500"
          fill="none"
          viewBox="0 0 24 24"
          stroke="currentColor"
          strokeWidth={2}
        >
          <path
            strokeLinecap="round"
            strokeLinejoin="round"
            d="M21 21l-5.197-5.197m0 0A7.5 7.5 0 105.196 5.196a7.5 7.5 0 0010.607 10.607z"
          />
        </svg>
        <input
          type="text"
          placeholder="Search circuits..."
          value={searchQuery}
          onChange={(e) => onSearchQuery(e.target.value)}
          className="pl-8 pr-3 py-1.5 text-sm bg-gray-800 border border-gray-700 rounded-lg text-white placeholder-gray-500 focus:outline-none focus:border-gray-500 w-48"
        />
      </div>

      {/* Sort dropdown */}
      <select
        value={sortBy}
        onChange={(e) => onSortBy(e.target.value as SortBy)}
        className="px-3 py-1.5 text-sm bg-gray-800 border border-gray-700 rounded-lg text-white focus:outline-none focus:border-gray-500 cursor-pointer"
      >
        <option value="date">Date modified</option>
        <option value="name">Name</option>
        <option value="owner">Owner</option>
      </select>

      {/* New Circuit button */}
      <button
        onClick={onCreateFile}
        className="px-4 py-1.5 bg-blue-600 rounded-lg text-sm font-medium hover:bg-blue-500 transition-colors cursor-pointer"
      >
        New Circuit
      </button>
    </div>
  );
}
