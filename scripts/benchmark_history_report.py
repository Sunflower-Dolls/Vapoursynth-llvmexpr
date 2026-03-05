#!/usr/bin/env python3

from __future__ import annotations

import argparse
import io
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def _iso_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _iso_to_datetime(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


class GitHubApi:
    def __init__(self, repo: str, token: str) -> None:
        if "/" not in repo:
            raise ValueError(f"Invalid repo '{repo}'. Expected owner/repo format.")
        self.repo = repo
        self.base = f"https://api.github.com/repos/{repo}"
        self._token = token
        self.headers = {
          "Accept": "application/vnd.github+json",
          "Authorization": f"Bearer {token}",
          "X-GitHub-Api-Version": "2022-11-28",
          "User-Agent": "benchmark-history-report",
        }

    def _request_json(self, url: str) -> dict[str, Any]:
        request = urllib.request.Request(url=url, headers=self.headers)
        with urllib.request.urlopen(request) as response:
            return json.loads(response.read().decode("utf-8"))

    def _request_bytes(self, url: str) -> bytes:
        request = urllib.request.Request(url=url, headers=self.headers)
        with urllib.request.urlopen(request) as response:
            return response.read()

    @staticmethod
    def _is_github_owned_host(hostname: str) -> bool:
        hostname = hostname.lower().strip(".")
        return hostname.endswith("github.com") or hostname.endswith(
            "githubusercontent.com"
        )

    def _request_bytes_follow_redirects(self, url: str) -> bytes:
        class _NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, req, fp, code, msg, hdrs, newurl):
                return None

        opener = urllib.request.build_opener(_NoRedirect())
        request = urllib.request.Request(url=url, headers=self.headers)

        try:
            with opener.open(request) as response:
                return response.read()
        except urllib.error.HTTPError as exc:
            if exc.code not in (301, 302, 303, 307, 308):
                raise

            location = exc.headers.get("Location")
            if not location:
                raise

            redirect_url = urllib.parse.urljoin(url, location)
            redirect_host = urllib.parse.urlparse(redirect_url).hostname or ""

            attempts: list[dict[str, str]] = [
                {
                    "User-Agent": self.headers["User-Agent"],
                }
            ]
            if self._is_github_owned_host(redirect_host):
                attempts.append(self.headers)

            last_exc: Exception | None = None
            for headers in attempts:
                try:
                    follow_request = urllib.request.Request(
                        url=redirect_url, headers=headers
                    )
                    with urllib.request.urlopen(follow_request) as response:
                        return response.read()
                except urllib.error.HTTPError as follow_exc:
                    last_exc = follow_exc
                    if follow_exc.code not in (401, 403):
                        raise

            if last_exc:
                raise last_exc
            raise

    def list_workflow_runs(
        self,
        workflow: str,
        branch: str,
        event: str,
        max_runs: int,
    ) -> list[dict[str, Any]]:
        runs: list[dict[str, Any]] = []
        page = 1

        while max_runs <= 0 or len(runs) < max_runs:
            query = {
                "per_page": "100",
                "page": str(page),
                "status": "completed",
            }
            if branch:
                query["branch"] = branch
            if event:
                query["event"] = event

            encoded = urllib.parse.urlencode(query)
            workflow_name = urllib.parse.quote(workflow, safe="")
            url = f"{self.base}/actions/workflows/{workflow_name}/runs?{encoded}"
            payload = self._request_json(url)
            page_runs = payload.get("workflow_runs", [])
            if not page_runs:
                break

            for run in page_runs:
                if run.get("conclusion") == "success":
                    runs.append(run)
                    if max_runs > 0 and len(runs) >= max_runs:
                        break

            if len(page_runs) < 100:
                break
            page += 1

        return runs

    def list_run_artifacts(self, run_id: int) -> list[dict[str, Any]]:
        artifacts: list[dict[str, Any]] = []
        page = 1
        while True:
            query = urllib.parse.urlencode({"per_page": "100", "page": str(page)})
            url = f"{self.base}/actions/runs/{run_id}/artifacts?{query}"
            payload = self._request_json(url)
            page_items = payload.get("artifacts", [])
            if not page_items:
                break
            artifacts.extend(page_items)
            if len(page_items) < 100:
                break
            page += 1
        return artifacts

    def download_artifact_json(self, url: str) -> dict[str, Any] | None:
        try:
            artifact_zip = self._request_bytes_follow_redirects(url)
        except urllib.error.HTTPError as exc:
            print(
                f"Warning: failed to download artifact: HTTP {exc.code}",
                file=sys.stderr,
            )
            return None
        except urllib.error.URLError as exc:
            print(
                f"Warning: failed to download artifact: {exc.reason}", file=sys.stderr
            )
            return None

        with zipfile.ZipFile(io.BytesIO(artifact_zip), "r") as archive:
            candidate = None
            for name in archive.namelist():
                if name.endswith(".json"):
                    candidate = name
                    break
            if candidate is None:
                return None
            raw = archive.read(candidate).decode("utf-8")
            return json.loads(raw)


def _float_dict(value: Any) -> dict[str, float]:
    if not isinstance(value, dict):
        return {}
    out: dict[str, float] = {}
    for key, item in value.items():
        if isinstance(item, (float, int)):
            out[str(key)] = float(item)
    return out


def _extract_ok_fps(results: Any) -> dict[str, dict[str, float]]:
    if not isinstance(results, dict):
        return {}

    extracted: dict[str, dict[str, float]] = {}
    for test_name, backend_map in results.items():
        if not isinstance(backend_map, dict):
            continue
        backend_fps: dict[str, float] = {}
        for backend, result in backend_map.items():
            if not isinstance(result, dict):
                continue
            if result.get("status") != "ok":
                continue
            fps = result.get("fps")
            if isinstance(fps, (float, int)):
                backend_fps[str(backend)] = float(fps)
        if backend_fps:
            extracted[str(test_name)] = backend_fps
    return extracted


def _target_from_artifact_name(artifact_name: str, prefix: str) -> str:
    if artifact_name.startswith(prefix):
        stripped = artifact_name[len(prefix) :]
        if stripped:
            return stripped
    return artifact_name


def _latest_artifacts_by_name(
    artifacts: list[dict[str, Any]],
    prefix: str,
) -> list[dict[str, Any]]:
    latest: dict[str, dict[str, Any]] = {}
    for artifact in artifacts:
        name = str(artifact.get("name", ""))
        if not name.startswith(prefix):
            continue
        if artifact.get("expired"):
            continue

        previous = latest.get(name)
        if previous is None:
            latest[name] = artifact
            continue

        created_at = str(artifact.get("created_at", ""))
        previous_created_at = str(previous.get("created_at", ""))
        if created_at > previous_created_at:
            latest[name] = artifact

    return list(latest.values())


def build_history_payload(
    *,
    repo: str,
    workflow: str,
    branch: str,
    event: str,
    artifact_prefix: str,
    runs_scanned: int,
    successful_runs: int,
    entries: list[dict[str, Any]],
) -> dict[str, Any]:
    first_commit_time = entries[0]["run_created_at"] if entries else None
    last_commit_time = entries[-1]["run_created_at"] if entries else None
    first_commit_sha = entries[0]["head_sha"] if entries else None
    last_commit_sha = entries[-1]["head_sha"] if entries else None

    return {
        "generated_at": _iso_now(),
        "repository": repo,
        "source_workflow": workflow,
        "filters": {
            "branch": branch,
            "event": event,
            "artifact_prefix": artifact_prefix,
        },
        "scan": {
            "runs_scanned": runs_scanned,
            "successful_runs": successful_runs,
            "benchmark_entries": len(entries),
        },
        "range": {
            "first_commit_time": first_commit_time,
            "last_commit_time": last_commit_time,
            "first_commit_sha": first_commit_sha,
            "last_commit_sha": last_commit_sha,
        },
        "entries": entries,
    }


def render_html(payload: dict[str, Any]) -> str:
    data_json = json.dumps(payload, separators=(",", ":"), ensure_ascii=True).replace(
        "</", "<\\/"
    )
    return """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Benchmark History Report</title>
  <script src="https://cdn.plot.ly/plotly-2.35.2.min.js"></script>
  <style>
    :root {
      color-scheme: light;
      --bg: #f7fafc;
      --card: #ffffff;
      --line: #d0d7de;
      --text: #1f2937;
      --muted: #4b5563;
      --accent: #065f46;
    }
    * {
      box-sizing: border-box;
    }
    body {
      margin: 0;
      font-family: "SF Pro Text", "Segoe UI", "Noto Sans", sans-serif;
      background: radial-gradient(circle at top right, #d1fae5, var(--bg) 36%);
      color: var(--text);
      padding: 24px;
    }
    .container {
      max-width: 1440px;
      margin: 0 auto;
    }
    .card {
      background: var(--card);
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 16px 18px;
      margin-bottom: 16px;
    }
    h1, h2 {
      margin: 0 0 12px;
    }
    h1 {
      font-size: 1.5rem;
    }
    h2 {
      font-size: 1.1rem;
    }
    .meta {
      color: var(--muted);
      line-height: 1.5;
      margin: 0;
      white-space: pre-line;
    }
    .chart {
      width: 100%;
      min-height: 430px;
    }
    .controls {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      margin-bottom: 10px;
    }
    label {
      font-size: 0.9rem;
      color: var(--muted);
      display: grid;
      gap: 4px;
    }
    select {
      min-width: 220px;
      padding: 6px 8px;
      border-radius: 8px;
      border: 1px solid var(--line);
      background: #fff;
      color: var(--text);
    }
    table {
      width: 100%;
      border-collapse: collapse;
      font-size: 0.9rem;
    }
    th, td {
      border: 1px solid var(--line);
      padding: 8px 10px;
      text-align: left;
    }
    th {
      background: #eff6ff;
      color: #1e3a8a;
    }
    .positive {
      color: #065f46;
      font-weight: 600;
    }
    .negative {
      color: #991b1b;
      font-weight: 600;
    }
    .neutral {
      color: var(--muted);
      font-weight: 600;
    }
    a {
      color: var(--accent);
      text-decoration: none;
    }
    a:hover {
      text-decoration: underline;
    }
  </style>
</head>
<body>
  <div class="container">
    <section class="card">
      <h1>Benchmark History Report</h1>
      <p class="meta" id="metaSummary"></p>
    </section>

    <section class="card">
      <h2>Geometric Mean FPS Trend</h2>
      <div id="gmChart" class="chart"></div>
    </section>

    <section class="card">
      <h2>Normalized Trend (vs First Data Point)</h2>
      <div id="ratioChart" class="chart"></div>
    </section>

    <section class="card">
      <h2>Per-Test FPS Trend</h2>
      <div class="controls">
        <label>Test
          <select id="testSelect"></select>
        </label>
        <label>Backend
          <select id="backendSelect"></select>
        </label>
      </div>
      <div id="testChart" class="chart"></div>
    </section>

    <section class="card">
      <h2>Latest Snapshot</h2>
      <table>
        <thead>
          <tr>
            <th>Series</th>
            <th>Latest FPS</th>
            <th>Tracked Commits</th>
            <th>Last Commit</th>
            <th>Change vs Previous</th>
            <th>Change vs First</th>
          </tr>
        </thead>
        <tbody id="latestTableBody"></tbody>
      </table>
    </section>
  </div>

  <script>
    const REPORT = __REPORT_DATA__;

    const entries = (REPORT.entries || []).slice().sort((a, b) => {
      if (a.run_created_at === b.run_created_at) {
        if (a.run_id === b.run_id) {
          return String(a.artifact_name || "").localeCompare(String(b.artifact_name || ""));
        }
        return Number(a.run_id || 0) - Number(b.run_id || 0);
      }
      return new Date(a.run_created_at) - new Date(b.run_created_at);
    });

    function shortSha(sha) {
      return sha ? String(sha).slice(0, 7) : "unknown";
    }

    function formatPct(value) {
      if (!Number.isFinite(value)) {
        return "<span class='neutral'>N/A</span>";
      }
      const cls = value > 0 ? "positive" : value < 0 ? "negative" : "neutral";
      const sign = value > 0 ? "+" : "";
      return `<span class="${cls}">${sign}${value.toFixed(2)}%</span>`;
    }

    function escapeHtml(text) {
      return String(text)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#39;");
    }

    function groupGeometricMean() {
      const grouped = {};
      for (const entry of entries) {
        const gm = entry.geometric_mean_fps || {};
        const target = entry.target || entry.artifact_name || "unknown";
        for (const [backend, fps] of Object.entries(gm)) {
          if (!Number.isFinite(fps)) {
            continue;
          }
          const series = `${target} | ${backend}`;
          if (!grouped[series]) {
            grouped[series] = [];
          }
          grouped[series].push({
            x: entry.run_created_at,
            y: fps,
            target,
            backend,
            runId: entry.run_id,
            runNumber: entry.run_number,
            runUrl: entry.run_url,
            commit: shortSha(entry.head_sha),
          });
        }
      }
      return grouped;
    }

    function buildSeriesTraces(grouped, normalize) {
      const traces = [];
      const names = Object.keys(grouped).sort();
      for (const name of names) {
        const points = grouped[name];
        if (!points || points.length === 0) {
          continue;
        }
        points.sort((a, b) => new Date(a.x) - new Date(b.x));
        const baseline = points[0].y;
        const y = points.map((point) => normalize ? point.y / baseline : point.y);
        const text = points.map((point, index) => {
          const value = y[index];
          const metric = normalize ? `${value.toFixed(3)}x` : `${value.toFixed(2)} FPS`;
          return [
            `${escapeHtml(name)}`,
            `Commit: ${point.commit}`,
            `Run #${point.runNumber} (${point.runId})`,
            metric
          ].join("<br>");
        });
        traces.push({
          name,
          mode: "lines+markers",
          x: points.map((point) => point.x),
          y,
          text,
          hovertemplate: "%{text}<extra></extra>",
        });
      }
      return traces;
    }

    function renderMainCharts(grouped) {
      const gmTraces = buildSeriesTraces(grouped, false);
      Plotly.newPlot(
        "gmChart",
        gmTraces,
        {
          margin: { l: 64, r: 24, t: 24, b: 48 },
          xaxis: { title: "Commit Time" },
          yaxis: { title: "Geometric Mean FPS" },
          legend: { orientation: "h" },
          hovermode: "x unified",
        },
        { responsive: true, displaylogo: false }
      );

      const ratioTraces = buildSeriesTraces(grouped, true);
      Plotly.newPlot(
        "ratioChart",
        ratioTraces,
        {
          margin: { l: 64, r: 24, t: 24, b: 48 },
          xaxis: { title: "Commit Time" },
          yaxis: { title: "Relative Performance (x)" },
          legend: { orientation: "h" },
          hovermode: "x unified",
        },
        { responsive: true, displaylogo: false }
      );
    }

    function listTests() {
      const found = new Set();
      for (const entry of entries) {
        const tests = entry.tests || {};
        for (const test of Object.keys(tests)) {
          found.add(test);
        }
      }
      return Array.from(found).sort();
    }

    function listBackendsForTest(testName) {
      const found = new Set();
      for (const entry of entries) {
        const tests = entry.tests || {};
        const backendMap = tests[testName] || {};
        for (const backend of Object.keys(backendMap)) {
          found.add(backend);
        }
      }
      return Array.from(found).sort();
    }

    function renderTestChart(testName, backendName) {
      const grouped = {};
      for (const entry of entries) {
        const tests = entry.tests || {};
        const backendMap = tests[testName] || {};
        const fps = backendMap[backendName];
        if (!Number.isFinite(fps)) {
          continue;
        }
        const target = entry.target || entry.artifact_name || "unknown";
        if (!grouped[target]) {
          grouped[target] = [];
        }
        grouped[target].push({
          x: entry.run_created_at,
          y: fps,
          commit: shortSha(entry.head_sha),
          runId: entry.run_id,
          runNumber: entry.run_number,
        });
      }

      const traces = [];
      const targets = Object.keys(grouped).sort();
      for (const target of targets) {
        const points = grouped[target].sort((a, b) => new Date(a.x) - new Date(b.x));
        traces.push({
          name: target,
          mode: "lines+markers",
          x: points.map((point) => point.x),
          y: points.map((point) => point.y),
          text: points.map((point) =>
            [
              `${escapeHtml(target)} | ${escapeHtml(backendName)}`,
              `Commit: ${point.commit}`,
              `Run #${point.runNumber} (${point.runId})`,
              `${point.y.toFixed(2)} FPS`,
            ].join("<br>")
          ),
          hovertemplate: "%{text}<extra></extra>",
        });
      }

      Plotly.newPlot(
        "testChart",
        traces,
        {
          title: `${testName} (${backendName})`,
          margin: { l: 64, r: 24, t: 42, b: 48 },
          xaxis: { title: "Commit Time" },
          yaxis: { title: "FPS" },
          legend: { orientation: "h" },
          hovermode: "x unified",
        },
        { responsive: true, displaylogo: false }
      );
    }

    function renderLatestTable(grouped) {
      const rows = [];
      const seriesNames = Object.keys(grouped).sort();
      for (const name of seriesNames) {
        const points = grouped[name].slice().sort((a, b) => new Date(a.x) - new Date(b.x));
        if (!points.length) {
          continue;
        }
        const latest = points[points.length - 1];
        const previous = points.length > 1 ? points[points.length - 2] : null;
        const first = points[0];

        const vsPrev = previous ? ((latest.y - previous.y) / previous.y) * 100 : NaN;
        const vsFirst = first ? ((latest.y - first.y) / first.y) * 100 : NaN;
        rows.push({
          series: name,
          latestFps: latest.y,
          commitCount: points.length,
          lastCommit: latest.commit,
          vsPrev,
          vsFirst,
        });
      }

      const body = document.getElementById("latestTableBody");
      body.innerHTML = rows.map((row) => `
        <tr>
          <td>${escapeHtml(row.series)}</td>
          <td>${row.latestFps.toFixed(2)}</td>
          <td>${row.commitCount}</td>
          <td><code>${escapeHtml(row.lastCommit)}</code></td>
          <td>${formatPct(row.vsPrev)}</td>
          <td>${formatPct(row.vsFirst)}</td>
        </tr>
      `).join("");
    }

    function renderSummary() {
      const meta = document.getElementById("metaSummary");
      const scan = REPORT.scan || {};
      const range = REPORT.range || {};
      const lines = [
        `Repository: ${REPORT.repository || "unknown"}`,
        `Workflow: ${REPORT.source_workflow || "unknown"} | Branch: ${(REPORT.filters || {}).branch || "any"} | Event: ${(REPORT.filters || {}).event || "any"}`,
        `Successful runs scanned: ${scan.successful_runs || 0}`,
        `Benchmark entries loaded: ${scan.benchmark_entries || 0}`,
        `Range: ${(range.first_commit_time || "N/A")} -> ${(range.last_commit_time || "N/A")}`,
        `Commit range: ${shortSha(range.first_commit_sha)} -> ${shortSha(range.last_commit_sha)}`,
        `Report generated at: ${REPORT.generated_at || "unknown"}`
      ];
      meta.textContent = lines.join("\\n");
    }

    function render() {
      renderSummary();

      if (entries.length === 0) {
        document.getElementById("gmChart").innerHTML = "<p>No benchmark data artifacts were found.</p>";
        document.getElementById("ratioChart").innerHTML = "<p>No benchmark data artifacts were found.</p>";
        document.getElementById("testChart").innerHTML = "<p>No benchmark data artifacts were found.</p>";
        return;
      }

      const gmGrouped = groupGeometricMean();
      renderMainCharts(gmGrouped);
      renderLatestTable(gmGrouped);

      const testSelect = document.getElementById("testSelect");
      const backendSelect = document.getElementById("backendSelect");
      const tests = listTests();

      testSelect.innerHTML = tests.map((name) => `<option value="${escapeHtml(name)}">${escapeHtml(name)}</option>`).join("");

      function refreshBackendsAndChart() {
        const selectedTest = testSelect.value;
        const backends = listBackendsForTest(selectedTest);
        backendSelect.innerHTML = backends.map((name) => `<option value="${escapeHtml(name)}">${escapeHtml(name)}</option>`).join("");
        if (backends.length > 0) {
          backendSelect.value = backends[0];
          renderTestChart(selectedTest, backendSelect.value);
        } else {
          document.getElementById("testChart").innerHTML = "<p>No backend data for this test.</p>";
        }
      }

      testSelect.addEventListener("change", refreshBackendsAndChart);
      backendSelect.addEventListener("change", () => {
        if (testSelect.value && backendSelect.value) {
          renderTestChart(testSelect.value, backendSelect.value);
        }
      });

      if (tests.length > 0) {
        testSelect.value = tests[0];
        refreshBackendsAndChart();
      } else {
        document.getElementById("testChart").innerHTML = "<p>No per-test data available.</p>";
      }
    }

    render();
  </script>
</body>
</html>
""".replace(
        "__REPORT_DATA__", data_json
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Collect benchmark artifacts from GitHub Actions and build a trend report."
    )
    parser.add_argument("--repo", default="", help="Repository in owner/repo format")
    parser.add_argument("--token", default="", help="GitHub token")
    parser.add_argument(
        "--source-workflow", default="build.yml", help="Workflow file or workflow ID"
    )
    parser.add_argument(
        "--branch", default="main", help="Branch filter for workflow runs"
    )
    parser.add_argument(
        "--event", default="push", help="Event filter for workflow runs"
    )
    parser.add_argument(
        "--artifact-prefix", default="benchmark-", help="Benchmark artifact name prefix"
    )
    parser.add_argument(
        "--max-runs",
        type=int,
        default=0,
        help="Maximum successful runs to inspect (0 means no limit)",
    )
    parser.add_argument("--output-json", required=True, help="Output JSON file path")
    parser.add_argument("--output-html", required=True, help="Output HTML file path")
    args = parser.parse_args()

    repo = args.repo or os.environ.get("GITHUB_REPOSITORY", "")
    token = args.token or os.environ.get("GITHUB_TOKEN", "")
    if not repo:
        print("Error: --repo is required (or set GITHUB_REPOSITORY).", file=sys.stderr)
        return 2
    if not token:
        print("Error: --token is required (or set GITHUB_TOKEN).", file=sys.stderr)
        return 2

    api = GitHubApi(repo=repo, token=token)
    print(
        f"Loading successful runs for workflow '{args.source_workflow}' "
        f"on branch '{args.branch}' with event '{args.event}'..."
    )
    successful_runs = api.list_workflow_runs(
        workflow=args.source_workflow,
        branch=args.branch,
        event=args.event,
        max_runs=args.max_runs,
    )

    entries: list[dict[str, Any]] = []
    for index, run in enumerate(successful_runs, start=1):
        run_id = int(run["id"])
        artifacts = api.list_run_artifacts(run_id)
        selected_artifacts = _latest_artifacts_by_name(artifacts, args.artifact_prefix)
        if not selected_artifacts:
            continue

        print(
            f"[{index}/{len(successful_runs)}] run #{run.get('run_number')} "
            f"({run_id}): {len(selected_artifacts)} benchmark artifacts"
        )

        for artifact in sorted(
            selected_artifacts, key=lambda item: str(item.get("name", ""))
        ):
            report = api.download_artifact_json(
                str(artifact.get("archive_download_url", ""))
            )
            if not report:
                continue

            artifact_name = str(artifact.get("name", "benchmark-unknown"))
            target = _target_from_artifact_name(artifact_name, args.artifact_prefix)
            meta = report.get("meta", {}) if isinstance(report, dict) else {}

            entry = {
                "run_id": run_id,
                "run_number": int(run.get("run_number", 0)),
                "run_attempt": int(run.get("run_attempt", 0)),
                "run_created_at": str(run.get("created_at", "")),
                "run_url": str(run.get("html_url", "")),
                "head_branch": str(run.get("head_branch", "")),
                "head_sha": str(run.get("head_sha", "")),
                "artifact_id": int(artifact.get("id", 0)),
                "artifact_name": artifact_name,
                "artifact_created_at": str(artifact.get("created_at", "")),
                "target": target,
                "platform": str(meta.get("platform", "")),
                "geometric_mean_fps": _float_dict(report.get("geometric_mean_fps")),
                "performance_ratios": _float_dict(report.get("performance_ratios")),
                "tests": _extract_ok_fps(report.get("results")),
            }
            entries.append(entry)

    entries.sort(
        key=lambda item: (
            _iso_to_datetime(item["run_created_at"]),
            item["run_id"],
            item["artifact_name"],
        )
    )

    payload = build_history_payload(
        repo=repo,
        workflow=args.source_workflow,
        branch=args.branch,
        event=args.event,
        artifact_prefix=args.artifact_prefix,
        runs_scanned=len(successful_runs),
        successful_runs=len(successful_runs),
        entries=entries,
    )

    json_path = Path(args.output_json)
    html_path = Path(args.output_html)
    json_path.parent.mkdir(parents=True, exist_ok=True)
    html_path.parent.mkdir(parents=True, exist_ok=True)

    json_path.write_text(
        json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8"
    )
    html_path.write_text(render_html(payload), encoding="utf-8")

    print(f"Wrote JSON report: {json_path}")
    print(f"Wrote HTML report: {html_path}")
    if entries:
        print(
            f"Tracked benchmark data from {entries[0]['run_created_at']} "
            f"to {entries[-1]['run_created_at']} ({len(entries)} entries)."
        )
    else:
        print("No benchmark artifacts found in scanned successful runs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
