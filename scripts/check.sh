#!/usr/bin/env bash
set -euo pipefail

work_dir="$(mktemp -d)"
snapshot_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir" "$snapshot_dir"' EXIT

./sqlproc_tests

cp -R ./tests/fixtures/sample_db_seed/. "$snapshot_dir"
mkdir -p "$work_dir/db"
cp -R ./tests/fixtures/sample_db_seed/. "$work_dir/db"

./sqlproc --db "$work_dir/db" --file ./queries/demo.sql >"$work_dir/demo.out"
diff -u ./tests/golden/demo.out "$work_dir/demo.out"

if ./sqlproc --db "$work_dir/db" --file ./queries/error.sql >"$work_dir/error.stdout" 2>"$work_dir/error.out"; then
  echo "check: expected error.sql to fail" >&2
  exit 1
fi
diff -u ./tests/golden/error.out "$work_dir/error.out"

if ./sqlproc --db "$work_dir/db" --file ./queries/type_error.sql >"$work_dir/type_error.stdout" 2>"$work_dir/type_error.out"; then
  echo "check: expected type_error.sql to fail" >&2
  exit 1
fi
diff -u ./tests/golden/type_error.out "$work_dir/type_error.out"

if ./sqlproc --db "$work_dir/db" --file ./queries/parse_error.sql >"$work_dir/parse_error.stdout" 2>"$work_dir/parse_error.out"; then
  echo "check: expected parse_error.sql to fail" >&2
  exit 1
fi
diff -u ./tests/golden/parse_error.out "$work_dir/parse_error.out"

diff -ru "$snapshot_dir" ./tests/fixtures/sample_db_seed

echo "check: all verification steps passed"
