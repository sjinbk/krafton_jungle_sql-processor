# AGENTS.md

이 저장소는 single-agent, harness-first 개발을 기준으로 준비되어 있습니다.

## Mission

좁은 범위의 SQL processor MVP를 안정적으로 유지하면서, 사람과 에이전트 모두가 쉽게 읽고 검증하고 안전하게 확장할 수 있는 repo 상태를 유지합니다.

## Read Order

1. `README.md`
2. `docs/spec/mvp.md`
3. `docs/testing/test-matrix.md`
4. `docs/exec-plans/active/sqlproc-v0.2-kickoff.md`
5. 동작 변경이 필요하면 `src/main.c`부터 읽고, 이후 parser/storage/executor 관련 모듈을 확인합니다.

## Canonical Environment

- Preferred: `.devcontainer/`의 Dev Container
- Acceptable alternative: `make`, `gcc`, `bash`가 있는 Linux 또는 WSL
- 현재 호스트 PowerShell 세션은 C 빌드의 source of truth가 아님

## Canonical Commands

- `make build`
- `make test`
- `make demo`
- `make check`
- `make sanitize`

## Working Agreement

- `docs/spec/mvp.md`를 동작 계약으로 취급합니다.
- `docs/testing/test-matrix.md`를 검증 계약으로 취급합니다.
- 동작이 바뀌면 문서, 테스트, fixtures, golden outputs를 같은 변경 안에서 함께 갱신합니다.
- checks 또는 demos 중에는 `tests/fixtures/sample_db_seed/`를 in place로 수정하지 않습니다.
- 범위를 넓히지 않습니다. 문서에 적힌 SQL subset만 지원합니다.

## Current Supported Scope

- `--file`을 통한 SQL file execution
- `;` 기준 statement splitting
- string literal 바깥에서의 `--` single-line comments
- `INSERT INTO [schema.]table [(columns...)] VALUES (...), (...);`
- `SELECT * FROM [schema.]table;`
- `SELECT col1, col2 FROM [schema.]table;`
- `.schema`, `.csv` 기반 file-backed storage
- Optional `--emit-ast` debug output to stderr

## Non-Goals

- `WHERE`, `UPDATE`, `DELETE`, `JOIN`, `ORDER BY`, `GROUP BY`, `LIMIT`
- `NULL`, floats, booleans, dates, expressions, quoted identifiers
- Native Windows-first build support
- Remote harness services or multi-agent orchestration

## Definition Of Done

- 문서에 정의된 MVP 동작이 구현되고 검증되어 있습니다.
- canonical environment에서 `make check`가 통과합니다.
- 검증 이후에도 seed fixtures가 바뀌지 않습니다.
- `README.md`만으로 짧은 live demo 설명이 가능합니다.
