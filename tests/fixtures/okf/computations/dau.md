---
type: Attested Computation
title: Daily Active Users
runtime: bigquery
executor: { resource: skills/run-dau.md }
parameters:
  - name: date
    type: date
    required: true
stale_after: 2020-01-01
---
# Computation

```sql
SELECT date, COUNT(DISTINCT user_id) AS dau FROM events_ GROUP BY date
```
