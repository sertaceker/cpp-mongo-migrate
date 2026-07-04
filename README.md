# MongoDB Migration Tool

This document explains:

- how to add new migrations to the project,
- the CLion / CMake workflow,
- transactional boundaries and limitations,
- and how to run the console application.

---

## Overview

This migration tool:

- includes all migration files under the `migrations/` folder in the executable at build time,
- sorts migrations by their `version()` value and executes them in order (`up()` / `down()`),
- stores the applied version in a `version` collection inside the target database.

**Version tracking details**
- Collection name: `version`
- Example document: `{ "version": 3 }`

The `main.cpp` flow attempts to run the entire operation in a single MongoDB transaction:

1. Run migrations (`up` / `down`)
2. If migrations succeed, update the tracked version
3. Commit at the end (or abort on failure)

> **Note:** MongoDB transactions require a **replica set** or a **sharded cluster**. Transactions do not work on standalone MongoDB instances.  
> If you do not want to use transactions, set `useTransaction = false`.

---

## Adding a New Migration

### 1) File Location and Naming

A new migration file **must** be added under the project's `migrations/` folder.

This is important because `CMakeLists.txt` automatically includes all `.cpp` files in that folder at build time. If the file is placed elsewhere:

- it will **not** be compiled,
- and it will **not** be executed by the migration tool.

If you still want to place it elsewhere, you must add it manually to `CMakeLists.txt`.

#### Recommended naming conventions

- File name format: `migration-<N>.cpp`  
  Examples: `migration-1.cpp`, `migration-2.cpp`, ...
- Class name format: `Migration<N>`  
  Examples: `Migration1`, `Migration2`, ...

These naming conventions are **not required** by the migration tool itself, but they are strongly recommended for team consistency and easier maintenance.

> **Note:** The `migrations/` folder must contain files with the `.cpp` extension. Other file types (e.g. `.h`, `.hpp`) are not compiled.

---

### 2) Creating the Migration Class

```cpp
#ifndef MIGRATION_1_CPP
#define MIGRATION_1_CPP

#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>

#include "../migration.hpp"

DECLARE_MIGRATION(Migration1, 1)

void Migration1::up(mongocxx::client& client, mongocxx::client_session& session)
{
    auto collection = client["testDB"]["testCollection1"];

    const auto testIndex = bsoncxx::from_json(R"(
    {
        "testField": 1
    })");
    collection.create_index(session, testIndex.view());

    const auto testDocument1 = bsoncxx::from_json(R"(
    {
        "testField": "value1",
        "otherField": 123
    })");
    collection.insert_one(session, testDocument1.view());
}

void Migration1::down(mongocxx::client& client, mongocxx::client_session& session)
{
    auto collection = client["testDB"]["testCollection1"];
    collection.drop();
}

#endif // MIGRATION_1_CPP
```

> **Tip:** `DECLARE_MIGRATION(Migration1, 1)` declares the class **and** automatically registers it in the migration registry.
> - First parameter: class name
> - Second parameter: migration version number

> **IMPORTANT:** The class name in `DECLARE_MIGRATION(...)` must exactly match the class name used in the method definitions (`Migration1::up`, `Migration1::down`). Otherwise, you will get a compilation error.

> **IMPORTANT:** The version number in `DECLARE_MIGRATION(...)` must be:
> - a **positive integer**
> - **unique** across all migrations  
    > This is required for deterministic ordering.

---

### 3) Running Migrations (CLion / CMake Workflow)

- Whenever a new migration is added (or new migration files are pulled from the repository), you must **Reload CMake Project** in the relevant project.  
  Otherwise, the newly added migration will not be compiled.
- Since the project is already included in the root `CMakeLists.txt`, you can build and run it normally from CLion.
- The application is a console app, so you need to pass the connection string and migration database name as arguments.

In **Run / Debug Configurations** → **Program arguments**, add arguments like:

```bash
mongodb://myuser:mypassword@localhost:27017/?replicaSet=NoSQLDBReplicaSet migrations
```

- **1st argument**: MongoDB connection string
- **2nd argument**: database name where migration tracking (`version` collection) is stored and migrations are applied

---

### 4) Transactional Boundaries

If the `useTransaction` flag is `true`, migration operations are executed inside a single transaction.

However, some operations may not be allowed inside a transaction (especially certain DDL operations), such as:

- dropping a collection
- dropping an index

In such cases, you should **not** use the `session` object for that operation.

> **IMPORTANT:** If you execute an operation without `session`, that operation runs **outside** the transaction.  
> As a result, if the migration tool fails later, previous changes executed outside the transaction will **not** be rolled back.
>
> Therefore, you should use `session` whenever possible and keep operations within transactional boundaries as much as possible.

---

## CLI Usage

### Accepted Parameters

```text
Usage: mongo-migrate <mongo-uri> <database-name> [step] [useTransaction]

<mongo-uri>        (required) MongoDB connection string
<database-name>    (required) Database name used for migration execution and version tracking
<step>             (optional) Migration target/step behavior:
                   - Positive step count for migrate up   (e.g. +2)
                   - Negative step count for migrate down (e.g. -1)
                   - Direct version number               (e.g. 5)
                   Default: migrate to latest version
<useTransaction>   (optional) true/false to enable/disable transactions. Default is true.
```

### Parameter Interpretation Notes

- If `step` is omitted, the tool migrates to the **latest** version.
- If a direct version number is provided (e.g. `5`), the tool moves:
    - **up** if current version is lower than 5
    - **down** if current version is higher than 5
- If `step` is omitted but `useTransaction` is provided, the boolean value can be passed as the third argument.

---

## Examples

### Migrate to the latest version (default)

```bash
./mongo-migrate mongodb://myuser:mypassword@localhost:27017/?replicaSet=NoSQLDBReplicaSet migrations
```

### Migrate forward by 2 steps

```bash
./mongo-migrate mongodb://myuser:mypassword@localhost:27017/?replicaSet=NoSQLDBReplicaSet migrations +2
```

### Roll back by 1 step

```bash
./mongo-migrate mongodb://myuser:mypassword@localhost:27017/?replicaSet=NoSQLDBReplicaSet migrations -1
```

### Migrate to latest version **without** transactions

```bash
./mongo-migrate mongodb://myuser:mypassword@localhost:27017/?replicaSet=NoSQLDBReplicaSet migrations false
```

### Roll back by 1 step **without** transactions

```bash
./mongo-migrate mongodb://myuser:mypassword@localhost:27017/?replicaSet=NoSQLDBReplicaSet migrations -1 false
```

### Migrate directly to version 5

This mode accepts a target version number directly.  
If the current version is above `5`, it migrates down; if below `5`, it migrates up.

```bash
./mongo-migrate mongodb://myuser:mypassword@localhost:27017/?replicaSet=NoSQLDBReplicaSet migrations 5
```

---

## Best Practices (Recommended)

- Keep each migration focused on a single logical change.
- Make `up()` and `down()` implementations as deterministic as possible.
- Avoid mixing transactional and non-transactional operations in the same migration unless necessary.
- Use unique, sequential version numbers to keep migration history easy to follow.
- After pulling new migrations from the repository, always reload CMake before building.

---

## Common Pitfalls

- **New migration file not executed**  
  Usually caused by forgetting to **Reload CMake Project** after adding a new `.cpp` file.

- **Compilation error due to class name mismatch**  
  The name in `DECLARE_MIGRATION(...)` must match the method definitions exactly.

- **Unexpected partial changes after failure**  
  Some operations may run outside transactions (when `session` is not used), so they will not be rolled back.