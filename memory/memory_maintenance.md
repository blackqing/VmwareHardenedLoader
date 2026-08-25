---
title: memory_maintenance
type: note
permalink: VmwareHardenedLoader/memory-maintenance
---

# Memory Maintenance

## Discovery Model

- Core principle: progressive discovery through references, building a graph of notes.
- Initially, agents should use `search_notes` to discover existing notes (names only).
- Agents should read [[core]] as the top-level entry point (graph root).
  This note should contain references to other notes covering major project domains.
  The referenced notes shall, in turn, contain references to even more specific notes, and so on.
  The depth of the graph shall depend on the project complexity.
- Use topics/folders to group related notes in order to make the content structure explicit.
  Folders can mirror project structure (e.g. modules like frontend/backend) or topics like debugging, architecture, etc.
- Note references must use Basic Memory wiki-links, e.g. `[[frontend/core]]`.
  The surrounding text should clearly indicate when to read the note/which content to expect.
  The text should provide more precise guidance than the note name alone,
  i.e. avoid a reference like "frontend debugging: `[[frontend/debugging]]`" and instead make clear which aspects of frontend debugging are covered.
- Notes themselves should not contain information about when to read them; this is the responsibility of the referring note.

## Style

Dense agent notes, not prose docs. Prefer invariants, terse bullets.
Avoid obvious context, rationale, and examples unless they prevent likely mistakes.
Keep guidance durable and generalizable, not task-local.

## Add/update threshold

Add or update notes only with stable, non-obvious project conventions that avoid complex rediscovery in the future.
Do not add: quick-read facts; generic language/framework knowledge; one-off task notes; volatile line-level details; behavior likely to change soon.

## Maintenance Actions

- Renaming notes: keep wiki-links accurate when using `edit_note` or rewriting the note.
- Checking for stale notes (e.g. after deletion): use `search_notes` and review remaining `[[wiki-link]]` targets.
