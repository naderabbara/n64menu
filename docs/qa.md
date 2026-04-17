# QA Tracking

This document defines the QA issue format for the NSO Menu project.

QA observations are tracked in Linear under the `NSO Menu` project. For QA findings, use the `Bug` label by default unless the item is clearly not a defect.

## Linear Usage

Use this document as the manual template when creating QA issues in Linear.

- Project: `NSO Menu`
- Team: `Development`
- Default label: `Bug`
- Title format: `QA-###: Short title`
- Keep QA IDs stable once assigned.
- Append new observations in numeric order.
- Preserve prior issue history when updating status or verification notes.
- Use `Resources` only when screenshots, videos, logs, or other attachments materially help reproduce or verify the issue.

## Linear Form Fields

| Field | Linear field type | Values / guidance |
| --- | --- | --- |
| Title | Text | `QA-###: Short title` |
| Priority | Dropdown | Use Linear priority values. |
| Status | Dropdown | `New`, `Confirmed`, `Needs repro`, `Fixed`, `Deferred` |
| Severity | Dropdown | `Blocker`, `High`, `Medium`, `Low`, `Polish` |
| Area | Text | Prefer one or more of: `Boot`, `Discovery/cache`, `Navigation`, `Rendering`, `Launch`, `Assets`, `Audio`, `Build` |
| Observation | Long text | What was seen during QA. |
| Expected | Long text | Intended behavior. |
| Likely cause | Long text | Current best technical hypothesis. |
| Proposed fix | Long text | Concrete fix direction. |
| Verification | Long text | Steps to prove the issue is fixed. |
| Notes | Long text | Context, constraints, or follow-up details. |
| Resources | File upload | Optional screenshots, video, logs, or repro files. |

## QA Issue Template

```text
QA-###: Short title
Priority:
Status: New | Confirmed | Needs repro | Fixed | Deferred
Severity: Blocker | High | Medium | Low | Polish
Area: Boot | Discovery/cache | Navigation | Rendering | Launch | Assets | Audio | Build

Observation:

Expected:

Likely cause:

Proposed fix:

Verification:

Notes:

Resources:
```

## Linear Description Template

When creating issues through tools that do not support native Linear templates, put the structured fields in the issue description:

```markdown
Status:
Severity:
Area:

Observation:

Expected:

Likely cause:

Proposed fix:

Verification:

Notes:

Resources:
```

