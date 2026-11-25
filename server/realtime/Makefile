# Ilmeee realtime server. CI (.github/workflows/deploy.yaml) runs `make lint`,
# `make test` and `make build TARGET=x86_64-unknown-linux-musl`.
TARGET ?=
BIN    := ilmeee-realtime
OUT    := build/$(BIN)

ifeq ($(TARGET),)
RELEASE_BIN := target/release/$(BIN)
TARGET_FLAG :=
else
RELEASE_BIN := target/$(TARGET)/release/$(BIN)
TARGET_FLAG := --target $(TARGET)
endif

.PHONY: run test lint build clean

# Local server on ws://127.0.0.1:9230/ws, token "dev", rooms saved in ./data
run:
	ILMEEE_RT_TOKEN=$${ILMEEE_RT_TOKEN:-dev} RT_DATA_DIR=$${RT_DATA_DIR:-./data} cargo run

test:
	cargo test --locked

lint:
	cargo clippy --locked --all-targets -- -D warnings

build:
	cargo build --locked --release $(TARGET_FLAG)
	mkdir -p build
	cp $(RELEASE_BIN) $(OUT)
	@ls -lh $(OUT)

clean:
	cargo clean
	$(RM) $(OUT)
