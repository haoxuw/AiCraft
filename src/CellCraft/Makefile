# Wrapper Makefile — forwards all targets to the repo-root Makefile with
# GAME=cellcraft, so you can `cd src/CellCraft && make game` without
# having to pass GAME= each time.
#
# NOTE: the primary CellCraft implementation is now the Three.js + TS web
# client in ./web/. The native `game` target is LEGACY / frozen but still
# builds. For active development, use `make web` (or `cd web && npm run dev`).

ROOT := $(CURDIR)/../..
GAME := cellcraft

TARGETS := game build configure clean server client stop \
           cellcraft-build cellcraft-godot

.PHONY: $(TARGETS) help web web-test
.DEFAULT_GOAL := web

$(TARGETS):
	@$(MAKE) -C $(ROOT) $@ GAME=$(GAME)

# Primary dev target: Vite dev server for the Three.js web client.
web:
	@cd web && npm run dev

# Web client tests.
web-test:
	@cd web && npm test

help:
	@echo "CellCraft wrapper — primary: web client (./web/); legacy: native C++ via $(ROOT)/Makefile"
	@echo "Targets: web web-test $(TARGETS)"
	@echo ""
	@echo "  make web            Primary: Vite dev server for Three.js web client"
	@echo "  make web-test       Primary: run web client tests"
	@echo ""
	@echo "  make game           [LEGACY] Launch the native C++ drawing MVP"
	@echo "  make game DEMO=1    [LEGACY] Native client with pre-populated demo strokes"
	@echo "  make cellcraft-godot  [LEGACY] Run the Godot prototype (visual reference)"
	@echo "  make stop           Kill all native cellcraft processes"
