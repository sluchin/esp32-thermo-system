format:
	docker compose run --rm format-fix
	npx --yes markdownlint-cli2 --fix

format-check:
	docker compose run --rm format
	npx --yes markdownlint-cli2

.PHONY: format format-check
