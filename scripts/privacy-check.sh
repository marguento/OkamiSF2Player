#!/bin/zsh
# Fails if anything personal would leave this Mac: run before every push to a public repo and
# before every release (release.sh runs it on the bundles it ships).
#
#   scripts/privacy-check.sh [binary-or-bundle ...]
#
# Checks every revision of every tracked file, every commit's author and message, and the given
# binaries for: this Mac's home folder path, e-mail addresses other than the allowed ones,
# private keys, key and credential files, and the personal patterns in PRIVATE_PATTERNS (one
# extended regex per line: your e-mails, account identifiers), which live in
# scripts/release.env, ignored by git, so the list itself is never published.
set -uo pipefail
cd "$(dirname "$0")/.."
[[ -f scripts/release.env ]] && source scripts/release.env
PRIVATE_PATTERNS=${PRIVATE_PATTERNS:-}
# E-mails allowed in the repo: GitHub's noreply address, the test font author's notice,
# Anthropic's co-author line, and license boilerplate.
ALLOWED='users\.noreply\.github\.com|vintagedreamworks@|noreply@anthropic\.com|@fsf\.org|@gnu\.org|@juce\.com'

failed=0
report() { echo "PRIVACY: $1"; failed=1; }

patterns=("/Users/${USER}" "${HOME}" "BEGIN [A-Z ]*PRIVATE KEY")
while IFS= read -r p; do [[ -n $p ]] && patterns+=("$p"); done <<< "$PRIVATE_PATTERNS"

# 1. Every revision of every tracked file.
revs=($(git rev-list --all))
for p in "${patterns[@]}"; do
    hits=$(git grep -I -l -E -e "$p" "${revs[@]}" 2>/dev/null | sed 's/^[0-9a-f]*://' | sort -u)
    [[ -n $hits ]] && report "\"$p\" in history: ${hits//$'\n'/, }"
done
emails=$(git grep -I -h -o -E '[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}' "${revs[@]}" 2>/dev/null | sort -u | grep -v -E "$ALLOWED")
[[ -n $emails ]] && report "e-mail addresses in history: ${emails//$'\n'/, }"
files=$(git log --all --pretty=format: --name-only | sort -u | grep -E '\.(p8|p12|pem|key|cer|mobileprovision|env)$|release\.env$')
[[ -n $files ]] && report "credential files in history: ${files//$'\n'/, }"

# 2. Commit metadata and messages.
meta=$(git log --all --format='%an <%ae> %cn <%ce>%n%B')
for p in "${patterns[@]}"; do
    grep -q -E -e "$p" <<< "$meta" && report "\"$p\" in a commit's author or message"
done
authors=$(git log --all --format='%ae%n%ce' | sort -u | grep -v -E "$ALLOWED")
[[ -n $authors ]] && report "commit e-mails: ${authors//$'\n'/, }"

# 3. Shipped binaries (bundles are searched inside).
for target in "$@"; do
    while IFS= read -r f; do
        text=$(strings -a "$f" 2>/dev/null)
        for p in "${patterns[@]}"; do
            grep -q -E -e "$p" <<< "$text" && report "\"$p\" inside $f"
        done
    done < <(find "$target" -type f -perm -u+x 2>/dev/null; [[ -f $target && ! -x $target ]] && echo "$target")
done

if (( failed )); then
    echo "Privacy check FAILED: fix the above (history: rewrite it) before publishing."
    exit 1
fi
echo "Privacy check passed (${#revs[@]} commits${1:+, $# binaries})."
