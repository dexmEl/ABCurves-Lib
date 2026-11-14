import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { mkdir, readdir, readFile, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../../", import.meta.url));
const destination = fileURLToPath(new URL("../build/docs.json", import.meta.url));
const paths = new Set([
	"AGENTS.md",
	"README.md",
	"CONTRIBUTING.md",
	"LICENSE",
	"NOTICE",
	"Native/DATASET_LICENSE.md",
	"Native/pyproject.toml",
	"mcp/README.md",
	"mcp/scripts/build-docs.mjs",
	"mcp/wrangler.jsonc",
	"web/README.md",
	"web/LICENSE",
	"web/NOTICE",
	"web/data/comparison.json",
	"web/data/reference.json",
	"tools/verify.py",
	"tools/test_runtime_package.py",
	"docs/evidence/reference-performance.json",
	"docs/evidence/reference-environment.json",
	"docs/evidence/reference-accuracy.json",
	"docs/evidence/model-assets.json",
	"examples/README.md",
	"examples/installed/CMakeLists.txt",
	"API/README.md",
	"API/c/include/abcurves.h",
	"API/cpp/include/abcurves/dll.hpp",
	"API/python/README.md",
	"API/python/src/abcurves_native/__init__.py",
	"API/rust/README.md",
	"API/rust/abcurves/src/lib.rs",
	"API/rust/abcurves-sys/src/lib.rs",
	"packaging/README.md",
	"packaging/runtime-readme.md",
	"packaging/sdk-readme.md",
	"packaging/publication.json",
]);

// Only public integration material enters the bundle. Never walk the whole checkout.
async function collect(directory, extension) {
	for (const entry of await readdir(resolve(root, directory), { withFileTypes: true })) {
		if (entry.isSymbolicLink() || ["evidence", "qualification"].includes(entry.name)) continue;
		const path = `${directory}/${entry.name}`;
		if (entry.isDirectory()) await collect(path, extension);
		else if (entry.isFile() && entry.name.endsWith(extension)) paths.add(path);
	}
}

await collect("docs", ".md");
await collect("lib/include/abcurves", ".hpp");
for (const [directory, extension] of [
	["API/c/examples", ".c"],
	["API/cpp/examples", ".cpp"],
	["API/python/examples", ".py"],
	["API/rust/abcurves/examples", ".rs"],
])
	await collect(directory, extension);

function languages(path) {
	for (const [language, pattern] of [
		["cpp", /API\/cpp\/|lib\/include\/|getting-started\/cpp\.md$/],
		["c", /API\/c\/|getting-started\/c\.md$/],
		["python", /API\/python\/|getting-started\/python\.md$/],
		["rust", /API\/rust\/|getting-started\/rust\.md$/],
	])
		if (pattern.test(path)) return [language];
	return ["c", "cpp", "python", "rust"];
}

const hash = (text) => createHash("sha256").update(text).digest("hex");
const documents = [];
for (const path of [...paths].sort()) {
	const text = (await readFile(resolve(root, path), "utf8"))
		.replace(/\r\n?/g, "\n")
		.replace(/\n$/, "");
	const kind = /\/examples\/.*\.(c|cpp|py|rs)$/.test(path)
		? "example"
		: path.endsWith(".md") && !path.startsWith("docs/api/")
			? "guide"
			: "reference";
	documents.push({
		path,
		title: path.endsWith(".md") ? (text.match(/^# (.+)$/m)?.[1] ?? path) : path,
		kind,
		languages: languages(path),
		sha256: hash(text),
		text,
	});
}

const project = await readFile(resolve(root, "API/python/pyproject.toml"), "utf8");
const header = await readFile(resolve(root, "API/c/include/abcurves.h"), "utf8");
const libraryVersion = project.match(/^version = "([^"]+)"/m)?.[1];
const abiMajor = header.match(/^#define ABC_ABI_MAJOR (\d+)u/m)?.[1];
const abiMinor = header.match(/^#define ABC_ABI_MINOR (\d+)u/m)?.[1];
if (!libraryVersion || !abiMajor || !abiMinor)
	throw new Error("Cannot determine library/ABI version");

function git(args) {
	try {
		return execFileSync("git", args, {
			cwd: root,
			encoding: "utf8",
			stdio: ["ignore", "pipe", "ignore"],
		}).trim();
	} catch {
		return null;
	}
}

const revision = git(["rev-parse", "HEAD"]);
const status = git(["status", "--porcelain", "--", ...documents.map((doc) => doc.path)]);
const snapshot = {
	library_version: libraryVersion,
	abi_version: `${abiMajor}.${abiMinor}`,
	source_revision: revision,
	source_modified: status === null ? null : status.length > 0,
	content_sha256: hash(JSON.stringify(documents.map(({ path, sha256 }) => ({ path, sha256 })))),
};
await mkdir(dirname(destination), { recursive: true });
await writeFile(destination, `${JSON.stringify({ snapshot, documents })}\n`);
console.log(
	`Bundled ${documents.length} public documents/examples for ABCurves ${libraryVersion} (${snapshot.content_sha256.slice(0, 12)}).`,
);
