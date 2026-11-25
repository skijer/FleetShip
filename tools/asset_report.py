"""Compares Fleet's asset catalogue with a local baseline and writes an HTML page to review it.

`fleet-test --asset-catalogue 1` writes logs/asset-catalogue/catalogue.json, the icons it decoded and a photo of
every item's model. This page flags every item whose icon, textbox icon, tint or display list changed since the
baseline, or that lost its icon or its drawing, and lets a person mark each one correct or wrong. The marks stay in
the browser and "Download review" saves them as asset-review.json. The baseline holds game art, so it stays local
(regression/asset-baseline under the install); --accept makes the current catalogue the new baseline.
`fleet-test --asset-review pending` judges the items in game instead (A correct, B wrong) into ingame-review.json, and
--accept-reviewed makes the catalogue the baseline only once that review marks every item correct.
"""

import argparse
import html
import json
import os
import shutil
import sys
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent


def read_catalogue(folder):
    path = folder / "catalogue.json"
    if not path.exists():
        return {}
    return {f"{row['game']}:{row['item']}": row for row in json.loads(path.read_text(encoding="utf-8"))}


def read_photos(folder):
    path = folder / "photos.json"
    return json.loads(path.read_text(encoding="utf-8")).get("photos", {}) if path.exists() else {}


def problems_of(row):
    found = []
    if not isinstance(row.get("icon"), dict):
        found.append("the game does not know the item")
        return found
    image = row.get("iconImage", {})
    if "problem" in image:
        found.append(image["problem"])
    display_list = row.get("displayList", {})
    if not display_list.get("drawn"):
        found.append("draws nothing")
    found += [f"display list: {issue}" for issue in display_list.get("issues", [])]
    return found


def changes_between(row, before):
    changes = []
    icon, old_icon = row.get("icon") or {}, before.get("icon") or {}
    for field in ("iconPath", "textboxIconPath", "tint"):
        if icon.get(field) != old_icon.get(field):
            changes.append(f"{field}: {old_icon.get(field)!r} -> {icon.get(field)!r}")
    if row.get("iconImage", {}).get("hash") != before.get("iconImage", {}).get("hash"):
        changes.append("icon pixels changed")
    display_list, old_list = row.get("displayList", {}), before.get("displayList", {})
    if display_list.get("fingerprint") != old_list.get("fingerprint"):
        added = sorted(set(display_list.get("resources", [])) - set(old_list.get("resources", [])))
        removed = sorted(set(old_list.get("resources", [])) - set(display_list.get("resources", [])))
        detail = "".join([f" +{name}" for name in added] + [f" -{name}" for name in removed])
        changes.append("display list changed" + (f":{detail}" if detail else " (same resources)"))
    return changes


def judge(current, baseline):
    rows = []
    for key, row in current.items():
        before = baseline.get(key)
        problems = problems_of(row)
        if before is None:
            status, notes = ("FAIL" if problems else "NEW"), problems
        else:
            changes = changes_between(row, before)
            new_problems = [problem for problem in problems if problem not in problems_of(before)]
            status = "FAIL" if changes or new_problems else ("KNOWN" if problems else "OK")
            notes = changes + new_problems + [f"(known) {problem}" for problem in problems if problem not in new_problems]
        rows.append((status, row, before, notes))
    for key, before in baseline.items():
        if key not in current:
            rows.append(("FAIL", before, before, ["the item is gone from the game"]))
    order = {"FAIL": 0, "NEW": 1, "KNOWN": 2, "OK": 3}
    return sorted(rows, key=lambda entry: (order[entry[0]], entry[1]["game"], entry[1]["item"]))


def image_cell(folder, report_folder, file, size):
    if not file:
        return ""
    source = os.path.relpath(folder / file, report_folder).replace("\\", "/")
    return f'<img loading="lazy" src="{html.escape(source)}" width="{size}" height="{size}">'


def review_cell(key):
    name = html.escape(key)
    return (f"<label><input type='radio' name='{name}' value='correct'> Correct</label><br>"
            f"<label><input type='radio' name='{name}' value='wrong'> Wrong</label><br>"
            "<input type='text' placeholder='note' size='18'>")


STYLE = """body{font:14px sans-serif;background:#1b1b1f;color:#ddd}table{border-collapse:collapse}
td,th{border:1px solid #444;padding:4px 6px;vertical-align:top}img{image-rendering:pixelated;background:#555}
.FAIL{background:#5a1f24}.NEW{background:#1f3a5a}.KNOWN{background:#4a4320}code{font-size:12px}
tr[data-verdict=correct] td:last-child{background:#1f4a2a}tr[data-verdict=wrong] td:last-child{background:#7a1f24}
.bar{position:sticky;top:0;background:#1b1b1f;padding:6px;z-index:1}"""

REVIEW_SCRIPT = """
const KEY = 'fleet-asset-review-v2';
const review = JSON.parse(localStorage.getItem(KEY) || '{}');
const rows = [...document.querySelectorAll('tr[data-key]')];
function store(row) {
  const verdict = row.querySelector('input[type=radio]:checked');
  const note = row.querySelector('input[type=text]').value;
  if (verdict || note) {
    review[row.dataset.key] = { verdict: verdict ? verdict.value : '', note };
  } else {
    delete review[row.dataset.key];
  }
  localStorage.setItem(KEY, JSON.stringify(review));
  row.dataset.verdict = verdict ? verdict.value : '';
  count();
}
function count() {
  const values = Object.values(review);
  document.getElementById('progress').textContent = `${values.filter(v => v.verdict).length} of ${rows.length}`
    + ` reviewed, ${values.filter(v => v.verdict === 'wrong').length} wrong`;
}
function show(filter) {
  for (const row of rows) {
    const verdict = row.dataset.verdict;
    row.hidden = (filter === 'pending' && verdict !== '') || (filter === 'wrong' && verdict !== 'wrong')
      || (filter === 'flagged' && !['FAIL', 'NEW', 'KNOWN'].includes(row.dataset.status));
  }
}
function download() {
  const text = JSON.stringify({ reviewed: new Date().toISOString(), items: review }, null, 1);
  const link = document.createElement('a');
  link.href = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
  link.download = 'asset-review.json';
  link.click();
}
function load(input) {
  input.files[0].text().then(text => {
    Object.assign(review, JSON.parse(text).items || {});
    localStorage.setItem(KEY, JSON.stringify(review));
    location.reload();
  });
}
for (const row of rows) {
  const saved = review[row.dataset.key] || {};
  for (const radio of row.querySelectorAll('input[type=radio]')) {
    radio.checked = radio.value === saved.verdict;
  }
  row.querySelector('input[type=text]').value = saved.note || '';
  row.dataset.verdict = saved.verdict || '';
  row.addEventListener('change', () => store(row));
}
count();
"""


def row_html(entry, current_folder, baseline_folder, photos, earlier):
    status, row, before, notes = entry
    key = f"{row['game']}:{row['item']}"
    if key in earlier:
        mark = earlier[key]
        notes = notes + [f"earlier review: {mark.get('verdict', '')} {mark.get('note', '')}".strip()]
    icon = row.get("icon") or {}
    display_list = row.get("displayList", {})
    changed_icon = before is not None and before is not row and "icon pixels changed" in notes
    baseline_file = (before or {}).get("iconImage", {}).get("file") if changed_icon else None
    resources = "<br>".join(html.escape(name) for name in display_list.get("resources", []))
    return (f"<tr class='{status}' data-key='{html.escape(key)}' data-status='{status}'><td>{status}</td>"
            f"<td>{row['game']}</td>"
            f"<td>{html.escape(row.get('label', row['item']))}<br><code>{html.escape(row['item'])}</code></td>"
            f"<td>{image_cell(current_folder, current_folder, row.get('iconImage', {}).get('file'), 48)}</td>"
            f"<td>{image_cell(current_folder, current_folder, photos.get(key), 128)}</td>"
            f"<td>{image_cell(baseline_folder, current_folder, baseline_file, 48)}</td>"
            f"<td><code>{html.escape(icon.get('iconPath', ''))}</code></td>"
            f"<td><code>{html.escape(icon.get('textboxIconPath', ''))}</code></td>"
            f"<td><code>{display_list.get('fingerprint', '-')}</code><br>{display_list.get('commands', 0)} commands"
            f"<details><summary>{len(display_list.get('resources', []))} resources</summary>{resources}</details>"
            f"</td><td>{'<br>'.join(html.escape(note) for note in notes)}</td><td>{review_cell(key)}</td></tr>")


def write_report(judged, current_folder, baseline_folder, has_baseline, earlier):
    counts = {status: sum(1 for entry in judged if entry[0] == status) for status in ("FAIL", "NEW", "KNOWN", "OK")}
    photos = read_photos(current_folder)
    summary = (f"{counts['FAIL']} failed, {counts['NEW']} new, {counts['KNOWN']} known problems, {counts['OK']}"
               f" unchanged." + ("" if has_baseline else " No baseline yet."))
    lines = ["<!DOCTYPE html><meta charset='utf-8'><title>Fleet asset review</title>",
             f"<style>{STYLE}</style><h1>Fleet asset review</h1><p>{summary}</p>",
             "<div class='bar'><span id='progress'></span> &nbsp; Show: "
             "<button onclick=\"show('all')\">all</button> <button onclick=\"show('pending')\">not reviewed</button>"
             " <button onclick=\"show('wrong')\">wrong</button> <button onclick=\"show('flagged')\">flagged</button>"
             " &nbsp; <button onclick='download()'>Download review</button> "
             "<label>Load review <input type='file' accept='.json' onchange='load(this)'></label></div>",
             "<table><tr><th>Status</th><th>Game</th><th>Item</th><th>Icon</th><th>Model</th><th>Baseline icon</th>"
             "<th>Icon path</th><th>Textbox icon</th><th>Display list</th><th>Notes</th><th>Review</th></tr>"]
    lines += [row_html(entry, current_folder, baseline_folder, photos, earlier) for entry in judged]
    lines.append(f"</table><script>{REVIEW_SCRIPT}</script>")
    report = current_folder / "report.html"
    report.write_text("\n".join(lines), encoding="utf-8")
    return report, counts


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--install", type=Path, default=REPOSITORY / "x64" / "Release")
    parser.add_argument("--baseline", type=Path, help="default: <install>/regression/asset-baseline")
    parser.add_argument("--accept", action="store_true", help="make this catalogue the baseline")
    parser.add_argument("--earlier-review", type=Path,
                        help="an asset-review.json whose marks show as notes (default: the in-game review)")
    parser.add_argument("--accept-reviewed", action="store_true",
                        help="make this catalogue the baseline if the review marks every item correct")
    args = parser.parse_args()
    current_folder = (args.install / "logs" / "asset-catalogue").resolve()
    baseline_folder = (args.baseline or args.install / "regression" / "asset-baseline").resolve()
    current = read_catalogue(current_folder)
    if not current:
        print(f"no catalogue in {current_folder}: run fleet-test --asset-catalogue 1 first")
        return 2
    baseline = read_catalogue(baseline_folder)
    review_file = args.earlier_review or current_folder / "ingame-review.json"
    earlier = {}
    if review_file.exists():
        earlier = json.loads(review_file.read_text(encoding="utf-8")).get("items", {})
    report, counts = write_report(judge(current, baseline), current_folder, baseline_folder, bool(baseline), earlier)
    print(f"{counts['FAIL']} failed, {counts['NEW']} new, {counts['KNOWN']} known, {counts['OK']} unchanged: {report}")
    unconfirmed = [key for key in current if earlier.get(key, {}).get("verdict") != "correct"]
    if args.accept_reviewed and unconfirmed:
        print(f"baseline kept: {len(unconfirmed)} items are not marked correct, e.g. {', '.join(unconfirmed[:5])}")
    if args.accept or (args.accept_reviewed and not unconfirmed):
        shutil.rmtree(baseline_folder, ignore_errors=True)
        shutil.copytree(current_folder, baseline_folder, ignore=shutil.ignore_patterns("report.html"))
        print(f"baseline updated: {baseline_folder}")
    return 1 if counts["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())
