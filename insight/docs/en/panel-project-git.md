# Git projects

Project → Create Git Project initializes a folder or reuses a repository root. Create Project from Git clones a local repository or remote URL into a new destination. Installed Git supplies the user's configuration and credential helpers. These operations do not push.

Git owns project membership. The project records `git_based=true` in `insight.geany`; opening and reindexing read tracked paths from the Git index. Ordinary projects retain their existing membership rules.

File and the toolbar next to Save provide Commit changes (branch with a green check) and Add to Git (file with a green plus). Add supports multiple files, respects ignore rules and rejects paths outside the repository.

Project → Add and Remove Project Files opens a sorted tracked-file list for Git projects. Add uses Git add. Remove selected uses Git rm --cached, preserving disk files and staging their removal from the repository. These membership buttons apply immediately; closing the window does not undo them.

Commit first offers to save modified project documents. The dialog lists sorted added, modified and removed files on the left, all checked initially. Unchecking excludes a file even if another Git client staged it. Renames appear as separate removal/addition paths. The editable proposal on the right summarizes changes and lists files. The untouched proposal follows checkbox changes. Manual edits are preserved; a button regenerates the proposal from the current selection. Cancel leaves Git unchanged.

Selected files commit their full current disk contents, replacing any partial staging for those files. Unselected staging survives. A private index isolates the commit. Stale file/index reviews and foreign index locks are rejected. Git identity, signing and hooks use the normal Git configuration. Partial commits during merge, rebase or cherry-pick are rejected; nested repositories and submodules must be committed in their own project. Push, fetch, branch switching and conflict resolution currently use an external Git client.
