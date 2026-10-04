#!/bin/bash
# Doxygen で、アプリごとに、ドキュメント (HTML) を docs/<アプリ>/html に生成する (docs/index.html から開く).
# 警告が 1 つでもあれば、失敗する.
#
# 2 つのアプリには、同じ名前の関数 (ble_init() など) があるので、アプリごとに、別々に生成する.
# Doxygen の日本語の出力には、"The selected output language "japanese" has not been updated
# since release ..." という、内容と関係のない警告が、必ず出るので、それだけは除く.
#
# docker compose run docs-thermo から呼ぶ (Doxygen と Graphviz は、Docker イメージにある).
set -eu

project=$(cd "$(dirname "$0")/.." && pwd)
status=0

cd "$project"
rm -rf docs
mkdir -p docs

for app in thermo-node thermo-gateway; do
    mkdir -p "docs/$app"
    if ! DOCS_APP=$app DOCS_NAME="Thermo System ($app)" doxygen Doxyfile > "docs/$app/doxygen.log" 2>&1; then
        cat "docs/$app/doxygen.log"
        exit 1
    fi

    warnings=$(grep -v 'selected output language' "docs/$app/doxygen-warnings.log" || true)
    if [ -n "$warnings" ]; then
        echo "$warnings"
        echo "docs: $app: $(echo "$warnings" | grep -c 'warning:') warning(s)"
        status=1
    fi
done

cat > docs/index.html <<'HTML'
<!DOCTYPE html>
<html lang="ja"><head><meta charset="utf-8"><title>Thermo System</title></head>
<body><h1>Thermo System</h1><ul>
<li><a href="thermo-node/html/index.html">Thermo Node</a></li>
<li><a href="thermo-gateway/html/index.html">Thermo Gateway</a></li>
</ul></body></html>
HTML

if [ "$status" -eq 0 ]; then
    echo "docs: ok (docs/index.html)"
fi
exit "$status"
