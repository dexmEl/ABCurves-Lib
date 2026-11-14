export type Language = "c" | "cpp" | "python" | "rust";
export type Kind = "guide" | "example" | "reference";
export interface Document {
	path: string;
	title: string;
	kind: Kind;
	languages: Language[];
	sha256: string;
	text: string;
}
export interface Bundle {
	snapshot: {
		library_version: string;
		abi_version: string;
		source_revision: string | null;
		source_modified: boolean | null;
		content_sha256: string;
	};
	documents: Document[];
}
interface Section {
	heading: string;
	anchor: string;
	level: number;
	start_line: number;
	end_line: number;
}
interface Filter {
	language?: Language;
	kind?: Kind;
}

const tokens = (value: string): string[] =>
	value.toLowerCase().match(/c\+\+|c#|[\p{L}\p{N}_]+/gu) ?? [];
const stopWords = new Set([
	"a",
	"an",
	"and",
	"are",
	"do",
	"for",
	"how",
	"i",
	"in",
	"is",
	"it",
	"of",
	"the",
	"to",
	"use",
	"with",
]);

export function sections(text: string): Section[] {
	const lines = text.split("\n");
	const result: Section[] = [];
	const anchors = new Map<string, number>();
	let fence: { marker: string; length: number } | undefined;
	for (let i = 0; i < lines.length; i++) {
		const marker = lines[i].match(/^\s{0,3}(`{3,}|~{3,})/);
		if (marker) {
			if (!fence) fence = { marker: marker[1][0], length: marker[1].length };
			else if (marker[1][0] === fence.marker && marker[1].length >= fence.length)
				fence = undefined;
			continue;
		}
		if (fence) continue;
		const match = lines[i].match(/^(#{1,6})\s+(.+?)(?:\s+#+)?\s*$/);
		if (!match) continue;
		const heading = match[2];
		const base = heading
			.toLowerCase()
			.replace(/[^\p{L}\p{N}_\s-]/gu, "")
			.replace(/\s/g, "-");
		const count = anchors.get(base) ?? 0;
		anchors.set(base, count + 1);
		result.push({
			heading,
			anchor: count ? `${base}-${count}` : base,
			level: match[1].length,
			start_line: i + 1,
			end_line: lines.length,
		});
	}
	for (let i = 0; i < result.length; i++) {
		const next = result.slice(i + 1).find((section) => section.level <= result[i].level);
		if (next) result[i].end_line = next.start_line - 1;
	}
	return result;
}

function metadata(doc: Document) {
	return {
		path: doc.path,
		title: doc.title,
		kind: doc.kind,
		languages: doc.languages,
		sha256: doc.sha256,
	};
}

export function createCatalog(bundle: Bundle) {
	const documents = bundle.documents.map((doc) => ({
		...doc,
		lines: doc.text.split("\n"),
		sections: doc.path.endsWith(".md") ? sections(doc.text) : [],
	}));
	const byPath = new Map(documents.map((doc) => [doc.path, doc]));
	const chunks = documents.flatMap((doc) => {
		const starts = [...new Set([1, ...doc.sections.map((section) => section.start_line)])].sort(
			(a, b) => a - b,
		);
		return starts.flatMap((start, i) => {
			const end = (starts[i + 1] ?? doc.lines.length + 1) - 1;
			const heading =
				[...doc.sections].reverse().find((section) => section.start_line <= start)
					?.heading ?? doc.title;
			const pieces = [];
			for (let line = start; line <= end; line += 80) {
				const endLine = Math.min(line + 79, end);
				const text = doc.lines.slice(line - 1, endLine).join("\n");
				const terms = tokens(`${doc.path} ${doc.title} ${heading} ${text}`);
				const counts = new Map<string, number>();
				for (const term of terms) counts.set(term, (counts.get(term) ?? 0) + 1);
				pieces.push({
					doc,
					heading,
					start_line: line,
					end_line: endLine,
					text,
					terms: counts,
					length: terms.length,
				});
			}
			return pieces;
		});
	});
	const frequencies = new Map<string, number>();
	for (const chunk of chunks)
		for (const term of chunk.terms.keys())
			frequencies.set(term, (frequencies.get(term) ?? 0) + 1);
	const averageLength =
		chunks.reduce((sum, chunk) => sum + chunk.length, 0) / Math.max(1, chunks.length);
	const matches = (doc: Document, filter: Filter) =>
		(!filter.language || doc.languages.includes(filter.language)) &&
		(!filter.kind || doc.kind === filter.kind);

	return {
		list(filter: Filter & { include_sections?: boolean } = {}) {
			return {
				snapshot: bundle.snapshot,
				documents: documents
					.filter((doc) => matches(doc, filter))
					.map((doc) => ({
						...metadata(doc),
						total_lines: doc.lines.length,
						...(filter.include_sections === false
							? {}
							: {
									sections: doc.sections.map(({ heading, anchor }) => ({
										heading,
										anchor,
									})),
								}),
					})),
			};
		},
		search(query: string, filter: Filter & { limit?: number } = {}) {
			if (!query.trim() || query.length > 300)
				throw new Error("Query must contain 1 to 300 characters.");
			if (
				filter.limit !== undefined &&
				(!Number.isInteger(filter.limit) || filter.limit < 1 || filter.limit > 10)
			)
				throw new Error("limit must be an integer from 1 to 10.");
			const terms = [...new Set(tokens(query).filter((term) => !stopWords.has(term)))];
			const scored = chunks
				.filter((chunk) => matches(chunk.doc, filter))
				.map((chunk) => {
					let score = 0;
					for (const term of terms) {
						const tf = chunk.terms.get(term) ?? 0;
						if (!tf) continue;
						const df = frequencies.get(term) ?? 0;
						const idf = Math.log(1 + (chunks.length - df + 0.5) / (df + 0.5));
						score +=
							(idf * (tf * 2.2)) /
							(tf + 1.2 * (0.25 + (0.75 * chunk.length) / averageLength));
						if (tokens(`${chunk.doc.title} ${chunk.heading}`).includes(term))
							score += idf * 2;
					}
					return { chunk, score };
				})
				.filter(({ score }) => score > 0)
				.sort(
					(a, b) =>
						b.score - a.score ||
						a.chunk.doc.path.localeCompare(b.chunk.doc.path) ||
						a.chunk.start_line - b.chunk.start_line,
				);
			return {
				snapshot: bundle.snapshot,
				query,
				results: scored.slice(0, filter.limit ?? 5).map(({ chunk, score }) => {
					const lower = chunk.text.toLowerCase();
					const positions = terms
						.map((term) => lower.indexOf(term))
						.filter((pos) => pos >= 0);
					const start = Math.max(
						0,
						(positions.length ? Math.min(...positions) : 0) - 120,
					);
					return {
						...metadata(chunk.doc),
						section: chunk.heading,
						start_line: chunk.start_line,
						end_line: chunk.end_line,
						excerpt: chunk.text.slice(start, start + 900),
						score: Number(score.toFixed(3)),
					};
				}),
			};
		},
		read(
			path: string,
			options: { section?: string; start_line?: number; max_lines?: number } = {},
		) {
			const doc = byPath.get(path);
			if (!doc)
				throw new Error(
					"Unknown document path. Use list_docs or search_docs for an exact bundled path.",
				);
			const requestedSection = options.section;
			const section =
				requestedSection === undefined
					? undefined
					: doc.sections.find(
							(item) =>
								item.anchor === requestedSection.replace(/^#/, "") ||
								item.heading.toLowerCase() === requestedSection.toLowerCase(),
						);
			if (options.section !== undefined && !section)
				throw new Error(
					"Unknown section. Use list_docs to see the document's headings and anchors.",
				);
			const first = section?.start_line ?? 1;
			const last = section?.end_line ?? doc.lines.length;
			const start = options.start_line ?? first;
			const max = options.max_lines ?? 120;
			if (!Number.isInteger(start) || start < first || start > last)
				throw new Error(
					`start_line must be between ${first} and ${last} for this document/section.`,
				);
			if (!Number.isInteger(max) || max < 1 || max > 200)
				throw new Error("max_lines must be an integer from 1 to 200.");
			const end = Math.min(start + max - 1, last);
			return {
				snapshot: bundle.snapshot,
				...metadata(doc),
				section: section?.heading ?? null,
				start_line: start,
				end_line: end,
				total_lines: doc.lines.length,
				next_start_line: end < last ? end + 1 : null,
				text: doc.lines.slice(start - 1, end).join("\n"),
			};
		},
	};
}
