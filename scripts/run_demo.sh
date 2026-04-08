#!/usr/bin/env bash
set -euo pipefail

./sqlproc --db ./sample_db --file ./queries/demo.sql
