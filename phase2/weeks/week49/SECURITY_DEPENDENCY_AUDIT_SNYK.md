# Week 49 Security Audit (Dependencies / Snyk CLI)

**Date:** 2026-03-12  
**Tooling:** Snyk CLI (`snyk test --all-projects`)  
**Scope:** Python dependencies (pip)  
**Repo path:** `C:\Users\jluca\Documents\JARVIS_OS`  
**Target file:** `requirements.txt`

## Result Summary

- **Dependencies tested:** 22
- **Issues found:** 9
- **Vulnerable paths:** 9

## Findings (from Snyk CLI output)

### Fixable by pinning/upgrading transitive dependencies

1. **filelock**
   - **Current:** `filelock@3.18.0`
   - **Fix:** pin to `filelock@3.20.3`
   - **Introduced by:** `huggingface-hub@0.33.4 > filelock@3.18.0`
   - **Issues:**
     - TOCTOU Race Condition (Medium) `SNYK-PYTHON-FILELOCK-14458335`
     - Symlink Attack (Medium) `SNYK-PYTHON-FILELOCK-14912448`

2. **requests**
   - **Current:** `requests@2.32.3`
   - **Fix:** pin to `requests@2.32.4`
   - **Introduced by:** `huggingface-hub@0.33.4 > requests@2.32.3`
   - **Issue:**
     - Insertion of Sensitive Information Into Sent Data (Medium) `SNYK-PYTHON-REQUESTS-10305723`

3. **urllib3**
   - **Current:** `urllib3@2.3.0`
   - **Fix:** pin to `urllib3@2.6.3`
   - **Introduced by:** `huggingface-hub@0.33.4 > requests@2.32.3 > urllib3@2.3.0`
   - **Issues:**
     - Open Redirect (Medium) `SNYK-PYTHON-URLLIB3-10390193`
     - Open Redirect (Medium) `SNYK-PYTHON-URLLIB3-10390194`
     - Data Amplification (High) `SNYK-PYTHON-URLLIB3-14192442`
     - Allocation Without Limits/Throttling (High) `SNYK-PYTHON-URLLIB3-14192443`
     - Data Amplification (High) `SNYK-PYTHON-URLLIB3-14896210`

### Issues with no direct upgrade or patch

1. **diskcache**
   - **Current:** `diskcache@5.6.3`
   - **Introduced by:** `llama-cpp-python@0.3.16 > diskcache@5.6.3`
   - **Issue:** Deserialization of Untrusted Data (High) `SNYK-PYTHON-DISKCACHE-15268422`
   - **Status:** **No upgrade or patch available** (per Snyk CLI)

## Recommended Remediation Actions

### 1) Add explicit pins for transitive dependencies

Add these pins to `requirements.txt` to force safe versions even when pulled transitively:

- `filelock==3.20.3`
- `requests==2.32.4`
- `urllib3==2.6.3`

### 2) Mitigate diskcache (no patch available)

Because `diskcache` is pulled in by `llama-cpp-python`, options are:

- **Preferred (risk reduction):** ensure `llama-cpp-python` is only installed where needed (e.g., Linux runtime hosts), and avoid using `diskcache` features unless required.
- **Operational control:** treat any on-disk cache contents as untrusted input; avoid loading cache artifacts from external sources.
- **Monitor:** re-run Snyk periodically; upgrade `llama-cpp-python` when it releases a fix or changes the dependency chain.

## Evidence (Command)

```text
(base) PS C:\Users\jluca\Documents\JARVIS_OS> snyk test --all-projects
... (see terminal capture for full output)
```

