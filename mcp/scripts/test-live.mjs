import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const endpoint = process.argv[2] ?? "http://127.0.0.1:8787/abcurves";
const bundle = JSON.parse(readFileSync(new URL("../build/docs.json", import.meta.url), "utf8"));
let requestId = 0;

async function rpc(method, params) {
	const id = ++requestId;
	const response = await fetch(endpoint, {
		method: "POST",
		headers: {
			"Content-Type": "application/json",
			Accept: "application/json, text/event-stream",
			"MCP-Protocol-Version": "2025-11-25",
		},
		body: JSON.stringify({ jsonrpc: "2.0", id, method, params }),
		signal: AbortSignal.timeout(15000),
	});
	const body = await response.text();
	assert.ok(response.ok, `${method}: HTTP ${response.status}: ${body.slice(0, 500)}`);
	const messages = response.headers.get("content-type")?.includes("text/event-stream")
		? body
				.split(/\r?\n\r?\n/)
				.filter((event) => /^data:/m.test(event))
				.map((event) =>
					JSON.parse(
						event
							.split(/\r?\n/)
							.filter((line) => line.startsWith("data:"))
							.map((line) => line.slice(5).trimStart())
							.join("\n"),
					),
				)
		: [JSON.parse(body)];
	const message = messages.find((message) => message.id === id);
	assert.ok(message, `No matching response for ${method}`);
	assert.equal(message.error, undefined, JSON.stringify(message.error));
	return message.result;
}

function data(result) {
	assert.ok(!result.isError, JSON.stringify(result.content));
	return (
		result.structuredContent ??
		JSON.parse(result.content.find((item) => item.type === "text").text)
	);
}

const init = await rpc("initialize", {
	protocolVersion: "2025-11-25",
	capabilities: {},
	clientInfo: { name: "abcurves-local-check", version: "0.1.0" },
});
assert.equal(init.serverInfo.name, "ABCurves Documentation");
assert.match(init.instructions, /AGENTS\.md/);
const tools = (await rpc("tools/list", {})).tools;
assert.deepEqual(tools.map((tool) => tool.name).sort(), ["list_docs", "read_doc", "search_docs"]);
assert.ok(
	tools.every((tool) => tool.annotations.readOnlyHint && !tool.annotations.destructiveHint),
);
const call = (name, args) => rpc("tools/call", { name, arguments: args });
const full = data(await call("list_docs", {}));
const compact = data(await call("list_docs", { include_sections: false }));
assert.deepEqual(
	compact.snapshot,
	bundle.snapshot,
	"The server must serve the current local bundle",
);
assert.deepEqual(compact.snapshot, full.snapshot);
assert.deepEqual(
	compact.documents,
	full.documents.map(({ sections: _sections, ...doc }) => doc),
);
assert.ok(compact.documents.every((doc) => !Object.hasOwn(doc, "sections")));
assert.ok(full.documents.some((doc) => doc.sections.length > 0));
assert.equal(compact.documents.length, bundle.documents.length);
for (const path of [
	"Native/DATASET_LICENSE.md",
	"mcp/README.md",
	"docs/evidence/reference-accuracy.json",
	"tools/test_runtime_package.py",
]) {
	const reference = data(await call("read_doc", { path, max_lines: 10 }));
	const local = bundle.documents.find((doc) => doc.path === path);
	assert.equal(reference.sha256, local.sha256);
	assert.equal(reference.text, local.text.split("\n").slice(0, 10).join("\n"));
}
const docs = data(await call("list_docs", { language: "rust", kind: "example" }));
assert.ok(docs.documents.some((doc) => doc.path === "API/rust/abcurves/examples/segments.rs"));
const search = data(await call("search_docs", { query: "calibration", limit: 10 }));
assert.ok(search.results.some((doc) => doc.path === "docs/guides/calibration.md"));
const guide = data(await call("read_doc", { path: "AGENTS.md", max_lines: 10 }));
assert.ok(guide.text.startsWith("# ABCurves integration assistant"));
assert.equal(guide.next_start_line, 11);
assert.equal(search.snapshot.content_sha256, guide.snapshot.content_sha256);
const validation = data(
	await call("read_doc", {
		path: "docs/guides/integration-validation.md",
		section: "2-establish-a-numerical-smoke-test",
	}),
);
assert.match(validation.text, /abc_segment_c_example/);
assert.match(validation.text, /cargo run/);
const missing = await call("read_doc", { path: "../.env" });
assert.equal(missing.isError, true);
assert.match(missing.content[0].text, /Unknown document path/);
console.log(
	`Live MCP checks passed: ${endpoint} (${compact.documents.length} documents, ${guide.snapshot.library_version}, ${guide.snapshot.content_sha256.slice(0, 12)}).`,
);
