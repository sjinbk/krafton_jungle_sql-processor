# MVP Specification

이 문서는 현재 SQL processor MVP의 동작 계약입니다.

## Goal

`--db`로 지정한 file-backed database root를 대상으로, 제한된 SQL subset을 실행합니다.

## CLI Contract

binary는 아래 형식으로 호출합니다.

```text
./sqlproc --db <db_root> --file <sql_file>
```

- `--db`는 database root directory를 가리킵니다.
- `--file`은 SQL text file을 가리킵니다.
- `--emit-ast`는 optional이며, 각 statement 실행 전에 debug AST summary를 stderr에 기록합니다.
- 그 외 argument shape는 모두 거부되며 stderr usage output과 exit code `1`을 반환합니다.

## Statement Processing

- 입력 파일은 plain text로 읽습니다.
- 모든 statement는 `;`로 끝나야 합니다.
- 빈 statement는 무시합니다.
- `--`는 string literal 바깥에서 single-line comment를 시작합니다.
- single-quoted strings를 지원합니다.
- string literal 안의 연속된 single quote 두 개는 escaped single quote를 의미합니다.
- unterminated string과 trailing semicolon 누락은 error입니다.

## Supported SQL

keyword는 대소문자를 구분하지 않고 매칭합니다. identifier는 입력된 형태를 유지합니다.

### INSERT

```text
INSERT INTO [schema.]table [(col1, col2, ...)] VALUES (...), (...);
```

- schema를 생략하면 `public`을 사용합니다.
- literal types는 `INT`, `TEXT`입니다.
- column list는 optional입니다.
- multi-row `VALUES` lists를 지원합니다.
- value 개수는 명시한 column list 길이 또는 전체 schema width와 일치해야 합니다.

### SELECT

```text
SELECT * FROM [schema.]table;
SELECT col1, col2 FROM [schema.]table;
```

- schema를 생략하면 `public`을 사용합니다.
- `*`는 schema column 전체를 schema order로 선택합니다.
- 명시적 projection columns는 schema 기준으로 검증합니다.

## Storage Contract

database layout:

```text
<db_root>/
  public/
    users.schema
    users.csv
```

Schema files:

- 한 줄에 하나의 column을 기록합니다.
- format: `name,TYPE`
- supported types: `INT`, `TEXT`

CSV files:

- 한 줄에 하나의 row를 기록합니다.
- `INT` values는 plain numbers로 저장합니다.
- `TEXT` values는 double-quoted 형태로 저장합니다.
- embedded double quotes는 doubled double quotes로 escape합니다.
- `TEXT` values에는 newline이 포함될 수 없습니다.

## Output Contract

`INSERT`는 아래 형식으로 출력합니다.

```text
INSERT OK (<n> rows)
```

`SELECT`는 ASCII table을 출력한 뒤 아래 형식을 이어서 출력합니다.

```text
(<n> rows)
```

하나의 SQL 파일에 statement가 2개 이상 있으면, 각 statement 결과 앞에 아래 header를 stdout에 출력합니다.

```text
-- Statement <n>: <normalized SQL summary>
```

summary는 statement 원문을 한 줄로 정규화한 식별용 텍스트이며, 너무 길면 `...`로 잘립니다.

하나의 파일에 여러 statement가 있을 경우, 입력 순서대로 실행하고 같은 순서로 output을 기록합니다.

## Error Contract

- input splitting과 parser errors는 `line:column: message` 형식으로 보고합니다.
- executor, schema, storage, generic I/O errors는 lower layer가 line/column metadata를 제공하지 않는 한 `error: message` 형식으로 보고합니다.
- 실행은 첫 번째 failing statement에서 중단합니다.

## Unsupported Features

- `WHERE`, `UPDATE`, `DELETE`, `JOIN`, `GROUP BY`, `ORDER BY`, `LIMIT`
- `NULL`, booleans, floats, dates, expressions
- Quoted identifiers
- comparison operators, predicates, `AND`, `OR`
- in-place schema changes

## Presentation Note

발표 준비에서는 이 문서를 "무엇을 지원하는가"를 설명하는 기준으로 사용하고,
코드 흐름 설명은 `docs/presentation/code-walkthrough.md`와 함께 읽으면 됩니다.
