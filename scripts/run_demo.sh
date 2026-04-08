#!/usr/bin/env bash
set -euo pipefail

tmp_db="$(mktemp -d)"
trap 'rm -rf "$tmp_db"' EXIT

cp -R ./sample_db/. "$tmp_db"
./sqlproc --db "$tmp_db" --file ./queries/demo.sql

