.PHONY: syntax fetch-bbt build docker-build

syntax:
	./scripts/test_shell_syntax.sh

fetch-bbt:
	./scripts/fetch_bitcoin_bash_tools.sh

build:
	@if [ -z "$(BR_SRC)" ]; then \
		echo "Set BR_SRC to your Buildroot source path" >&2; \
		exit 1; \
	fi
	./scripts/build_thunderden.sh --buildroot-dir "$(BR_SRC)"

docker-build:
	./scripts/docker_build_thunderden.sh
