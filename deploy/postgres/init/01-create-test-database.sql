-- Creates the dedicated test database used by the repository integration tests.
-- Production data lives in the database named by POSTGRES_DB / PGDATABASE, so the
-- test suite never shares a database with the application.
CREATE DATABASE inerxia_test OWNER inerxia;