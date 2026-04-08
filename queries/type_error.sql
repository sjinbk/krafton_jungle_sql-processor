-- type_error.sql
-- INT column에 TEXT literal을 넣어 execution-time type mismatch를 유도한다.
INSERT INTO users (id, name, age) VALUES ('oops', 'kim', 25);
