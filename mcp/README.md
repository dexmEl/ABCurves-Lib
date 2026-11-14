# ABCurves documentation MCP

A read-only Cloudflare Worker that gives an implementing agent ABCurves documentation, the branching integration conversation, runnable examples and exact public declarations. The agent implements and tests in the user's local project using its own tools. This server does not execute code, inspect a user's project, control devices or call an LLM.

## Run locally

Use Node.js >=24 and npm. Keep this directory inside the ABCurves source checkout: the bundle is generated from its canonical documentation and bindings.

```powershell
cd mcp
npm ci
npm run dev
```

Connect an MCP client that supports remote Streamable HTTP to `http://127.0.0.1:8787/abcurves`. Wrangler's root URL is not the MCP endpoint. A hosted client needs a deployed, reachable HTTPS endpoint; its own localhost is not your machine.

For an integration request, ask the connected agent to read `AGENTS.md` through `read_doc`, follow its questions and use the selected language guide and validation guide. Server instructions point to that workflow, but the client agent is responsible for following it.

## Tools

| Tool          | Purpose                                                         | Example arguments                          |
| ------------- | --------------------------------------------------------------- | ------------------------------------------ |
| `list_docs`   | Discover exact paths, hashes, language tags and section anchors | `{"language":"python","kind":"example"}`   |
| `search_docs` | Ranked excerpts with source line ranges                         | `{"query":"output calibration","limit":5}` |
| `read_doc`    | Read a file or Markdown section, with pagination                | `{"path":"AGENTS.md","max_lines":120}`     |

Language filters accept `c`, `cpp`, `python` and `rust`. Kind filters accept `guide`, `example` and `reference`. Shared guidance is included for every language. For compact discovery, call `list_docs` with `{"include_sections":false}` to omit headings and anchors; they remain included by default. Another language can use the documented C ABI through an application-owned adapter; there is no automatic binding generator.

Search returns at most 10 excerpts, each at most 900 characters. Read the source for full context. `read_doc` defaults to 120 lines and allows at most 200. Follow `next_start_line` with the same path and section until it is null. Lines are 1-based absolute source lines even when a section is selected. Section accepts an exact heading or a listed anchor (optionally prefixed with `#`). Paths must exactly match the bundled catalog.

## Documentation snapshot

`npm run docs:build` creates the ignored `build/docs.json` from an explicit set of public root documents, public Markdown guides, language examples, C/C++ headers and Python/Rust binding sources. It also includes the linked dataset license, retained JSON evidence, package manifest, validation scripts, comparison-site documentation and MCP setup/build references. Evidence files are individually selected; qualification directories, model binaries, private files and installed dependencies are excluded. See [the build script](scripts/build-docs.mjs) for the source selection.

Wrangler runs that command before local builds and deployment. After editing repository documentation outside `mcp/src`, restart `npm run dev` to rebuild and reload the live bundle. `npm run docs:build` refreshes the catalog used by tests and the next Worker build. Compare the returned `content_sha256` to confirm which snapshot a client is reading. The server never fetches arbitrary files or URLs at request time.

Every result includes the library version, ABI version, source Git revision/modified status and a SHA-256 content identity. Individual documents have hashes too. The content identity is deterministic for unchanged normalized source content. Git fields are null when building without Git metadata. A modified checkout is identified as modified; its commit alone does not describe its content.

One deployment serves one snapshot. Compare it with the user's installed release before applying APIs; older release selection is not implemented. Update canonical docs first and rebuild/deploy the MCP with the corresponding library release to keep them aligned.

## Verify

From `mcp/`:

```powershell
npm run type-check
npm test
npm run lint
npm run test:live
npx wrangler deploy --dry-run --outdir build/worker
```

The live check requires Wrangler to be running, and accepts a different endpoint: `npm run test:live -- https://mcp.nanoed.net/abcurves`. It verifies the MCP handshake, tool discovery, compact listing, language filtering, search, document/section reading, pagination and unknown-path errors. The catalog tests verify the real bundled corpus, internal Markdown links, content hashes, C-symbol search and retrieval behavior. Every relative Markdown link must resolve to a bundled document; source-only directories use explicit repository URLs. These checks validate the documentation service; the [integration validation guide](../docs/guides/integration-validation.md) describes the tests an agent runs in a consumer's project.

## Deploy when ready

The configured public endpoint is `https://mcp.nanoed.net/abcurves`. The active `nanoed.net` zone must belong to the Cloudflare account you deploy to. [wrangler.jsonc](wrangler.jsonc) contains this Custom Domain setting:

```jsonc
"routes": [
	{ "pattern": "mcp.nanoed.net", "custom_domain": true }
]
```

The pattern is the hostname only; the Worker handles the `/abcurves` path. Cloudflare creates the DNS record and HTTPS certificate. A Custom Domain cannot be created over an existing CNAME record. See [Cloudflare Custom Domains](https://developers.cloudflare.com/workers/configuration/routing/custom-domains/) for setup requirements. For another domain, update the Wrangler route and the hostname/origin allowlists in `src/index.ts` together.

After local verification, log in to the account containing that domain and deploy from `mcp/`:

```powershell
npx wrangler login
npm run deploy
npm run test:live -- https://mcp.nanoed.net/abcurves
```

Connect clients to `https://mcp.nanoed.net/abcurves`. The Worker stays online independently of your local Wrangler process. Documentation changes require another deployment; the hosted server serves its bundled snapshot.

This starter is public and has no authentication; bundle only the intended public integration material. It uses Cloudflare's stateless MCP handler and needs no database, model inference service or Durable Object.
