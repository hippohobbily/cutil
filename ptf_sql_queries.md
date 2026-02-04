# IBM i PTF SQL Queries Reference

Complete reference for querying PTF information using SQL Services.

**IMPORTANT:** This document has been verified against IBM documentation. Always use the schema discovery queries to confirm what exists on your system.

---

## Verified PTF-Related Views

| Schema | View/Table | Description | Verified |
|--------|------------|-------------|----------|
| `QSYS2` | `PTF_INFO` | Main PTF status and metadata | YES |
| `QSYS2` | `GROUP_PTF_INFO` | PTF group information | YES |
| `SYSTOOLS` | `GROUP_PTF_DETAILS` | PTFs within groups (compares to IBM PSP) | YES |
| `SYSTOOLS` | `GROUP_PTF_CURRENCY` | Group levels vs IBM Fix Central | YES |

**NOTE:** There is NO `QSYS2.PTF_OBJECT` view. To get PTF object information, you must use the CL command `DSPPTF` with `PTFOBJ(*YES)`.

---

## Schema Discovery Queries

**Use these first to verify what exists on your system.**

### List All PTF-Related Views in QSYS2

```sql
SELECT TABLE_NAME, TABLE_TEXT
FROM QSYS2.SYSTABLES
WHERE TABLE_SCHEMA = 'QSYS2'
  AND TABLE_NAME LIKE '%PTF%'
ORDER BY TABLE_NAME
```

### List All PTF-Related Views in SYSTOOLS

```sql
SELECT TABLE_NAME, TABLE_TEXT
FROM QSYS2.SYSTABLES
WHERE TABLE_SCHEMA = 'SYSTOOLS'
  AND TABLE_NAME LIKE '%PTF%'
ORDER BY TABLE_NAME
```

### List All Columns in a Table

```sql
SELECT COLUMN_NAME, DATA_TYPE, LENGTH, NUMERIC_SCALE, IS_NULLABLE, COLUMN_TEXT
FROM QSYS2.SYSCOLUMNS
WHERE TABLE_SCHEMA = 'QSYS2'
  AND TABLE_NAME = 'PTF_INFO'
ORDER BY ORDINAL_POSITION
```

### Search for Tables by Keyword

```sql
SELECT TABLE_SCHEMA, TABLE_NAME, TABLE_TEXT
FROM QSYS2.SYSTABLES
WHERE UPPER(TABLE_NAME) LIKE '%PTF%'
   OR UPPER(TABLE_TEXT) LIKE '%PTF%'
ORDER BY TABLE_SCHEMA, TABLE_NAME
```

### Get Sample Row from Any Table

```sql
SELECT * FROM QSYS2.PTF_INFO FETCH FIRST 1 ROW ONLY
```

---

## QSYS2.PTF_INFO - Verified Columns

Source: [IBM Documentation - PTF_INFO view](https://www.ibm.com/docs/en/i/7.3?topic=services-ptf-info-view)

| Column | Data Type | Description |
|--------|-----------|-------------|
| `PTF_PRODUCT_ID` | VARCHAR(7) | Product ID (e.g., 5770SS1) |
| `PTF_PRODUCT_OPTION` | VARCHAR(6) | Product option |
| `PTF_PRODUCT_RELEASE_LEVEL` | VARCHAR(6) | Product release level |
| `PTF_PRODUCT_DESCRIPTION` | VARCHAR(132) | Product description |
| `PTF_IDENTIFIER` | VARCHAR(7) | PTF ID (e.g., SI82745) |
| `PTF_RELEASE_LEVEL` | VARCHAR(6) | PTF release level |
| `PTF_PRODUCT_LOAD` | VARCHAR(4) | Product load |
| `PTF_LOADED_STATUS` | VARCHAR(19) | Status (see values below) |
| `PTF_SAVE_FILE` | VARCHAR(3) | Save file exists (YES/NO) |
| `PTF_COVER_LETTER` | VARCHAR(3) | Cover letter exists (YES/NO) |
| `PTF_ON_ORDER` | VARCHAR(3) | PTF on order (YES/NO) |
| `PTF_IPL_ACTION` | VARCHAR(19) | Action at IPL |
| `PTF_ACTION_PENDING` | VARCHAR(3) | Action pending (YES/NO) |
| `PTF_ACTION_REQUIRED` | VARCHAR(12) | Required action |
| `PTF_IPL_REQUIRED` | VARCHAR(9) | IPL required (YES/NO) |
| `PTF_IS_RELEASED` | VARCHAR(3) | PTF released (YES/NO) |
| `PTF_MINIMUM_LEVEL` | VARCHAR(2) | Minimum level |
| `PTF_MAXIMUM_LEVEL` | VARCHAR(2) | Maximum level |
| `PTF_STATUS_TIMESTAMP` | TIMESTAMP | Status change timestamp |
| `PTF_SUPERCEDED_BY_PTF` | VARCHAR(7) | Superseding PTF ID |
| `PTF_CREATION_TIMESTAMP` | TIMESTAMP | PTF creation timestamp |
| `PTF_TECHNOLOGY_REFRESH_PTF` | VARCHAR(3) | Is TR PTF (YES/NO) |
| `PTF_TEMPORARY_APPLY_TIMESTAMP` | TIMESTAMP | Temp apply timestamp |

**Additional columns (added in later TRs):**
- `PTF_LATEST_SUPERSEDING_PTF` - Added in 7.5 Level 5 / 7.4 Level 26
- `PTF_PRODUCT_OPTION_2` - Added in 7.5 Level 4 / 7.4 Level 25

**PTF_LOADED_STATUS Values:**
- `NOT LOADED` - PTF save file exists but not loaded
- `LOADED` - Loaded but not applied
- `APPLIED` - Temporarily applied
- `PERMANENTLY APPLIED` - Permanently applied
- `SUPERSEDED` - Replaced by newer PTF
- `DAMAGED` - PTF is damaged

---

## QSYS2.GROUP_PTF_INFO - Verified Columns

Source: [IBM Documentation - GROUP_PTF_INFO view](https://www.ibm.com/docs/en/i/7.6.0?topic=services-group-ptf-info-view)

| Column | Data Type | Description |
|--------|-----------|-------------|
| `COLLECTED_TIME` | TIMESTAMP | When row was generated |
| `PTF_GROUP_NAME` | VARCHAR(60) | Group name (e.g., SF99730) |
| `PTF_GROUP_DESCRIPTION` | VARCHAR(100) | Group description |
| `PTF_GROUP_LEVEL` | INTEGER | Group level number |
| `PTF_GROUP_TARGET_RELEASE` | VARCHAR(6) | Target release |
| `PTF_GROUP_STATUS` | VARCHAR(20) | Status (see values below) |
| `PTF_GROUP_APPLY_TIMESTAMP` | TIMESTAMP(0) | Most recent apply timestamp |

**PTF_GROUP_STATUS Values:**
- `UNKNOWN`
- `NOT APPLICABLE`
- `SUPPORTED ONLY`
- `NOT INSTALLED`
- `INSTALLED`
- `ERROR`
- `APPLY AT NEXT IPL`
- `RELATED GROUP`
- `ON ORDER`

---

## PTF Object Information - NO SQL VIEW EXISTS

**There is no SQL view for PTF objects.** You must use CL commands:

### Get PTF Objects via CL Command

```sh
system "DSPPTF LICPGM(5770SS1) SELECT(SI82745) PTFOBJ(*YES) OUTPUT(*PRINT)"
```

### Get PTF Objects to Output File

```sh
system "DSPPTF LICPGM(5770SS1) SELECT(SI82745) PTFOBJ(*YES) OUTPUT(*OUTFILE) OUTFILE(QTEMP/PTFOBJ)"
```

### Copy Output to Stream File

```sh
system "DSPPTF LICPGM(5770SS1) SELECT(SI82745) PTFOBJ(*YES) OUTPUT(*PRINT)"
system "CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('/tmp/ptf_objects.txt') SPLNBR(*LAST)"
```

### Display Output File Layout

To see the fields in the PTF output file:
```sh
system "DSPFFD FILE(QSYS/QADSPPTF)"
```

---

## PTF Status Queries

### List All PTFs for a Product

```sql
SELECT PTF_IDENTIFIER, PTF_LOADED_STATUS, PTF_IPL_ACTION
FROM QSYS2.PTF_INFO
WHERE PTF_PRODUCT_ID = '5770SS1'
ORDER BY PTF_IDENTIFIER
```

### Find a Specific PTF

```sql
SELECT *
FROM QSYS2.PTF_INFO
WHERE PTF_IDENTIFIER = 'SI82745'
```

### PTFs Requiring IPL

```sql
SELECT PTF_IDENTIFIER, PTF_PRODUCT_ID, PTF_LOADED_STATUS
FROM QSYS2.PTF_INFO
WHERE PTF_IPL_REQUIRED = 'YES'
```

### PTFs with Pending Actions

```sql
SELECT PTF_IDENTIFIER, PTF_PRODUCT_ID, PTF_IPL_ACTION, PTF_ACTION_PENDING
FROM QSYS2.PTF_INFO
WHERE PTF_ACTION_PENDING = 'YES'
```

### Superseded PTFs

```sql
SELECT PTF_IDENTIFIER, PTF_PRODUCT_ID, PTF_SUPERCEDED_BY_PTF
FROM QSYS2.PTF_INFO
WHERE PTF_SUPERCEDED_BY_PTF IS NOT NULL
```

### PTF Count by Status

```sql
SELECT PTF_PRODUCT_ID, PTF_LOADED_STATUS, COUNT(*) AS COUNT
FROM QSYS2.PTF_INFO
GROUP BY PTF_PRODUCT_ID, PTF_LOADED_STATUS
ORDER BY PTF_PRODUCT_ID, PTF_LOADED_STATUS
```

### Recently Applied PTFs

```sql
SELECT PTF_IDENTIFIER, PTF_PRODUCT_ID, PTF_LOADED_STATUS, PTF_STATUS_TIMESTAMP
FROM QSYS2.PTF_INFO
WHERE PTF_STATUS_TIMESTAMP > CURRENT_TIMESTAMP - 30 DAYS
ORDER BY PTF_STATUS_TIMESTAMP DESC
```

### Temporarily Applied PTFs

```sql
SELECT PTF_IDENTIFIER, PTF_PRODUCT_ID, PTF_STATUS_TIMESTAMP
FROM QSYS2.PTF_INFO
WHERE PTF_LOADED_STATUS = 'APPLIED'
ORDER BY PTF_PRODUCT_ID
```

### Loaded But Not Applied

```sql
SELECT PTF_IDENTIFIER, PTF_PRODUCT_ID
FROM QSYS2.PTF_INFO
WHERE PTF_LOADED_STATUS = 'LOADED'
ORDER BY PTF_PRODUCT_ID, PTF_IDENTIFIER
```

### Technology Refresh PTFs

```sql
SELECT PTF_IDENTIFIER, PTF_PRODUCT_ID, PTF_LOADED_STATUS
FROM QSYS2.PTF_INFO
WHERE PTF_TECHNOLOGY_REFRESH_PTF = 'YES'
ORDER BY PTF_IDENTIFIER
```

### Damaged PTFs

```sql
SELECT PTF_IDENTIFIER, PTF_PRODUCT_ID, PTF_STATUS_TIMESTAMP
FROM QSYS2.PTF_INFO
WHERE PTF_LOADED_STATUS = 'DAMAGED'
```

---

## PTF Group Queries

### List All PTF Groups

```sql
SELECT PTF_GROUP_NAME, PTF_GROUP_DESCRIPTION, PTF_GROUP_LEVEL, PTF_GROUP_STATUS
FROM QSYS2.GROUP_PTF_INFO
ORDER BY PTF_GROUP_NAME
```

### Not Installed Groups

```sql
SELECT PTF_GROUP_NAME, PTF_GROUP_LEVEL, PTF_GROUP_STATUS
FROM QSYS2.GROUP_PTF_INFO
WHERE PTF_GROUP_STATUS = 'NOT INSTALLED'
```

### Cumulative PTF Groups

```sql
SELECT PTF_GROUP_NAME, PTF_GROUP_DESCRIPTION, PTF_GROUP_LEVEL
FROM QSYS2.GROUP_PTF_INFO
WHERE PTF_GROUP_NAME LIKE 'SF99%'
ORDER BY PTF_GROUP_NAME
```

### Groups Pending IPL

```sql
SELECT PTF_GROUP_NAME, PTF_GROUP_LEVEL, PTF_GROUP_STATUS
FROM QSYS2.GROUP_PTF_INFO
WHERE PTF_GROUP_STATUS = 'APPLY AT NEXT IPL'
```

---

## SYSTOOLS Views (Require Internet Access)

These views compare your system to IBM's Fix Central.

### Check PTF Group Currency

```sql
SELECT PTF_GROUP_ID, PTF_GROUP_TITLE,
       PTF_GROUP_LEVEL_INSTALLED, PTF_GROUP_LEVEL_AVAILABLE,
       PTF_GROUP_CURRENCY
FROM SYSTOOLS.GROUP_PTF_CURRENCY
ORDER BY PTF_GROUP_ID
```

### Find Outdated Groups

```sql
SELECT PTF_GROUP_ID, PTF_GROUP_TITLE,
       PTF_GROUP_LEVEL_INSTALLED, PTF_GROUP_LEVEL_AVAILABLE
FROM SYSTOOLS.GROUP_PTF_CURRENCY
WHERE PTF_GROUP_CURRENCY <> 'CURRENT'
```

### PTFs in a Group (from GROUP_PTF_DETAILS)

```sql
SELECT PTF_IDENTIFIER, PTF_STATUS, PTF_LOADED_STATUS
FROM SYSTOOLS.GROUP_PTF_DETAILS
WHERE PTF_GROUP_NAME = 'SF99730'
ORDER BY PTF_IDENTIFIER
```

---

## Product PTF Summary

```sql
SELECT PTF_PRODUCT_ID,
       SUM(CASE WHEN PTF_LOADED_STATUS = 'PERMANENTLY APPLIED' THEN 1 ELSE 0 END) AS PERM_APPLIED,
       SUM(CASE WHEN PTF_LOADED_STATUS = 'APPLIED' THEN 1 ELSE 0 END) AS TEMP_APPLIED,
       SUM(CASE WHEN PTF_LOADED_STATUS = 'LOADED' THEN 1 ELSE 0 END) AS LOADED,
       SUM(CASE WHEN PTF_LOADED_STATUS = 'SUPERSEDED' THEN 1 ELSE 0 END) AS SUPERSEDED,
       COUNT(*) AS TOTAL
FROM QSYS2.PTF_INFO
GROUP BY PTF_PRODUCT_ID
ORDER BY PTF_PRODUCT_ID
```

---

## System Health Summary

### PTFs Needing Attention

```sql
SELECT PTF_IDENTIFIER, PTF_PRODUCT_ID, PTF_LOADED_STATUS, PTF_ACTION_PENDING
FROM QSYS2.PTF_INFO
WHERE PTF_ACTION_PENDING = 'YES'
   OR PTF_IPL_REQUIRED = 'YES'
   OR PTF_LOADED_STATUS = 'LOADED'
ORDER BY PTF_PRODUCT_ID, PTF_IDENTIFIER
```

### IPL Required Summary

```sql
SELECT
    SUM(CASE WHEN PTF_IPL_REQUIRED = 'YES' THEN 1 ELSE 0 END) AS IPL_REQUIRED,
    SUM(CASE WHEN PTF_ACTION_PENDING = 'YES' THEN 1 ELSE 0 END) AS ACTIONS_PENDING,
    SUM(CASE WHEN PTF_LOADED_STATUS = 'LOADED' THEN 1 ELSE 0 END) AS NOT_APPLIED
FROM QSYS2.PTF_INFO
```

---

## Shell Script Usage

```sh
#!/QOpenSys/usr/bin/ksh

# Basic query
db2 "SELECT PTF_IDENTIFIER, PTF_LOADED_STATUS FROM QSYS2.PTF_INFO WHERE PTF_PRODUCT_ID = '5770SS1'"

# Query with output to file
db2 "SELECT * FROM QSYS2.PTF_INFO" > /tmp/ptf_list.txt

# Get table columns (verify what exists)
db2 "SELECT COLUMN_NAME, DATA_TYPE FROM QSYS2.SYSCOLUMNS WHERE TABLE_SCHEMA = 'QSYS2' AND TABLE_NAME = 'PTF_INFO'"

# Count PTFs
db2 "SELECT COUNT(*) FROM QSYS2.PTF_INFO WHERE PTF_PRODUCT_ID = '5770SS1'"

# Get PTF objects (no SQL view - must use CL)
system "DSPPTF LICPGM(5770SS1) SELECT(SI82745) PTFOBJ(*YES) OUTPUT(*PRINT)"
```

---

## CL Commands Reference

### Display PTF Information

```
DSPPTF LICPGM(5770SS1) SELECT(SI82745)
DSPPTF LICPGM(5770SS1) SELECT(SI82745) COVER(*YES) OUTPUT(*PRINT)
DSPPTF LICPGM(5770SS1) SELECT(*ALL) RLS(*ALL) OUTPUT(*PRINT)
```

### Display PTF Objects (NO SQL EQUIVALENT)

```
DSPPTF LICPGM(5770SS1) SELECT(SI82745) PTFOBJ(*YES) OUTPUT(*PRINT)
DSPPTF LICPGM(5770SS1) SELECT(SI82745) PTFOBJ(*YES) OUTPUT(*OUTFILE) OUTFILE(QTEMP/PTFOBJ)
```

### Work with PTF Groups

```
WRKPTFGRP
```

### Display Save File

```
DSPSAVF FILE(QGPL/QSI82745J1)
```

### Shell Execution

```sh
system "DSPPTF LICPGM(5770SS1) SELECT(SI82745) PTFOBJ(*YES) OUTPUT(*PRINT)"
system "CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('/tmp/output.txt') SPLNBR(*LAST)"
```

---

## Sources

- [IBM - QSYS2.PTF_INFO](https://www.ibm.com/support/pages/qsys2ptfinfo)
- [IBM - PTF_INFO view documentation](https://www.ibm.com/docs/en/i/7.3?topic=services-ptf-info-view)
- [IBM - GROUP_PTF_INFO view](https://www.ibm.com/docs/en/i/7.6.0?topic=services-group-ptf-info-view)
- [IBM - SYSTOOLS.GROUP_PTF_DETAILS](https://www.ibm.com/support/pages/node/1126983)
- [IBM - PTFs FAQ](https://www.ibm.com/support/pages/ptfs-faqs-question-and-answers)
- [RPGPGM.COM - Quick way to find if PTF present](https://www.rpgpgm.com/2014/12/quick-way-to-find-if-ptf-present-and.html)

---

## Notes

- All queries target verified views in `QSYS2` and `SYSTOOLS` schemas
- Column names may vary slightly between OS releases - use discovery queries
- `SYSTOOLS` views require internet access to compare with IBM Fix Central
- **There is NO SQL view for PTF objects** - use `DSPPTF PTFOBJ(*YES)`
- Some views require `*ALLOBJ` or `*USE` authority to DSPPTF/WRKPTFGRP
