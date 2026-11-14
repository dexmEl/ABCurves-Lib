import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { posix } from "node:path";
import test from "node:test";
import { createCatalog, sections, type Bundle } from "../src/catalog.ts";

const bundle: Bundle = JSON.parse(
	readFileSync(new URL("../build/docs.json", import.meta.url), "utf8"),
);
const catalog = createCatalog(bundle);

test("the corpus includes the integration flow, calibration, validation and all four languages", () => {
	const paths = new Set(catalog.list().documents.map((doc) => doc.path));
	for (const path of [
		"AGENTS.md",
		"docs/guides/calibration.md",
		"docs/guides/integration-validation.md",
		"docs/getting-started/c.md",
		"docs/getting-started/cpp.md",
		"docs/getting-started/python.md",
		"docs/getting-started/rust.md",
		"API/c/include/abcurves.h",
		"API/cpp/include/abcurves/dll.hpp",
		"API/python/src/abcurves_native/__init__.py",
		"API/rust/abcurves/src/lib.rs",
		"API/c/examples/segments.c",
		"API/cpp/examples/segments.cpp",
		"API/python/examples/segments.py",
		"API/rust/abcurves/examples/segments.rs",
		"Native/DATASET_LICENSE.md",
		"mcp/README.md",
		"web/README.md",
		"tools/verify.py",
		"tools/test_runtime_package.py",
		"packaging/publication.json",
	])
		assert.ok(paths.has(path), `missing ${path}`);
	assert.match(bundle.snapshot.library_version, /^\d+\.\d+\.\d+/);
	assert.match(bundle.snapshot.abi_version, /^\d+\.\d+$/);
	assert.match(bundle.snapshot.content_sha256, /^[a-f0-9]{64}$/);
	assert.ok(bundle.documents.every((doc) => /^[a-f0-9]{64}$/.test(doc.sha256)));
	assert.ok(![...paths].some((path) => /node_modules|\.env|docs\/qualification/.test(path)));
	assert.deepEqual([...paths].filter((path) => path.startsWith("docs/evidence/")).sort(), [
		"docs/evidence/model-assets.json",
		"docs/evidence/reference-accuracy.json",
		"docs/evidence/reference-environment.json",
		"docs/evidence/reference-performance.json",
	]);
});

test("compact discovery preserves metadata and filters while omitting sections", () => {
	for (const filter of [{}, { language: "cpp" as const, kind: "guide" as const }]) {
		const full = catalog.list(filter);
		const compact = catalog.list({ ...filter, include_sections: false });
		assert.deepEqual(compact.snapshot, full.snapshot);
		assert.deepEqual(
			compact.documents,
			full.documents.map(({ sections: _sections, ...doc }) => doc),
		);
		assert.ok(compact.documents.every((doc) => !Object.hasOwn(doc, "sections")));
		assert.ok(full.documents.some((doc) => doc.sections!.length > 0));
		assert.deepEqual(catalog.list({ ...filter, include_sections: true }), full);
	}
});

test("every document paginates losslessly and matches its content hash", () => {
	for (const doc of bundle.documents) {
		let start: number | null = 1;
		const pages: string[] = [];
		while (start !== null) {
			const page = catalog.read(doc.path, { start_line: start, max_lines: 200 });
			assert.equal(page.start_line, start, doc.path);
			assert.ok(page.end_line - page.start_line < 200, doc.path);
			pages.push(page.text);
			start = page.next_start_line;
		}
		const text = pages.join("\n");
		assert.equal(text, doc.text, doc.path);
		assert.equal(createHash("sha256").update(text).digest("hex"), doc.sha256, doc.path);
	}
	assert.equal(
		createHash("sha256")
			.update(JSON.stringify(bundle.documents.map(({ path, sha256 }) => ({ path, sha256 }))))
			.digest("hex"),
		bundle.snapshot.content_sha256,
	);
});

test("relative Markdown links resolve to bundled documents and valid section anchors", () => {
	const byPath = new Map(bundle.documents.map((doc) => [doc.path, doc]));
	const errors: string[] = [];
	let links = 0;
	for (const doc of bundle.documents.filter((doc) => doc.path.endsWith(".md"))) {
		let fence: { marker: string; length: number } | undefined;
		const prose = doc.text
			.split("\n")
			.filter((line) => {
				const marker = line.match(/^\s{0,3}(`{3,}|~{3,})/);
				if (marker) {
					if (!fence) fence = { marker: marker[1][0], length: marker[1].length };
					else if (marker[1][0] === fence.marker && marker[1].length >= fence.length)
						fence = undefined;
					return false;
				}
				return !fence;
			})
			.join("\n");
		const targets = [
			...prose.matchAll(/\]\(\s*(?:<([^>]+)>|([^\s)]+))(?:\s+"[^"]*")?\s*\)/g),
			...prose.matchAll(/^\s{0,3}\[[^\]]+\]:\s*(?:<([^>]+)>|(\S+))/gm),
		].map((match) => match[1] ?? match[2]);
		for (const target of targets) {
			if (/^[a-z][a-z0-9+.-]*:|^\/\//i.test(target)) continue;
			links++;
			const [path, anchor] = target.split("#");
			const resolved = path
				? posix.normalize(posix.join(posix.dirname(doc.path), decodeURIComponent(path)))
				: doc.path;
			const destination = byPath.get(resolved);
			if (!destination) errors.push(`${doc.path}: ${target} is not bundled (${resolved})`);
			else if (
				anchor &&
				!sections(destination.text).some(
					(section) => section.anchor === decodeURIComponent(anchor),
				)
			)
				errors.push(`${doc.path}: ${target} has no matching anchor`);
		}
	}
	assert.ok(links >= 249, `unexpectedly few internal links checked: ${links}`);
	assert.deepEqual(errors, []);
});

test("all public C functions are discoverable in the C header", () => {
	const header = bundle.documents.find((doc) => doc.path === "API/c/include/abcurves.h")!;
	const symbols = [...header.text.matchAll(/^ABC_API[^\n]*\bABC_CALL\s+(abc_\w+)\s*\(/gm)].map(
		(match) => match[1],
	);
	assert.ok(symbols.length >= 55);
	for (const symbol of symbols) {
		assert.ok(
			catalog
				.search(symbol, { language: "c", limit: 10 })
				.results.some((result) => result.path === header.path),
			`C header not found for ${symbol}`,
		);
	}
});

test("language filters retain shared guidance and select matching language examples", () => {
	for (const language of ["c", "cpp", "python", "rust"] as const) {
		const docs = catalog.list({ language }).documents;
		assert.ok(docs.some((doc) => doc.path === "AGENTS.md"));
		assert.ok(docs.some((doc) => doc.path === `docs/getting-started/${language}.md`));
		assert.ok(
			!docs.some(
				(doc) =>
					/getting-started\/(c|cpp|python|rust)\.md$/.test(doc.path) &&
					doc.path !== `docs/getting-started/${language}.md`,
			),
		);
		const examples = catalog.list({ language, kind: "example" }).documents;
		assert.ok(examples.length > 0);
		assert.ok(examples.every((doc) => doc.path.startsWith(`API/${language}/`)));
	}
});

test("search retrieves calibration, exact API symbols, Renderer and local validation", () => {
	for (const [query, path, language] of [
		["calibration", "docs/guides/calibration.md", undefined],
		["abc_model_load_embedded", "API/c/include/abcurves.h", "c"],
		["Renderer", "docs/advanced/renderer.md", undefined],
		["Validate an application integration", "docs/guides/integration-validation.md", undefined],
	] as const) {
		const response = catalog.search(query, { language, limit: 10 });
		assert.ok(
			response.results.some((result) => result.path === path),
			`${query} did not retrieve ${path}`,
		);
		assert.ok(
			response.results.every(
				(result) => result.excerpt.length <= 900 && result.start_line <= result.end_line,
			),
		);
		assert.deepEqual(response.snapshot, bundle.snapshot);
	}
	assert.deepEqual(catalog.search("nonexistentabcdefxyz").results, []);
	assert.throws(() => catalog.search(" "), /Query/);
	assert.throws(() => catalog.search("model", { limit: 11 }), /limit/);
});

test("pagination reconstructs the exact normalized source and respects section boundaries", () => {
	let start: number | null = 1;
	const pages: string[] = [];
	while (start !== null) {
		const page = catalog.read("AGENTS.md", { start_line: start, max_lines: 31 });
		pages.push(page.text);
		start = page.next_start_line;
	}
	assert.equal(pages.join("\n"), bundle.documents.find((doc) => doc.path === "AGENTS.md")!.text);
	const heading = catalog
		.list()
		.documents.find((doc) => doc.path === "AGENTS.md")!
		.sections.find((section) => section.heading.startsWith("1. "))!;
	const page = catalog.read("AGENTS.md", { section: `#${heading.anchor}`, max_lines: 200 });
	assert.ok(page.text.startsWith("## 1."));
	assert.ok(!page.text.includes("## 2."));
	assert.equal(page.next_start_line, null);
	assert.equal(catalog.read("AGENTS.md", { section: heading.heading }).text, page.text);
});

test("Markdown headings ignore code fences, include child sections and disambiguate repeated anchors", () => {
	const text = "# Root\n## Setup\n```md\n# Fake\n~~~\n```\n### Child\nbody\n## Setup\nlast";
	assert.deepEqual(
		sections(text).map(({ heading, anchor, start_line, end_line }) => ({
			heading,
			anchor,
			start_line,
			end_line,
		})),
		[
			{ heading: "Root", anchor: "root", start_line: 1, end_line: 10 },
			{ heading: "Setup", anchor: "setup", start_line: 2, end_line: 8 },
			{ heading: "Child", anchor: "child", start_line: 7, end_line: 8 },
			{ heading: "Setup", anchor: "setup-1", start_line: 9, end_line: 10 },
		],
	);
	const small = createCatalog({
		snapshot: bundle.snapshot,
		documents: [
			{
				path: "test.md",
				title: "Root",
				kind: "guide",
				languages: ["c"],
				sha256: "fixture",
				text,
			},
		],
	});
	assert.equal(small.read("test.md", { section: "setup-1" }).text, "## Setup\nlast");
	assert.throws(
		() => small.read("test.md", { section: "setup", start_line: 9 }),
		/between 2 and 8/,
	);
});

test("read rejects unknown paths, sections and invalid ranges", () => {
	for (const path of ["../LICENSE", "https://example.com/", "C:/secret", "missing.md"]) {
		assert.throws(() => catalog.read(path), /Unknown document path/);
	}
	assert.throws(
		() => catalog.read("AGENTS.md", { section: "missing heading" }),
		/Unknown section/,
	);
	for (const start_line of [0, 1.5, Number.NaN, 999999])
		assert.throws(() => catalog.read("AGENTS.md", { start_line }), /start_line/);
	for (const max_lines of [0, 1.5, 201])
		assert.throws(() => catalog.read("AGENTS.md", { max_lines }), /max_lines/);
});
