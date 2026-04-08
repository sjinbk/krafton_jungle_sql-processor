# SQL Processor v0.2 in C

이 저장소는 C로 작성한 작은 파일 기반 SQL processor MVP를 설명하고 시연하기 위한 repo입니다.

핵심 목표는 넓은 SQL 호환성이 아니라, 제한된 범위를 안정적으로 처리하는 demo-ready MVP를 만드는 것입니다. 발표나 코드 리뷰에서는 별도 자료 없이 이 `README.md`를 기준으로 프로젝트 목적, 지원 범위, 실행 흐름, 검증 방법을 설명할 수 있어야 합니다.

## 이 저장소가 보여주는 것

- `INSERT`와 `SELECT` 중심의 좁은 SQL subset 구현
- `.schema`, `.csv` 파일을 사용하는 file-backed storage
- 계약 문서, 테스트 문서, golden output까지 포함한 harness-first 개발 흐름
- `make demo`, `make check`로 바로 시연 가능한 검증 루프

## 먼저 읽을 문서

- `AGENTS.md`
- `docs/spec/mvp.md`
- `docs/testing/test-matrix.md`
- `docs/exec-plans/active/sqlproc-v0.2-kickoff.md`

## Canonical Environment

가능하면 `.devcontainer/`의 Dev Container를 사용합니다.

현재 호스트 PowerShell 세션은 `gcc`, `make`, `bash` 기준의 C 빌드 환경이 아니므로 source of truth로 간주하지 않습니다. 실제 기준 환경은 Dev Container, Linux, 또는 WSL입니다.

## Build And Verification

```text
make build
make test
make demo
make check
make sanitize
```

- `make demo`는 happy-path SQL 파일을 임시 데이터베이스 복사본에 실행합니다.
- `make check`는 가장 신뢰하는 evaluator loop입니다. 테스트, golden output 비교, fixture isolation 검사를 함께 수행합니다.

## 발표용 설명 순서

1. 이 문서와 `docs/spec/mvp.md`, `docs/testing/test-matrix.md`를 보여주며 프로젝트 계약을 설명합니다.
2. `make test`로 parser/storage 중심 단위 검증이 준비되어 있음을 보여줍니다.
3. `make demo`로 `INSERT`와 `SELECT`의 end-to-end 흐름을 시연합니다.
4. 마지막으로 `make check`와 seed fixture isolation 규칙을 언급해 재현 가능성과 안전한 검증 방식을 설명합니다.

## 가장 안전한 데모 실행 방법

발표나 확인 용도에서는 아래처럼 `make demo`를 우선 사용하면 됩니다.

```text
make demo
```

이 명령은 `scripts/run_demo.sh`를 통해 `./sample_db`를 임시 디렉터리로 복사한 뒤 실행합니다. 따라서 tracked sample data를 직접 오염시키지 않습니다.

## Manual Run

`./sample_db`에 직접 쓰는 방식은 피하는 것이 좋습니다. 수동 실행이 필요하면 임시 복사본을 만들어 사용합니다.

```bash
tmp_db="$(mktemp -d)"
cp -R ./sample_db/. "$tmp_db"
./sqlproc --db "$tmp_db" --file ./queries/demo.sql
```

Optional debug mode:

```bash
tmp_db="$(mktemp -d)"
cp -R ./sample_db/. "$tmp_db"
./sqlproc --emit-ast --db "$tmp_db" --file ./queries/demo.sql
```

`--emit-ast`는 각 statement header가 stdout에 출력된 뒤 compact parsed AST summary를 stderr에 기록합니다.

## Example SQL

```sql
-- insert rows and read them back
INSERT INTO users (id, name, age) VALUES (3, 'kim', 25), (4, 'lee', 29);
SELECT * FROM users;
SELECT id, name FROM public.users;
```

## Expected Demo Output

`queries/demo.sql`을 실행하면 stdout은 아래와 같은 형태를 보여줍니다.

```text
-- Statement 1: INSERT INTO users (id, name, age) VALUES (3, 'kim', 25),...
INSERT OK (2 rows)
-- Statement 2: SELECT * FROM users
+----+-------+-----+
| id | name  | age |
+----+-------+-----+
| 1  | alice | 31  |
| 2  | bob   | 24  |
| 3  | kim   | 25  |
| 4  | lee   | 29  |
+----+-------+-----+
(4 rows)
-- Statement 3: SELECT id, name FROM public.users
+----+-------+
| id | name  |
+----+-------+
| 1  | alice |
| 2  | bob   |
| 3  | kim   |
| 4  | lee   |
+----+-------+
(4 rows)
```

실패 예시는 발표 중 scope와 error contract를 설명할 때 함께 보여주기 좋습니다.

```text
error: unknown column 'nickname' in SELECT
1:8: expected projection list or '*'
error: type mismatch for column 'id'
```

위 문자열은 각각 `tests/golden/error.out`, `tests/golden/parse_error.out`, `tests/golden/type_error.out` 기준입니다.

## Supported MVP Scope

이 MVP는 아래 범위만 지원합니다.

- `INSERT INTO [schema.]table [(col1, col2, ...)] VALUES (...), (...);`
- `SELECT * FROM [schema.]table;`
- `SELECT col1, col2 FROM [schema.]table;`
- 하나의 SQL 파일 안에 여러 statement 작성
- string literal 바깥에서의 `--` single-line comment
- `.schema`, `.csv`를 사용하는 file-backed storage
- `SELECT` 결과의 ASCII table 출력
- Optional `--emit-ast` debug output to stderr

세부 동작 계약은 `docs/spec/mvp.md`를 기준으로 합니다.

## Scope Limits

이번 MVP는 아래 기능을 지원하지 않습니다.

- `WHERE`, `UPDATE`, `DELETE`, `JOIN`, `ORDER BY`, `GROUP BY`, `LIMIT`
- `NULL`, floats, booleans, dates, expressions
- Quoted identifiers
- In-place schema evolution

## Storage Layout

```text
sample_db/
  public/
    users.schema
    users.csv
```

Example schema file:

```text
id,INT
name,TEXT
age,INT
```

`.schema`는 컬럼 이름과 타입을 정의하고, `.csv`는 실제 row 데이터를 저장합니다.

## Repository Map

- `src/`: CLI, input splitting, tokenization, parsing, schema loading, storage, execution
- `include/`: 현재 MVP의 public headers
- `queries/`: demo 및 failure-driving SQL files
- `tests/test_main.c`: 주요 regression tests
- `tests/golden/`: expected end-to-end stderr/stdout
- `tests/fixtures/sample_db_seed/`: immutable seed database used as a copy source
- `scripts/check.sh`: canonical verification loop

## Presentation Reading Order

발표 준비에서는 아래 순서로 코드를 읽으면 실행 흐름과 책임 분리가 자연스럽게 이어집니다.

1. `src/main.c`
2. `src/input.c`
3. `src/tokenizer.c`
4. `src/parser.c`
5. `src/schema.c`
6. `src/storage.c`
7. `src/executor.c`
8. `src/ast.c`
9. `src/error.c`
10. `tests/test_main.c`
11. `queries/demo.sql`
12. `queries/error.sql`, `queries/parse_error.sql`, `queries/type_error.sql`

함께 보면 좋은 발표 보조 문서:

- `docs/spec/mvp.md`: 지원 범위와 비목표
- `docs/testing/test-matrix.md`: 어떤 계약을 어떤 테스트가 증명하는지
- `docs/presentation/code-walkthrough.md`: 발표 흐름, 코드리뷰 포인트, 마무리 멘트 초안

## 발표 때 강조하면 좋은 포인트

- 이 프로젝트는 범위를 넓히는 대신 SQL subset을 명확히 제한했습니다.
- 구현만 있는 repo가 아니라, spec, test matrix, golden output, fixture isolation까지 함께 갖춘 형태입니다.
- `make demo`는 시연용, `make check`는 최종 검증용이라는 역할 분리가 분명합니다.
- seed fixture를 직접 바꾸지 않는 규칙 덕분에 반복 시연과 반복 검증이 안전합니다.
