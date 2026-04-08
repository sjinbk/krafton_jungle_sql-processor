# Test Matrix

이 문서는 현재 MVP의 검증 계약입니다.

## Canonical Evaluator Loop

아래 명령은 Dev Container, Linux, 또는 WSL 환경에서 실행합니다.

```text
make build
make test
make check
```

`make check`는 authoritative end-to-end gate입니다. 항상 deterministic하게 유지되어야 합니다.

## Fixture Rules

- `tests/fixtures/sample_db_seed/`는 immutable seed input으로 취급합니다.
- row append 가능성이 있는 모든 흐름은 temporary copy를 대상으로 실행합니다.
- end-to-end verification은 checks 이후에도 seed fixture가 unchanged임을 증명해야 합니다.

## Coverage Matrix

| Area | Source Of Truth | Verification |
| --- | --- | --- |
| CLI happy path | `src/main.c`, `README.md` | `make demo` and `make check` run `queries/demo.sql` through `sqlproc` |
| Multi-statement file execution | `src/input.c` | `queries/demo.sql` plus `tests/golden/demo.out` |
| Single-line comments | `src/input.c` | `queries/demo.sql` starts with a comment and still executes correctly |
| Statement termination rules | `src/input.c` | `test_split_statements_requires_semicolon` |
| Tokenization and string parsing | `src/tokenizer.c` | `make test` parser and literal tests |
| INSERT parsing | `src/parser.c` | `test_parser_insert` |
| SELECT projection parsing | `src/parser.c` | `test_parser_select_projection` |
| Schema loading and CSV reads | `src/schema.c`, `src/storage.c` | `test_schema_and_storage` |
| CSV escaping | `src/storage.c` | `test_storage_csv_escaping` |
| AST debug output | `src/ast.c`, `src/main.c` | `test_statement_fprint` |
| Executor projection errors | `src/executor.c` | `queries/error.sql` plus `tests/golden/error.out` |
| Type mismatch handling | `src/executor.c` | `queries/type_error.sql` plus `tests/golden/type_error.out` |
| Parser failure for malformed SELECT | `src/parser.c` | `queries/parse_error.sql` plus `tests/golden/parse_error.out` |
| Seed fixture isolation | `scripts/check.sh` | `make check` snapshots and diffs `tests/fixtures/sample_db_seed/` |

## Required Evidence

- `make test` prints `All tests passed`
- `make check` ends with `check: all verification steps passed`
- Golden file diffs are clean
- Seed fixture diff is clean

## When Behavior Changes

- end-to-end 동작이 바뀌면 `tests/test_main.c`의 가장 가까운 unit test를 함께 갱신합니다.
- end-to-end 동작이 바뀌면 `queries/`의 대응 입력도 업데이트하거나 추가합니다.
- 같은 변경 안에서 `tests/golden/`의 대응 golden output도 업데이트하거나 추가합니다.
- 지원 SQL subset이나 output contract가 바뀌면 `docs/spec/mvp.md`도 함께 업데이트합니다.
