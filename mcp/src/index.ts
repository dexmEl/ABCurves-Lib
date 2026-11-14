import { McpServer } from "@modelcontextprotocol/server";
import { createMcpHandler } from "agents/mcp/server";
import { z } from "zod";
import bundle from "../build/docs.json";
import { createCatalog, type Bundle } from "./catalog";

const catalog = createCatalog(bundle as Bundle);
const language = z.enum(["c", "cpp", "python", "rust"]).optional();
const kind = z.enum(["guide", "example", "reference"]).optional();
const annotations = {
	readOnlyHint: true,
	destructiveHint: false,
	idempotentHint: true,
	openWorldHint: false,
};

function result(action: () => Record<string, unknown>) {
	try {
		const value = action();
		return {
			content: [{ type: "text" as const, text: JSON.stringify(value) }],
			structuredContent: value,
		};
	} catch (error) {
		return {
			isError: true,
			content: [
				{
					type: "text" as const,
					text: JSON.stringify({
						error:
							error instanceof Error
								? error.message
								: "Unable to read documentation.",
						snapshot: bundle.snapshot,
					}),
				},
			],
		};
	}
}

function createServer() {
	const server = new McpServer(
		{ name: "ABCurves Documentation", version: "0.1.0" },
		{
			instructions: [
				"For an ABCurves integration request, first read AGENTS.md using read_doc, following next_start_line until complete.",
				"Follow its branching questions about the 8/16/32 ms model, calibration, language, platform and timing; reuse answers already known from the user or project.",
				"Then read the chosen language guide and docs/guides/integration-validation.md. Verify exact signatures in bundled public headers/bindings and examples.",
				"Use list_docs/search_docs for exact paths and section anchors. read_doc is paginated; next_start_line indicates more content.",
				"This server serves one bundled documentation snapshot. Check its library_version, abi_version and content_sha256 against the user's installed library; it cannot fetch other versions.",
				"The agent edits, builds and tests in the user's local project with its own tools. This MCP provides read-only context and never runs code or measures device calibration.",
			].join(" "),
		},
	);

	server.registerTool(
		"list_docs",
		{
			title: "List ABCurves documentation",
			description:
				"Discover bundled guides, examples, public declarations and supporting references. Returns exact paths and Markdown headings/anchors by default; set include_sections=false for compact discovery. Shared guides remain included when filtering by language. Start integration with AGENTS.md.",
			inputSchema: z.object({ language, kind, include_sections: z.boolean().optional() }),
			annotations,
		},
		async (filter) => result(() => catalog.list(filter)),
	);

	server.registerTool(
		"search_docs",
		{
			title: "Search ABCurves documentation",
			description:
				"Search the bundled library documentation and source examples. Returns ranked excerpts with exact paths, source line ranges and snapshot metadata. limit is an integer from 1 to 10 (default 5). Use read_doc for full context and exact API declarations.",
			inputSchema: z.object({
				query: z.string().trim().min(1).max(300),
				language,
				kind,
				limit: z.number().int().min(1).max(10).optional(),
			}),
			annotations,
		},
		async ({ query, ...filter }) => result(() => catalog.search(query, filter)),
	);

	server.registerTool(
		"read_doc",
		{
			title: "Read ABCurves documentation",
			description:
				"Read an exact bundled path, optionally selecting a Markdown heading or anchor. start_line is a 1-based absolute source line, including within sections. max_lines is an integer from 1 to 200 (default 120). Follow next_start_line with the same path/section to continue. No filesystem or URL access.",
			inputSchema: z.object({
				path: z.string().min(1).max(300),
				section: z.string().min(1).max(300).optional(),
				start_line: z.number().int().min(1).optional(),
				max_lines: z.number().int().min(1).max(200).optional(),
			}),
			annotations,
		},
		async ({ path, ...options }) => result(() => catalog.read(path, options)),
	);

	return server;
}

const endpointPath = "/abcurves";
const handler = createMcpHandler(createServer, {
	route: endpointPath,
	allowedHostnames: ["mcp.nanoed.net"],
	allowedOriginHostnames: ["mcp.nanoed.net"],
});

export default {
	fetch(request: Request, env: Env, ctx: ExecutionContext) {
		if (new URL(request.url).pathname !== endpointPath) {
			return new Response(`ABCurves documentation MCP endpoint: ${endpointPath}\n`, {
				status: 404,
			});
		}
		return handler(request, env, ctx);
	},
} satisfies ExportedHandler<Env>;
