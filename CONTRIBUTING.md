## Contributing

Everyone working on this repo follows the same two rules:

1. **Write clean commit messages** (see [Commit Standards](#commit-standards)).
2. **Never push directly to `main`.** All changes land through a **Merge Request (MR)** that gets reviewed first (see [Workflow](#workflow)).

These keep history readable and `main` always deployable.

---

## Workflow

`main` is protected. Direct pushes to it are not allowed. To get your change in:

```bash
# 1. Start from an up-to-date main
git checkout main
git pull

# 2. Create a branch for your work
git checkout -b feat/short-description

# 3. Commit your changes (follow the commit standards below)
git add .
git commit -m "feat: add user login form"

# 4. Push your branch
git push -u origin feat/short-description

# 5. Open a Merge Request targeting main, request a reviewer
```

**Rules:**

- One MR = one logical change. Keep them small and focused.
- An MR needs **at least one approval** before it can merge.
- Make sure CI / tests pass before requesting review.
- Resolve conflicts by rebasing or merging `main` into your branch — never force-push over shared history.

### Branch naming

Use a `type/short-description` format, words separated by hyphens:

| Type        | Use for                          | Example                      |
|-------------|----------------------------------|------------------------------|
| `feat/`     | New feature                      | `feat/export-csv`            |
| `fix/`      | Bug fix                          | `fix/null-pointer-checkout`  |
| `docs/`     | Documentation only               | `docs/update-readme`         |
| `refactor/` | Code change, no behavior change  | `refactor/auth-service`      |
| `chore/`    | Tooling, deps, config            | `chore/bump-eslint`          |

---

## Commit Standards

We follow [**Conventional Commits**](https://www.conventionalcommits.org/). Each commit message looks like:

```
<type>(<optional scope>): <short summary>

<optional body explaining what and why>

<optional footer, e.g. references to issues>
```

### Types

| Type       | When to use                                          |
|------------|------------------------------------------------------|
| `feat`     | A new feature                                        |
| `fix`      | A bug fix                                             |
| `docs`     | Documentation only changes                           |
| `style`    | Formatting, whitespace — no code logic change        |
| `refactor` | Code change that neither fixes a bug nor adds a feature |
| `perf`     | A performance improvement                            |
| `test`     | Adding or fixing tests                               |
| `chore`    | Build process, tooling, dependencies                 |

### Rules for the summary line

- Use the **imperative mood**: "add", not "added" or "adds".
- Keep it **under ~50 characters** and **lowercase** (after the type).
- **No period** at the end.
- Explain the *why* in the body when it isn't obvious from the summary.

### Examples

```
feat(auth): add password reset via email

fix(api): handle empty response from payment gateway

docs: add setup steps to README

refactor(cart): extract pricing logic into its own module
```

A breaking change is marked with `!` after the type, and a `BREAKING CHANGE:` footer:

```
feat(api)!: remove deprecated v1 endpoints

BREAKING CHANGE: clients must migrate to /v2 routes.
```

---

## Quick reference

- Branch → commit with a conventional message → push → open MR → get review → merge.
- Keep MRs small, keep history clean.