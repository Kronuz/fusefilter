#!/usr/bin/env python3
"""Breaks the build in fusefilter.hh in sixteen ways and requires test/build_test.cc to fail on each.

    CXX=clang++ tools/mutants.py [-j N] [name...]

Each mutant is one textual edit of fusefilter.hh, compiled with the test and run; it is killed
when the test fails to build, exits non-zero, or runs past the timeout. The unedited header must
pass first. Exits 1 when a mutant survives.
"""
import concurrent.futures
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
CXX = os.environ.get("CXX", "c++")
TIMEOUT = 180

# (name, text in fusefilter.hh, replacement, what the edit does)
MUTANTS = [
	("dedup_skipped", "if (f.has_duplicates(keys, w)) {", "if (false && f.has_duplicates(keys, w)) {",
		"a failed attempt never looks for duplicates, so a set with one never builds"),
	("dedup_always", "return std::adjacent_find(core.begin(), core.end()) != core.end();", "return true;",
		"every failure sorts the keys as if it had found a pair, so keys taken over change when they hold no duplicate"),
	("core_needs_three", "count[p[0]] >= 2 && count[p[1]] >= 2 && count[p[2]] >= 2", "count[p[0]] >= 3 && count[p[1]] >= 3 && count[p[2]] >= 3",
		"a pair of equal keys alone in its slots is not in the core the check reads"),
	("core_unsorted", "std::sort(core.begin(), core.end());\n\t\treturn std::adjacent_find", "return std::adjacent_find",
		"equal hashes in the core are not next to each other"),
	("wrapped_counts_trusted", "w.saturated || (", "(",
		"after a count wrapped, the counts still choose the keys to compare, and a set with 257 equal keys never builds"),
	("saturation_unchecked", "if (total != 3 * n) {", "if (false) {",
		"a slot whose count wrapped to 1 is peeled as if it held one key"),
	("no_restart", "keys = unique;\n\t\t\t\t\t\t\tagain = true;", "keys = unique;",
		"after the dedup the seeds go on instead of starting again, so the seed differs from the sorted build's"),
	("no_unique", "unique.erase(std::unique(unique.begin(), unique.end()), unique.end());", "",
		"the keys are sorted but the duplicates stay"),
	("dedup_of_nothing", "if (!owned) { unique.assign(keys.begin(), keys.end()); }", "",
		"the span overload dedups an empty copy"),
	("dedup_not_in_place", "std::vector<std::uint64_t>& unique = owned ? *owned : copy;", "std::vector<std::uint64_t>& unique = copy; unique.assign(keys.begin(), keys.end());",
		"the vector taken over is not the one deduplicated"),
	("lone_slot_two_values", "const unsigned lone = (p[0] != s) + (p[0] != s && p[1] != s);", "const unsigned lone = (p[0] != s);",
		"the lone slot is never the third"),
	("other_slot_repeated", "lone == 2 ? p[1] : p[2]}", "p[2]}",
		"a key whose lone slot is the third removes itself from the third slot instead of the second"),
	("assign_two_slots", "^ fp[p[0]] ^ fp[p[1]] ^ fp[p[2]]);", "^ fp[p[0]] ^ fp[p[1]]);",
		"a fingerprint ignores its third slot"),
	("scatter_without_sums", "for (std::size_t i = 1; i < (std::size_t(1) << (64 - shift)) + 1; ++i) { start[i] += start[i - 1]; }", "",
		"every bucket starts at the front of the hashes"),
	("peel_stale_entry", "if (count[s] != 1) { continue; }", "",
		"a queued slot that has since lost its last key is peeled"),
	("queue_second_count", "tail += count[t] == 1;", "tail += count[t] == 2;",
		"slots are queued when they hold two keys"),
]


def build_and_run(work, name, source):
	src = work / name
	src.mkdir()
	(src / "fusefilter.hh").write_text(source)
	exe = src / "build_test"
	cmd = [CXX, "-std=c++20", "-O1", "-I", str(src), "-I", str(ROOT / "test"), str(ROOT / "test" / "build_test.cc"), "-o", str(exe)]
	built = subprocess.run(cmd, capture_output=True, text=True)
	if built.returncode:
		return "killed (does not build)", built.stderr.strip().splitlines()[:1]
	try:
		ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=TIMEOUT)
	except subprocess.TimeoutExpired:
		return "killed (timeout)", []
	if ran.returncode:
		lines = [l for l in (ran.stderr + ran.stdout).splitlines() if "CHECK" in l or "terminate" in l or "what()" in l]
		return "killed", lines[:1]
	return "SURVIVED", []


def main():
	args = sys.argv[1:]
	jobs = 4
	if args[:1] == ["-j"]:
		jobs, args = int(args[1]), args[2:]
	text = (ROOT / "fusefilter.hh").read_text()
	chosen = [m for m in MUTANTS if not args or m[0] in args]
	for name, old, new, _ in chosen:
		if text.count(old) != 1:
			sys.exit(f"{name}: the text to edit appears {text.count(old)} times in fusefilter.hh, not once")
	with tempfile.TemporaryDirectory() as tmp:
		work = pathlib.Path(tmp)
		verdict, _ = build_and_run(work, "unedited", text)
		if verdict != "SURVIVED":
			sys.exit(f"the unedited header fails the test ({verdict})")
		with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
			futures = {m[0]: pool.submit(build_and_run, work, m[0], text.replace(m[1], m[2])) for m in chosen}
			survivors = 0
			for name, _, _, what in chosen:
				verdict, detail = futures[name].result()
				survivors += verdict == "SURVIVED"
				print(f"{name:24} {verdict:24} {what}" + (f"\n{'':24} {detail[0]}" if detail else ""))
	print(f"{len(chosen) - survivors} of {len(chosen)} mutants killed")
	sys.exit(1 if survivors else 0)


main()
